/* mp_enemy_sync_floor.c: the floor under the enemy block, raised to the records that may not wait.
 *
 * A packet from a host is shared in a fixed order: the bodies' reserve, a floor for the enemy
 * block, the reliable messages that are due, and the rest to the enemies. The floor was a fixed 128
 * bytes, and a substep in which many enemies died at once needs more than that for the records
 * that may not wait. With the messages due at the same time the block got its 128 and left deaths
 * out, each one a replica that goes on standing on the peer's screen for another substep, and an
 * event in its window that is lost once the window has passed.
 *
 * Every number here is the one the bridge works with: the payload's own buffer, the bodies'
 * reserve of a view with three far bodies, a real channel with the largest state notes of a run
 * with four players queued in it, and the functions the bridge calls in the order it calls them.
 * The split with a fixed floor is kept as the reference and shows the cut. What would be silent if
 * it were wrong:
 *
 *   a death left out of a block while the floor could have held it;
 *   a floor that moves a single byte in a quiet level, which would change what two players see;
 *   a question for the floor that moves the interest rule by being asked.
 */
#include "unittest.h"

#include "mp_budget_rule.h"
#include "mp_channel.h"
#include "mp_enemy_interest_rule.h"
#include "mp_enemy_sync.h"
#include "mp_enemy_sync_internal.h"
#include "mp_enemy_wire.h"
#include "mp_session.h"
#include "mp_snapshot.h"
#include "mp_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define HERE    57u
#define ENEMIES 40u
#define DEATHS  30u

/* The bridge's reserve for the bodies of one view: the snapshot's header, the two ticks and a
 * state byte a slot, and the largest record of each body in it. */
#define RESERVE(bodies) (8u + MP_SNAPSHOT_MAX_BODIES + MP_WIRE_BODY_MAX_BYTES * (bodies))

static uint8_t      s_block[2048];
static uint32_t     s_tick = 1000u;
static mp_channel_t s_channel;

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

static const mp_enemy_sync_interest_t SOURCE = { &fake_viewer, &fake_subject, NULL };

/* An enemy standing near every peer's player, which stands at the origin. */
static void enemy(size_t key, float x)
{
    mp_enemy_record_t  *record  = &s_records[key];
    mp_enemy_subject_t *subject = &s_subjects[key];
    uint32_t            packed  = 0;

    s_live[key] = true;
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

    memset(subject, 0, sizeof *subject);
    subject->read          = true;
    subject->has_placement = !mp_wire_key_is_copy((uint32_t)key);
    subject->placement[0]  = x;
    subject->position[0]   = x;
    subject->wake          = 10.0f;
    subject->keep          = 30.0f;
}

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

/* One census as the host takes it; the engine's pass over the pool is played by hand. */
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
        p->current                              = s_records[key];
        p->current.value[MP_ENEMY_F_INDEX]      = mp_enemy_sync_wire_index(key);
        p->current.value[MP_ENEMY_F_GENERATION] = p->generation;
        p->current_ok                           = true;
    }
    mp_enemy_sync_set_interest(&SOURCE);
}

/* A block with room for everything, out and acknowledged. */
static void quiet_substep(void)
{
    size_t bytes = 0;

    census();
    (void)mp_enemy_sync_encode_for(0u, s_block, sizeof s_block, &bytes);
    mp_enemy_sync_sent_for(0u, ++s_tick);
    mp_enemy_sync_acked_for(0u, s_tick, 0u);
}

static size_t deaths_described(void)
{
    size_t count = 0;
    size_t key;

    for (key = 0; key < DEATHS; ++key) {
        count += mp_enemy_sync_state()->view[0].described[key] ? 1u : 0u;
    }
    return count;
}

/* The three largest state notes of a run with four players, queued on a fresh channel: the map
 * digest of the largest level, the roster of four and the setup. */
static void queue_the_state_notes(void)
{
    static const size_t sizes[] = { 680u, 230u, 110u };
    uint8_t             note[680];
    size_t              i;

    mp_channel_init(&s_channel);
    memset(note, 0x86, sizeof note);
    for (i = 0; i < sizeof sizes / sizeof sizes[0]; ++i) {
        (void)mp_channel_send(&s_channel, note, sizes[i]);
    }
}

/* The split as send_to_peer makes it, for a view with `bodies` far bodies and an enemy floor. */
static size_t enemy_room(size_t bodies, size_t enemy_floor, size_t *due)
{
    size_t reserve = RESERVE(bodies);

    *due = mp_channel_due_bytes(&s_channel, 100u, mp_budget_message_limit(reserve, enemy_floor));
    return mp_budget_enemy_room(MP_SESSION_PAYLOAD_BYTES, reserve, *due);
}

static void check_thirty_deaths_at_four_players(void)
{
    enemy_sync_state_t *s = mp_enemy_sync_state();
    interest_counts_t   before;
    send_interest_t     rule;
    size_t              need;
    size_t              floor_bytes;
    size_t              due_fixed = 0;
    size_t              due = 0;
    size_t              room;
    size_t              bytes = 0;
    size_t              key;

    ut_section("thirty deaths in one substep, four players, the state notes due");
    fresh_host();
    for (key = 0; key < ENEMIES; ++key) {
        enemy(key, 5.0f);
    }
    quiet_substep();
    quiet_substep();
    for (key = 0; key < DEATHS; ++key) {
        s_records[key].value[MP_ENEMY_F_STATE] = MP_ENEMY_STATE_DEATH | MP_ENEMY_HAS_HEAD;
    }
    census();
    need = mp_enemy_sync_frame_bytes(0u) + mp_enemy_sync_must_bytes(0u);
    ut_checkf(need > MP_BUDGET_ENEMY_FLOOR_BYTES,
              "the block's head and the thirty deaths need %u byte(s), more than the fixed %u",
              (unsigned)need, (unsigned)MP_BUDGET_ENEMY_FLOOR_BYTES);

    queue_the_state_notes();
    room = enemy_room(3u, MP_BUDGET_ENEMY_FLOOR_BYTES, &due_fixed);
    before = s->counts[0];
    ut_check(mp_enemy_sync_encode_for(0u, s_block, room, &bytes), "the reference block is built");
    ut_checkf(room < need && s->counts[0].must_unfit > before.must_unfit &&
                  deaths_described() < DEATHS,
              "the reference, the fixed floor: %u byte(s) of messages due leave the block %u, and "
              "%u of the thirty deaths are left out of it",
              (unsigned)due_fixed, (unsigned)room, (unsigned)(DEATHS - deaths_described()));
    mp_enemy_sync_abandon_for(0u);

    census();
    rule        = s->view[0].interest;
    floor_bytes = mp_enemy_sync_floor_bytes(0u);
    ut_checkf(floor_bytes == need && mp_enemy_sync_floor_bytes(0u) == floor_bytes,
              "the floor is raised to what they need, %u byte(s), and asked twice it answers the "
              "same", (unsigned)floor_bytes);
    ut_check(memcmp(&rule, &s->view[0].interest, sizeof rule) == 0,
             "and being asked moved nothing in the interest rule");
    room   = enemy_room(3u, floor_bytes, &due);
    before = s->counts[0];
    ut_checkf(room >= need && due < due_fixed,
              "with the floor the block keeps %u byte(s), and the messages beside it shrink from "
              "%u to %u byte(s); the rest rides the overflow packet",
              (unsigned)room, (unsigned)due_fixed, (unsigned)due);
    ut_check(mp_enemy_sync_encode_for(0u, s_block, room, &bytes), "the block is built");
    ut_checkf(s->counts[0].must_unfit == before.must_unfit && deaths_described() == DEATHS,
              "all thirty deaths are in it (%u)", (unsigned)deaths_described());
    ut_checkf(s->counts[0].floor_raised == before.floor_raised + 1u &&
                  s->counts[0].floor_most >= need &&
                  s->counts[0].floor_short == before.floor_short,
              "counted as a block with its floor raised, to %u, and given the room",
              (unsigned)s->counts[0].floor_most);
    mp_enemy_sync_abandon_for(0u);

    census();
    before = s->counts[0];
    ut_check(mp_enemy_sync_encode_for(0u, s_block, MP_BUDGET_ENEMY_FLOOR_BYTES, &bytes),
             "a block given only the fixed floor is built");
    ut_check(s->counts[0].floor_raised == before.floor_raised + 1u &&
                 s->counts[0].floor_short == before.floor_short + 1u,
             "and counted as one whose raised floor it did not get");
}

static void check_a_quiet_level_keeps_the_split(void)
{
    size_t key;
    size_t due;
    size_t bodies;
    bool   same = true;

    ut_section("a quiet level keeps the split byte for byte");
    fresh_host();
    for (key = 0; key < ENEMIES; ++key) {
        enemy(key, 5.0f);
    }
    quiet_substep();
    quiet_substep();
    census();
    ut_check(mp_enemy_sync_must_bytes(0u) == 0u &&
                 mp_enemy_sync_floor_bytes(0u) == MP_BUDGET_ENEMY_FLOOR_BYTES,
             "nothing that may not wait: the floor is the fixed one");
    enemy(MP_WIRE_KEY_COPY_BASE + 2u, 5.0f);
    census();
    ut_check(mp_enemy_sync_frame_bytes(0u) > MP_ENEMY_SYNC_HEADER_BYTES &&
                 mp_enemy_sync_floor_bytes(0u) == MP_BUDGET_ENEMY_FLOOR_BYTES,
             "a copy alive takes the copies' head, and the fixed floor holds it");
    ut_check(mp_budget_message_limit(RESERVE(1u), mp_enemy_sync_floor_bytes(0u)) == 954u &&
                 mp_budget_message_limit(RESERVE(3u), mp_enemy_sync_floor_bytes(0u)) == 822u,
             "two players keep their 954 bytes for messages, four their 822");
    for (bodies = 1u; bodies <= 3u; bodies += 2u) {
        size_t reserve   = RESERVE(bodies);
        size_t old_limit = MP_CHANNEL_PAYLOAD_BYTES - MP_BUDGET_ENEMY_LENGTH_BYTES - reserve -
                           MP_BUDGET_ENEMY_FLOOR_BYTES;

        for (due = 0; due <= MP_CHANNEL_PAYLOAD_BYTES; ++due) {
            size_t seated = due < old_limit ? due : old_limit;
            size_t now    = mp_budget_message_limit(reserve, mp_enemy_sync_floor_bytes(0u));
            size_t seated_now = due < now ? due : now;

            same = same && seated_now == seated &&
                   mp_budget_enemy_room(MP_SESSION_PAYLOAD_BYTES, reserve, seated_now) ==
                       mp_budget_enemy_room(MP_SESSION_PAYLOAD_BYTES, reserve, seated);
        }
    }
    ut_check(same, "for every amount due, with two players and with four, the messages and the "
                   "enemies get what the fixed floor gave them");
}

static void check_before_a_census(void)
{
    ut_section("before a census the floor is the fixed one");
    mp_enemy_sync_reset();
    ut_check(mp_enemy_sync_floor_bytes(0u) == MP_BUDGET_ENEMY_FLOOR_BYTES &&
                 mp_enemy_sync_floor_bytes(MP_ENEMY_SYNC_VIEWS) == MP_BUDGET_ENEMY_FLOOR_BYTES,
             "no census, or a view past the last, asks for the fixed floor");
}

int main(void)
{
    check_thirty_deaths_at_four_players();
    check_a_quiet_level_keeps_the_split();
    check_before_a_census();

    ut_section("the report runs");
    mp_enemy_sync_report();
    ut_check(true, "reporting after all of the above is not a fault");

    mp_enemy_sync_set_interest(NULL);
    return ut_summary("mp_enemy_sync_floor");
}
