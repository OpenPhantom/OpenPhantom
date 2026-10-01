/* Two clients arriving beside the host at once, each on a seat of its own: the order of the
 * session's slots, through the real seat search, its wish and the order that reads the roster.
 *
 * A field run put both clients of SWAMP on 112.04 27.72. Slot 1 found its two own directions of
 * the near ring not walkable and went on into direction 4, which is the first of slot 2's, and
 * slot 2 took direction 4 in the same moment: a seat another machine is about to hand out is no
 * body. Every client foresees the seats of the client slots below its own, with the bodies each
 * of them sees, and keeps them clear.
 *
 * What is replaced is the engine under the search and the bridge beside the order: the three probes
 * answer from a flat floor with the points the test makes unwalkable, crawl spaces and holes, the
 * cells are fields of this file, and so are the roster and the far banks. One machine at a time is
 * played: the far bodies it sees are noted, its own body is the hero position, and its arrival wish
 * runs one look.
 *
 * SIZE NOTE: over 600 lines. A third of it is the engine and the session the test plays; the seam,
 * when it grows, is that third into a stand-in of its own, as mp_seat_world's is named.
 */
#include "unittest.h"

#include "mp_bridge_far.h"
#include "mp_bridge_roster.h"
#include "mp_cells.h"
#include "mp_placements.h"
#include "mp_roster.h"
#include "mp_seat.h"
#include "mp_seat_order.h"
#include "mp_signatures.h"
#include "mp_signatures_world.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ==============================================================================================
 * The engine, as this test plays it.
 * ============================================================================================ */

#define WORLD_RECORD_BYTES  0x60u
#define WORLD_CLOCK_SECONDS 0x54u   /* the world's own clock inside that record, in seconds */
#define POINTS              8u
#define NEAR_A_POINT        0.1f

typedef struct little_world {
    float  floor_z;
    float  unwalkable[POINTS][3];   /* a walkable line that ends here is stopped */
    size_t unwalkable_count;
    float  crawl[POINTS][3];        /* a crawl space over each of these */
    size_t crawl_count;
    bool   crawl_everywhere;        /* and over every point but `open` */
    float  open[3];
    float  hole[3];                 /* no floor at all under this point */
    bool   hole_on;
} little_world_t;

typedef struct little_engine {
    uint32_t game_mode;
    uint32_t world;
    uint8_t  world_record[WORLD_RECORD_BYTES];
    bool     own_known;
    float    own[3];
    uint32_t substep;
} little_engine_t;

static little_world_t  wld;
static little_engine_t eng;

static bool at(const float a[3], const float b[3])
{
    float dx = a[0] - b[0];
    float dy = a[1] - b[1];

    return dx * dx + dy * dy < NEAR_A_POINT * NEAR_A_POINT;
}

/* `points` is `count` points of three floats each. */
static bool in_list(const float position[3], const float *points, size_t count)
{
    size_t i;

    for (i = 0; i < count; ++i) {
        if (at(position, &points[3u * i])) {
            return true;
        }
    }
    return false;
}

static void __cdecl fake_probe_floor(const float position[3], void *ground)
{
    float distance = wld.floor_z - position[2];

    if (wld.hole_on && at(position, wld.hole)) {
        distance = MP_PROBE_NO_FLOOR;
    }
    memcpy(ground, &distance, sizeof distance);
}

static float __cdecl fake_head_clearance(const float position[3], uint16_t mask)
{
    (void)mask;
    if (wld.crawl_everywhere && !at(position, wld.open)) {
        return 1.0f;
    }
    return in_list(position, &wld.crawl[0][0], wld.crawl_count) ? 1.0f : 0.0f;
}

static float __cdecl fake_walkable_distance(uintptr_t world, const float from[3],
                                            const float to[3])
{
    (void)world;
    (void)from;
    return in_list(to, &wld.unwalkable[0][0], wld.unwalkable_count) ? 1.0f : 0.0f;
}

uintptr_t mp_cells_address(mp_cell_t cell)
{
    switch (cell) {
    case MP_CELL_GAME_MODE: return (uintptr_t)&eng.game_mode;
    case MP_CELL_LEVEL:     return (uintptr_t)&eng.world;
    default:                return 0u;
    }
}

bool mp_cells_hero_position(float out[3])
{
    if (!eng.own_known) {
        return false;
    }
    memcpy(out, eng.own, sizeof eng.own);
    return true;
}

size_t mp_signatures_world_resolve(void)
{
    return (size_t)MP_WORLD_SITE_COUNT;
}

uintptr_t mp_signatures_world_address(mp_world_site_t site)
{
    switch (site) {
    case MP_WORLD_SITE_PROBE_FLOOR:       return (uintptr_t)&fake_probe_floor;
    case MP_WORLD_SITE_HEAD_CLEARANCE:    return (uintptr_t)&fake_head_clearance;
    case MP_WORLD_SITE_WALKABLE_DISTANCE: return (uintptr_t)&fake_walkable_distance;
    default:                              return 0u;
    }
}

bool mp_placements_table(uintptr_t *world, uint32_t *count, uint32_t *table)
{
    (void)world;
    (void)count;
    (void)table;
    return false;
}

/* ---- the session the order reads: the roster and which far bank shows a slot ----------------- */

#define SLOTS 8u   /* one past the highest slot a session holds */

typedef struct little_session {
    bool        roster_known;
    mp_roster_t roster;
    size_t      bank_of_slot[SLOTS];
    bool        occupied[SLOTS];   /* by bank: a far body of this world stands behind it */
} little_session_t;

static little_session_t session;

/* The roster holds these slots from now on. */
static void stand_in_roster(const uint8_t *slots, size_t count)
{
    size_t i;

    memset(&session.roster, 0, sizeof session.roster);
    for (i = 0; i < count && i < MP_ROSTER_MAX_ENTRIES; ++i) {
        session.roster.entry[i].slot = slots[i];
        ++session.roster.count;
    }
    session.roster_known = true;
}

/* No roster has arrived, and no bank shows anybody. */
static void stand_in_no_session(void)
{
    memset(&session, 0, sizeof session);
}

/* Bank `bank` shows `slot` from now on; 0 takes the slot's bank away. */
static void stand_in_bank_shows(uint8_t slot, size_t bank)
{
    if (slot < SLOTS) {
        session.bank_of_slot[slot] = bank;
    }
}

bool mp_bridge_roster_current(mp_roster_t *out)
{
    if (out == NULL || !session.roster_known) {
        return false;
    }
    *out = session.roster;
    return true;
}

size_t mp_bridge_far_bank_of_slot(uint8_t slot)
{
    return slot < SLOTS ? session.bank_of_slot[slot] : 0u;
}

bool mp_bridge_far_occupied(size_t bank)
{
    return bank < SLOTS && session.occupied[bank];
}

/* ==============================================================================================
 * The session, one machine at a time.
 * ============================================================================================ */

static void set_the_clock(float seconds)
{
    memcpy(eng.world_record + WORLD_CLOCK_SECONDS, &seconds, sizeof seconds);
}

/* A level on a flat floor at `floor_z`, with nothing in the way yet. */
static void open_the_level(float floor_z)
{
    memset(&wld, 0, sizeof wld);
    wld.floor_z   = floor_z;
    eng.game_mode = 2u;
    eng.world     = (uint32_t)(uintptr_t)eng.world_record;
    set_the_clock(1.0f);
    stand_in_no_session();
    mp_seat_world_ended();
    mp_seat_note_no_body(0u);
    mp_seat_note_no_body(1u);
    mp_seat_note_no_body(2u);
}

/* The point of `direction` on the ring of `radius` around `anchor`. */
static void ring_point(const float anchor[3], size_t direction, float radius, float out[3])
{
    float offset[2];

    mp_seat_rule_ring_offset(direction, 0u, radius, offset);
    out[0] = anchor[0] + offset[0];
    out[1] = anchor[1] + offset[1];
    out[2] = anchor[2];
}

static void unwalkable(const float anchor[3], size_t direction)
{
    ring_point(anchor, direction, MP_SEAT_RING_NEAR, wld.unwalkable[wld.unwalkable_count++]);
}

static void crawl_over(const float anchor[3], size_t direction)
{
    ring_point(anchor, direction, MP_SEAT_RING_NEAR, wld.crawl[wld.crawl_count++]);
}

/* What one machine sees: this player's own body, and a far body per bank, NULL for none. The
 * host is always bank 1. */
static void machine_sees(const float own[3], const float *bank1, const float *bank2,
                         const float *bank3)
{
    const float *far[3] = { bank1, bank2, bank3 };
    size_t       i;

    eng.own_known = own != NULL;
    if (own != NULL) {
        memcpy(eng.own, own, sizeof eng.own);
    }
    for (i = 0; i < 3u; ++i) {
        if (far[i] != NULL) {
            mp_seat_note_body(i, far[i], 0.0f, true);
        } else {
            mp_seat_note_no_body(i);
        }
    }
}

/* This machine's player of `slot` arrives beside the host at `host`: one look of the arrival's
 * wish, as the arrival starts and drives it. */
static bool arrive(uint8_t slot, const float host[3], mp_seat_counts_t *counts, float seat[3])
{
    mp_seat_wish_t wish;
    float          heading = 0.0f;

    mp_seat_order_wish_beside_host(&wish, "the offset", slot, false);
    mp_seat_wish_follow(&wish, host, 0.0f);
    return mp_seat_wish_step(&wish, ++eng.substep, counts, seat, &heading);
}

static float apart(const float a[3], const float b[3])
{
    float dx = a[0] - b[0];
    float dy = a[1] - b[1];
    float dz = a[2] - b[2];

    return sqrtf(dx * dx + dy * dy + dz * dz);
}

static const uint8_t ROSTER_OF_THREE[3] = { 0u, 1u, 2u };

/* ==============================================================================================
 * Two clients arriving together.
 * ============================================================================================ */

static const float HOST[3] = { 50.0f, 50.0f, 10.0f };

/* Slot 1's two own directions of the near ring lie under crawl spaces, so it goes on into
 * direction 4, the first of slot 2's. Both clients still stand at the level start beside the
 * host, off the ring. */
static void two_crawl_spaces(void)
{
    open_the_level(HOST[2]);
    crawl_over(HOST, 2u);
    crawl_over(HOST, 3u);
}

static const float SLOT1_AT_START[3] = { 50.2f, 50.1f, 10.0f };
static const float SLOT2_AT_START[3] = { 49.8f, 49.9f, 10.0f };

static void check_two_clients_are_handed_two_points(void)
{
    mp_seat_counts_t one;
    mp_seat_counts_t two;
    float            a[3] = { 0.0f, 0.0f, 0.0f };
    float            b[3] = { 0.0f, 0.0f, 0.0f };
    float            expected[3];

    ut_section("two clients arriving together are handed two points at least a unit apart");
    memset(&one, 0, sizeof one);
    memset(&two, 0, sizeof two);
    two_crawl_spaces();
    stand_in_roster(ROSTER_OF_THREE, 3u);

    /* Slot 1's machine: the host in bank 1, slot 2 in bank 2. */
    stand_in_bank_shows(0u, 1u);
    stand_in_bank_shows(2u, 2u);
    machine_sees(SLOT1_AT_START, HOST, SLOT2_AT_START, NULL);
    ut_check(arrive(1u, HOST, &one, a), "slot 1 is handed a point");
    ring_point(HOST, 4u, MP_SEAT_RING_NEAR, expected);
    ut_checkf(apart(a, expected) < 0.01f,
              "past its two own directions, on direction 4 (%.2f %.2f)", (double)a[0],
              (double)a[1]);
    ut_checkf(one.order_lower == 0u && one.order_held == 0u,
              "slot 1 comes after nobody (%u lower slot(s))", (unsigned)one.order_lower);

    /* Slot 2's machine: the host in bank 1, slot 1 in bank 2. */
    stand_in_bank_shows(2u, 0u);
    stand_in_bank_shows(1u, 2u);
    machine_sees(SLOT2_AT_START, HOST, SLOT1_AT_START, NULL);
    ut_check(arrive(2u, HOST, &two, b), "slot 2 is handed a point");
    ut_checkf(apart(a, b) >= MP_SEAT_BODY_CLEARANCE,
              "not slot 1's: the two points are %.2f units apart", (double)apart(a, b));
    ring_point(HOST, 5u, MP_SEAT_RING_NEAR, expected);
    ut_checkf(apart(b, expected) < 0.01f,
              "slot 2 goes on to direction 5 (%.2f %.2f)", (double)b[0], (double)b[1]);
    ut_checkf(two.order_lower == 1u && two.order_held == 1u && two.order_none == 0u,
              "seated after 1 lower slot, 1 candidate held for its seat (%u, %u)",
              (unsigned)two.order_lower, (unsigned)two.order_held);
    ut_checkf(two.refused[MP_SEAT_TAKEN] == 1u && two.searches == 1u && two.found_nothing == 0u,
              "the held candidate is a taken one, and the look found a seat (%u taken)",
              (unsigned)two.refused[MP_SEAT_TAKEN]);
    mp_seat_report("the offset's seat:", "the offset's fallback:", &two);
}

/* The slots come from the roster. A lower slot whose body no bank shows yet, because its machine
 * is still loading, is foreseen all the same; one that is not in the session is nobody. */
static void check_the_slots_come_from_the_roster(void)
{
    mp_seat_counts_t counts;
    float            seat[3] = { 0.0f, 0.0f, 0.0f };
    float            expected[3];

    ut_section("slot 1 is in the roster and no bank shows it yet: slot 2 still "
               "keeps its seat clear");
    memset(&counts, 0, sizeof counts);
    two_crawl_spaces();
    stand_in_roster(ROSTER_OF_THREE, 3u);
    stand_in_bank_shows(0u, 1u);
    machine_sees(SLOT2_AT_START, HOST, NULL, NULL);
    ut_check(arrive(2u, HOST, &counts, seat), "slot 2 is handed a point");
    ring_point(HOST, 5u, MP_SEAT_RING_NEAR, expected);
    ut_checkf(apart(seat, expected) < 0.01f,
              "direction 5, beside the seat slot 1 will take (%.2f %.2f)", (double)seat[0],
              (double)seat[1]);

    ut_section("slot 1 is not in the roster: slot 2 takes its own first direction");
    {
        static const uint8_t WITHOUT_ONE[2] = { 0u, 2u };

        memset(&counts, 0, sizeof counts);
        two_crawl_spaces();
        stand_in_roster(WITHOUT_ONE, 2u);
        stand_in_bank_shows(0u, 1u);
        machine_sees(SLOT2_AT_START, HOST, NULL, NULL);
        ut_check(arrive(2u, HOST, &counts, seat), "slot 2 is handed a point");
        ring_point(HOST, 4u, MP_SEAT_RING_NEAR, expected);
        ut_checkf(apart(seat, expected) < 0.01f && counts.order_lower == 0u,
                  "direction 4, its own first (%.2f %.2f)", (double)seat[0], (double)seat[1]);
    }

    ut_section("no roster has arrived: the wish searches as it did, and the report counts it");
    memset(&counts, 0, sizeof counts);
    two_crawl_spaces();
    machine_sees(SLOT2_AT_START, HOST, SLOT1_AT_START, NULL);
    ut_check(arrive(2u, HOST, &counts, seat), "slot 2 is handed a point");
    ring_point(HOST, 4u, MP_SEAT_RING_NEAR, expected);
    ut_checkf(apart(seat, expected) < 0.01f && counts.order_no_roster == 1u,
              "direction 4, and 1 wish begun with no roster (%u)",
              (unsigned)counts.order_no_roster);
}

/* A player who joins a running session finds the slots below its own standing in the level. In
 * a field run slot 3 held two candidates for slot 1 and slot 2, who had stood beside the host for
 * minutes. A late player's lower slot that stands in this world is a body, not a reservation; one
 * that is still in its lobby is foreseen as ever, and a start everybody takes together keeps the
 * order whatever stands. */
static void check_a_late_player_holds_no_seat_for_the_standing(void)
{
    static const uint8_t ROSTER_OF_FOUR[4] = { 0u, 1u, 2u, 3u };
    mp_seat_wish_t       wish;

    ut_section("a late player's lower slots that stand already hold no seat");
    open_the_level(10.0f);
    stand_in_roster(ROSTER_OF_FOUR, 4u);
    stand_in_bank_shows(0u, 1u);
    stand_in_bank_shows(1u, 2u);
    stand_in_bank_shows(2u, 3u);
    session.occupied[2] = true;
    session.occupied[3] = true;
    mp_seat_order_wish_beside_host(&wish, "the offset", 3u, true);
    ut_checkf(wish.order_known && wish.order_count == 0u,
              "slot 3, late, with slots 1 and 2 standing: no seat held for either (%u)",
              (unsigned)wish.order_count);
    mp_seat_order_wish_beside_host(&wish, "the offset", 3u, false);
    ut_checkf(wish.order_count == 2u,
              "the same players arriving together: both lower slots foreseen, as before (%u)",
              (unsigned)wish.order_count);
    session.occupied[3] = false;
    mp_seat_order_wish_beside_host(&wish, "the offset", 3u, true);
    ut_checkf(wish.order_count == 1u && wish.order_slot[0] == 2u,
              "late, with slot 2 still in its lobby: slot 2 alone is foreseen (%u)",
              (unsigned)wish.order_count);
    stand_in_no_session();
}

/* A lower slot's machine sees this player's body and not its own, and so does the foresight: a
 * foresight that saw the lower slot's body and not this one's would foresee a seat that slot never
 * takes, and hand this player the one it does. */
static void check_the_foresight_sees_what_the_lower_slot_sees(void)
{
    mp_seat_counts_t counts;
    float            slot1[3] = { 0.0f, 0.0f, 0.0f };
    float            slot2[3] = { 0.0f, 0.0f, 0.0f };
    float            direction4[3];
    float            direction5[3];

    two_crawl_spaces();
    ring_point(HOST, 4u, MP_SEAT_RING_NEAR, direction4);
    ring_point(HOST, 5u, MP_SEAT_RING_NEAR, direction5);
    stand_in_roster(ROSTER_OF_THREE, 3u);

    ut_section("slot 2 stands on direction 4 already: slot 1 goes past it, and slot 2 knows it");
    memset(&counts, 0, sizeof counts);
    stand_in_bank_shows(0u, 1u);
    stand_in_bank_shows(2u, 2u);
    machine_sees(SLOT1_AT_START, HOST, direction4, NULL);
    ut_check(arrive(1u, HOST, &counts, slot1), "slot 1's own machine hands it a point");
    memset(&counts, 0, sizeof counts);
    stand_in_bank_shows(2u, 0u);
    stand_in_bank_shows(1u, 2u);
    machine_sees(direction4, HOST, SLOT1_AT_START, NULL);
    ut_check(arrive(2u, HOST, &counts, slot2), "slot 2's machine hands it a point");
    ut_checkf(apart(slot1, direction5) < 0.01f && apart(slot2, direction4) < 0.01f,
              "slot 1 on direction 5, slot 2 where it stands (%.2f units apart)",
              (double)apart(slot1, slot2));

    ut_section("slot 1 stands on its seat already: the foresight leaves its body out");
    memset(&counts, 0, sizeof counts);
    machine_sees(SLOT2_AT_START, HOST, direction4, NULL);
    ut_check(arrive(2u, HOST, &counts, slot2), "slot 2 is handed a point");
    ut_checkf(apart(slot2, direction5) < 0.01f && counts.order_held == 0u,
              "direction 5, the next one, and nothing held for a seat foreseen elsewhere (%u)",
              (unsigned)counts.order_held);
}

/* ==============================================================================================
 * A field run with three players, on FEDSHIP and SWAMP.
 * ============================================================================================ */

/* FEDSHIP: slot 1 found direction 2 not walkable and took direction 3, slot 2 took direction 4.
 * Neither changes. SWAMP, with the anchor the log shows: slot 1 found directions 2 and 3 not
 * walkable and took direction 4, and slot 2 took the same point. Slot 2 no longer does. */
static void check_the_field_run(void)
{
    static const float FEDSHIP_HOST[3] = { 122.30f, 129.00f, 42.00f };
    static const float SWAMP_ANCHOR[3] = { 114.04f, 27.72f, 26.79f };
    static const float SWAMP_START[3]  = { 109.00f, 13.00f, 28.00f };
    mp_seat_counts_t   counts;
    float              slot1[3] = { 0.0f, 0.0f, 0.0f };
    float              slot2[3] = { 0.0f, 0.0f, 0.0f };

    ut_section("the field run on FEDSHIP: both clients where they were");
    memset(&counts, 0, sizeof counts);
    open_the_level(FEDSHIP_HOST[2]);
    unwalkable(FEDSHIP_HOST, 2u);
    stand_in_roster(ROSTER_OF_THREE, 3u);
    stand_in_bank_shows(0u, 1u);
    stand_in_bank_shows(2u, 2u);
    machine_sees(FEDSHIP_HOST, FEDSHIP_HOST, FEDSHIP_HOST, NULL);
    ut_check(arrive(1u, FEDSHIP_HOST, &counts, slot1), "slot 1 is handed a point");
    stand_in_bank_shows(2u, 0u);
    stand_in_bank_shows(1u, 2u);
    ut_check(arrive(2u, FEDSHIP_HOST, &counts, slot2), "slot 2 is handed a point");
    ut_checkf(fabsf(slot1[0] - 120.89f) < 0.01f && fabsf(slot1[1] - 130.41f) < 0.01f,
              "slot 1 at 120.89 130.41 as in the field run (%.2f %.2f)", (double)slot1[0],
              (double)slot1[1]);
    ut_checkf(fabsf(slot2[0] - 120.30f) < 0.01f && fabsf(slot2[1] - 129.00f) < 0.01f,
              "slot 2 at 120.30 129.00 as in the field run (%.2f %.2f)", (double)slot2[0],
              (double)slot2[1]);

    ut_section("the field run on SWAMP: slot 1 where it was, slot 2 no longer on the same point");
    memset(&counts, 0, sizeof counts);
    open_the_level(SWAMP_ANCHOR[2]);
    unwalkable(SWAMP_ANCHOR, 2u);
    unwalkable(SWAMP_ANCHOR, 3u);
    stand_in_roster(ROSTER_OF_THREE, 3u);
    stand_in_bank_shows(0u, 1u);
    stand_in_bank_shows(2u, 2u);
    machine_sees(SWAMP_START, SWAMP_ANCHOR, SWAMP_START, NULL);
    ut_check(arrive(1u, SWAMP_ANCHOR, &counts, slot1), "slot 1 is handed a point");
    stand_in_bank_shows(2u, 0u);
    stand_in_bank_shows(1u, 2u);
    ut_check(arrive(2u, SWAMP_ANCHOR, &counts, slot2), "slot 2 is handed a point");
    ut_checkf(fabsf(slot1[0] - 112.04f) < 0.01f && fabsf(slot1[1] - 27.72f) < 0.01f,
              "slot 1 at 112.04 27.72 as in the field run (%.2f %.2f)", (double)slot1[0],
              (double)slot1[1]);
    ut_checkf(apart(slot1, slot2) >= MP_SEAT_BODY_CLEARANCE,
              "slot 2 at %.2f %.2f, %.2f units from slot 1 rather than on its point",
              (double)slot2[0], (double)slot2[1], (double)apart(slot1, slot2));
    ut_checkf(fabsf(slot2[0] - 112.63f) < 0.01f && fabsf(slot2[1] - 26.31f) < 0.01f,
              "on direction 5 (%.2f %.2f)", (double)slot2[0], (double)slot2[1]);
}

/* ==============================================================================================
 * What the foresight does when it finds nothing, and when the anchor moves.
 * ============================================================================================ */

static void check_a_lower_slot_that_finds_nothing(void)
{
    static const uint8_t ROSTER_OF_FOUR[4] = { 0u, 1u, 2u, 3u };
    mp_seat_counts_t     counts;
    float                seat[3] = { 0.0f, 0.0f, 0.0f };

    ut_section("one free point around the host: slot 1 takes it, slots 2 and 3 find none");
    memset(&counts, 0, sizeof counts);
    open_the_level(HOST[2]);
    wld.crawl_everywhere = true;
    ring_point(HOST, 6u, MP_SEAT_RING_FAR, wld.open);
    stand_in_roster(ROSTER_OF_FOUR, 4u);
    stand_in_bank_shows(0u, 1u);
    machine_sees(SLOT2_AT_START, HOST, NULL, NULL);
    ut_check(!arrive(3u, HOST, &counts, seat), "slot 3 is handed nothing on this look");
    ut_checkf(counts.order_lower == 2u && counts.order_none == 1u && counts.order_held == 1u,
              "after 2 lower slots, 1 foresight that found none, 1 candidate held (%u, %u, %u)",
              (unsigned)counts.order_lower, (unsigned)counts.order_none,
              (unsigned)counts.order_held);
    ut_checkf(counts.searches == 1u && counts.found_nothing == 1u && counts.anchors_tried == 1u,
              "and its own search counts one look that found nothing, the foresights none of "
              "theirs (%u anchor(s) tried)", (unsigned)counts.anchors_tried);

    ut_section("a falling anchor stays a falling anchor");
    memset(&counts, 0, sizeof counts);
    open_the_level(HOST[2]);
    wld.hole_on = true;
    memcpy(wld.hole, HOST, sizeof wld.hole);
    stand_in_roster(ROSTER_OF_FOUR, 4u);
    machine_sees(SLOT2_AT_START, HOST, NULL, NULL);
    ut_check(!arrive(3u, HOST, &counts, seat), "nothing is handed over");
    ut_checkf(counts.anchor_falling == 1u && counts.order_none == 0u && counts.anchors_tried == 1u,
              "counted once as falling, and not as a lower slot that found nothing (%u, %u)",
              (unsigned)counts.anchor_falling, (unsigned)counts.order_none);
}

/* ==============================================================================================
 * The neighbours of a seat handed out.
 * ============================================================================================ */

static void check_the_neighbours_of_a_seat(void)
{
    mp_seat_counts_t counts;
    float            seat[3] = { 0.0f, 0.0f, 0.0f };
    float            direction4[3];

    ring_point(HOST, 4u, MP_SEAT_RING_NEAR, direction4);

    ut_section("two seconds after the hand-over the other client stands a seat away");
    memset(&counts, 0, sizeof counts);
    two_crawl_spaces();
    stand_in_roster(ROSTER_OF_THREE, 3u);
    stand_in_bank_shows(0u, 1u);
    stand_in_bank_shows(1u, 2u);
    machine_sees(SLOT2_AT_START, HOST, NULL, NULL);
    ut_check(arrive(2u, HOST, &counts, seat), "slot 2 is handed a point");
    ut_checkf(fabsf(counts.handed_nearest - MP_SEAT_RING_NEAR) < 0.01f &&
                  counts.later_nearest < 0.0f,
              "the host is %.2f units from it at the hand-over, and the later reading is still "
              "to come (%.2f)", (double)counts.handed_nearest, (double)counts.later_nearest);
    set_the_clock(2.0f);
    machine_sees(SLOT2_AT_START, HOST, direction4, NULL);
    ut_check(counts.later_nearest < 0.0f, "one second on, nothing is read yet");
    set_the_clock(3.1f);
    machine_sees(SLOT2_AT_START, HOST, direction4, NULL);
    ut_checkf(fabsf(counts.later_nearest - apart(seat, direction4)) < 0.01f &&
                  counts.handed_close == 0u,
              "two seconds on, slot 1 stands %.2f units away, and no hand-over was within a unit",
              (double)counts.later_nearest);

    ut_section("with no order the two land on one point, and the line says so");
    memset(&counts, 0, sizeof counts);
    two_crawl_spaces();
    machine_sees(SLOT2_AT_START, HOST, NULL, NULL);
    ut_check(arrive(2u, HOST, &counts, seat), "slot 2 is handed a point");
    set_the_clock(3.1f);
    machine_sees(SLOT2_AT_START, HOST, direction4, NULL);
    ut_checkf(counts.later_nearest < 0.01f && counts.handed_close == 1u,
              "slot 1 on the same point (%.2f units): 1 hand-over within a unit (%u)",
              (double)counts.later_nearest, (unsigned)counts.handed_close);
    mp_seat_report("the offset's seat:", "the offset's fallback:", &counts);

    ut_section("a level that ends before the two seconds takes the watch with it");
    memset(&counts, 0, sizeof counts);
    two_crawl_spaces();
    machine_sees(SLOT2_AT_START, HOST, NULL, NULL);
    ut_check(arrive(2u, HOST, &counts, seat), "slot 2 is handed a point");
    mp_seat_world_ended();
    set_the_clock(3.1f);
    machine_sees(SLOT2_AT_START, HOST, direction4, NULL);
    ut_check(counts.later_nearest < 0.0f && counts.handed_close == 0u,
             "nothing is read against a seat of a level that is gone");
}

int main(void)
{
    mp_seat_install();
    ut_check(mp_seat_probes_resolved(), "the seat search binds the probes this test plays");

    check_two_clients_are_handed_two_points();
    check_the_slots_come_from_the_roster();
    check_a_late_player_holds_no_seat_for_the_standing();
    check_the_foresight_sees_what_the_lower_slot_sees();
    check_the_field_run();
    check_a_lower_slot_that_finds_nothing();
    check_the_neighbours_of_a_seat();
    return ut_summary("mp_seat_order");
}
