/* mp_enemy_sync_choose.c: which records of the enemy block one peer gets, and in what order.
 *
 * The host's sending half was one walk: from where the last block for that peer stopped, round the
 * table, as many records as fit. It is split in two here. mp_enemy_sync_send.c keeps the census and
 * the bookkeeping of what each peer is believed to hold; this file decides, for one peer and one
 * substep, which live keys are described, by the interest rule (mp_enemy_interest_rule.h), and
 * writes them into the block. The presence bitmap in front of the records is untouched by any of
 * it and names every live key in every block.
 *
 * What the rule is asked comes from the world through the callbacks a session hands over
 * (mp_enemy_sync_set_interest), read once a census: each key once for all peers, each peer's player
 * once. With none handed over, which is a unit test with no engine and a loopback, every key is
 * unmeasured and ranked in the middle class.
 *
 * The world events for the peer go in front of the records, and their bytes are taken out of the
 * room before a single record is chosen.
 *
 * SIZE NOTE: a little over 600 lines. The code is the choice, the floor and the writing of one
 * peer's block, and they share the one offer they build; the length is the reasoning at
 * encode_record about when a record may be a delta, which a maintainer needs beside that one
 * condition. The next seam is the question for the floor (mp_enemy_sync_must_bytes and its
 * helpers), which reads the offer and writes nothing.
 */
#include "mp_enemy_sync_internal.h"

#include "mp_budget_rule.h"
#include "mp_enemy_interest_rule.h"
#include "mp_enemy_wire.h"
#include "mp_npc_copies.h"
#include "mp_wire.h"
#include "mp_world_event.h"

#include "common/logging.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* A record is measured on its own before the block takes it, in a scratch that has to hold
 * the wire's largest record, 70 bytes today, and leaves the wire room to grow a field. A record
 * that did not fit here would be one the block leaves out, so the size is checked against the
 * wire's own answer whenever a session hands the world over. */
#define RECORD_SCRATCH_BYTES 256u

void mp_enemy_sync_set_interest(const mp_enemy_sync_interest_t *interest)
{
    enemy_sync_state_t *s = mp_enemy_sync_state();

    if (interest == NULL) {
        memset(&s->interest, 0, sizeof s->interest);
    } else {
        s->interest = *interest;
        if (mp_enemy_wire_max_bytes() > RECORD_SCRATCH_BYTES) {
            log_warning("an enemy record can be %u bytes and the block measures one in %u, so "
                        "every record longer than that is left out of every block",
                        (unsigned)mp_enemy_wire_max_bytes(), (unsigned)RECORD_SCRATCH_BYTES);
        }
    }
    s->subjects_read = false;
    memset(s->viewer_read, 0, sizeof s->viewer_read);
}

/* The bytes in front of a record's delta: a placement's key and generation, or a copy's k in two
 * bytes and its generation. */
static size_t identity_of(size_t key)
{
    return mp_wire_key_is_copy((uint32_t)key) ? MP_ENEMY_SYNC_COPY_IDENTITY_BYTES
                                              : MP_ENEMY_SYNC_IDENTITY_BYTES;
}

static void put_identity(uint8_t *out, size_t key, uint8_t generation)
{
    if (mp_wire_key_is_copy((uint32_t)key)) {
        size_t k = key - MP_WIRE_KEY_COPY_BASE;

        out[0] = (uint8_t)(k & 0xFFu);
        out[1] = (uint8_t)(k >> 8);
        out[2] = generation;
    } else {
        out[0] = (uint8_t)key;
        out[1] = generation;
    }
}

/* One row's record: a delta against what this view is believed to hold, or whole wherever there
 * is no such belief to delta against.
 *
 * Before the first acknowledgement names one of this view's payloads, its mirror is only what was
 * SENT, and nothing says any of it arrived: the payloads of a client that is still loading are
 * refused there whole, and the first one it does take would be read against a base it never got.
 * Quake 3 answers the same question the same way, a delta only against a snapshot the client has
 * acknowledged and a full one while there is none. After that the mirror moves when a payload
 * goes out and the acknowledgements say which ones never arrived.
 *
 * A key the view does not know is whole as well, and not against zero. It is either a key opened
 * again because a payload carrying it was lost, or a life this view has not described yet. For the
 * first the receiver still holds an older description of the same life and reads every field left
 * out of the mask from it, so a record against zero would leave every field that fell to zero in
 * the lost payload at its old value there: an overlay clip, a playhead, a flag.
 *
 * And a key the view knows goes whole until an acknowledgement has named a payload that carried
 * this life of it whole. `known` moves on when a payload goes out, so a key whose first whole
 * record was lost, on the wire or refused by a client whose level had not opened, was otherwise
 * deltaed at once against a base the far side never got, and read there against nothing. That is
 * the Quake 3 rule for the one record that matters most; the rest still delta against what was
 * sent, because a copy of every unacknowledged record per view costs a megabyte a peer.
 *
 * Pure: the question for the floor and the choice of the block measure with it too. */
static bool encode_record(const enemy_sync_state_t *s, const send_view_t *v, size_t key,
                          uint8_t *out, size_t room, size_t *written, bool *whole)
{
    const placement_t *p = &s->placement[key];

    *whole = !v->confirmed || !v->known[key] || !v->based[key];
    if (*whole) {
        return mp_enemy_wire_encode_whole(&p->current, out, room, written);
    }
    return mp_enemy_wire_encode(&p->current, &v->mirror[key], out, room, written);
}

/* The same record, written into the block and counted, and the view told whether it went whole. */
static bool describe(enemy_sync_state_t *s, size_t view, size_t key, uint8_t *out, size_t room,
                     size_t *written)
{
    send_view_t *v     = &s->view[view];
    bool         whole = false;

    if (!encode_record(s, v, key, out, room, written, &whole)) {
        return false;
    }
    v->described_whole[key] = whole;
    if (whole) {
        ++s->counts[view].whole;
        if (!v->confirmed) {
            ++s->sent_whole;
        } else if (!v->known[key]) {
            ++s->opened_whole;
        } else {
            ++s->whole_unbased;
        }
    }
    return true;
}

/* ==============================================================================================
 * What the interest rule is asked, and what it answers for one block.
 * ============================================================================================ */

/* Every live key's subject, once a census, for every view: what the rule measures it by. A copy's
 * owner comes out of the host's table rather than out of the engine. */
static void read_subjects(enemy_sync_state_t *s)
{
    size_t key;

    if (s->subjects_read) {
        return;
    }
    s->subjects_read = true;
    for (key = 0; key < MP_ENEMY_SYNC_KEYS; ++key) {
        const placement_t  *p       = &s->placement[key];
        mp_enemy_subject_t *subject = &s->subject[key];

        memset(subject, 0, sizeof *subject);
        if (!p->current_ok) {
            continue;
        }
        if (s->interest.subject != NULL && !s->interest.subject(key, p->actor, subject)) {
            memset(subject, 0, sizeof *subject);
        }
        if (mp_wire_key_is_copy((uint32_t)key) && s->copies_host != NULL) {
            (void)mp_npc_copies_owner(s->copies_host, (uint32_t)(key - MP_WIRE_KEY_COPY_BASE),
                                      &subject->owner);
        }
    }
}

/* Where one view's player stands, once a census. */
static const mp_enemy_viewer_t *viewer_of(enemy_sync_state_t *s, size_t view)
{
    mp_enemy_viewer_t *viewer = &s->viewer[view];

    if (!s->viewer_read[view]) {
        s->viewer_read[view] = true;
        memset(viewer, 0, sizeof *viewer);
        if (s->interest.viewer == NULL || !s->interest.viewer(view, viewer)) {
            viewer->placed = false;
        }
    }
    return viewer;
}

/* What the rule is asked about one live key for one view. The class is judged against the class
 * this view last gave it, which is what the hysteresis needs; the step moves that on afterwards. */
static mp_enemy_interest_ask_t ask_for(const enemy_sync_state_t *s, size_t view, size_t key,
                                       const mp_enemy_viewer_t *viewer)
{
    const send_view_t        *v       = &s->view[view];
    const placement_t        *p       = &s->placement[key];
    const mp_enemy_subject_t *subject = &s->subject[key];
    mp_enemy_interest_ask_t   ask;

    memset(&ask, 0, sizeof ask);
    ask.generation    = p->generation;
    ask.reach         = mp_enemy_interest_reach(viewer, subject,
                                                (mp_enemy_reach_t)v->interest.row[key].reach);
    ask.event_waiting = mp_world_event_waits_on(view, key, p->generation);
    ask.watched       = mp_enemy_interest_watched(&p->current);
    ask.shooting   = subject->read && subject->shooting;
    /* Going for this view's player: its attacks are aimed at that player's body, or it is a copy
     * that player owns. Or it hit that player within the last second, which the host learnt when
     * it addressed the hit to this view's peer (mp_enemy_sync_note_struck). */
    ask.engaged = (subject->read && subject->target != 0u && subject->target == viewer->body) ||
                  (mp_wire_key_is_copy((uint32_t)key) && subject->owner != 0u &&
                   subject->owner == viewer->slot);
    ask.struck  = mp_enemy_interest_struck(&v->interest.row[key]);
    return ask;
}

size_t mp_enemy_sync_must_bytes(size_t view)
{
    enemy_sync_state_t      *s = mp_enemy_sync_state();
    const send_view_t       *v;
    const mp_enemy_viewer_t *viewer;
    uint8_t                  scratch[RECORD_SCRATCH_BYTES];
    size_t                   total = 0;
    size_t                   key;

    if (!s->send_ready || view >= MP_ENEMY_SYNC_VIEWS) {
        return 0u;
    }
    read_subjects(s);
    viewer = viewer_of(s, view);
    v      = &s->view[view];
    for (key = 0; key < MP_ENEMY_SYNC_KEYS; ++key) {
        mp_enemy_interest_ask_t ask;
        size_t                  written = 0;
        bool                    whole   = false;

        if (!s->placement[key].current_ok) {
            continue;
        }
        ask = ask_for(s, view, key, viewer);
        if (mp_enemy_interest_must(&v->interest.row[key], &ask) == 0u) {
            continue;
        }
        if (encode_record(s, v, key, scratch, sizeof scratch, &written, &whole)) {
            total += identity_of(key) + written;
        }
    }
    return total;
}

/* The keys offered to one block. Static because the block is built inside the engine's substep and
 * this is four kilobytes; nothing builds two blocks at once. */
typedef struct block_offer {
    mp_enemy_interest_pick_t picks[MP_ENEMY_SYNC_KEYS];
    uint8_t                  must[MP_ENEMY_SYNC_KEYS];
    bool                     chosen[MP_ENEMY_SYNC_KEYS];
    bool                     for_event[MP_ENEMY_SYNC_KEYS];   /* first for an event, not near */
    size_t                   count;
} block_offer_t;

static block_offer_t offer;

static size_t oldest_bucket(uint8_t reach)
{
    return reach == (uint8_t)MP_ENEMY_REACH_NONE ? (size_t)MP_ENEMY_REACH_MIDDLE : (size_t)reach;
}

/* Every live key through one substep of the rule for this view, and the ones it offers, in the
 * order they are written. A key that is not live starts over. */
static void offer_keys(enemy_sync_state_t *s, size_t view, bool silent)
{
    send_view_t             *v      = &s->view[view];
    interest_counts_t       *counts = &s->counts[view];
    const mp_enemy_viewer_t *viewer = viewer_of(s, view);
    size_t                   key;

    offer.count = 0;
    for (key = 0; key < MP_ENEMY_SYNC_KEYS; ++key) {
        mp_enemy_interest_row_t  *row = &v->interest.row[key];
        mp_enemy_interest_pick_t *pick;
        mp_enemy_interest_ask_t   ask;
        uint32_t                  must;
        mp_enemy_tier_t           tier;

        offer.chosen[key] = false;
        if (!s->placement[key].current_ok) {
            mp_enemy_interest_idle(row);
            continue;
        }
        ask  = ask_for(s, view, key, viewer);
        must = mp_enemy_interest_must(row, &ask);
        counts->struck_boosted += (ask.struck && !ask.engaged) ? 1u : 0u;
        mp_enemy_interest_step(row, &ask);
        tier = mp_enemy_interest_tier(row, &ask, must);
        if (tier == MP_ENEMY_TIER_SKIP) {
            counts->far_withheld += (ask.reach == MP_ENEMY_REACH_FAR && !row->seen) ? 1u : 0u;
            continue;
        }
        if (row->age > counts->oldest[oldest_bucket(row->reach)]) {
            counts->oldest[oldest_bucket(row->reach)] = row->age;
        }
        offer.must[key]      = (uint8_t)must;
        offer.for_event[key] = tier == MP_ENEMY_TIER_FIRST && ask.reach != MP_ENEMY_REACH_NEAR;
        pick               = &offer.picks[offer.count++];
        pick->key          = (uint16_t)key;
        pick->turn         = (uint16_t)((key + MP_ENEMY_SYNC_KEYS - v->next) % MP_ENEMY_SYNC_KEYS);
        pick->accumulator  = row->accumulator;
        pick->tier         = (uint8_t)tier;
        pick->age          = row->age;
    }
    mp_enemy_interest_order(offer.picks, offer.count, silent);
}

/* Which of the offered keys the room holds, in order, each measured by the record the block will
 * write for it. A record that does not fit is passed over and a smaller one behind it may still
 * go; each part counts its records in a byte. The round robin's cursor moves behind the last key
 * chosen. Returns the bytes the records that may not wait took, or SIZE_MAX when one of them was
 * left out. */
static size_t choose(enemy_sync_state_t *s, size_t view, size_t room, bool silent)
{
    send_view_t       *v      = &s->view[view];
    interest_counts_t *counts = &s->counts[view];
    uint8_t            scratch[RECORD_SCRATCH_BYTES];
    size_t             used   = 0;
    size_t             extras = 0;
    size_t             must_bytes = 0;
    bool               must_fit   = true;
    size_t             in_part[2] = { 0u, 0u };
    size_t             i;

    for (i = 0; i < offer.count; ++i) {
        const mp_enemy_interest_pick_t *pick  = &offer.picks[i];
        size_t                          key   = pick->key;
        size_t                          id    = identity_of(key);
        size_t                          part  = mp_wire_key_is_copy((uint32_t)key) ? 1u : 0u;
        size_t                          written = 0;
        bool                            whole = false;
        uint32_t                        must  = offer.must[key];
        bool                            fits  = false;

        if (mp_enemy_interest_admits(pick, silent, extras) && in_part[part] < 255u &&
            used + id < room) {
            size_t space = room - used - id;

            if (space > sizeof scratch) {
                space = sizeof scratch;
            }
            fits = encode_record(s, v, key, scratch, space, &written, &whole);
        }
        if (must != 0u) {
            ++counts->must;
            counts->must_state += (must & MP_ENEMY_MUST_STATE) != 0u ? 1u : 0u;
            counts->must_life  += (must & MP_ENEMY_MUST_LIFE) != 0u ? 1u : 0u;
        }
        if (!fits) {
            if (must != 0u) {
                ++counts->must_unfit;
                must_fit = false;
            } else {
                ++counts->deferred;
            }
            continue;
        }
        offer.chosen[key] = true;
        v->next           = (key + 1u) % MP_ENEMY_SYNC_KEYS;
        used += id + written;
        ++in_part[part];
        if (must != 0u) {
            must_bytes += id + written;
        } else {
            ++extras;
        }
    }
    return must_fit ? must_bytes : SIZE_MAX;
}

/* The chosen records of one part, in the order they were chosen. */
static size_t write_part(enemy_sync_state_t *s, size_t view, bool copies, uint8_t *out, size_t at,
                         size_t end, size_t *count)
{
    send_view_t       *v      = &s->view[view];
    interest_counts_t *counts = &s->counts[view];
    size_t             i;

    *count = 0;
    for (i = 0; i < offer.count; ++i) {
        size_t       key     = offer.picks[i].key;
        placement_t *p       = &s->placement[key];
        size_t       id      = identity_of(key);
        size_t       written = 0;

        if (!offer.chosen[key] || mp_wire_key_is_copy((uint32_t)key) != copies) {
            continue;
        }
        put_identity(out + at, key, p->generation);
        if (!describe(s, view, key, out + at + id, end - at - id, &written)) {
            offer.chosen[key] = false;   /* measured to fit a moment ago; never taken */
            continue;
        }
        at += id + written;
        v->pending[key]   = p->current;
        v->described[key] = true;
        ++*count;
        ++counts->records;
        ++counts->by_reach[v->interest.row[key].reach & 3u];
        if (offer.picks[i].tier == (uint8_t)MP_ENEMY_TIER_FIRST) {
            counts->first_near += offer.for_event[key] ? 0u : 1u;
            counts->first_event += offer.for_event[key] ? 1u : 0u;
        }
    }
    return at;
}

/* The copies' part without its records is its length byte, its bitmap and its count. */
static size_t copies_head_bytes(const enemy_sync_state_t *s)
{
    return (s->copy_bitmap_bytes != 0u) ? 2u + s->copy_bitmap_bytes : 0u;
}

size_t mp_enemy_sync_frame_bytes(size_t view)
{
    return MP_ENEMY_SYNC_HEADER_BYTES + copies_head_bytes(mp_enemy_sync_state()) +
           mp_world_event_part_bytes(view);
}

bool mp_enemy_sync_view_concerns(size_t view, uint32_t key, uint8_t life)
{
    enemy_sync_state_t            *s = mp_enemy_sync_state();
    const mp_enemy_interest_row_t *row;
    mp_enemy_reach_t               reach;

    if (!s->send_ready || view >= MP_ENEMY_SYNC_VIEWS || key >= MP_ENEMY_SYNC_KEYS) {
        return false;
    }
    row = &s->view[view].interest.row[key];
    if (mp_enemy_interest_holds(row, life)) {
        return true;
    }
    /* A key the census no longer reads has no class to be judged by: gone, it concerns only a
     * peer that holds the life. */
    if (!s->placement[key].current_ok) {
        return false;
    }
    read_subjects(s);
    reach = mp_enemy_interest_reach(viewer_of(s, view), &s->subject[key],
                                    (mp_enemy_reach_t)row->reach);
    return !(reach == MP_ENEMY_REACH_FAR && !row->seen);
}

bool mp_enemy_sync_wakes_for(uint32_t key, const float position[3])
{
    enemy_sync_state_t *s = mp_enemy_sync_state();
    mp_enemy_viewer_t   viewer;

    if (!s->send_ready || key >= MP_ENEMY_SYNC_KEYS || position == NULL ||
        !s->placement[key].current_ok) {
        return false;
    }
    read_subjects(s);
    memset(&viewer, 0, sizeof viewer);
    viewer.placed = true;
    memcpy(viewer.position, position, sizeof viewer.position);
    return mp_enemy_interest_reach(&viewer, &s->subject[key], MP_ENEMY_REACH_NONE) ==
           MP_ENEMY_REACH_NEAR;
}

bool mp_enemy_sync_view_position(size_t view, float out[3])
{
    enemy_sync_state_t      *s = mp_enemy_sync_state();
    const mp_enemy_viewer_t *viewer;

    if (!s->send_ready || view >= MP_ENEMY_SYNC_VIEWS || out == NULL) {
        return false;
    }
    viewer = viewer_of(s, view);
    if (!viewer->placed) {
        return false;
    }
    memcpy(out, viewer->position, sizeof viewer->position);
    return true;
}

/* The floor for a block that needs `need` bytes for its head and its records that may not wait:
 * that, and never less than the budget's fixed floor, which a quiet substep keeps to the byte. */
static size_t floor_for(size_t need)
{
    return need > MP_BUDGET_ENEMY_FLOOR_BYTES ? need : MP_BUDGET_ENEMY_FLOOR_BYTES;
}

/* The frame first: it decides which events concern the view, and the records are asked after. */
size_t mp_enemy_sync_floor_bytes(size_t view)
{
    size_t frame = mp_enemy_sync_frame_bytes(view);

    return floor_for(frame + mp_enemy_sync_must_bytes(view));
}

void mp_enemy_sync_note_struck(uint32_t key, uint8_t slot)
{
    enemy_sync_state_t *s    = mp_enemy_sync_state();
    size_t              view = 0;

    /* The view comes from the world's own slot rule, the one the hit was addressed by, so the
     * enemy is ranked up for exactly the peer that was sent the hit. */
    if (key >= MP_ENEMY_SYNC_KEYS || s->interest.view_of_slot == NULL ||
        !s->interest.view_of_slot(slot, &view) || view >= MP_ENEMY_SYNC_VIEWS) {
        return;
    }
    mp_enemy_interest_note_struck(&s->view[view].interest.row[key]);
    ++s->counts[view].struck_noted;
}

/* What the report says of the floor: how often it rose, how far, and whether the block then got the
 * room it rose to. A block given less is one whose payload could not hold its floor at all. */
static void count_the_floor(interest_counts_t *counts, size_t need, size_t capacity)
{
    size_t floor_bytes = floor_for(need);

    if (floor_bytes <= MP_BUDGET_ENEMY_FLOOR_BYTES) {
        return;
    }
    ++counts->floor_raised;
    if (floor_bytes > counts->floor_most) {
        counts->floor_most = (uint32_t)floor_bytes;
    }
    counts->floor_short += capacity < floor_bytes ? 1u : 0u;
}

bool mp_enemy_sync_encode_for(size_t view, uint8_t *out, size_t capacity, size_t *bytes)
{
    enemy_sync_state_t *s     = mp_enemy_sync_state();
    send_view_t        *v;
    interest_counts_t  *counts;
    size_t              at    = 0;
    size_t              count = 0;
    size_t              head;
    size_t              frame;
    size_t              predicted;
    size_t              must_bytes;
    bool                silent;

    if (!s->send_ready || view >= MP_ENEMY_SYNC_VIEWS || out == NULL || bytes == NULL ||
        capacity < MP_ENEMY_SYNC_HEADER_BYTES) {
        return false;
    }
    /* The copies' part without its records is taken out of the room first, and a block that
     * cannot hold it is not built, because a block without the part says the host has no copy
     * alive. A copy's records need no room of their own beyond that: a copy has its rank like a
     * placement, and what it has accumulated brings it in when the placements fill the block. */
    head = copies_head_bytes(s);
    if (capacity < MP_ENEMY_SYNC_HEADER_BYTES + head) {
        ++s->copy_crowded;
        return false;
    }
    v      = &s->view[view];
    counts = &s->counts[view];
    *bytes = 0;
    memset(v->described, 0, sizeof v->described);

    /* Asked before the rule moves a single accumulator, as whoever sizes the floor asks it, and in
     * the same order. */
    read_subjects(s);
    frame     = mp_enemy_sync_frame_bytes(view);
    predicted = mp_enemy_sync_must_bytes(view);
    count_the_floor(counts, frame + predicted, capacity);
    silent    = v->interest.silent >= MP_ENEMY_INTEREST_SILENT;
    if (v->interest.silent < 0xFFFFFFFFu) {
        ++v->interest.silent;
    }

    out[1] = (uint8_t)(s->level & 0xFFu);
    out[2] = (uint8_t)(s->level >> 8);
    at = 1u + MP_ENEMY_SYNC_LEVEL_BYTES;
    memcpy(out + at, s->bitmap, sizeof s->bitmap);
    at += sizeof s->bitmap;

    /* The world events, then the records in what is left. A room smaller than the floor the frame
     * asked for keeps the newest events and leaves the rest for the next block. */
    at += mp_world_event_write(view, out + at, capacity - head - at);
    offer_keys(s, view, silent);
    must_bytes = choose(s, view, capacity - head - at, silent);

    at     = write_part(s, view, false, out, at, capacity - head, &count);
    out[0] = (uint8_t)count;
    s->records_sent += (uint32_t)count;

    if (head != 0u) {
        size_t count_at = at + 1u + s->copy_bitmap_bytes;

        out[at] = (uint8_t)s->copy_bitmap_bytes;
        memcpy(out + at + 1u, s->copy_bitmap, s->copy_bitmap_bytes);
        at            = write_part(s, view, true, out, count_at + 1u, capacity, &count);
        out[count_at] = (uint8_t)count;
        ++s->copy_blocks_sent;
        s->copy_records_sent += (uint32_t)count;
    }

    ++counts->blocks;
    counts->throttled += silent ? 1u : 0u;
    if (must_bytes != SIZE_MAX) {
        if (must_bytes > counts->must_bytes_most) {
            counts->must_bytes_most = (uint32_t)must_bytes;
        }
        counts->must_bytes_apart += must_bytes != predicted ? 1u : 0u;
    }
    *bytes = at;
    ++s->blocks_sent;
    return true;
}
