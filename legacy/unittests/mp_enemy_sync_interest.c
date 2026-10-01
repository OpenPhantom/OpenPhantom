/* mp_enemy_sync_interest.c: the interest rule inside the enemy block.
 *
 * SIZE NOTE: over 600 lines. Every section lays out a census and a block and reads the block back
 * through the same fake world and helpers; a second file would have to copy them.
 *
 * The rule alone is mp_enemy_interest. Here it runs where it matters, between the census and the
 * bytes of one peer's block, against a world a fake source answers: where each peer's player
 * stands, and where each enemy is with its two radii. What would be silent if it were wrong:
 *
 *   a key the rule holds back that drops out of the presence bitmap too, which makes the peer let
 *   its replica go and run it with its own AI;
 *   a far key a peer was never told about described anyway, and a known far one described every
 *   substep;
 *   a first description in the near class waiting behind a crowd;
 *   a silent peer paid the whole block every substep;
 *   the question for the floor answering other bytes than the block then writes first, or moving
 *   an accumulator by being asked;
 *   a new life at a level's first substep counted as one that may not wait;
 *   a key opened again after a loss jumping the queue, or a deferred key described against a
 *   mirror it was never sent;
 *   a view given up whole forgetting the lives its peer still holds;
 *   a copy that never gets its turn beside the placements;
 *   an enemy that hit a peer's player waiting behind the crowd for that peer, or ranked up for
 *   every peer, or for longer than a second;
 *   and with no world to read, anything but a plain round robin over the keys.
 *
 * There is no engine, so the census finds nothing: a row is made live by hand and its record put
 * where the census would leave it, as the neighbouring tests of this module do.
 */
#include "unittest.h"

#include "mp_enemy_interest_rule.h"
#include "mp_enemy_sync.h"
#include "mp_enemy_sync_internal.h"
#include "mp_enemy_wire.h"
#include "mp_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define HERE   57u
#define HEADER MP_ENEMY_SYNC_HEADER_BYTES

/* The copies' head with a bitmap of one byte: its length, the byte, the count. */
#define HEAD_ONE_BYTE 3u

static uint8_t  s_block[2048];
static uint32_t s_tick = 1000u;

/* The world the fake source answers from. */
static mp_enemy_viewer_t  s_viewers[MP_ENEMY_SYNC_VIEWS];
static mp_enemy_subject_t s_subjects[MP_ENEMY_SYNC_KEYS];
static mp_enemy_record_t  s_records[MP_ENEMY_SYNC_KEYS];
static bool               s_live[MP_ENEMY_SYNC_KEYS];

static bool fake_viewer(size_t view, mp_enemy_viewer_t *out)
{
    *out = s_viewers[view];
    return out->placed;
}

static bool fake_subject(size_t key, uintptr_t actor, mp_enemy_subject_t *out)
{
    (void)actor;
    *out = s_subjects[key];
    return out->read;
}

/* The session's slot rule: world slot n is the player of peer n - 1, the host's own is slot 0. */
static bool fake_view_of_slot(uint8_t slot, size_t *view)
{
    if (slot == 0u || slot > MP_ENEMY_SYNC_VIEWS) {
        return false;
    }
    *view = (size_t)slot - 1u;
    return true;
}

static const mp_enemy_sync_interest_t SOURCE = { &fake_viewer, &fake_subject, NULL,
                                                 &fake_view_of_slot };

static void standing_record(mp_enemy_record_t *record, float x)
{
    uint32_t packed = 0;

    memset(record, 0, sizeof *record);
    (void)mp_enemy_wire_put_position(x, &packed);
    record->value[MP_ENEMY_F_POS_X] = packed;
    (void)mp_enemy_wire_put_position(5.0f, &packed);
    record->value[MP_ENEMY_F_POS_Y] = packed;
    (void)mp_enemy_wire_put_position(7.0f, &packed);
    record->value[MP_ENEMY_F_POS_Z]   = packed;
    record->value[MP_ENEMY_F_HEADING] = 12000u;
    record->value[MP_ENEMY_F_STATE]   = 1u | MP_ENEMY_HAS_HEAD;
    record->value[MP_ENEMY_F_HEALTH]  = mp_enemy_wire_put_health(60);
    record->value[MP_ENEMY_F_CLIP]    = 4u;
    record->value[MP_ENEMY_F_HEAD]    = 48u;
}

/* An enemy on the x axis, with the engine's two radii, alive from the next census. The peers
 * stand at the origin. */
static void enemy(size_t key, float x, float wake, float keep)
{
    mp_enemy_subject_t *subject = &s_subjects[key];

    s_live[key] = true;
    standing_record(&s_records[key], x);
    memset(subject, 0, sizeof *subject);
    subject->read          = true;
    subject->has_placement = !mp_wire_key_is_copy((uint32_t)key);
    subject->placement[0]  = x;
    subject->position[0]   = x;
    subject->wake          = wake;
    subject->keep          = keep;
}

/* Where an enemy is, as both the record and the source see it. */
static void move(size_t key, float x)
{
    uint32_t packed = 0;

    (void)mp_enemy_wire_put_position(x, &packed);
    s_records[key].value[MP_ENEMY_F_POS_X] = packed;
    s_subjects[key].placement[0]           = x;
    s_subjects[key].position[0]            = x;
}

/* A host in a level, nobody described, every peer's player at the origin. */
static void fresh_host(void)
{
    size_t view;

    mp_enemy_sync_reset();
    mp_enemy_sync_set_enabled(true);
    mp_enemy_sync_set_level(true, HERE);
    (void)mp_enemy_sync_begin_send();
    memset(s_live, 0, sizeof s_live);
    memset(s_subjects, 0, sizeof s_subjects);
    memset(s_viewers, 0, sizeof s_viewers);
    for (view = 0; view < MP_ENEMY_SYNC_VIEWS; ++view) {
        s_viewers[view].placed = true;
        s_viewers[view].body   = 0x2000u + (uint32_t)view;
        s_viewers[view].slot   = (uint8_t)(view + 1u);
    }
    mp_enemy_sync_set_interest(&SOURCE);
}

/* One census as the host takes it: who is alive, which life, and what each record holds. Handing
 * the source over again is what tells the block its answers belong to a new census. */
static void census(void)
{
    enemy_sync_state_t *s = mp_enemy_sync_state();
    size_t              key;

    for (key = 0; key < MP_ENEMY_SYNC_KEYS; ++key) {
        placement_t *p = &s->placement[key];

        p->was_live = p->live;
        p->live     = s_live[key];
        p->actor    = s_live[key] ? (uintptr_t)(0x00A01000u + 16u * key) : 0u;
    }
    mp_enemy_sync_describe_census();
    for (key = 0; key < MP_ENEMY_SYNC_KEYS; ++key) {
        placement_t *p = &s->placement[key];

        if (!s_live[key]) {
            continue;
        }
        p->current                           = s_records[key];
        p->current.value[MP_ENEMY_F_INDEX]      = mp_enemy_sync_wire_index(key);
        p->current.value[MP_ENEMY_F_GENERATION] = p->generation;
        p->current_ok                        = true;
    }
    mp_enemy_sync_set_interest(&SOURCE);
}

static bool encode(size_t view, size_t capacity, size_t *bytes)
{
    return mp_enemy_sync_encode_for(view, s_block, capacity, bytes);
}

static uint32_t send_out(size_t view)
{
    uint32_t tick = ++s_tick;

    mp_enemy_sync_sent_for(view, tick);
    return tick;
}

/* A census, a block with room for everything, and the payload out. */
static void substep(size_t view)
{
    size_t bytes = 0;

    census();
    (void)encode(view, sizeof s_block, &bytes);
    (void)send_out(view);
}

static bool described(size_t view, size_t key)
{
    return mp_enemy_sync_state()->view[view].described[key];
}

static bool listed(size_t key)
{
    return (s_block[1u + MP_ENEMY_SYNC_LEVEL_BYTES + key / 8u] & (uint8_t)(1u << (key & 7u))) != 0u;
}

static size_t whole_bytes(size_t key)
{
    enemy_sync_state_t *s = mp_enemy_sync_state();
    uint8_t             scratch[128];
    size_t              bytes = 0;

    return mp_enemy_wire_encode_whole(&s->placement[key].current, scratch, sizeof scratch, &bytes)
               ? bytes
               : 0u;
}

static void check_the_bitmap_and_the_far(void)
{
    size_t bytes = 0;
    size_t records;
    size_t t;

    uint32_t withheld;

    ut_section("the bitmap names every live key, whatever the rule wrote");
    fresh_host();
    withheld = mp_enemy_sync_state()->counts[0].far_withheld;
    enemy(5u, 200.0f, 10.0f, 30.0f);   /* far, and never told of */
    enemy(6u, 5.0f, 10.0f, 30.0f);     /* near */
    census();
    ut_check(encode(0u, sizeof s_block, &bytes), "the block is built");
    ut_check(s_block[0] == 1u && described(0u, 6u) && !described(0u, 5u),
             "the near key is described and the far one the peer was never told of is not");
    ut_check(listed(5u) && listed(6u),
             "and both are in the bitmap: the peer must not let a replica go it may hold");
    ut_check(mp_enemy_sync_state()->counts[0].far_withheld == withheld + 1u,
             "the far key held back is counted");
    (void)send_out(0u);

    ut_section("a far key gets its first description once it comes into the keep radius");
    move(5u, 20.0f);
    census();
    (void)encode(0u, sizeof s_block, &bytes);
    ut_check(described(0u, 5u), "at 20 it is middle and described");
    (void)send_out(0u);

    ut_section("and far again, a key the peer knows goes every eighth substep and no more often");
    move(5u, 200.0f);
    records = 0;
    for (t = 0; t < 32u; ++t) {
        census();
        (void)encode(0u, sizeof s_block, &bytes);
        records += described(0u, 5u) ? 1u : 0u;
        ut_checkf(described(0u, 6u), "substep %u: the near one goes every substep", (unsigned)t);
        (void)send_out(0u);
    }
    ut_checkf(records == 32u / MP_ENEMY_INTEREST_FAR_CADENCE,
              "thirty two substeps with room for all carry the far key %u times, not 32",
              (unsigned)records);
}

static void check_a_first_description_near(void)
{
    const size_t one = MP_ENEMY_SYNC_IDENTITY_BYTES;
    uint32_t     first;
    size_t       room;
    size_t       bytes = 0;
    size_t       key;
    size_t       t;

    ut_section("a first description near goes before the accumulated crowd");
    fresh_host();
    for (key = 10u; key < 30u; ++key) {
        enemy(key, 20.0f, 10.0f, 30.0f);   /* twenty middle ones */
    }
    census();
    room = HEADER + 3u * (one + whole_bytes(10u));
    for (t = 0; t < 5u; ++t) {
        if (t != 0u) {
            census();
        }
        (void)encode(0u, room, &bytes);
        (void)send_out(0u);
    }
    first = mp_enemy_sync_state()->counts[0].first_near;
    enemy(40u, 5.0f, 10.0f, 30.0f);
    census();
    ut_check(encode(0u, room, &bytes) && s_block[0] == 3u, "a block of three records");
    ut_checkf(s_block[HEADER] == 40u && described(0u, 40u),
              "and the first is the enemy that just woke beside the peer (key %u)",
              (unsigned)s_block[HEADER]);
    ut_check(mp_enemy_sync_state()->counts[0].first_near == first + 1u,
             "counted as a first description");
}

static void check_a_silent_peer(void)
{
    enemy_sync_state_t *s = mp_enemy_sync_state();
    size_t              bytes = 0;
    size_t              key;
    size_t              t;
    uint32_t            last = 0;
    uint32_t            throttled;

    ut_section("a peer whose acknowledgements are silent gets what may not wait and four more");
    fresh_host();
    throttled = s->counts[0].throttled;
    for (key = 1u; key <= 10u; ++key) {
        enemy(key, 20.0f, 10.0f, 30.0f);
    }
    for (t = 0; t < MP_ENEMY_INTEREST_SILENT; ++t) {
        census();
        (void)encode(0u, sizeof s_block, &bytes);
        ut_checkf(s_block[0] == 10u, "block %u, before the silence counts, carries all ten",
                  (unsigned)t);
        last = send_out(0u);
    }
    s_records[3].value[MP_ENEMY_F_STATE] = MP_ENEMY_STATE_DEATH | MP_ENEMY_HAS_HEAD;
    census();
    ut_check(encode(0u, sizeof s_block, &bytes) && s_block[0] == 5u,
             "the next carries five: the death that may not wait and four more");
    ut_check(described(0u, 3u), "the death among them");
    ut_check(s->counts[0].throttled == throttled + 1u, "and the substep is counted as throttled");
    (void)send_out(0u);

    mp_enemy_sync_acked_for(0u, last, 0u);
    census();
    ut_check(s->view[0].confirmed && encode(0u, sizeof s_block, &bytes) && s_block[0] == 10u,
             "an acknowledgement that confirms the view ends the silence, and all ten go again");
    (void)send_out(0u);
}

static void check_the_question_for_the_floor(void)
{
    enemy_sync_state_t *s = mp_enemy_sync_state();
    send_interest_t     before;
    interest_counts_t   counted;
    size_t              bytes = 0;
    size_t              asked;
    size_t              key;
    uint32_t            tick;

    ut_section("at a level's first substep nothing may not wait");
    fresh_host();
    for (key = 1u; key <= 6u; ++key) {
        enemy(key, key < 4u ? 5.0f : 20.0f, 10.0f, 30.0f);
    }
    census();
    ut_check(mp_enemy_sync_must_bytes(0u) == 0u,
             "six new lives, none of them described before: the floor needs no bytes for them");
    counted = s->counts[0];
    (void)encode(0u, sizeof s_block, &bytes);
    ut_check(s->counts[0].must == counted.must, "and the block counts none");
    tick = send_out(0u);
    mp_enemy_sync_acked_for(0u, tick, 0u);

    ut_section("the question for the floor names what the block writes first, and moves nothing");
    s_live[4] = false;
    substep(0u);
    s_records[2].value[MP_ENEMY_F_STATE]    = MP_ENEMY_STATE_DEATH | MP_ENEMY_HAS_HEAD;
    s_records[3].value[MP_ENEMY_F_NODES_LO] = 1u << 4;
    s_live[4] = true;
    census();
    counted = s->counts[0];
    before = s->view[0].interest;
    asked  = mp_enemy_sync_must_bytes(0u);
    ut_check(asked != 0u && mp_enemy_sync_must_bytes(0u) == asked,
             "a death, a node hidden and a new life take bytes, and asked twice it answers the "
             "same");
    ut_check(memcmp(&before, &s->view[0].interest, sizeof before) == 0,
             "and being asked moved no accumulator, no age and no class");
    ut_check(mp_enemy_sync_frame_bytes(0u) == HEADER,
             "the block's own part is its header, its bitmap and the world events' empty head "
             "while no copy is alive and no event waits");
    ut_check(encode(0u, mp_enemy_sync_frame_bytes(0u) + asked, &bytes) && s_block[0] == 3u &&
                 described(0u, 2u) && described(0u, 3u) && described(0u, 4u) &&
                 bytes == mp_enemy_sync_frame_bytes(0u) + asked,
             "a room of exactly that and the answer holds all three and nothing else");
    ut_check(s->counts[0].must_unfit == counted.must_unfit, "none of them left out");
    mp_enemy_sync_abandon_for(0u);
    census();
    counted = s->counts[0];
    ut_check(encode(0u, sizeof s_block, &bytes) && described(0u, 2u) && described(0u, 3u) &&
                 described(0u, 4u),
             "the block carries all three");
    ut_checkf(s->counts[0].must_bytes_apart == counted.must_bytes_apart &&
                  s->counts[0].must_bytes_most >= asked,
              "and they took exactly the %u byte(s) the question named", (unsigned)asked);
    ut_check(s->counts[0].must_state == counted.must_state + 2u &&
                 s->counts[0].must_life == counted.must_life + 1u &&
                 s->counts[0].must == counted.must + 3u &&
                 s->counts[0].must_unfit == counted.must_unfit,
             "two changes a watcher sees and one new life, none left out");
    ut_check(s_block[HEADER] >= 2u && s_block[HEADER] <= 4u,
             "and the first record of the block is one of them");
    (void)send_out(0u);
}

static void check_opened_and_deferred_keys(void)
{
    enemy_sync_state_t *s = mp_enemy_sync_state();
    uint8_t             scratch[128];
    size_t              delta1 = 0;
    size_t              delta2 = 0;
    size_t              bytes = 0;
    uint32_t            first;
    uint32_t            third;

    ut_section("a key opened again after a loss waits for its turn and then goes whole");
    fresh_host();
    enemy(1u, 20.0f, 10.0f, 30.0f);
    enemy(2u, 20.0f, 10.0f, 30.0f);
    census();
    (void)encode(0u, sizeof s_block, &bytes);
    first = send_out(0u);
    mp_enemy_sync_acked_for(0u, first, 0u);
    substep(0u);                        /* its payload is lost */
    census();
    (void)encode(0u, sizeof s_block, &bytes);
    third = send_out(0u);
    ut_check(s->view[0].known[1] && s->view[0].known[2], "the third payload went out with both");
    mp_enemy_sync_acked_for(0u, third, 0u);
    ut_check(!s->view[0].known[1] && !s->view[0].known[2],
             "and its acknowledgement steps over the lost one and opens both");
    census();
    ut_check(mp_enemy_sync_must_bytes(0u) == 0u,
             "an opened key is not one that may not wait: nothing is lost by its waiting");
    ut_check(encode(0u, HEADER + MP_ENEMY_SYNC_IDENTITY_BYTES + whole_bytes(1u), &bytes) &&
                 s_block[0] == 1u && bytes == HEADER + MP_ENEMY_SYNC_IDENTITY_BYTES +
                                                  whole_bytes(1u),
             "when its turn comes, in a room for one, it goes whole");
    mp_enemy_sync_acked_for(0u, send_out(0u), 0u);

    ut_section("a deferred key is described against the mirror it was last sent");
    census();
    (void)encode(0u, sizeof s_block, &bytes);
    mp_enemy_sync_acked_for(0u, send_out(0u), 0u);
    move(1u, 21.0f);
    census();
    ut_check(encode(0u, HEADER, &bytes) && s_block[0] == 0u, "a block with no room defers it");
    mp_enemy_sync_abandon_for(0u);
    census();
    (void)mp_enemy_wire_encode(&s->placement[1].current, &s->view[0].mirror[1], scratch,
                               sizeof scratch, &delta1);
    (void)mp_enemy_wire_encode(&s->placement[2].current, &s->view[0].mirror[2], scratch,
                               sizeof scratch, &delta2);
    ut_check(encode(0u, sizeof s_block, &bytes) && described(0u, 1u) && s_block[0] == 2u,
             "the next block with room carries it");
    ut_checkf(bytes == HEADER + 2u * MP_ENEMY_SYNC_IDENTITY_BYTES + delta1 + delta2,
              "as a delta of %u byte(s) against what the peer was last sent", (unsigned)delta1);
    ut_check(delta1 < whole_bytes(1u), "which is shorter than a whole record");
    (void)send_out(0u);
}

/* A view confirmed by a payload that carried nothing, so no key of it is held yet. */
static void confirmed_empty(void)
{
    size_t bytes = 0;

    fresh_host();
    census();
    (void)encode(0u, sizeof s_block, &bytes);
    mp_enemy_sync_acked_for(0u, send_out(0u), 0u);
}

static void check_a_delta_waits_for_its_whole_record(void)
{
    enemy_sync_state_t *s     = mp_enemy_sync_state();
    const size_t        whole = HEADER + MP_ENEMY_SYNC_IDENTITY_BYTES + whole_bytes(1u);
    size_t              bytes = 0;
    uint32_t            first;
    uint32_t            third;
    uint32_t            levels;
    uint8_t             life;

    ut_section("a delta waits until a whole record of its life is acknowledged");
    confirmed_empty();
    ut_check(s->view[0].confirmed, "a payload with no enemy in it confirms the view");
    enemy(1u, 20.0f, 10.0f, 30.0f);
    census();
    ut_check(encode(0u, sizeof s_block, &bytes) && bytes == whole, "a new key goes whole");
    first = send_out(0u);
    census();
    ut_check(encode(0u, sizeof s_block, &bytes) && bytes == whole,
             "and whole again while that payload is unacknowledged: the peer may never have got "
             "it, and a delta would be read there against nothing (seen after a film)");
    (void)send_out(0u);
    mp_enemy_sync_acked_for(0u, first, 0u);
    ut_check(s->view[0].based[1], "the acknowledgement of the first proves the key held whole");
    census();
    ut_check(encode(0u, sizeof s_block, &bytes) && bytes < whole, "and the next record is a delta");
    (void)send_out(0u);

    ut_section("a whole record the peer never took leaves the key unheld");
    confirmed_empty();
    enemy(1u, 20.0f, 10.0f, 30.0f);
    census();
    (void)encode(0u, sizeof s_block, &bytes);
    (void)send_out(0u);                               /* lost */
    census();
    ut_check(encode(0u, HEADER, &bytes) && s_block[0] == 0u, "a block with no room for it");
    mp_enemy_sync_acked_for(0u, send_out(0u), 0u);    /* its bit says the lost one was not held */
    ut_check(!s->view[0].known[1] && !s->view[0].based[1], "the key is opened and not held");

    ut_section("a whole record the bits say was held makes the key held");
    census();
    ut_check(encode(0u, sizeof s_block, &bytes) && bytes == whole, "it goes whole");
    third = send_out(0u);
    census();
    (void)encode(0u, HEADER, &bytes);
    mp_enemy_sync_acked_for(0u, send_out(0u), 1u);    /* bit 0: the substep before, third */
    ut_check(third + 1u == s_tick && s->view[0].known[1] && s->view[0].based[1],
             "stepped over with its bit set, its whole record counts as held");
    census();
    ut_check(encode(0u, sizeof s_block, &bytes) && bytes < whole, "and the next record is a delta");
    (void)send_out(0u);

    ut_section("a late acknowledgement of an old life's whole record does not hold the new life");
    confirmed_empty();
    enemy(1u, 20.0f, 10.0f, 30.0f);
    census();
    (void)encode(0u, sizeof s_block, &bytes);
    first = send_out(0u);
    s_live[1] = false;
    census();                                         /* it died */
    s_live[1] = true;
    census();                                         /* and came back: a new life */
    mp_enemy_sync_acked_for(0u, first, 0u);
    ut_check(!s->view[0].based[1], "the old life's slot no longer names the key");
    ut_check(encode(0u, sizeof s_block, &bytes) && bytes == whole, "so the new life goes whole");
    (void)send_out(0u);

    /* There is no engine here, so the census of begin_send finds nothing whatever the level, and a
     * key would be a new life at the next census either way. What tells the rule is `was_live`
     * straight after that census: the old level's census left the key live, and only the rule
     * makes the census of the new one take it as not live before. */
    ut_section("a census in another level starts every placement over");
    fresh_host();
    enemy(1u, 20.0f, 10.0f, 30.0f);
    census();
    levels = s->census_new_level;
    life   = s->placement[1].generation;
    mp_enemy_sync_set_level(true, HERE + 1u);
    (void)mp_enemy_sync_begin_send();
    ut_check(s->census_new_level == levels + 1u, "the census counts the level it has not seen");
    ut_check(!s->placement[1].was_live,
             "and takes the key as not live before, whatever the old level's census said");
    census();
    ut_check(s->placement[1].generation == (uint8_t)(life + 1u), "so the key starts a new life");
    mp_enemy_sync_set_level(true, HERE);
}

static void check_a_view_given_up(void)
{
    enemy_sync_state_t *s = mp_enemy_sync_state();
    uint32_t            tick;

    ut_section("a view given up whole keeps the lives its peer still holds");
    fresh_host();
    enemy(1u, 20.0f, 10.0f, 30.0f);
    substep(0u);
    tick = s_tick;
    mp_enemy_sync_acked_for(0u, tick, 0u);
    ut_check(s->view[0].confirmed && s->view[0].interest.row[1].seen, "the view knows key 1");
    mp_enemy_sync_acked_for(0u, tick + MP_ENEMY_SYNC_ACK_RING + 8u, 0u);
    ut_check(!s->view[0].confirmed && !s->view[0].known[1] && s->view[0].interest.row[1].seen,
             "an acknowledgement from further back than the ring gives the view up, and the "
             "rule still knows its peer was told of key 1");
    s_live[1] = false;
    substep(0u);
    s_live[1] = true;
    census();
    ut_check(mp_enemy_sync_must_bytes(0u) != 0u,
             "so the new life of key 1 may not wait: its peer holds the old one");
    mp_enemy_sync_forget_view(0u);
    ut_check(mp_enemy_sync_must_bytes(0u) == 0u,
             "a new connection on the same seat holds nothing, and owes nothing");
}

static void check_the_copies_rank(void)
{
    enemy_sync_state_t *s = mp_enemy_sync_state();
    size_t              room;
    size_t              bytes = 0;
    size_t              t;
    size_t              placements = 0;
    size_t              copies     = 0;

    ut_section("a copy ranks with the placements");
    fresh_host();
    enemy(1u, 20.0f, 10.0f, 30.0f);
    enemy(MP_WIRE_KEY_COPY_BASE + 2u, 20.0f, 10.0f, 30.0f);
    census();
    ut_check(mp_enemy_sync_frame_bytes(0u) == HEADER + HEAD_ONE_BYTE,
             "with a copy alive the block's own part takes the copies' head as well");
    room = HEADER + HEAD_ONE_BYTE + MP_ENEMY_SYNC_COPY_IDENTITY_BYTES +
           whole_bytes(MP_WIRE_KEY_COPY_BASE + 2u);
    for (t = 0; t < 6u; ++t) {
        if (t != 0u) {
            census();
        }
        ut_check(encode(0u, room, &bytes), "a block with room for one record and the head");
        placements += described(0u, 1u) ? 1u : 0u;
        copies += described(0u, MP_WIRE_KEY_COPY_BASE + 2u) ? 1u : 0u;
        (void)send_out(0u);
    }
    ut_checkf(placements == 3u && copies == 3u,
              "six blocks carry the placement three times and the copy three times (%u, %u)",
              (unsigned)placements, (unsigned)copies);
    ut_check(s->copy_blocks_sent != 0u, "and every block carried the copies' head");
}

static void check_no_world_is_the_round_robin(void)
{
    size_t bytes = 0;
    size_t room;
    size_t key;
    size_t t;
    bool     in_turn = true;
    uint32_t unmeasured;

    ut_section("with no world to read every key is middle, and the walk is the round robin");
    fresh_host();
    unmeasured = mp_enemy_sync_state()->counts[0].by_reach[MP_ENEMY_REACH_NONE];
    mp_enemy_sync_set_interest(NULL);
    for (key = 0; key < 10u; ++key) {
        enemy(key, 20.0f, 10.0f, 30.0f);
    }
    census();
    mp_enemy_sync_set_interest(NULL);
    room = HEADER + 4u * (MP_ENEMY_SYNC_IDENTITY_BYTES + whole_bytes(0u));
    for (t = 0; t < 5u; ++t) {
        size_t i;

        (void)encode(0u, room, &bytes);
        for (i = 0; i < 4u; ++i) {
            size_t expected = (t * 4u + i) % 10u;

            in_turn = in_turn && described(0u, expected);
        }
        (void)send_out(0u);
    }
    ut_check(in_turn,
             "blocks of four take 0 to 3, 4 to 7, 8, 9, 0 and 1, and so on, in turn");
    ut_check(mp_enemy_sync_state()->counts[0].by_reach[MP_ENEMY_REACH_NONE] == unmeasured + 20u,
             "every record counted as one with no position to measure against");
}

static void check_a_hit_ranks_its_enemy_up(void)
{
    enemy_sync_state_t *s   = mp_enemy_sync_state();
    const size_t        one = MP_ENEMY_SYNC_IDENTITY_BYTES;
    interest_counts_t   before[MP_ENEMY_SYNC_VIEWS];
    size_t              bytes = 0;
    size_t              room;
    size_t              key;
    size_t              t;

    ut_section("an enemy that hit a peer's player goes first for that peer, for a second");
    fresh_host();
    for (key = 1u; key <= 6u; ++key) {
        enemy(key, 20.0f, 10.0f, 30.0f);   /* six middle ones, nobody's target */
    }
    census();
    for (t = 0; t < 2u; ++t) {
        (void)encode(t, sizeof s_block, &bytes);
        (void)send_out(t);
    }
    memcpy(before, s->counts, sizeof before);
    mp_enemy_sync_note_struck(4u, 1u);             /* key 4 hit the player of slot 1, view 0 */
    mp_enemy_sync_note_struck(4u, 0u);             /* the host's own player has no view */
    mp_enemy_sync_note_struck(4u, 4u);             /* nor has a slot past the last view */
    mp_enemy_sync_note_struck(0xFFFFu, 2u);        /* a hit with no attacker has no row */
    ut_check(s->counts[0].struck_noted == before[0].struck_noted + 1u &&
                 s->counts[1].struck_noted == before[1].struck_noted &&
                 s->counts[2].struck_noted == before[2].struck_noted,
             "the hit is noted for the view of slot 1 alone, and the three others for none");

    room = HEADER + one + whole_bytes(1u);
    census();
    ut_check(encode(0u, room, &bytes) && s_block[0] == 1u && described(0u, 4u),
             "with room for one record, the view whose player it hit gets that enemy first");
    (void)send_out(0u);
    ut_check(encode(1u, room, &bytes) && s_block[0] == 1u && !described(1u, 4u),
             "and the other view gets the round robin's next");
    (void)send_out(1u);
    ut_check(s->counts[0].struck_boosted == before[0].struck_boosted + 1u &&
                 s->counts[1].struck_boosted == before[1].struck_boosted,
             "counted as a substep the hit doubled where the enemy's target would not have");

    for (t = 1u; t < MP_ENEMY_INTEREST_STRUCK_SUBSTEPS + 4u; ++t) {
        substep(0u);
    }
    ut_checkf(s->counts[0].struck_boosted ==
                  before[0].struck_boosted + MP_ENEMY_INTEREST_STRUCK_SUBSTEPS,
              "for a second and no longer: %u substep(s)",
              (unsigned)(s->counts[0].struck_boosted - before[0].struck_boosted));
    ut_check(!mp_enemy_interest_struck(&s->view[0].interest.row[4]), "and then it has run out");
}

static void check_the_gaps_a_client_sees(void)
{
    enemy_sync_state_t *s = mp_enemy_sync_state();
    uint8_t             bitmap[MP_ENEMY_SYNC_BITMAP_BYTES];
    bool                named[MP_ENEMY_SYNC_KEYS];
    uint32_t            over;
    uint32_t            measured;

    ut_section("a client counts the gaps between two records of one replica");
    mp_enemy_sync_reset();
    memset(bitmap, 0, sizeof bitmap);
    memset(named, 0, sizeof named);
    bitmap[0] = 0x80u;   /* key 7 */
    named[7]  = true;
    over      = s->gaps_over;
    measured  = s->gaps_measured;
    mp_enemy_sync_note_gaps(bitmap, named, 100u);
    mp_enemy_sync_note_gaps(bitmap, named, 110u);
    mp_enemy_sync_note_gaps(bitmap, named, 150u);
    ut_check(s->gaps_measured == measured + 2u && s->gaps_over == over + 1u &&
                 s->gap_longest >= 40u,
             "ten substeps is within the far class's wait, forty is not");
    named[7] = false;
    mp_enemy_sync_note_gaps(bitmap, named, 190u);
    bitmap[0] = 0u;
    mp_enemy_sync_note_gaps(bitmap, named, 191u);
    bitmap[0] = 0x80u;
    named[7]  = true;
    mp_enemy_sync_note_gaps(bitmap, named, 300u);
    ut_check(s->gaps_measured == measured + 2u,
             "a key the host stopped listing starts a new run: its next record is no gap");
    mp_enemy_sync_reset();
    mp_enemy_sync_note_gaps(bitmap, named, 900u);
    ut_check(s->gaps_measured == measured + 2u, "and so does every key after a reset");
}

int main(void)
{
    check_the_bitmap_and_the_far();
    check_a_first_description_near();
    check_a_silent_peer();
    check_the_question_for_the_floor();
    check_opened_and_deferred_keys();
    check_a_delta_waits_for_its_whole_record();
    check_a_view_given_up();
    check_the_copies_rank();
    check_no_world_is_the_round_robin();
    check_a_hit_ranks_its_enemy_up();
    check_the_gaps_a_client_sees();

    ut_section("the report runs");
    mp_enemy_sync_report();
    ut_check(true, "reporting after all of the above is not a fault");

    mp_enemy_sync_set_interest(NULL);
    return ut_summary("mp_enemy_sync_interest");
}
