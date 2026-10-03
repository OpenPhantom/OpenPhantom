/* The one ring loop of the seat search, held still while it was rebuilt.
 *
 * Every seat this feature hands out goes through one loop: the near ring first, then the far one,
 * and on each ring the eight directions in an order. Every caller asks it from the seated
 * player's slot, and that order was always the slot's start direction and then one step at a
 * time, wrapping. What is pinned here, through the real search on a little world the test
 * plays, is that loop: for each of the four slots of a session and every set of directions the test
 * makes unwalkable on both rings at once, the seat found is the first free candidate of that
 * formula, and every candidate refused on the way is counted as not walkable. The formula is
 * written out in this file rather than called, so that the loop is measured against the old logic
 * and not against itself.
 *
 * The second half is what the loop gained: the order handed in by the caller, and the reading of
 * the floor under the anchor handed back, by which the host's scene tells a player on a mover
 * from one in the air.
 */
#include "unittest.h"

#include "mp_seat.h"
#include "mp_seat_internal.h"
#include "mp_seat_little_world.h"
#include "mp_seat_rule.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* One search with nothing to keep away from but the bodies handed in. */
static mp_seat_outcome_t probe(const float target[3], bool beside, uint8_t slot,
                               const mp_seat_body_t *bodies, size_t body_count,
                               mp_seat_counts_t *counts, float seat[3])
{
    return mp_seat_probe_avoiding(target, beside, slot, bodies, body_count, NULL, counts,
                                  seat);
}

static const float ANCHOR[3] = { 40.0f, 60.0f, 5.0f };
static const float RINGS[2]  = { MP_SEAT_RING_NEAR, MP_SEAT_RING_FAR };

/* A level on a flat floor at the anchor's height, every direction in `near` unwalkable on the
 * near ring and every one in `far` on the far ring. */
static void open_with_rings_blocked(unsigned near, unsigned far)
{
    size_t ring;
    size_t direction;

    little_world_open(ANCHOR[2]);
    mp_seat_world_ended();
    for (ring = 0u; ring < 2u; ++ring) {
        unsigned blocked = ring == 0u ? near : far;

        for (direction = 0u; direction < MP_SEAT_RING_STEPS; ++direction) {
            float point[3];

            if ((blocked & (1u << direction)) == 0u) {
                continue;
            }
            ring_point(ANCHOR, direction, RINGS[ring], point);
            unwalkable_at(point);
        }
    }
}

/* The same directions blocked on both rings. */
static void open_with_blocked(unsigned blocked)
{
    open_with_rings_blocked(blocked, blocked);
}

/* The old loop, written out: the first free candidate from the slot's start direction, the near
 * ring before the far one, and how many were refused before it. False when none is free. */
static bool the_old_loop_of(uint8_t slot, unsigned near, unsigned far, float seat[3],
                            unsigned *refused)
{
    size_t start = ((size_t)slot * 2u) % MP_SEAT_RING_STEPS;
    size_t ring;
    size_t step;

    *refused = 0u;
    for (ring = 0u; ring < 2u; ++ring) {
        unsigned blocked = ring == 0u ? near : far;

        for (step = 0u; step < MP_SEAT_RING_STEPS; ++step) {
            size_t direction = (start + step) % MP_SEAT_RING_STEPS;

            if ((blocked & (1u << direction)) != 0u) {
                ++*refused;
                continue;
            }
            ring_point(ANCHOR, direction, RINGS[ring], seat);
            return true;
        }
    }
    return false;
}

static bool the_old_loop(uint8_t slot, unsigned blocked, float seat[3], unsigned *refused)
{
    return the_old_loop_of(slot, blocked, blocked, seat, refused);
}

/* The near ring all blocked and the far ring by every set: the seat found on the far ring is the
 * one the old loop names, which the same sets on both rings never reach while any is free. */
static void check_a_find_on_the_far_ring(void)
{
    unsigned wrong = 0u;
    unsigned far_found = 0u;
    uint8_t  slot;

    ut_section("the near ring all blocked, the far ring by all 256 sets");
    mp_seat_install();
    for (slot = 0u; slot < 4u; ++slot) {
        unsigned far;

        for (far = 0u; far < 256u; ++far) {
            mp_seat_counts_t  counts;
            float             seat[3]     = { 0.0f, 0.0f, 0.0f };
            float             expected[3] = { 0.0f, 0.0f, 0.0f };
            unsigned          refused     = 0u;
            bool              free_one;
            mp_seat_outcome_t outcome;

            memset(&counts, 0, sizeof counts);
            open_with_rings_blocked(0xFFu, far);
            free_one = the_old_loop_of(slot, 0xFFu, far, expected, &refused);
            outcome  = probe(ANCHOR, true, slot, NULL, 0u, &counts, seat);
            if (outcome != (free_one ? MP_SEAT_FOUND : MP_SEAT_NONE_FREE) ||
                (free_one && apart(seat, expected) > 0.0001f) ||
                counts.refused[MP_SEAT_NOT_WALKABLE] != refused) {
                ++wrong;
            }
            far_found += free_one ? 1u : 0u;
        }
    }
    ut_checkf(wrong == 0u && far_found == 4u * 255u,
              "1024 case(s), %u found on the far ring, all as the old loop said (wrong %u)",
              far_found, wrong);
}

static void check_every_slot_and_every_blocked_set(void)
{
    unsigned wrong_seat    = 0u;
    unsigned wrong_count   = 0u;
    unsigned wrong_outcome = 0u;
    unsigned cases         = 0u;
    uint8_t  slot;

    ut_section("the slot's order, for four slots and all 256 sets of blocked directions");
    mp_seat_install();
    for (slot = 0u; slot < 4u; ++slot) {
        unsigned blocked;

        for (blocked = 0u; blocked < 256u; ++blocked) {
            mp_seat_counts_t  counts;
            float             seat[3]     = { 0.0f, 0.0f, 0.0f };
            float             expected[3] = { 0.0f, 0.0f, 0.0f };
            unsigned          refused     = 0u;
            bool              free_one;
            mp_seat_outcome_t outcome;

            memset(&counts, 0, sizeof counts);
            open_with_blocked(blocked);
            free_one = the_old_loop(slot, blocked, expected, &refused);
            outcome  = probe(ANCHOR, true, slot, NULL, 0u, &counts, seat);
            ++cases;
            if (outcome != (free_one ? MP_SEAT_FOUND : MP_SEAT_NONE_FREE)) {
                ++wrong_outcome;
                continue;
            }
            if (free_one && apart(seat, expected) > 0.0001f) {
                ++wrong_seat;
            }
            if (counts.refused[MP_SEAT_NOT_WALKABLE] != refused) {
                ++wrong_count;
            }
        }
    }
    ut_checkf(wrong_outcome == 0u, "%u case(s): found or none free as the old loop said, wrong %u "
              "time(s)", cases, wrong_outcome);
    ut_checkf(wrong_seat == 0u, "the seat the old loop names, to a ten thousandth of a unit, wrong "
              "%u time(s)", wrong_seat);
    ut_checkf(wrong_count == 0u, "every candidate before it counted as not walkable, wrong %u "
              "time(s)", wrong_count);
}

/* The point itself first, when it is no body to step away from: it is the seat while it is free,
 * and with a crawl space over it the rings answer exactly as beside a body. */
static void check_the_point_itself(void)
{
    uint8_t slot;
    bool    on_the_point = true;
    bool    rings_after  = true;

    ut_section("a point is tried itself first, then its rings in the slot's order");
    mp_seat_install();
    for (slot = 0u; slot < 4u; ++slot) {
        mp_seat_counts_t counts;
        float            seat[3]     = { 0.0f, 0.0f, 0.0f };
        float            expected[3] = { 0.0f, 0.0f, 0.0f };
        unsigned         refused     = 0u;

        memset(&counts, 0, sizeof counts);
        open_with_blocked(0x03u);
        on_the_point = on_the_point &&
                       probe(ANCHOR, false, slot, NULL, 0u, &counts, seat) ==
                           MP_SEAT_FOUND &&
                       apart(seat, ANCHOR) < 0.0001f;

        memset(&counts, 0, sizeof counts);
        open_with_blocked(0x03u);
        memcpy(wld.crawl[wld.crawl_count++], ANCHOR, sizeof ANCHOR);
        (void)the_old_loop(slot, 0x03u, expected, &refused);
        rings_after = rings_after &&
                      probe(ANCHOR, false, slot, NULL, 0u, &counts, seat) ==
                          MP_SEAT_FOUND &&
                      apart(seat, expected) < 0.0001f &&
                      counts.refused[MP_SEAT_LOW_CEILING] == 1u &&
                      counts.refused[MP_SEAT_NOT_WALKABLE] == refused;
    }
    ut_check(on_the_point, "a free point is its own seat, for every slot");
    ut_check(rings_after, "a point under a crawl space is refused once, and its rings answer as "
                          "the old loop does");
}

/* A body on a candidate takes it, and the loop goes on to the next of the slot's order. */
static void check_a_body_on_the_way(void)
{
    uint8_t slot;
    bool    next_one = true;

    ut_section("a body on the slot's first candidate sends the loop to the next");
    mp_seat_install();
    for (slot = 0u; slot < 4u; ++slot) {
        mp_seat_counts_t counts;
        mp_seat_body_t   body;
        float            seat[3]     = { 0.0f, 0.0f, 0.0f };
        float            expected[3] = { 0.0f, 0.0f, 0.0f };
        size_t           start       = ((size_t)slot * 2u) % MP_SEAT_RING_STEPS;

        memset(&counts, 0, sizeof counts);
        memset(&body, 0, sizeof body);
        open_with_blocked(0u);
        body.known  = true;
        body.stands = true;
        ring_point(ANCHOR, start, MP_SEAT_RING_NEAR, body.position);
        ring_point(ANCHOR, (start + 1u) % MP_SEAT_RING_STEPS, MP_SEAT_RING_NEAR, expected);
        next_one = next_one &&
                   probe(ANCHOR, true, slot, &body, 1u, &counts, seat) == MP_SEAT_FOUND &&
                   apart(seat, expected) < 0.0001f && counts.refused[MP_SEAT_TAKEN] == 1u;
    }
    ut_check(next_one, "taken once, then the next direction, for every slot");
}

/* ==============================================================================================
 * The order handed in, and the floor under the anchor handed back.
 * ============================================================================================ */

static void check_the_slot_order_through_the_new_door(void)
{
    uint8_t slot;
    bool    same = true;

    ut_section("the slot's order handed in through the ordered search answers as the slot's own "
               "search");
    mp_seat_install();
    for (slot = 0u; slot < 4u; ++slot) {
        unsigned blocked;

        for (blocked = 0u; blocked < 256u; blocked += 7u) {
            mp_seat_counts_t  one;
            mp_seat_counts_t  two;
            mp_seat_search_t  search;
            float             a[3] = { 0.0f, 0.0f, 0.0f };
            float             b[3] = { 0.0f, 0.0f, 0.0f };
            mp_seat_outcome_t first;
            mp_seat_outcome_t second;

            memset(&one, 0, sizeof one);
            memset(&two, 0, sizeof two);
            memset(&search, 0, sizeof search);
            mp_seat_rule_slot_order(slot, search.order);
            open_with_blocked(blocked);
            first = probe(ANCHOR, true, slot, NULL, 0u, &one, a);
            open_with_blocked(blocked);
            second = mp_seat_probe_ordered(ANCHOR, true, &search, NULL, 0u, &two, b, NULL);
            same = same && first == second && apart(a, b) < 0.0001f &&
                   memcmp(one.refused, two.refused, sizeof one.refused) == 0;
        }
    }
    ut_check(same, "the same outcome, seat and refusals for every slot and a spread of sets");
}

static void check_the_floor_under_the_anchor(void)
{
    mp_seat_counts_t counts;
    mp_seat_search_t search;
    mp_seat_floor_t  floor = MP_SEAT_FLOOR_OK;
    float            seat[3] = { 0.0f, 0.0f, 0.0f };
    float            high[3];

    ut_section("the floor under the anchor comes back: a fall, a mover, water or a drop");
    mp_seat_install();
    memset(&search, 0, sizeof search);
    mp_seat_rule_slot_order(0u, search.order);

    memset(&counts, 0, sizeof counts);
    open_with_blocked(0u);
    ut_check(mp_seat_probe_ordered(ANCHOR, false, &search, NULL, 0u, &counts, seat, &floor) ==
                     MP_SEAT_FOUND &&
                 floor == MP_SEAT_FLOOR_OK,
             "an anchor on its floor: found, the floor said to be there");

    memset(&counts, 0, sizeof counts);
    open_with_blocked(0u);
    wld.hole_on = true;
    memcpy(wld.hole, ANCHOR, sizeof ANCHOR);
    ut_check(mp_seat_probe_ordered(ANCHOR, false, &search, NULL, 0u, &counts, seat, &floor) ==
                     MP_SEAT_ANCHOR_MOVING &&
                 floor == MP_SEAT_FLOOR_NONE,
             "nothing under it: moving, and no floor");

    memset(&counts, 0, sizeof counts);
    open_with_blocked(0u);
    wld.mover_on = true;
    memcpy(wld.mover, ANCHOR, sizeof ANCHOR);
    ut_check(mp_seat_probe_ordered(ANCHOR, false, &search, NULL, 0u, &counts, seat, &floor) ==
                     MP_SEAT_ANCHOR_MOVING &&
                 floor == MP_SEAT_FLOOR_MOVER,
             "a mover under it: moving, and a mover, which a scene does not wait for");

    memset(&counts, 0, sizeof counts);
    open_with_blocked(0u);
    memcpy(high, ANCHOR, sizeof high);
    high[2] += 3.0f * MP_SEAT_FLOOR_BAND;
    ut_check(mp_seat_probe_ordered(high, false, &search, NULL, 0u, &counts, seat, &floor) ==
                     MP_SEAT_ANCHOR_MOVING &&
                 floor == MP_SEAT_FLOOR_FAR,
             "the floor far below, as under a swimmer: moving, and far");
}

/* The host's own body is not in the list the place is searched with: a host that happens to stand
 * on the place would otherwise take it from himself and be put two units beside it. */
static void check_the_host_on_the_place(void)
{
    mp_seat_counts_t counts;
    mp_seat_search_t search;
    mp_seat_body_t   host;
    float            seat[3]     = { 0.0f, 0.0f, 0.0f };
    float            expected[3] = { 0.0f, 0.0f, 0.0f };

    ut_section("the place is searched without the host's own body");
    mp_seat_install();
    memset(&search, 0, sizeof search);
    memset(&host, 0, sizeof host);
    mp_seat_rule_slot_order(0u, search.order);
    host.known  = true;
    host.stands = true;
    memcpy(host.position, ANCHOR, sizeof ANCHOR);

    memset(&counts, 0, sizeof counts);
    open_with_blocked(0u);
    ring_point(ANCHOR, 0u, MP_SEAT_RING_NEAR, expected);
    ut_check(mp_seat_probe_ordered(ANCHOR, false, &search, &host, 1u, &counts, seat, NULL) ==
                     MP_SEAT_FOUND &&
                 apart(seat, expected) < 0.0001f && counts.refused[MP_SEAT_TAKEN] == 1u,
             "with the host in the list the place is taken, and he would stand beside it");

    memset(&counts, 0, sizeof counts);
    open_with_blocked(0u);
    ut_check(mp_seat_probe_ordered(ANCHOR, false, &search, NULL, 0u, &counts, seat, NULL) ==
                     MP_SEAT_FOUND &&
                 apart(seat, ANCHOR) < 0.0001f,
             "without him it is the place itself");
}

int main(void)
{
    check_every_slot_and_every_blocked_set();
    check_a_find_on_the_far_ring();
    check_the_point_itself();
    check_a_body_on_the_way();
    check_the_slot_order_through_the_new_door();
    check_the_floor_under_the_anchor();
    check_the_host_on_the_place();
    return ut_summary("the seat search's ring loop");
}
