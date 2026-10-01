/* mp_world_event.c: the moments of the world on the host: posted, settled by the census, and
 * carried in each peer's blocks until an acknowledgement proves them. See the header; a client's
 * half is mp_world_event_client.c, and the two share nothing but the event's type.
 *
 * SIZE NOTE: over 600 lines. Posting grew a rank for a full substep and the census a walk that
 * tells an actor that is gone from one passed over, beside the views' parts and their four moments.
 * The next seam is the views: everything from `concerns` to `mp_world_event_forget_view` reads the
 * ring and writes nothing the posting half keeps, and would go to a file of its own with the ring's
 * type in the internal header.
 */
#include "mp_world_event.h"

#include "mp_enemy_bind.h"
#include "mp_enemy_sync.h"
#include "mp_enemy_wire.h"
#include "mp_wire.h"
#include "mp_world_event_internal.h"
#include "mp_world_event_rule.h"

#include "common/logging.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* How many sent payloads a view remembers by the events they carried. The same window the enemy
 * records keep for theirs: an acknowledgement further back than this proves nothing here either. */
#define STAMPS 32u

#define VIEWS MP_ENEMY_SYNC_VIEWS

_Static_assert(VIEWS <= 8u, "a view is a bit in a byte of every event");
_Static_assert(MP_WORLD_EVENT_RING <= 0xFFFFu, "a ring slot is remembered in sixteen bits");

/* Where an event of the ring stands. */
enum { SLOT_FREE = 0, SLOT_POSTED, SLOT_OPEN, SLOT_DEAD };

typedef struct host_event {
    uint8_t          state;
    mp_world_event_t wire;         /* as it travels, the age aside */
    uintptr_t        actor;        /* the actor that made it, until the census settles its life */
    bool             key_known;    /* the binding named its key when it was posted */
    bool             life_noted;   /* the life last counted for its key when it was posted */
    uint8_t          life_then;
    bool             in_pool;      /* the census's walk met its actor in this substep */
    uint32_t         clock;        /* the substep of this module's clock it was made in */
    float            place[3];     /* where it happened, for whom it concerns and for the wire */
    float            radius;       /* how far a heard kind carries from there, 0 for a seen one */
    uint8_t          decided;      /* per view: whether it concerns the view has been asked */
    uint8_t          concerns;
    uint8_t          staged;       /* per view: a block to that peer carried it */
    uint8_t          delivered;    /* per view: an acknowledged block carried it */
} host_event_t;

typedef struct view_events {
    uint16_t written_slot[MP_WORLD_EVENT_PART_MAX];   /* the part the last encode wrote */
    uint16_t written_seq[MP_WORLD_EVENT_PART_MAX];
    uint8_t  written;
    uint32_t stamp_tick[STAMPS];
    bool     stamp_valid[STAMPS];
    uint8_t  stamp_count[STAMPS];
    uint16_t stamp_slot[STAMPS][MP_WORLD_EVENT_PART_MAX];
    uint16_t stamp_seq[STAMPS][MP_WORLD_EVENT_PART_MAX];
    uint16_t music_state;
    uint16_t music_sequence;

    bool     used;          /* a part was written for this view since the process began */
    uint32_t blocks;
    uint32_t offered;       /* events first put in a block to this peer */
    uint32_t not_relevant;
    uint32_t out_of_earshot;   /* of them, a heard kind whose player stood too far */
    uint32_t acknowledged;
    uint32_t left_out;      /* an event that concerned the view and did not fit a block */
    uint32_t unacknowledged;   /* staged, and out of the window with no proof */
} view_events_t;

typedef struct world_event_state {
    host_event_t  ring[MP_WORLD_EVENT_RING];
    uint32_t      head;
    uint32_t      count;
    uint16_t      sequence;        /* the newest number handed out, never reset */
    uint32_t      clock;
    uint32_t      posted_now;      /* taken in this substep */
    view_events_t view[VIEWS];

    uint32_t posted;
    uint32_t posted_kind[MP_WORLD_EVENT_KINDS];
    uint32_t full;                 /* refused: the substep or the ring had no room */
    uint32_t full_seen;            /* of them, a kind that is seen */
    uint32_t gave_way;             /* taken, then dropped in a full substep for a kind seen */
    uint32_t unclaimed;            /* no census settled its life, or none ran */
    uint32_t passed_over;          /* its actor stood beside the one the census read */
    uint32_t bound;                /* the census read its actor */
    uint32_t anchored;             /* the life last counted, the actor gone */
    mp_world_event_music_fn_t music;
} world_event_state_t;

static world_event_state_t events;

static uint8_t bit_of(size_t view)
{
    return (uint8_t)(1u << view);
}

static host_event_t *ring_at(uint32_t i)
{
    return &events.ring[(events.head + i) % MP_WORLD_EVENT_RING];
}

static uint32_t age_of(const host_event_t *e)
{
    return events.clock - e->clock;
}

/* Where this substep's events begin in the ring. Everything a hull posted in it sits at the end,
 * and nothing older is still waiting for its life: the end of every substep settles or drops it. */
static uint32_t first_of_this_substep(void)
{
    return events.count - (events.posted_now < events.count ? events.posted_now : events.count);
}

/* ==============================================================================================
 * The host: posting, and the census that settles each event's life.
 * ============================================================================================ */

/* The slot for an event of `kind`: the next one of the ring while the substep has room, otherwise
 * the newest event of this substep whose kind gives way to it. That one was posted a moment ago
 * and no census has settled it, so no peer was ever offered it; its place and its slot of the
 * substep's share go to the new one, and the share stays what the client's memory is sized for.
 * NULL when neither is there. */
static host_event_t *take_slot(uint8_t kind)
{
    uint32_t i;

    if (events.posted_now < MP_WORLD_EVENT_POSTS_PER_SUBSTEP &&
        events.count < MP_WORLD_EVENT_RING) {
        ++events.count;
        ++events.posted_now;
        return ring_at(events.count - 1u);
    }
    for (i = events.count; i-- > first_of_this_substep();) {
        host_event_t *e = ring_at(i);

        if (e->state == SLOT_POSTED && e->clock == events.clock &&
            mp_world_event_gives_way(e->wire.kind, kind)) {
            ++events.gave_way;
            return e;
        }
    }
    return NULL;
}

/* One event at an actor, numbered, its life left to the census. The place and the reach are kept
 * for the question whether it concerns a peer, and the place travels when the record's own
 * quantisation carries it. */
static bool post(uint8_t kind, uintptr_t actor, uint16_t a, const uint8_t *tail, size_t tail_bytes,
                 const float *place, float radius)
{
    host_event_t *e;
    uint32_t      key       = 0;
    uint8_t       life      = 0;
    bool          key_known = false;

    /* A client replays through the trampolines and its own hulls stay quiet; a side with no
     * session, or one whose census does not run, has nobody to tell. */
    if (!mp_enemy_sync_describing() || actor == 0u || !mp_world_event_kind_known(kind) ||
        tail_bytes != mp_world_event_tail_bytes(kind) || (tail_bytes != 0u && tail == NULL)) {
        return false;
    }
    /* The key is the actor's own, read now while the actor is certainly there; the census that
     * follows names it again. A side with no binding to ask learns it from the census alone. */
    if (mp_enemy_bind_index(actor, &key)) {
        if (key >= MP_WIRE_KEY_COUNT) {
            ++events.unclaimed;
            return false;
        }
        key_known = true;
    }
    e = take_slot(kind);
    if (e == NULL) {
        ++events.full;
        events.full_seen += mp_world_event_kind_seen(kind) ? 1u : 0u;
        return false;
    }
    memset(e, 0, sizeof *e);
    events.sequence    = mp_world_event_sequence_next(events.sequence);
    e->state           = SLOT_POSTED;
    e->wire.sequence   = events.sequence;
    e->wire.kind       = kind;
    e->wire.at_actor   = true;
    e->wire.a          = a;
    e->wire.key        = (uint16_t)key;
    e->wire.tail_bytes = (uint8_t)tail_bytes;
    if (tail_bytes != 0u) {
        memcpy(e->wire.tail, tail, tail_bytes);
    }
    if (place != NULL) {
        uint32_t wire[3] = { 0u, 0u, 0u };

        memcpy(e->place, place, sizeof e->place);
        e->radius         = radius;
        e->wire.has_place = mp_enemy_wire_put_position(place[0], &wire[0]) &&
                            mp_enemy_wire_put_position(place[1], &wire[1]) &&
                            mp_enemy_wire_put_position(place[2], &wire[2]);
        e->wire.place[0]  = (uint16_t)wire[0];
        e->wire.place[1]  = (uint16_t)wire[1];
        e->wire.place[2]  = (uint16_t)wire[2];
    }
    e->actor      = actor;
    e->clock      = events.clock;
    e->key_known  = key_known;
    e->life_noted = key_known && mp_enemy_sync_generation(key, &life);
    e->life_then  = life;
    ++events.posted;
    ++events.posted_kind[kind];
    return true;
}

bool mp_world_event_post_at_actor_placed(uint8_t kind, uintptr_t actor, uint16_t a,
                                         const float place[3], float radius, const uint8_t *tail,
                                         size_t tail_bytes)
{
    return post(kind, actor, a, tail, tail_bytes, place, radius);
}

bool mp_world_event_post_at_actor(uint8_t kind, uintptr_t actor, uint16_t a, const uint8_t *tail,
                                  size_t tail_bytes)
{
    return post(kind, actor, a, tail, tail_bytes, NULL, 0.0f);
}

bool mp_world_event_post_heard(uint8_t kind, uintptr_t actor, uint16_t a, const float place[3],
                               float radius)
{
    if (place == NULL || !mp_world_event_kind_heard(kind) ||
        mp_world_event_tail_bytes(kind) != 0u) {
        return false;
    }
    return post(kind, actor, a, NULL, 0u, place, radius);
}

void mp_world_event_set_music_source(mp_world_event_music_fn_t source)
{
    events.music = source;
}

void mp_world_event_census_saw(uintptr_t actor)
{
    uint32_t i;

    /* Asked for every actor of every walk, a client's included; only the events of this substep
     * can still be waiting, and a side that posted none has nothing to look at. */
    for (i = first_of_this_substep(); events.posted_now != 0u && i < events.count; ++i) {
        host_event_t *e = ring_at(i);

        if (e->state == SLOT_POSTED && e->actor == actor) {
            e->in_pool = true;
        }
    }
}

void mp_world_event_census_row(uint32_t key, uint8_t life, uintptr_t actor)
{
    uint32_t i;

    /* Everything still waiting for its life was posted in this substep, so a substep in which
     * nothing was posted has nothing to walk; the census asks this for every live key. */
    for (i = first_of_this_substep(); events.posted_now != 0u && i < events.count; ++i) {
        host_event_t *e = ring_at(i);

        if (e->state == SLOT_POSTED && e->actor == actor && (!e->key_known || e->wire.key == key)) {
            e->wire.key  = (uint16_t)key;
            e->wire.life = life;
            e->state     = SLOT_OPEN;
            ++events.bound;
        }
    }
}

void mp_world_event_census_over(void)
{
    uint32_t i;

    for (i = first_of_this_substep(); i < events.count; ++i) {
        host_event_t *e = ring_at(i);

        if (e->state != SLOT_POSTED) {
            continue;
        }
        switch (mp_world_event_unread(e->in_pool, e->life_noted)) {
        case MP_WORLD_EVENT_ANCHORED:
            e->wire.life = e->life_then;
            e->state     = SLOT_OPEN;
            ++events.anchored;
            break;
        case MP_WORLD_EVENT_PASSED_OVER:
            e->state = SLOT_DEAD;
            ++events.passed_over;
            break;
        case MP_WORLD_EVENT_UNCLAIMED:
        default:
            e->state = SLOT_DEAD;
            ++events.unclaimed;
            break;
        }
    }
}

/* An event leaves the ring from its oldest end: one dropped, or one past the window, whose peers
 * that were sent it and never proved it are counted. */
static void retire(void)
{
    while (events.count != 0u) {
        host_event_t *e = ring_at(0u);
        size_t        view;

        if (e->state != SLOT_DEAD && age_of(e) < MP_WORLD_EVENT_WINDOW) {
            return;
        }
        for (view = 0; e->state == SLOT_OPEN && view < VIEWS; ++view) {
            if ((e->staged & bit_of(view)) != 0u && (e->delivered & bit_of(view)) == 0u) {
                ++events.view[view].unacknowledged;
            }
        }
        e->state    = SLOT_FREE;
        events.head = (events.head + 1u) % MP_WORLD_EVENT_RING;
        --events.count;
    }
}

void mp_world_event_census_done(void)
{
    uint32_t i;

    for (i = 0; i < events.count; ++i) {
        host_event_t *e = ring_at(i);

        if (e->state == SLOT_POSTED) {
            e->state = SLOT_DEAD;   /* no census ran: a host with no peer tells nobody */
            ++events.unclaimed;
        }
    }
    ++events.clock;
    events.posted_now = 0u;
    retire();
}

/* ==============================================================================================
 * The host: each view's part.
 * ============================================================================================ */

/* Asked once per view and kept. */
static bool concerns(size_t view, host_event_t *e)
{
    float at[3]  = { 0.0f, 0.0f, 0.0f };
    bool  placed = false;
    bool  yes;

    if ((e->decided & bit_of(view)) != 0u) {
        return (e->concerns & bit_of(view)) != 0u;
    }
    if (e->wire.has_place) {
        placed = mp_enemy_sync_view_position(view, at);
    }
    yes = mp_world_event_concerns(
        e->wire.kind, e->wire.at_actor,
        e->wire.at_actor && mp_enemy_sync_view_concerns(view, e->wire.key, e->wire.life),
        e->wire.has_place, e->place, placed, at, e->radius);
    e->decided |= bit_of(view);
    if (yes) {
        e->concerns |= bit_of(view);
    } else {
        ++events.view[view].not_relevant;
        events.view[view].out_of_earshot +=
            (e->wire.has_place && placed && mp_world_event_kind_heard(e->wire.kind)) ? 1u : 0u;
    }
    return yes;
}

/* Which events go to view `view` in a room of `room` bytes: the newest first while the part takes
 * them, written oldest first so a client performs them in the order they happened. Returns their
 * bytes; `left_out` counts the ones that concerned the view and did not fit. */
static size_t choose_events(size_t view, size_t room, uint16_t *slots, uint32_t *count,
                            uint32_t *left_out)
{
    size_t   used = 0;
    uint32_t n    = 0;
    uint32_t i;

    *left_out = 0u;
    for (i = events.count; i-- > 0u;) {
        host_event_t    *e = ring_at(i);
        mp_world_event_t wire;
        size_t           bytes;

        if (e->state != SLOT_OPEN || age_of(e) >= MP_WORLD_EVENT_WINDOW ||
            (e->delivered & bit_of(view)) != 0u || !concerns(view, e)) {
            continue;
        }
        wire     = e->wire;
        wire.age = (uint8_t)age_of(e);
        bytes    = mp_world_event_bytes(&wire);
        if (bytes == 0u || n >= MP_WORLD_EVENT_PART_MAX || used + bytes > room) {
            ++*left_out;
            continue;
        }
        slots[n++] = (uint16_t)((events.head + i) % MP_WORLD_EVENT_RING);
        used += bytes;
    }
    /* Newest first in, oldest first out. */
    for (i = 0; i < n / 2u; ++i) {
        uint16_t swap = slots[i];

        slots[i]         = slots[n - 1u - i];
        slots[n - 1u - i] = swap;
    }
    *count = n;
    return used;
}

size_t mp_world_event_part_bytes(size_t view)
{
    uint16_t slots[MP_WORLD_EVENT_PART_MAX];
    uint32_t count    = 0;
    uint32_t left_out = 0;

    /* Asked only after a census: whether an event concerns a view is decided here and kept, and
     * without a census there is nothing to decide it by. */
    if (view >= VIEWS || !mp_enemy_sync_describing()) {
        return 0u;
    }
    return choose_events(view, MP_WORLD_EVENT_PART_MAX_BYTES, slots, &count, &left_out);
}

size_t mp_world_event_write(size_t view, uint8_t *out, size_t room)
{
    view_events_t        *v;
    mp_world_event_head_t head;
    uint16_t              slots[MP_WORLD_EVENT_PART_MAX];
    uint32_t              count    = 0;
    uint32_t              left_out = 0;
    size_t                at       = MP_WORLD_EVENT_HEAD_BYTES;
    uint32_t              i;

    if (view >= VIEWS || out == NULL || room < MP_WORLD_EVENT_HEAD_BYTES) {
        return 0u;
    }
    v          = &events.view[view];
    v->written = 0u;
    (void)choose_events(view, room - MP_WORLD_EVENT_HEAD_BYTES, slots, &count, &left_out);
    for (i = 0; i < count; ++i) {
        host_event_t    *e = &events.ring[slots[i]];
        mp_world_event_t wire;
        size_t           wrote = 0;

        wire     = e->wire;
        wire.age = (uint8_t)age_of(e);
        if (!mp_world_event_put(&wire, out + at, room - at, &wrote)) {
            break;   /* measured to fit a moment ago */
        }
        at += wrote;
        v->written_slot[v->written] = slots[i];
        v->written_seq[v->written]  = wire.sequence;
        ++v->written;
        v->offered += (e->staged & bit_of(view)) == 0u ? 1u : 0u;
        e->staged |= bit_of(view);
    }
    /* The music is asked afresh for every block, so a claim that ends is said to end at once. */
    if (events.music != NULL) {
        events.music(view, &v->music_state, &v->music_sequence);
    }
    head.count          = v->written;
    head.music_state    = v->music_state;
    head.music_sequence = v->music_sequence;
    mp_world_event_put_head(&head, out);
    v->left_out += left_out;
    v->used = true;
    ++v->blocks;
    return at;
}

bool mp_world_event_waits_on(size_t view, uint32_t key, uint8_t life)
{
    uint32_t i;

    if (view >= VIEWS) {
        return false;
    }
    for (i = 0; i < events.count; ++i) {
        const host_event_t *e = ring_at(i);

        if (e->state == SLOT_OPEN && e->wire.at_actor && e->wire.key == key &&
            e->wire.life == life && age_of(e) < MP_WORLD_EVENT_WINDOW &&
            (e->delivered & bit_of(view)) == 0u &&
            ((e->decided & bit_of(view)) == 0u || (e->concerns & bit_of(view)) != 0u)) {
            return true;
        }
    }
    return false;
}

void mp_world_event_sent_for(size_t view, uint32_t tick)
{
    view_events_t *v;
    uint32_t       slot;

    if (view >= VIEWS) {
        return;
    }
    v    = &events.view[view];
    slot = tick % STAMPS;
    v->stamp_tick[slot]  = tick;
    v->stamp_valid[slot] = true;
    v->stamp_count[slot] = v->written;
    memcpy(v->stamp_slot[slot], v->written_slot, sizeof v->written_slot);
    memcpy(v->stamp_seq[slot], v->written_seq, sizeof v->written_seq);
    v->written = 0u;
}

/* Nought is never an acknowledgement: it is what a side that holds nothing says, and what a peer
 * that is forgotten is acknowledged with on its way out. */
void mp_world_event_acked_for(size_t view, uint32_t tick)
{
    view_events_t *v;
    uint32_t       slot;
    uint32_t       i;

    if (view >= VIEWS || tick == 0u) {
        return;
    }
    v    = &events.view[view];
    slot = tick % STAMPS;
    if (!v->stamp_valid[slot] || v->stamp_tick[slot] != tick) {
        return;
    }
    for (i = 0; i < v->stamp_count[slot]; ++i) {
        host_event_t *e = &events.ring[v->stamp_slot[slot][i]];

        if (e->state == SLOT_OPEN && e->wire.sequence == v->stamp_seq[slot][i] &&
            (e->delivered & bit_of(view)) == 0u) {
            e->delivered |= bit_of(view);
            ++v->acknowledged;
        }
    }
}

void mp_world_event_abandon_for(size_t view)
{
    if (view < VIEWS) {
        events.view[view].written = 0u;
    }
}

/* A new connection was not there for anything that happened before it. */
void mp_world_event_forget_view(size_t view)
{
    view_events_t *v;
    uint32_t       i;

    if (view >= VIEWS) {
        return;
    }
    v = &events.view[view];
    v->written = 0u;
    memset(v->stamp_valid, 0, sizeof v->stamp_valid);
    v->music_state    = MP_WORLD_EVENT_MUSIC_NONE;
    v->music_sequence = MP_WORLD_EVENT_MUSIC_NONE;
    for (i = 0; i < events.count; ++i) {
        host_event_t *e = ring_at(i);

        e->decided  |= bit_of(view);
        e->concerns &= (uint8_t)~bit_of(view);
        e->staged   &= (uint8_t)~bit_of(view);
    }
}

/* ==============================================================================================
 * Both.
 * ============================================================================================ */

void mp_world_event_reset(void)
{
    size_t view;

    memset(events.ring, 0, sizeof events.ring);
    events.head       = 0u;
    events.count      = 0u;
    events.posted_now = 0u;
    for (view = 0; view < VIEWS; ++view) {
        view_events_t *v = &events.view[view];

        v->written = 0u;
        memset(v->stamp_valid, 0, sizeof v->stamp_valid);
        v->music_state    = MP_WORLD_EVENT_MUSIC_NONE;
        v->music_sequence = MP_WORLD_EVENT_MUSIC_NONE;
    }
    mp_world_event_client_reset();
}

void mp_world_event_report(void)
{
    size_t view;

    log_info("the world events (host): %u posted (emitter %u, clang %u, limb %u, sound %u, blast "
             "%u, zap %u), %u refused for a full substep or ring (%u of them a kind that is seen), "
             "%u that gave way in a full substep to a kind that is seen, %u for an actor the "
             "census did not read or in a substep no census ran, %u for an actor the census passed "
             "over for another under its key, %u settled by the census, %u on the life last "
             "counted; the newest number %u",
             (unsigned)events.posted,
             (unsigned)events.posted_kind[MP_WORLD_EVENT_SCRIPT_EMITTER],
             (unsigned)events.posted_kind[MP_WORLD_EVENT_NPC_CLANG],
             (unsigned)events.posted_kind[MP_WORLD_EVENT_LIMB_FLY],
             (unsigned)events.posted_kind[MP_WORLD_EVENT_SCRIPT_SOUND],
             (unsigned)events.posted_kind[MP_WORLD_EVENT_EXPLODE_AT],
             (unsigned)events.posted_kind[MP_WORLD_EVENT_ZAP_ARCS], (unsigned)events.full,
             (unsigned)events.full_seen, (unsigned)events.gave_way, (unsigned)events.unclaimed,
             (unsigned)events.passed_over, (unsigned)events.bound, (unsigned)events.anchored,
             (unsigned)events.sequence);
    for (view = 0; view < VIEWS; ++view) {
        const view_events_t *v = &events.view[view];

        if (!v->used) {
            continue;
        }
        log_info("the world events to one peer: peer %u, %u block(s), %u event(s) put in them, %u "
                 "not relevant to it (%u of them out of earshot), %u acknowledged, %u time(s) one "
                 "was left out of a full part, %u expired unacknowledged; music %u and %u claimed "
                 "for it",
                 (unsigned)view, (unsigned)v->blocks, (unsigned)v->offered,
                 (unsigned)v->not_relevant, (unsigned)v->out_of_earshot,
                 (unsigned)v->acknowledged, (unsigned)v->left_out,
                 (unsigned)v->unacknowledged, (unsigned)v->music_state,
                 (unsigned)v->music_sequence);
    }
    mp_world_event_client_report();
}
