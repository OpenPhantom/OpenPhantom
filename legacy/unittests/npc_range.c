/* npc_range.c: the scaled NPC activation radius against the removal radius.
 *
 * The placements below are the shapes of the shipped levels: activation radii 5 to 32, and the
 * removal radius as a multiple of it, from the 12 placements where it is not larger at all to the
 * 2.5 of the first record in ASSAULT. The first test keeps the old formula as the reference, so the
 * defect it removed stays written down.
 */
#include "unittest.h"

#include "npc_range.h"

#include <stdbool.h>
#include <stdio.h>

static const float ACTIVE[]  = { 5.0f, 8.0f, 10.0f, 12.0f, 15.0f, 16.0f, 20.0f, 32.0f };
static const float RATIO[]   = { 0.9f, 1.0f, 1.12f, 1.2f, 1.25f, 1.33f, 1.5f, 1.6f, 2.0f, 2.5f };
static const float SCALE[]   = { 1.0f, 1.25f, 1.33f, 1.5f, 1.75f, 2.0f };

#define COUNT(a) (sizeof (a) / sizeof (a)[0])

/* The formula before the cap. */
static float scaled_before(float active, float scale)
{
    return active * scale;
}

static void test_the_old_formula_woke_past_the_removal_radius(void)
{
    bool  capped = false;
    float now = npc_range_scaled(10.0f, 12.5f, 1.5f, &capped);

    /* A placement of the most common shape, removal 1.25 x activation, at NpcRangeScale 1.5. */
    ut_check(scaled_before(10.0f, 1.5f) >= 12.5f,
             "the old formula reaches the removal radius (the reference for the defect)");
    ut_check(now < 12.5f, "the scaled radius stays inside the removal radius");
    ut_check(capped, "and says that it was capped");
}

static void test_no_scaled_radius_reaches_a_removal_radius_it_did_not_already_reach(void)
{
    size_t a, r, s;
    int    bad = 0;

    for (a = 0; a < COUNT(ACTIVE); ++a) {
        for (r = 0; r < COUNT(RATIO); ++r) {
            for (s = 0; s < COUNT(SCALE); ++s) {
                float active = ACTIVE[a];
                float removal = active * RATIO[r];
                bool  capped = false;
                float got = npc_range_scaled(active, removal, SCALE[s], &capped);

                if (got < active || got > active * SCALE[s]) {
                    ++bad;
                    printf("  out of [active, scaled]: %.2f, %.2f x %.2f -> %.3f\n",
                           (double)active, (double)removal, (double)SCALE[s], (double)got);
                }
                if (active < removal * NPC_RANGE_REMOVAL_SHARE &&
                    !(got <= removal * NPC_RANGE_REMOVAL_SHARE)) {
                    ++bad;
                    printf("  past the share: %.2f, %.2f x %.2f -> %.3f\n", (double)active,
                           (double)removal, (double)SCALE[s], (double)got);
                }
                if (capped != (got < active * SCALE[s])) {
                    ++bad;
                    printf("  capped flag wrong: %.2f, %.2f x %.2f\n", (double)active,
                           (double)removal, (double)SCALE[s]);
                }
            }
        }
    }
    ut_check(bad == 0, "every scaled radius lies in [active, scaled] and inside the share");
}

static void test_the_engine_value_is_untouched_at_scale_one(void)
{
    size_t a, r;
    int    moved = 0;

    for (a = 0; a < COUNT(ACTIVE); ++a) {
        for (r = 0; r < COUNT(RATIO); ++r) {
            bool capped = true;

            if (npc_range_scaled(ACTIVE[a], ACTIVE[a] * RATIO[r], 1.0f, &capped) != ACTIVE[a] ||
                capped) {
                ++moved;
            }
        }
    }
    ut_check(moved == 0, "at 1.0 every radius is the authored one and nothing is capped");
}

static void test_a_placement_authored_without_a_gap_keeps_its_own_radius(void)
{
    bool capped = false;

    /* 12 shipped placements have a removal radius no larger than the activation radius. */
    ut_check(npc_range_scaled(10.0f, 9.0f, 2.0f, &capped) == 10.0f,
             "removal below activation: the authored radius, never less");
    ut_check(capped, "and the scale was taken back");
}

static void test_the_two_zeros(void)
{
    bool capped = true;

    ut_check(npc_range_scaled(0.0f, 30.0f, 2.0f, &capped) == 0.0f && !capped,
             "an activation radius of 0 is always active and stays 0");
    ut_check(npc_range_scaled(12.0f, 0.0f, 2.0f, &capped) == 24.0f && !capped,
             "a removal radius of 0 is never tested, so nothing caps the scale");
}

static void test_a_wide_gap_is_not_capped(void)
{
    bool capped = true;

    /* ASSAULT enemy000: activation 12, removal 30. */
    ut_check(npc_range_scaled(12.0f, 30.0f, 2.0f, &capped) == 24.0f && !capped,
             "24 lies inside 0.89 x 30, so the full scale stands");
}

int main(void)
{
    test_the_old_formula_woke_past_the_removal_radius();
    test_no_scaled_radius_reaches_a_removal_radius_it_did_not_already_reach();
    test_the_engine_value_is_untouched_at_scale_one();
    test_a_placement_authored_without_a_gap_keeps_its_own_radius();
    test_the_two_zeros();
    test_a_wide_gap_is_not_capped();

    return ut_summary("npc_range");
}
