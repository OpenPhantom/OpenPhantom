/* The seat rules, pure: every seat this feature hands out is searched by them.
 *
 * Each rule has a known right answer and runs here with no game in the process: where a player's
 * ring starts, what a floor reading and a floor's surface make of a candidate, whether a body or
 * the place of a death is in the way, whether a seat ended a life, which standing player and which
 * client slots come first, when a search that finds nothing falls back and to which stage, and
 * which authored point the co-operative fallback takes.
 *
 * The field run with four players is the case behind most of them. The host died in a pocket where
 * all eight candidates around his anchor failed, the search found nothing 34993 times over minutes,
 * and nothing ever ended it. The clients, meanwhile, had all been handed the same point at the
 * level start. Both are pinned here as a test that fails against the old behaviour.
 */
#include "unittest.h"

#include "mp_seat_rule.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static double horizontal_distance(const float a[2], const float b[2])
{
    double dx = (double)a[0] - (double)b[0];
    double dy = (double)a[1] - (double)b[1];

    return sqrt(dx * dx + dy * dy);
}

static void check_the_ring_starts_per_slot(void)
{
    float  first[4][2];
    size_t slot;
    size_t other;

    ut_section("each slot starts its ring a quarter turn from the next");
    ut_check(mp_seat_rule_ring_start(0u) == 0u,
             "the host's ring starts along positive x, as before");
    ut_check(mp_seat_rule_ring_start(1u) == 2u, "slot 1 starts a quarter turn on");
    ut_check(mp_seat_rule_ring_start(2u) == 4u, "slot 2 a half turn on");
    ut_check(mp_seat_rule_ring_start(3u) == 6u, "slot 3 three quarters on");
    ut_check(mp_seat_rule_ring_start(4u) == 0u,
             "and a fifth slot wraps rather than walks off the ring");

    ut_section("four players seated at once are not handed the same point");
    for (slot = 0; slot < 4u; ++slot) {
        mp_seat_rule_ring_offset(mp_seat_rule_ring_start((uint8_t)slot), 0u, MP_SEAT_RING_NEAR,
                                 first[slot]);
    }
    for (slot = 0; slot < 4u; ++slot) {
        for (other = slot + 1u; other < 4u; ++other) {
            ut_checkf(horizontal_distance(first[slot], first[other]) >
                          2.0 * (double)MP_SEAT_BODY_CLEARANCE,
                      "the first candidates of slots %u and %u stand apart (%.2f units)",
                      (unsigned)slot, (unsigned)other,
                      horizontal_distance(first[slot], first[other]));
        }
    }
}

static void check_the_ring(void)
{
    float  offset[2];
    float  first[2];
    float  ninth[2];
    size_t step;

    ut_section("eight directions, all of them the radius out");
    for (step = 0; step < (size_t)MP_SEAT_RING_STEPS; ++step) {
        mp_seat_rule_ring_offset(2u, step, MP_SEAT_RING_FAR, offset);
        ut_checkf(fabs(sqrt((double)offset[0] * (double)offset[0] +
                            (double)offset[1] * (double)offset[1]) -
                       (double)MP_SEAT_RING_FAR) < 0.001,
                  "step %u of the far ring sits four units out", (unsigned)step);
    }

    ut_section("step 0 is the start direction");
    mp_seat_rule_ring_offset(0u, 0u, 2.0f, offset);
    ut_near((double)offset[0], 2.0, 0.001, "direction 0 lies along x");
    ut_near((double)offset[1], 0.0, 0.001, "and not along y");
    mp_seat_rule_ring_offset(2u, 0u, 2.0f, offset);
    ut_near((double)offset[0], 0.0, 0.001, "direction 2 has no x");
    ut_near((double)offset[1], 2.0, 0.001, "and lies along y");

    ut_section("the index wraps");
    mp_seat_rule_ring_offset(6u, 0u, 2.0f, first);
    mp_seat_rule_ring_offset(6u, (size_t)MP_SEAT_RING_STEPS, 2.0f, ninth);
    ut_near((double)ninth[0], (double)first[0], 0.001, "the ninth step is the first again in x");
    ut_near((double)ninth[1], (double)first[1], 0.001, "and in y");
    mp_seat_rule_ring_offset(0u, 0u, 2.0f, NULL);
    ut_check(true, "and a null destination is answered by writing nothing");
}

static void check_what_a_floor_reading_makes_of_a_candidate(void)
{
    ut_section("one reading, one verdict");
    ut_check(mp_seat_rule_floor_verdict(MP_SEAT_FLOOR_OK) == MP_SEAT_FREE,
             "a floor under the feet leaves the candidate free so far");
    ut_check(mp_seat_rule_floor_verdict(MP_SEAT_FLOOR_NONE) == MP_SEAT_NO_FLOOR,
             "no floor at all is its own reason");
    ut_check(mp_seat_rule_floor_verdict(MP_SEAT_FLOOR_FAR) == MP_SEAT_A_DROP,
             "a floor far below is a drop or water");
    ut_check(mp_seat_rule_floor_verdict(MP_SEAT_FLOOR_MOVER) == MP_SEAT_ON_MOVER,
             "and a mover is refused: no seat is handed out on a lift");
}

static void check_whether_a_body_is_in_the_way(void)
{
    mp_seat_body_t bodies[3];
    const float    seat[3] = { 10.0f, 10.0f, 5.0f };

    memset(bodies, 0, sizeof bodies);
    bodies[0].known       = true;
    bodies[0].stands      = true;
    bodies[0].position[0] = 13.0f;
    bodies[0].position[1] = 10.0f;
    bodies[0].position[2] = 5.0f;

    ut_section("a body closer than the clearance takes the seat");
    ut_check(!mp_seat_rule_near_a_body(seat, bodies, 3u, MP_SEAT_BODY_CLEARANCE),
             "a body three units off leaves it free");
    bodies[1].known       = true;
    bodies[1].position[0] = 10.5f;
    bodies[1].position[1] = 10.0f;
    bodies[1].position[2] = 5.0f;
    ut_check(mp_seat_rule_near_a_body(seat, bodies, 3u, MP_SEAT_BODY_CLEARANCE),
             "a body half a unit off takes it, standing or lying: a corpse is in the way too");
    bodies[1].position[2] = 9.0f;
    ut_check(!mp_seat_rule_near_a_body(seat, bodies, 3u, MP_SEAT_BODY_CLEARANCE),
             "a body on the floor four units above is not in the way of this one");

    ut_section("an empty entry is nobody");
    bodies[1].known       = false;
    bodies[1].position[2] = 5.0f;
    ut_check(!mp_seat_rule_near_a_body(seat, bodies, 3u, MP_SEAT_BODY_CLEARANCE),
             "a far bank this machine has no pose for stands nowhere");
    ut_check(!mp_seat_rule_near_a_body(seat, NULL, 3u, MP_SEAT_BODY_CLEARANCE),
             "and no list is no body");
}

static void check_which_anchor_is_tried_first(void)
{
    mp_seat_body_t bodies[3];
    const float    died_at[3] = { 0.0f, 0.0f, 0.0f };
    size_t         order[3];
    size_t         count;

    memset(bodies, 0, sizeof bodies);
    bodies[0].known       = true;
    bodies[0].stands      = true;
    bodies[0].position[0] = 30.0f;
    bodies[1].known       = true;
    bodies[1].stands      = false;
    bodies[1].position[0] = 1.0f;
    bodies[2].known       = true;
    bodies[2].stands      = true;
    bodies[2].position[0] = 5.0f;

    ut_section("every standing player is an anchor, the one nearest the death first");
    count = mp_seat_rule_anchor_order(bodies, 3u, died_at, order, 3u);
    ut_checkf(count == 2u, "two of the three are standing (%u anchors)", (unsigned)count);
    ut_check(count == 2u && order[0] == 2u && order[1] == 0u,
             "the one five units from the death before the one thirty units off, so a search whose "
             "first anchor stands in a pocket tries the next one on the same look");

    ut_section("with the death unknown, bank order");
    count = mp_seat_rule_anchor_order(bodies, 3u, NULL, order, 3u);
    ut_check(count == 2u && order[0] == 0u && order[1] == 2u, "the first standing bank first");

    ut_section("bounds");
    count = mp_seat_rule_anchor_order(bodies, 3u, died_at, order, 1u);
    ut_check(count == 1u && order[0] == 2u, "a short list keeps the nearest");
    bodies[0].stands = false;
    bodies[2].stands = false;
    ut_check(mp_seat_rule_anchor_order(bodies, 3u, died_at, order, 3u) == 0u,
             "nobody standing is no anchor");
}

static void check_the_clock_of_a_stage(void)
{
    mp_seat_wait_t wait;
    uint32_t       now;
    bool           over = false;

    ut_section("a search on a good anchor that finds nothing runs out after three seconds");
    mp_seat_rule_wait_start(&wait);
    ut_check(!mp_seat_rule_wait_look(&wait, 1000u, MP_SEAT_LOOK_EMPTY),
             "the first look starts the clock and counts nothing");
    ut_check(!mp_seat_rule_wait_look(&wait, 1000u + MP_SEAT_GIVE_UP_SUBSTEPS - 1u,
                                     MP_SEAT_LOOK_EMPTY),
             "one substep short of it is still a wait");
    ut_check(mp_seat_rule_wait_look(&wait, 1000u + MP_SEAT_GIVE_UP_SUBSTEPS, MP_SEAT_LOOK_EMPTY),
             "and on it the stage is over");

    ut_section("a moving anchor stops that clock");
    mp_seat_rule_wait_start(&wait);
    (void)mp_seat_rule_wait_look(&wait, 0u, MP_SEAT_LOOK_EMPTY);
    (void)mp_seat_rule_wait_look(&wait, 50u, MP_SEAT_LOOK_EMPTY);
    ut_check(!mp_seat_rule_wait_look(&wait, 300u, MP_SEAT_LOOK_MOVING),
             "two hundred and fifty substeps on a lift are not counted against the anchor");
    ut_check(!mp_seat_rule_wait_look(&wait, 345u, MP_SEAT_LOOK_EMPTY),
             "ninety five empty ones in all are not yet the three seconds");
    ut_check(mp_seat_rule_wait_look(&wait, 346u, MP_SEAT_LOOK_EMPTY),
             "ninety six are");

    ut_section("the gates shut is no look at all");
    mp_seat_rule_wait_start(&wait);
    (void)mp_seat_rule_wait_look(&wait, 0u, MP_SEAT_LOOK_EMPTY);
    ut_check(!mp_seat_rule_wait_look(&wait, 5000u, MP_SEAT_LOOK_NONE),
             "a long cutscene with the player module parked counts nothing");
    ut_check(!mp_seat_rule_wait_look(&wait, 5001u, MP_SEAT_LOOK_EMPTY),
             "and the look after it counts one substep, not five thousand");

    ut_section("but no stage waits for ever");
    mp_seat_rule_wait_start(&wait);
    (void)mp_seat_rule_wait_look(&wait, 0u, MP_SEAT_LOOK_MOVING);
    ut_check(!mp_seat_rule_wait_look(&wait, MP_SEAT_WAIT_CAP_SUBSTEPS - 1u, MP_SEAT_LOOK_MOVING),
             "an anchor riding a mover is waited for almost twenty seconds");
    ut_check(mp_seat_rule_wait_look(&wait, MP_SEAT_WAIT_CAP_SUBSTEPS, MP_SEAT_LOOK_MOVING),
             "and then the stage is over anyway");

    ut_section("the substep counter may wrap");
    mp_seat_rule_wait_start(&wait);
    (void)mp_seat_rule_wait_look(&wait, 0xFFFFFFF0u, MP_SEAT_LOOK_EMPTY);
    ut_check(!mp_seat_rule_wait_look(&wait, 0x00000010u, MP_SEAT_LOOK_EMPTY),
             "thirty two substeps across the wrap are thirty two, not four billion");

    ut_section("the four player run: the same empty search, frame after frame, ends");
    mp_seat_rule_wait_start(&wait);
    /* A frame at 120 per second is a quarter of a substep; `now` counts frames here. */
    now = 0u;
    while (!over && now < 4000u) {
        over = mp_seat_rule_wait_look(&wait, now / 4u, MP_SEAT_LOOK_EMPTY);
        ++now;
    }
    ut_checkf(over && (now - 1u) / 4u == MP_SEAT_GIVE_UP_SUBSTEPS,
              "after %u frames, three seconds of simulation, the search falls back instead of "
              "trying the same eight candidates for good", (unsigned)now);
}

static void check_the_stages(void)
{
    ut_section("beside an anchor, then the fallback, then that point as authored");
    ut_check(mp_seat_rule_next_stage(MP_SEAT_STAGE_ANCHOR, true) == MP_SEAT_STAGE_FALLBACK,
             "a search beside a player falls back to an authored point");
    ut_check(mp_seat_rule_next_stage(MP_SEAT_STAGE_FALLBACK, true) == MP_SEAT_STAGE_AS_AUTHORED,
             "and when nothing around that point is free, the point itself");
    ut_check(mp_seat_rule_next_stage(MP_SEAT_STAGE_ANCHOR, false) == MP_SEAT_STAGE_AS_AUTHORED,
             "a search on a point the caller named has no fallback but that point");
    ut_check(mp_seat_rule_next_stage(MP_SEAT_STAGE_AS_AUTHORED, true) == MP_SEAT_STAGE_AS_AUTHORED,
             "and the last stage is the last");
}

static void check_the_co_operative_fallback(void)
{
    static const float POINTS[3][3] = {
        { 0.0f, 0.0f, 0.0f },     /* the level start */
        { 40.0f, 0.0f, 0.0f },
        { 200.0f, 0.0f, 0.0f },
    };
    const float anchor[3] = { 45.0f, 0.0f, 0.0f };
    bool        unlocked[3] = { true, true, true };
    bool        start     = true;

    ut_section("the free point nearest the anchor, not the one farthest from the living");
    ut_check(mp_seat_rule_nearest_free(POINTS, unlocked, 3u, anchor, &start) == 1u && !start,
             "the point five units from the anchor, where the deathmatch would take the one two "
             "hundred off, alone at the other end of the level");
    unlocked[1] = false;
    ut_check(mp_seat_rule_nearest_free(POINTS, unlocked, 3u, anchor, &start) == 0u && start,
             "that one locked, the level start is nearer than the far point");
    unlocked[0] = false;
    ut_check(mp_seat_rule_nearest_free(POINTS, unlocked, 3u, anchor, &start) == 2u && !start,
             "the level start locked too, the only free point");
    unlocked[2] = false;
    ut_check(mp_seat_rule_nearest_free(POINTS, unlocked, 3u, anchor, &start) == 0u && start,
             "nothing free is the level start, never nothing");
    ut_check(mp_seat_rule_nearest_free(POINTS, NULL, 3u, anchor, &start) == 1u && !start,
             "no lock list means every point is free");
    ut_check(mp_seat_rule_nearest_free(POINTS, unlocked, 0u, anchor, &start) == MP_SEAT_NO_POINT,
             "and an empty table has no answer");
}

/* What the seat search refused before these rules: nothing about the floor's face, nothing about
 * the place of the death, nothing about a seat that had just ended a life. */
static bool the_old_search_refuses(void)
{
    return false;
}

static void check_what_the_floor_is(void)
{
    ut_section("a floor that hurts and water are no seat");
    ut_check(mp_seat_rule_surface_verdict(MP_SEAT_SURFACE_HURTS) == MP_SEAT_HURTS,
             "the surface bit 0x400, three health every fifth of a second, refuses the seat");
    ut_check(mp_seat_rule_surface_verdict(MP_SEAT_SURFACE_WATER) == MP_SEAT_WATER,
             "and so does water, 0x40");
    ut_check(mp_seat_rule_surface_verdict(MP_SEAT_SURFACE_HURTS | MP_SEAT_SURFACE_WATER |
                                          0x0002u) == MP_SEAT_HURTS,
             "a face that is both is counted as the one that hurts");
    ut_check(mp_seat_rule_surface_verdict(0u) == MP_SEAT_FREE &&
                 mp_seat_rule_surface_verdict(0x0800u) == MP_SEAT_FREE,
             "no face, and a face with other bits only (0x800, a crawl space's), leave the seat "
             "free");
    ut_check(mp_seat_rule_surface_verdict(MP_SEAT_SURFACE_HURTS) != MP_SEAT_FREE &&
                 !the_old_search_refuses(),
             "which the search before this rule took as a seat like any other");
}

static void check_the_place_of_the_death(void)
{
    /* The field run: the seat three re-entries were given, the fan's victim's death beside it. */
    static const float SEAT[3]   = { 132.09f, 146.83f, 35.00f };
    static const float DEATH[3]  = { 144.91f, 140.07f, 36.16f };
    static const float NEAR[3]   = { 147.91f, 140.07f, 36.16f };
    float              ring[2];
    float              on_ring[3];
    size_t             step;
    unsigned           open = 0u;

    ut_section("a seat keeps out of the place of the death");
    ut_check(!mp_seat_rule_within(SEAT, DEATH, MP_SEAT_DEATH_CLEARANCE),
             "the field run's seat, 14.5 units from the death, is free");
    ut_check(mp_seat_rule_within(NEAR, DEATH, MP_SEAT_DEATH_CLEARANCE),
             "a candidate three units from the death is too near");
    for (step = 0; step < MP_SEAT_RING_STEPS; ++step) {
        mp_seat_rule_ring_offset(0u, step, MP_SEAT_RING_FAR, ring);
        on_ring[0] = DEATH[0] + ring[0];
        on_ring[1] = DEATH[1] + ring[1];
        on_ring[2] = DEATH[2];
        open += mp_seat_rule_within(on_ring, DEATH, MP_SEAT_DEATH_CLEARANCE) ? 0u : 1u;
    }
    ut_checkf(open == MP_SEAT_RING_STEPS, "every candidate of the second ring around the place "
              "of the death stays open (%u of %u)", open, (unsigned)MP_SEAT_RING_STEPS);
    ut_check(!mp_seat_rule_within(NULL, DEATH, 4.0f) && !mp_seat_rule_within(SEAT, DEATH, 0.0f),
             "no point, or no radius, is within nothing");
}

static void check_a_seat_that_ended_a_life(void)
{
    ut_section("a seat whose life ended at once is not handed out again");
    ut_check(mp_seat_rule_ended_a_life(false, 0.0f, 12.0f),
             "a body that died before it was seen standing locks its seat");
    ut_check(mp_seat_rule_ended_a_life(true, 10.0f, 10.0f + 1.0f / 32.0f),
             "a life that ended one substep after the landing locks it");
    ut_check(mp_seat_rule_ended_a_life(true, 10.0f, 12.5f),
             "and so does one that ended 2.5 seconds after, which is 160 substeps under the "
             "sixty frames cheat and would have been counted as five seconds in substeps");
    ut_check(!mp_seat_rule_ended_a_life(true, 10.0f, 10.0f + MP_SEAT_KILL_WINDOW_SECONDS),
             "a life of five seconds or more is an ordinary death, and the seat stays open");
    ut_check(!mp_seat_rule_ended_a_life(true, 30.0f, 2.0f),
             "a clock that ran backwards is a new level, and nothing is locked");
    ut_check(mp_seat_rule_ended_a_life(false, 0.0f, 0.0f) != the_old_search_refuses(),
             "which the search before this rule never locked at all");
}

/* In a field run two clients arrived in SWAMP at once and both were handed 112.04 27.72. The
 * later slot foresees the seats of the lower ones, and the list of them is the same on every
 * client because it comes from the roster, lowest first, whatever order the roster lists them
 * in. */
static void check_which_slots_come_first(void)
{
    static const uint8_t THREE[3]    = { 0u, 1u, 2u };
    static const uint8_t SHUFFLED[5] = { 3u, 0u, 2u, 1u, 2u };
    static const uint8_t NO_ONE[2]   = { 0u, 2u };
    uint8_t              lower[3]    = { 0u, 0u, 0u };
    size_t               count;

    ut_section("an arriving client is seated after the client slots below its own");
    count = mp_seat_rule_lower_slots(THREE, 3u, 0u, 2u, lower, 3u);
    ut_checkf(count == 1u && lower[0] == 1u,
              "slot 2 of a three player session comes after slot 1 alone (%u slot(s))",
              (unsigned)count);
    count = mp_seat_rule_lower_slots(THREE, 3u, 0u, 1u, lower, 3u);
    ut_checkf(count == 0u, "slot 1 comes after nobody: the host is no client (%u)",
              (unsigned)count);
    count = mp_seat_rule_lower_slots(SHUFFLED, 5u, 0u, 3u, lower, 3u);
    ut_checkf(count == 2u && lower[0] == 1u && lower[1] == 2u,
              "slot 3 after 1 and 2, lowest first and each once, however the roster lists them "
              "(%u slot(s))", (unsigned)count);
    count = mp_seat_rule_lower_slots(SHUFFLED, 5u, 0u, 3u, lower, 1u);
    ut_checkf(count == 1u && lower[0] == 1u, "and no more than the caller has room for (%u)",
              (unsigned)count);

    ut_section("a slot that is not in the roster is nobody to come after");
    count = mp_seat_rule_lower_slots(NO_ONE, 2u, 0u, 2u, lower, 3u);
    ut_checkf(count == 0u, "slot 1 left the session, so slot 2 takes its own ring (%u)",
              (unsigned)count);
    ut_check(mp_seat_rule_lower_slots(NULL, 3u, 0u, 2u, lower, 3u) == 0u,
             "and no roster at all is no order");
}

/* The slot's order is the loop the search always ran, written out here as it was. */
static void check_the_slot_order(void)
{
    uint8_t slot;
    bool    same = true;

    ut_section("the slot's order is the old start-and-step loop, for every slot");
    for (slot = 0u; slot < 16u; ++slot) {
        uint8_t order[MP_SEAT_RING_STEPS];
        size_t  start = ((size_t)slot * 2u) % (size_t)MP_SEAT_RING_STEPS;
        size_t  step;

        mp_seat_rule_slot_order(slot, order);
        for (step = 0u; step < (size_t)MP_SEAT_RING_STEPS; ++step) {
            same = same && order[step] == (uint8_t)((start + step) % MP_SEAT_RING_STEPS);
        }
    }
    ut_check(same, "sixteen slots, each the start direction and then one step at a time");
}

int main(void)
{
    check_the_ring_starts_per_slot();
    check_the_ring();
    check_the_slot_order();
    check_what_a_floor_reading_makes_of_a_candidate();
    check_whether_a_body_is_in_the_way();
    check_which_anchor_is_tried_first();
    check_the_clock_of_a_stage();
    check_the_stages();
    check_the_co_operative_fallback();
    check_what_the_floor_is();
    check_the_place_of_the_death();
    check_a_seat_that_ended_a_life();
    check_which_slots_come_first();
    return ut_summary("mp_seat_rule");
}
