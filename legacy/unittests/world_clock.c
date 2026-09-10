/* world_clock.c: putting movers back on the simulation's lattice instead of the frame's.
 *
 * The substep loop clamps the world clock to the frame's target on the last substep of every
 * frame, so a mover's sample pair spans the gap between frames while the alpha that blends it is
 * a phase within a simulation step. Three attempts to compensate for that in the draw failed. This
 * removes the clamp instead, and the sequence it produces is the whole claim: one substep per
 * call, exactly, whatever the frame rate.
 *
 * The lattice walk at the end is the check worth having. Everything else here is a boundary.
 */
#include "unittest.h"

#include "world_clock.h"

#include <math.h>

#define STEP (double)WORLD_CLOCK_SUBSTEP_SECONDS

/* The substep loop, driven at `fps`, with one 50 ms frame injected at `hitch_frame` when that is
 * not negative. Answers whether every advance the clock made was exactly one substep. */
static bool walk_is_even(double fps, int hitch_frame, double *out_worst, int *out_advances)
{
    double sim_time = 0.0;
    double target   = 0.0;
    float  clock    = 0.0f;
    bool   have     = false;
    int    n;

    *out_worst    = 0.0;
    *out_advances = 0;

    for (n = 0; n < 600; ++n) {
        target += (n == hitch_frame) ? 0.050 : (1.0 / fps);
        while (sim_time < target) {
            double requested = sim_time + STEP;
            float  granted;

            if (requested > target) {
                requested = target;               /* the loop's own clamp */
            }
            granted = world_clock_substep_time((float)requested, clock, have, (float)STEP);

            if (have) {
                double error = fabs(((double)granted - (double)clock) - STEP);

                if (error > *out_worst) {
                    *out_worst = error;
                }
                ++(*out_advances);
            }
            clock     = granted;
            have      = true;
            sim_time += STEP;
        }
    }
    return *out_worst < 1.0e-6;
}

int main(void)
{
    float  answer;
    double target;
    double sim_time;
    float  clock;
    bool   have_clock;
    double worst_advance_error = 0.0;
    int    advances = 0;
    int    n;

    ut_section("nothing to un-clamp against");

    answer = world_clock_substep_time(0.5f, 0.0f, false, (float)STEP);
    ut_near((double)answer, 0.5, 1e-7,
            "the first call of a level is passed through, because there is no predecessor");

    ut_section("the requested value is not consulted once there is a previous one");

    /* This is a REGRESSION, and it is the whole reason the two walks at the end of this file
     * exist. An earlier version passed the requested value through whenever it was already a step
     * or more ahead, on the grounds that such a call was on the lattice already. It is on the
     * SIMULATION's lattice, which sits a fraction of a step from this one, so the first frame to
     * run two substeps snapped across and advanced every mover by 1.4 steps on that frame. The
     * answer is a step past the previous value whatever was asked for. */
    answer = world_clock_substep_time(1.0f + (float)STEP, 1.0f, true, (float)STEP);
    ut_near((double)answer, 1.0 + STEP, 1e-7,
            "a request one step ahead gets a step past the previous value, which is the same");

    answer = world_clock_substep_time(1.0f + 2.0f * (float)STEP, 1.0f, true, (float)STEP);
    ut_near((double)answer, 1.0 + STEP, 1e-7,
            "a request two steps ahead still gets one step, so the lattice cannot be snapped");

    ut_section("a value the loop clamped");

    /* The case this exists for. The frame's target fell short of the substep's end, so the loop
     * asked for the target and the mover would have integrated by that short amount. */
    answer = world_clock_substep_time(1.008f, 1.0f, true, (float)STEP);
    ut_near((double)answer, 1.0 + STEP, 1e-7,
            "a clamped value is carried out to a whole step past the previous one");

    answer = world_clock_substep_time(1.0f, 1.0f, true, (float)STEP);
    ut_near((double)answer, 1.0 + STEP, 1e-7,
            "and so is a repeat of the previous value, which is a frame that advanced nothing");

    ut_section("a level opening");

    /* The one event that must pass straight through. Un-clamping against the previous level's
     * last value would start the new level a whole level's duration in the future, and every
     * mover would swallow that gap on its first tick: measured once at 59 movers each stepping
     * 2.031 s at one go, which left the characters standing in the air. */
    answer = world_clock_substep_time(0.0f, 120.0f, true, (float)STEP);
    ut_near((double)answer, 0.0, 1e-7, "a clock that has gone backwards is passed through");

    ut_section("a step that cannot be trusted");

    answer = world_clock_substep_time(1.008f, 1.0f, true, 0.0f);
    ut_near((double)answer, 1.008, 1e-6, "a step of zero changes nothing");
    answer = world_clock_substep_time(1.008f, 1.0f, true, -0.03f);
    ut_near((double)answer, 1.008, 1e-6, "and neither does a negative one");

    ut_check(!isfinite(world_clock_substep_time((float)NAN, 1.0f, true, (float)STEP)),
             "a requested time that is not a number is passed on, not replaced with an invention");
    answer = world_clock_substep_time(1.008f, (float)NAN, true, (float)STEP);
    ut_near((double)answer, 1.008, 1e-6, "a previous value that is not a number changes nothing");
    answer = world_clock_substep_time(1.008f, 1.0f, true, (float)INFINITY);
    ut_near((double)answer, 1.008, 1e-6, "and neither does an endless step");

    ut_section("the lattice, walked as the substep loop walks it");

    /* The accumulator, at 60 frames a second against a 32 Hz simulation. The loop runs while the
     * simulation time is behind the frame's target, sets the clock to the substep's end clamped to
     * that target, and advances the simulation by a whole step. What has to come out of this is a
     * clock that advances by exactly one step on every call it makes, because that is what puts a
     * mover's sample pair on the same lattice as the alpha which blends it. */
    have_clock = false;
    clock      = 0.0f;
    sim_time   = 0.0;
    target     = 0.0;

    for (n = 0; n < 600; ++n) {
        target += 1.0 / 60.0;
        while (sim_time < target) {
            double requested = sim_time + STEP;
            float  granted;

            if (requested > target) {
                requested = target;               /* the loop's own clamp */
            }
            granted = world_clock_substep_time((float)requested, clock, have_clock, (float)STEP);

            if (have_clock) {
                double advance = (double)granted - (double)clock;
                double error   = fabs(advance - STEP);

                if (error > worst_advance_error) {
                    worst_advance_error = error;
                }
                ++advances;
            }
            clock      = granted;
            have_clock = true;
            sim_time  += STEP;
        }
    }

    /* 600 frames at 60 a second is ten seconds, and ten seconds of a 32 Hz simulation is 320
     * substeps, so that is how many advances there are to make. An earlier version of this check
     * asked for more than 550 on the assumption that a substep runs every frame; it does not, and
     * that assumption is the same one that made three attempts at the draw-side weight fail. The
     * count is worth pinning down for that reason alone. */
    ut_checkf(advances >= 318 && advances <= 322,
              "the walk made %d advances over 600 frames, which is ten seconds at 32 Hz",
              advances);
    ut_checkf(worst_advance_error < 1.0e-6,
              "every one of them moved the clock by exactly one substep, worst error %.2e s",
              worst_advance_error);

    ut_section("the two cases a steady 60 fps walk cannot reach");

    /* A steady 60 fps never runs two substeps in one frame, so the walk above cannot see the
     * fault the regression at the top describes. These two can: 25 frames a second runs two on
     * some frames from the start, and one long frame at 60 does it once. Both are checked for the
     * same property, that no call ever advances the clock by anything other than one step. */
    ut_check(walk_is_even(25.0, -1, &worst_advance_error, &advances),
             "at 25 fps, where a frame runs two substeps, every advance is one step");
    ut_checkf(worst_advance_error < 1.0e-6, "worst error at 25 fps %.2e s", worst_advance_error);

    ut_check(walk_is_even(60.0, 10, &worst_advance_error, &advances),
             "and with one 50 ms frame injected at 60 fps, the case that moved the lattice");
    ut_checkf(worst_advance_error < 1.0e-6,
              "worst error across the injected frame %.2e s", worst_advance_error);

    return ut_summary("world clock");
}
