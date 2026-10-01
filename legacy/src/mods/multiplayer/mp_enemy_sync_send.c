/* mp_enemy_sync_send.c: the host's half of the enemy block.
 *
 * It reads the census for every peer, and it keeps what each peer is believed to hold, advancing
 * that belief only when the payload was taken. Which records one peer's block carries is decided
 * in mp_enemy_sync_choose.c, each a delta against that belief. The receiving half, the table and
 * the census are in mp_enemy_sync.c; the three share the state through mp_enemy_sync_internal.h.
 *
 * The budget, measured through this codec rather than estimated: 11 bytes for one walking actor
 * including its two bytes of identity, and 442 bytes for 37 of them with the block header and
 * the length in front of it, against an earlier estimate of 23 and 851. Part of the difference
 * is real, a steady record is a delta and a walking actor changes three fields; part is not a
 * saving at all, because the records measured carried no node rotations, and a head that turns
 * costs up to twenty bytes more that the 11 does not include. Either way the measured peak fits
 * one payload, and the sweep across packets is only needed for the first one after a level or a
 * join.
 *
 * The copies an editor spawned ride in a part of their own behind the placements (see the
 * header). Their rows come out of the same census, one walk a substep, and each counts its
 * lives like a placement does.
 *
 * The census also settles the life of every world event a hull posted in this substep, and the
 * four moments of a view's payload are the world events' too: each hook is called before anything
 * here can return early, because a payload that went out carried its events whatever this view
 * then decides about its records.
 */
#include "mp_enemy_sync_internal.h"

#include "mp_enemy_bind.h"
#include "mp_enemy_dead_watch.h"
#include "mp_enemy_wire.h"
#include "mp_payload_prefix.h"
#include "mp_world_event.h"

#include "common/logging.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void set_bit(uint8_t *map, size_t index)
{
    map[index >> 3] |= (uint8_t)(1u << (index & 7u));
}

static void clear_bit(uint8_t *map, size_t index)
{
    map[index >> 3] &= (uint8_t)~(uint8_t)(1u << (index & 7u));
}

static bool has_bit(const uint8_t *map, size_t index)
{
    return (map[index >> 3] & (uint8_t)(1u << (index & 7u))) != 0u;
}

/* A copy's record carries k in the index field, which is a byte. */
_Static_assert(MP_WIRE_COPY_MAX <= 256u, "k must fit the record's index field");
_Static_assert(MP_WIRE_COPY_MAX % 8u == 0u, "the copies' bitmap is whole bytes");

/* The bits of an acknowledgement reach thirty one substeps behind its newest, and an
 * acknowledgement further ahead of the last one than this ring is a view given up, so every
 * substep one steps over lies inside the bits. That holds only while the ring and the client's
 * history are the same length, which two different constants say. */
_Static_assert(MP_ENEMY_SYNC_ACK_RING == MP_PAYLOAD_ACK_WINDOW + 1u,
               "the stamps a view remembers are not the ticks the bits can name");

uint32_t mp_enemy_sync_wire_index(size_t key)
{
    return (uint32_t)(mp_wire_key_is_copy((uint32_t)key) ? key - MP_WIRE_KEY_COPY_BASE : key);
}

/* A key whose record means nothing to any view any more: it died, or it is a life no view has
 * seen. `leave_slots` also takes it out of every slot's list of whole keys, or a late
 * acknowledgement of a payload that carried it whole would count what comes back under the key as
 * held. The census asks for that once per change, at a new life and at the census that first finds
 * the key dead, rather than for every dead key in every substep, which would walk every slot of
 * every view each time. A copy can come back under the same life number, so the death is where its
 * slots have to go. */
static void forget_in_every_view(enemy_sync_state_t *s, size_t index, bool leave_slots)
{
    size_t v;
    size_t slot;

    for (v = 0; v < MP_ENEMY_SYNC_VIEWS; ++v) {
        send_view_t *view = &s->view[v];

        view->known[index] = false;
        view->based[index] = false;
        if (!leave_slots) {
            continue;
        }
        for (slot = 0; slot < MP_ENEMY_SYNC_ACK_RING; ++slot) {
            clear_bit(view->whole_keys[slot], index);
        }
    }
}

static unsigned count_bits(const uint8_t *map)
{
    unsigned count = 0u;
    size_t   i;

    for (i = 0; i < MP_ENEMY_SYNC_KEYS; ++i) {
        count += has_bit(map, i) ? 1u : 0u;
    }
    return count;
}

/* Whether a view still has a line of its budget for what its acknowledgements did. The budget
 * lives beside the counters rather than in the view, because a view given up whole must not win
 * its lines back by it; a reset clears it with the rest of a world. */
static bool view_line(enemy_sync_state_t *s, const send_view_t *v)
{
    size_t view = (size_t)(v - s->view);

    if (view >= MP_ENEMY_SYNC_VIEWS) {
        return false;
    }
    if (s->lines_view[view] >= MP_ENEMY_SYNC_LINES_VIEW) {
        ++s->lines_left_out;
        return false;
    }
    ++s->lines_view[view];
    return true;
}

/* The keys a payload carried whole are held there now, and their next records may be deltas. */
static void mark_based(enemy_sync_state_t *s, send_view_t *v, size_t slot)
{
    size_t i;

    for (i = 0; i < MP_ENEMY_SYNC_KEYS; ++i) {
        if (has_bit(v->whole_keys[slot], i) && !v->based[i]) {
            v->based[i] = true;
            ++s->based_acknowledged;
        }
    }
}

/* One row out of the census, a placement or a copy, read once for every view. True when it is
 * alive, which is its bit in the block whether or not its record could be read. */
static bool take_row(enemy_sync_state_t *s, size_t key)
{
    placement_t *p = &s->placement[key];

    p->current_ok = false;
    if (!p->live || p->actor == 0) {
        /* A dead key's mirror means nothing; the census that first finds it dead also takes it out
         * of the slots. */
        forget_in_every_view(s, key, p->was_live);
        return false;
    }

    /* A new life must not be described against the previous one's mirror in anybody's view, and
     * the generation tells the receiver the same. A copy's life is the one its grant made, the
     * same number the relay sends, and a copy the table does not hand out is not described at
     * all. A placement counts its lives by the census: one not live a substep ago is new. */
    if (mp_wire_key_is_copy((uint32_t)key) && s->copies_host != NULL) {
        uint32_t k          = (uint32_t)(key - MP_WIRE_KEY_COPY_BASE);
        uint8_t  generation = 0;

        if (!mp_npc_copies_describable(s->copies_host, k) ||
            !mp_npc_copies_generation(s->copies_host, k, &generation)) {
            ++s->copy_undescribed;
            forget_in_every_view(s, key, true);   /* rare: a live copy not handed out */
            return false;
        }
        if (generation != p->generation) {
            p->generation = generation;
            forget_in_every_view(s, key, true);
        }
    } else if (!p->was_live) {
        ++p->generation;
        forget_in_every_view(s, key, true);
    }
    /* The world events the hulls saw this actor make are this life's, whether or not its record
     * reads: the census found it alive under this key. */
    mp_world_event_census_row((uint32_t)key, p->generation, p->actor);
    if (mp_enemy_bind_read(p->actor, &p->current)) {
        p->current.value[MP_ENEMY_F_INDEX]      = mp_enemy_sync_wire_index(key);
        p->current.value[MP_ENEMY_F_GENERATION] = p->generation;
        p->current_ok                           = true;
        mp_enemy_dead_watch_row((uint32_t)key, p->generation, (uint32_t)p->actor, &p->current);
    }
    return true;
}

void mp_enemy_sync_describe_census(void)
{
    enemy_sync_state_t *s = mp_enemy_sync_state();
    size_t              i;

    memset(s->bitmap, 0, sizeof s->bitmap);
    for (i = 0; i < MP_ENEMY_SYNC_MAX_PLACEMENTS; ++i) {
        if (take_row(s, i)) {
            set_bit(s->bitmap, i);
        }
    }
    /* The copies, from the same census. */
    memset(s->copy_bitmap, 0, sizeof s->copy_bitmap);
    s->copy_bitmap_bytes = 0;
    for (i = 0; i < MP_WIRE_COPY_MAX; ++i) {
        if (take_row(s, MP_WIRE_KEY_COPY_BASE + i)) {
            set_bit(s->copy_bitmap, i);
            s->copy_bitmap_bytes = i / 8u + 1u;
        }
    }
    mp_world_event_census_over();
}

bool mp_enemy_sync_describing(void)
{
    return mp_enemy_sync_state()->send_ready;
}

/* A census in a level other than the last one's starts every placement over as a new life, even
 * one that stood under the same key a substep ago in the world before. The table is reset at a
 * level's end, but a census that still ran in the old world after that reset would otherwise carry
 * its lives, and every view's belief in them, into the new one, and the first records of the new
 * world would be deltas against the old. */
static void start_over_in_a_new_level(enemy_sync_state_t *s)
{
    size_t i;

    if (s->census_level_known && s->census_level != s->level) {
        for (i = 0; i < MP_ENEMY_SYNC_MAX_PLACEMENTS; ++i) {
            s->placement[i].live = false;   /* the census copies this into was_live */
        }
        ++s->census_new_level;
    }
    s->census_level       = s->level;
    s->census_level_known = true;
}

bool mp_enemy_sync_begin_send(void)
{
    enemy_sync_state_t *s = mp_enemy_sync_state();

    s->send_ready = false;
    if (!s->enabled) {
        return false;
    }
    if (!s->level_known) {
        ++s->unbuilt;   /* nothing to describe, and no name to put on it */
        return false;
    }
    start_over_in_a_new_level(s);
    mp_enemy_sync_take_census();
    mp_enemy_sync_describe_census();
    /* What the interest rule read belongs to the census before this one. */
    s->subjects_read = false;
    memset(s->viewer_read, 0, sizeof s->viewer_read);
    s->send_ready = true;
    return true;
}

/* Whether lhs is the more recent of two wrapping substep numbers, the test the whole wire uses. */
static bool tick_after(uint32_t lhs, uint32_t rhs)
{
    return (lhs != rhs) && ((uint32_t)(lhs - rhs) < 0x80000000u);
}

/* Whether a payload of this view went out on `tick` and the ring still remembers it. */
static bool stamped(const send_view_t *v, uint32_t tick)
{
    size_t slot = (size_t)(tick % MP_ENEMY_SYNC_ACK_RING);

    return v->sent_valid[slot] && v->sent_tick[slot] == tick;
}

/* The first acknowledgement that names a payload of this view since the view was reset.
 *
 * The far side has decoded that payload, and it was whole, like every payload before it. What it
 * does not prove is anything about the payloads in front of it, so a key whose last description
 * went out before that payload and in none since is opened again. The keys carried by that
 * payload or by one still in flight behind it stay known, and a later acknowledgement that steps
 * over one of those opens its keys as for any other loss.
 *
 * Nought is never a confirmation: it is what a side that holds nothing acknowledges. */
static bool confirm_view(enemy_sync_state_t *s, send_view_t *v, uint32_t tick)
{
    uint8_t  carried[MP_ENEMY_SYNC_KEY_BITMAP_BYTES];
    size_t   slot;
    size_t   i;
    unsigned opened = 0u;

    if (tick == 0u || !stamped(v, tick)) {
        return false;
    }
    memset(carried, 0, sizeof carried);
    for (slot = 0; slot < MP_ENEMY_SYNC_ACK_RING; ++slot) {
        if (v->sent_valid[slot] &&
            (v->sent_tick[slot] == tick || tick_after(v->sent_tick[slot], tick))) {
            for (i = 0; i < sizeof carried; ++i) {
                carried[i] |= v->sent_keys[slot][i];
            }
        }
    }
    for (i = 0; i < MP_ENEMY_SYNC_KEYS; ++i) {
        if (v->known[i] && (carried[i >> 3] & (uint8_t)(1u << (i & 7u))) == 0u) {
            v->known[i] = false;
            ++s->reopened;
            ++opened;
        }
    }
    slot = (size_t)(tick % MP_ENEMY_SYNC_ACK_RING);
    if (view_line(s, v)) {
        log_info("enemies, a view confirmed: peer %u by the payload of substep %u, which carried "
                 "%u key(s), %u of them whole; %u key(s) opened again that no payload from it on "
                 "carried",
                 (unsigned)(v - s->view), (unsigned)tick, count_bits(v->sent_keys[slot]),
                 count_bits(v->whole_keys[slot]), opened);
    }
    mark_based(s, v, slot);
    v->confirmed  = true;
    v->acked_tick = tick;
    ++s->views_confirmed;
    return true;
}

/* A view given up whole starts every record over, and keeps what the interest rule knows of it:
 * its peer still holds every replica it was told about, whose new lives are still owed to it, and
 * the silence that gave the view up is still a silence. A new connection is
 * mp_enemy_sync_forget_view, which takes that too. */
static void give_up_view(send_view_t *v)
{
    memset(v, 0, offsetof(send_view_t, interest));
}

/* That is everything in front of the interest, so the interest has to be the view's last member. */
_Static_assert(offsetof(send_view_t, interest) + sizeof(send_interest_t) == sizeof(send_view_t),
               "the interest is the last member of a view");

/* Every described flag is cleared here, whether or not it was stamped, and the two encodes either
 * side of a send must not be able to leave one standing.
 *
 * An encode whose payload is then refused, which happens whenever the session has no peer to send
 * to, leaves its flags set unless something clears them; the next stamp would then believe the far
 * side was sent bytes that were built two encodes ago and never went out. The mirror is then wrong
 * for that placement until the level ends, and every delta after it describes a body nobody has.
 *
 * A key that went out also starts over in the interest rule: its accumulator and its age go back
 * to nought, and the view remembers which life it described and what a watcher saw in it. */
void mp_enemy_sync_sent_for(size_t view, uint32_t tick)
{
    enemy_sync_state_t *s = mp_enemy_sync_state();
    send_view_t        *v;
    size_t              slot;
    size_t              i;

    if (view >= MP_ENEMY_SYNC_VIEWS) {
        return;
    }
    mp_world_event_sent_for(view, tick);
    v    = &s->view[view];
    slot = (size_t)(tick % MP_ENEMY_SYNC_ACK_RING);
    /* The slot still holds a payload that no acknowledgement has named or stepped over, and it is
     * about to be forgotten, after which nothing can tell whether it arrived. What is measured
     * here is the LAG of the newest acknowledgement behind the payload going out; the
     * acknowledgement side measures the GAP one acknowledgement jumps over, and the two are
     * different numbers. A far side that refuses every payload stops acknowledging and is caught
     * by this one; so is a line whose acknowledgements are steady, gapless and more than a ring
     * late, and that view is then never confirmed again and sends whole records for good, which is
     * correct and costs bytes. Quake 3 gives up in the same way, on the age of the frame the client
     * asks to delta against. */
    if (v->confirmed && v->sent_valid[slot] && tick_after(v->sent_tick[slot], v->acked_tick)) {
        if (view_line(s, v)) {
            log_info("enemies, a view given up whole: peer %u at the send of substep %u, no "
                     "acknowledgement having come for a whole ring (the last named substep %u)",
                     (unsigned)view, (unsigned)tick, (unsigned)v->acked_tick);
        }
        give_up_view(v);
        ++s->view_given_up;
        ++s->given_up_unheard;
        return;
    }
    v->sent_tick[slot]  = tick;
    v->sent_valid[slot] = true;
    memset(v->sent_keys[slot], 0, sizeof v->sent_keys[slot]);
    memset(v->whole_keys[slot], 0, sizeof v->whole_keys[slot]);

    for (i = 0; i < MP_ENEMY_SYNC_KEYS; ++i) {
        if (v->described[i]) {
            v->mirror[i] = v->pending[i];
            v->known[i]  = true;
            set_bit(v->sent_keys[slot], i);
            if (v->described_whole[i]) {
                set_bit(v->whole_keys[slot], i);
            }
            mp_enemy_interest_written(&v->interest.row[i],
                                      (uint8_t)v->pending[i].value[MP_ENEMY_F_GENERATION],
                                      mp_enemy_interest_watched(&v->pending[i]));
        }
        v->described[i]       = false;
        v->described_whole[i] = false;
    }
}

/* Every payload the far side says it holds proves the world events it carried, the named one and
 * each one a bit names, whatever the view then decides about its records. */
static void acknowledge_events(size_t view, const mp_payload_ack_t *ack)
{
    uint32_t k;

    mp_world_event_acked_for(view, ack->newest);
    for (k = 0; k < MP_PAYLOAD_ACK_WINDOW; ++k) {
        if ((ack->bits & (1u << k)) != 0u) {
            mp_world_event_acked_for(view, ack->newest - 1u - k);
        }
    }
}

/* One stamped substep the acknowledgement stepped over. Held there, it poisons nothing: every key
 * it carried was taken, and the ones it carried whole are held whole. Missing, it poisons every key
 * it carried, even one a later payload that was taken carried too, because that one was read
 * against a mirror without it; the ones it carried whole stay unheld. */
static void step_over(enemy_sync_state_t *s, send_view_t *v, const mp_payload_ack_t *ack,
                      uint32_t missed)
{
    size_t slot = (size_t)(missed % MP_ENEMY_SYNC_ACK_RING);
    size_t i;

    ++s->bits_stepped;
    if (mp_payload_ack_holds(ack, missed)) {
        ++s->bits_kept;
        mark_based(s, v, slot);
        memset(v->sent_keys[slot], 0, sizeof v->sent_keys[slot]);
        memset(v->whole_keys[slot], 0, sizeof v->whole_keys[slot]);
        return;
    }
    ++s->bits_missing;
    if (view_line(s, v)) {
        log_info("enemies, a payload not taken there: peer %u, substep %u, %u key(s) opened again, "
                 "%u of them carried whole",
                 (unsigned)(v - s->view), (unsigned)missed, count_bits(v->sent_keys[slot]),
                 count_bits(v->whole_keys[slot]));
    }
    for (i = 0; i < MP_ENEMY_SYNC_KEYS; ++i) {
        if ((v->sent_keys[slot][i >> 3] & (uint8_t)(1u << (i & 7u))) != 0u) {
            v->known[i] = false;
            ++s->reopened;
            ++s->bits_missing_keys;
        }
    }
    memset(v->sent_keys[slot], 0, sizeof v->sent_keys[slot]);
    memset(v->whole_keys[slot], 0, sizeof v->whole_keys[slot]);
}

/* The far side has decoded the payload of `tick` and every field it carried is now in its mirror.
 * The work here is about the payloads it did NOT name.
 *
 * An acknowledgement names the newest payload the far side decoded, and its bits the ones before it
 * that it decoded as well. A substep the acknowledgements step over whose bit is clear was never
 * taken there: lost on the wire, or refused whole because this side's enemy block could not be
 * applied. Its keys are opened again, which makes their next description a whole record rather
 * than a delta, and the far side is whole again inside one round. One whose bit is set was only
 * overtaken by a newer payload in the same substep of the far side, and its keys stay known.
 *
 * Opening a key rather than replaying the payload is the cheap half of the trade. Replaying it
 * exactly, the way Quake 3 does with its snapshot history, means keeping a copy of every record of
 * every unacknowledged substep, which here is a megabyte a peer. A whole record is 45 bytes for a
 * fighting actor against the 23 of a steady delta, and buys the same correctness for 48 bytes a
 * substep.
 *
 * The keys stay open for the turnaround of the acknowledgement, and during it the far side decodes
 * a handful of deltas against a mirror one payload behind. That is a transient of about a tenth of
 * a second and it heals itself; what it replaces was permanent. */
void mp_enemy_sync_acked_for(size_t view, uint32_t tick, uint32_t bits)
{
    enemy_sync_state_t *s = mp_enemy_sync_state();
    send_view_t        *v;
    uint32_t            missed;
    mp_payload_ack_t    ack;

    if (view >= MP_ENEMY_SYNC_VIEWS) {
        return;
    }
    ack.newest = tick;
    ack.bits   = tick != 0u ? bits & ~(uint32_t)MP_PAYLOAD_ACK_UNUSED : 0u;
    /* Before anything else: a stamp from before the view was given up still proves its events. */
    acknowledge_events(view, &ack);
    v = &s->view[view];
    if (!v->confirmed) {
        /* Every record is whole until the view is confirmed, so an acknowledgement that names
         * none of its payloads has nothing to teach it: an old substep from before a reset, or the
         * nought of a side that holds nothing. A payload that carried no enemy block is stamped
         * all the same and does confirm the view; nothing it carried becomes held, so the keys
         * the confirmation keeps known still go whole until their own whole record is named. */
        if (confirm_view(s, v, tick)) {
            ++s->acked;
            v->interest.silent = 0u;
        }
        return;
    }
    if (!tick_after(tick, v->acked_tick)) {
        return;   /* a reordered or repeated acknowledgement says nothing new */
    }
    if ((uint32_t)(tick - v->acked_tick) > MP_ENEMY_SYNC_ACK_RING) {
        /* Further back than this view remembers, so which payloads were missed cannot be told.
         * The whole view is given up, and it describes every placement whole until an
         * acknowledgement confirms it again. */
        if (view_line(s, v)) {
            log_info("enemies, a view given up whole: peer %u, an acknowledgement names substep "
                     "%u, further ahead of the last one (%u) than the ring remembers",
                     (unsigned)view, (unsigned)tick, (unsigned)v->acked_tick);
        }
        give_up_view(v);
        ++s->view_given_up;
        return;
    }
    for (missed = v->acked_tick + 1u; missed != tick; ++missed) {
        if (!stamped(v, missed)) {
            /* Nothing of this side's went out on that substep. A bit that says the far side
             * holds it anyway names a stamp from before a reset, or a word that is not true. */
            if (mp_payload_ack_holds(&ack, missed)) {
                ++s->bits_unstamped;
            }
            continue;
        }
        step_over(s, v, &ack, missed);
    }
    if (stamped(v, tick)) {
        mark_based(s, v, (size_t)(tick % MP_ENEMY_SYNC_ACK_RING));   /* the one it names */
    }
    v->acked_tick = tick;
    ++s->acked;
    v->interest.silent = 0u;
}

/* Called when an encode's payload did NOT go out. It forgets what that encode described rather
 * than letting the next commit adopt it. */
void mp_enemy_sync_abandon_for(size_t view)
{
    enemy_sync_state_t *s = mp_enemy_sync_state();

    if (view >= MP_ENEMY_SYNC_VIEWS) {
        return;
    }
    memset(s->view[view].described, 0, sizeof s->view[view].described);
    memset(s->view[view].described_whole, 0, sizeof s->view[view].described_whole);
    mp_world_event_abandon_for(view);
    ++s->abandoned;
}

void mp_enemy_sync_forget_view(size_t view)
{
    enemy_sync_state_t *s = mp_enemy_sync_state();

    if (view < MP_ENEMY_SYNC_VIEWS) {
        memset(&s->view[view], 0, sizeof s->view[view]);
        mp_world_event_forget_view(view);
    }
}

/* Its own line: the enemies' main line is at the log's length already. `missing` is the number
 * that has to fall to the payloads the far side refused, and `kept` is what used to be opened. */
void mp_enemy_sync_report_acknowledgement_bits(void)
{
    const enemy_sync_state_t *s = mp_enemy_sync_state();

    log_info("enemies, the acknowledgement bits (host): %u substep(s) stepped over, %u of them "
             "decoded there and kept, %u missing and opened again (%u key(s)); %u bit(s) naming a "
             "substep this view never stamped",
             (unsigned)s->bits_stepped, (unsigned)s->bits_kept, (unsigned)s->bits_missing,
             (unsigned)s->bits_missing_keys, (unsigned)s->bits_unstamped);
}
