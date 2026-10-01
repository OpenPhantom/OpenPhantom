/* mp_actor_loop.c: the loops of the actors' scripts, on the host's note and on a client's
 * replicas. See the header.
 */
#include "mp_actor_loop.h"

#include "mp_enemy_sync.h"
#include "mp_level_state_rule.h"
#include "mp_script_sound_rule.h"
#include "mp_wire.h"

#include "common/logging.h"
#include "common/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* How many notes a loop waits for the census to read its actor before it is dropped. A loop is
 * started inside the engine's substep and the note is built before that substep's census, so an
 * actor woken in the same substep is read one note later; three is that and a margin. */
#define UNSETTLED_NOTES 3u

/* A client starts at most one loop per wanted entry, and a few more stand while a replica that
 * was removed has not been noticed yet. */
#define STARTED_MAX (MP_LEVEL_STATE_LOOPS_MAX * 2u)

/* One loop on the host, from the call that started it until something ends it. */
typedef struct host_loop {
    bool      used;
    bool      settled;     /* the census read the actor, and `life` is its life */
    uintptr_t actor;
    uint16_t  key;
    uint8_t   life;
    uint16_t  call;
    uint8_t   unsettled;   /* notes built while the census had not read the actor */
} host_loop_t;

/* One loop this client started on a replica. */
typedef struct client_loop {
    bool      used;
    uint16_t  key;
    uint8_t   life;
    uint16_t  call;
    uintptr_t actor;
    int32_t   channel;     /* what the replica's handle named at the last look */
    float     at[3];       /* where the replica stood at the last look */
    bool      refusal_counted;
} client_loop_t;

typedef struct loops_state {
    uint32_t resets;       /* the enemy table's reset count the memory below belongs to */
    bool     resets_known;
    const mp_actor_loop_engine_t *engine;

    host_loop_t host[MP_LEVEL_STATE_LOOPS_MAX];
    uint8_t     host_written;

    mp_level_loop_t want[MP_LEVEL_STATE_LOOPS_MAX];
    uint8_t         wanted;
    bool            waiting_counted[MP_LEVEL_STATE_LOOPS_MAX];
    client_loop_t   started[STARTED_MAX];

    uint32_t h_calls;         /* the script's calls, one a substep while the host's gate refuses */
    uint32_t h_started;       /* loops put on record */
    uint32_t h_new_life;      /* of those, in a slot whose old life's loop was still on record */
    uint32_t h_replaced;
    uint32_t h_again;         /* the call on record asked for again */
    uint32_t h_stopped;
    uint32_t h_ended;
    uint32_t h_never_read;
    uint32_t h_full;
    uint32_t h_no_key;
    uint32_t h_stop_unheld;   /* command 17 of an actor with no loop on record */

    uint32_t c_notes;
    uint32_t c_starts;
    uint32_t c_taken_over;    /* a loop the replica's own script had started, kept as the host's */
    uint32_t c_own_stopped;   /* another sound the replica's own script had started, stopped */
    uint32_t c_refused;
    uint32_t c_stopped;
    uint32_t c_left;
    uint32_t c_waited;
    uint32_t c_unreadable;
    uint32_t c_full;
} loops_state_t;

static loops_state_t loops;

/* Everything above belongs to one enemy table: after its reset, which every way out of a level and
 * a session takes, no key, life or actor here means anything. Nothing is touched on the way out:
 * a level's end frees every channel itself, and after a session the actors are the engine's. */
static void settle_reset(void)
{
    uint32_t now = mp_enemy_sync_resets();

    if (loops.resets_known && loops.resets == now) {
        return;
    }
    memset(loops.host, 0, sizeof loops.host);
    loops.host_written = 0u;
    loops.wanted       = 0u;
    memset(loops.waiting_counted, 0, sizeof loops.waiting_counted);
    memset(loops.started, 0, sizeof loops.started);
    loops.resets       = now;
    loops.resets_known = true;
}

void mp_actor_loop_set_engine(const mp_actor_loop_engine_t *engine)
{
    loops.engine = engine;
}

/* ==============================================================================================
 * The host.
 * ============================================================================================ */

static host_loop_t *host_find(uintptr_t actor)
{
    size_t i;

    for (i = 0; i < MP_LEVEL_STATE_LOOPS_MAX; ++i) {
        if (loops.host[i].used && loops.host[i].actor == actor) {
            return &loops.host[i];
        }
    }
    return NULL;
}

/* The one question the table asks of the census: whether it reads this loop's actor under its key,
 * and in which life. */
static bool census_reads(const host_loop_t *h, uint8_t *life)
{
    return mp_enemy_sync_actor_for(h->key) == h->actor && mp_enemy_sync_generation(h->key, life);
}

/* Whether the loop on record for this actor's slot is the calling life's. The pool hands a slot
 * to the next actor, so the same address under another key, or a settled loop the census no longer
 * reads in its life, is a new life whose script starts its own; one the census has not read yet
 * cannot tell and is taken as the same. */
static bool still_its_life(const host_loop_t *h, uint32_t key)
{
    uint8_t life = 0u;

    if (h->key != key) {
        return false;
    }
    return !h->settled || (census_reads(h, &life) && life == h->life);
}

void mp_actor_loop_started(uintptr_t actor, uint32_t key, uint16_t call)
{
    host_loop_t *h;
    size_t       i;

    settle_reset();
    ++loops.h_calls;
    if (actor == 0u || key >= MP_WIRE_KEY_COUNT) {
        ++loops.h_no_key;
        return;
    }
    h = host_find(actor);
    if (h != NULL && !still_its_life(h, key)) {
        h->used = false;
        ++loops.h_ended;
        ++loops.h_new_life;
        h = NULL;
    }
    if (h != NULL) {
        if (h->call != call) {
            ++loops.h_replaced;
            h->call = call;
        } else {
            ++loops.h_again;
        }
        return;
    }
    for (i = 0; i < MP_LEVEL_STATE_LOOPS_MAX; ++i) {
        if (!loops.host[i].used) {
            h = &loops.host[i];
            memset(h, 0, sizeof *h);
            h->used  = true;
            h->actor = actor;
            h->key   = (uint16_t)key;
            h->call  = call;
            ++loops.h_started;
            return;
        }
    }
    ++loops.h_full;
}

void mp_actor_loop_stopped(uintptr_t actor)
{
    host_loop_t *h;

    settle_reset();
    h = host_find(actor);
    if (h == NULL) {
        ++loops.h_stop_unheld;
        return;
    }
    h->used = false;
    ++loops.h_stopped;
}

/* Whether a loop goes into this note: its actor is what the census reads under its key, in the
 * life it was started in. The first time the census reads the actor, that life is taken; a loop
 * that no longer matches is forgotten, and one the census never reads is dropped after a while. */
static bool host_says(host_loop_t *h)
{
    uint8_t life = 0u;
    bool    read = census_reads(h, &life);

    if (!h->settled) {
        if (read) {
            h->settled = true;
            h->life    = life;
            return true;
        }
        if (++h->unsettled >= UNSETTLED_NOTES) {
            h->used = false;
            ++loops.h_never_read;
        }
        return false;
    }
    if (!read || life != h->life) {
        h->used = false;
        ++loops.h_ended;
        return false;
    }
    return true;
}

void mp_actor_loop_write(mp_level_state_note_t *note)
{
    size_t i;

    settle_reset();
    if (note == NULL) {
        return;
    }
    note->parts |= (uint8_t)MP_LEVEL_STATE_PART_ACTOR_LOOPS;
    note->loops = 0u;
    for (i = 0; i < MP_LEVEL_STATE_LOOPS_MAX; ++i) {
        host_loop_t *h = &loops.host[i];

        if (!h->used || !host_says(h)) {
            continue;
        }
        note->loop[note->loops].key  = h->key;
        note->loop[note->loops].life = h->life;
        note->loop[note->loops].call = h->call;
        ++note->loops;
    }
    loops.host_written = note->loops;
}

/* ==============================================================================================
 * A client.
 * ============================================================================================ */

void mp_actor_loop_take(const mp_level_state_note_t *note)
{
    settle_reset();
    if (note == NULL || (note->parts & MP_LEVEL_STATE_PART_ACTOR_LOOPS) == 0u ||
        note->loops > MP_LEVEL_STATE_LOOPS_MAX) {
        return;
    }
    ++loops.c_notes;
    loops.wanted = note->loops;
    memcpy(loops.want, note->loop, note->loops * sizeof note->loop[0]);
    memset(loops.waiting_counted, 0, sizeof loops.waiting_counted);
}

/* The cell and the place of a replica, as the engine keeps them. */
static bool look_at(uintptr_t actor, int32_t *channel, float at[3])
{
    if (!memory_try_read(actor + MP_ACTOR_LOOP_HANDLE, channel, sizeof *channel) ||
        !memory_try_read(actor + MP_ACTOR_SOUND_POS, at, 3u * sizeof at[0])) {
        ++loops.c_unreadable;
        return false;
    }
    return true;
}

/* A replica that no longer stands for its key and life was removed, or its slot is somebody
 * else's now. What it was playing goes on where it stood, as the host's does, and stops owning
 * the cell. Everything else of it is forgotten; a replica of the same life built again starts its
 * own. */
static void look_after_the_started(void)
{
    size_t i;

    for (i = 0; i < STARTED_MAX; ++i) {
        client_loop_t *c = &loops.started[i];

        if (!c->used) {
            continue;
        }
        if (loops.engine->stands(c->key, c->actor, c->life)) {
            (void)look_at(c->actor, &c->channel, c->at);
            continue;
        }
        if (c->channel >= 0 &&
            loops.engine->leave(c->channel,
                                (const int32_t *)(c->actor + MP_ACTOR_LOOP_HANDLE), c->at)) {
            ++loops.c_left;
        }
        c->used = false;
    }
}

static client_loop_t *started_for(uint16_t key, uint8_t life)
{
    size_t i;

    for (i = 0; i < STARTED_MAX; ++i) {
        client_loop_t *c = &loops.started[i];

        if (c->used && c->key == key && c->life == life) {
            return c;
        }
    }
    return NULL;
}

static bool is_wanted(const client_loop_t *c)
{
    size_t i;

    for (i = 0; i < loops.wanted; ++i) {
        if (loops.want[i].key == c->key && loops.want[i].life == c->life) {
            return true;
        }
    }
    return false;
}

/* The replica of the life the host names, or 0 while this side holds another life or none. */
static uintptr_t replica_of(const mp_level_loop_t *want)
{
    uint8_t life = 0u;

    if (!mp_enemy_sync_generation(want->key, &life) || life != want->life) {
        return 0u;
    }
    return mp_enemy_sync_replica_for(want->key);
}

/* A row for a loop this side is about to start, before it starts: a loop started with no row to
 * remember it could never be stopped. */
static client_loop_t *new_row(const mp_level_loop_t *want)
{
    size_t i;

    for (i = 0; i < STARTED_MAX; ++i) {
        client_loop_t *c = &loops.started[i];

        if (!c->used) {
            memset(c, 0, sizeof *c);
            c->used = true;
            c->key  = want->key;
            c->life = want->life;
            return c;
        }
    }
    ++loops.c_full;
    return NULL;
}

/* A replica whose own script ran before the host described it may hold a loop in its cell already.
 * A second start there would leave the first playing with that cell as its owner, and the end of
 * either writes -1 over the other's number. The sound the host wants is taken over as it plays;
 * another sound is stopped first, the engine's way; a number the cell no longer owns is stale and
 * is played over. True when the cell's loop was taken over. */
static bool take_over(uintptr_t actor, int32_t channel, const float at[3],
                      const mp_level_loop_t *want)
{
    client_loop_t *c;

    if (channel < 0 || loops.engine->found == NULL) {
        return false;
    }
    switch (loops.engine->found(channel, (const int32_t *)(actor + MP_ACTOR_LOOP_HANDLE),
                                want->call)) {
    case MP_ACTOR_LOOP_FOUND_SAME:
        if ((c = new_row(want)) != NULL) {
            c->call    = want->call;
            c->actor   = actor;
            c->channel = channel;
            memcpy(c->at, at, sizeof c->at);
            ++loops.c_taken_over;
        }
        return true;
    case MP_ACTOR_LOOP_FOUND_OTHER:
        (void)loops.engine->stop(actor);
        ++loops.c_own_stopped;
        return false;
    case MP_ACTOR_LOOP_FOUND_NONE:
    default:
        return false;
    }
}

static void start(client_loop_t *c, uintptr_t actor, const mp_level_loop_t *want)
{
    int32_t channel = -1;
    float   at[3];

    if (!look_at(actor, &channel, at)) {
        return;
    }
    if (c == NULL && take_over(actor, channel, at, want)) {
        return;
    }
    if (c == NULL && (c = new_row(want)) == NULL) {
        return;
    }
    loops.engine->play(want->call, (int32_t *)(actor + MP_ACTOR_LOOP_HANDLE),
                       (const float *)(actor + MP_ACTOR_SOUND_POS));
    (void)look_at(actor, &channel, at);
    c->call    = want->call;
    c->actor   = actor;
    c->channel = channel;
    memcpy(c->at, at, sizeof c->at);
    if (channel >= 0) {
        ++loops.c_starts;
        c->refusal_counted = false;
    } else if (!c->refusal_counted) {
        ++loops.c_refused;   /* once until it plays: asked again every substep while far */
        c->refusal_counted = true;
    }
}

static void play_the_wanted(void)
{
    size_t i;

    for (i = 0; i < loops.wanted; ++i) {
        const mp_level_loop_t *want    = &loops.want[i];
        client_loop_t         *c       = started_for(want->key, want->life);
        uintptr_t              actor   = replica_of(want);
        mp_actor_loop_step_t   step;

        if (actor == 0u) {
            loops.c_waited += loops.waiting_counted[i] ? 0u : 1u;
            loops.waiting_counted[i] = true;
            continue;
        }
        step = mp_actor_loop_step(true, want->call, c != NULL, c != NULL ? c->call : 0u,
                                  c != NULL && c->channel >= 0);
        if (step == MP_ACTOR_LOOP_REPLACE) {
            (void)loops.engine->stop(c->actor);
            ++loops.c_stopped;
        }
        if (step == MP_ACTOR_LOOP_START || step == MP_ACTOR_LOOP_REPLACE) {
            start(c, actor, want);
        }
    }
}

static void stop_the_unwanted(void)
{
    size_t i;

    for (i = 0; i < STARTED_MAX; ++i) {
        client_loop_t *c = &loops.started[i];

        if (!c->used || is_wanted(c)) {
            continue;
        }
        if (mp_actor_loop_step(false, 0u, true, c->call, c->channel >= 0) ==
            MP_ACTOR_LOOP_STOP) {
            (void)loops.engine->stop(c->actor);
            ++loops.c_stopped;
        }
        c->used = false;
    }
}

void mp_actor_loop_flush(void)
{
    settle_reset();
    if (loops.engine == NULL) {
        return;
    }
    look_after_the_started();
    stop_the_unwanted();
    play_the_wanted();
}

void mp_actor_loop_report(void)
{
    /* The calls are the script's, which asks again in every substep its actor's cell holds no
     * channel, so while the host's own gate refuses for distance; the starts are the loops. */
    log_info("the actors' loops (host): %u call(s) of a script that asked for a loop, %u started "
             "one on record (%u of them a new life in the slot of an actor whose loop was still on "
             "record), %u changed the call on record, %u asked for the one on record again; %u "
             "stopped by command 17 (%u more with no loop on record), %u ended by a removal or a "
             "new life, %u the census never read, %u refused for a full table, %u without a key; "
             "%u in the last note",
             (unsigned)loops.h_calls, (unsigned)loops.h_started, (unsigned)loops.h_new_life,
             (unsigned)loops.h_replaced, (unsigned)loops.h_again, (unsigned)loops.h_stopped,
             (unsigned)loops.h_stop_unheld, (unsigned)loops.h_ended, (unsigned)loops.h_never_read,
             (unsigned)loops.h_full, (unsigned)loops.h_no_key, (unsigned)loops.host_written);
    log_info("the actors' loops (client): %u note(s) taken, %u wanted in the last, %u started "
             "here, %u taken over where the replica's own script had started it, %u of another "
             "sound the replica's own script had started stopped first, %u refused by the "
             "engine's start gate (distance, a duplicate or no channel), %u stopped here, %u left "
             "playing where their replica was removed, %u waited for a replica, %u at a replica "
             "that did not read, %u refused for a full table",
             (unsigned)loops.c_notes, (unsigned)loops.wanted, (unsigned)loops.c_starts,
             (unsigned)loops.c_taken_over, (unsigned)loops.c_own_stopped,
             (unsigned)loops.c_refused, (unsigned)loops.c_stopped, (unsigned)loops.c_left,
             (unsigned)loops.c_waited, (unsigned)loops.c_unreadable, (unsigned)loops.c_full);
}
