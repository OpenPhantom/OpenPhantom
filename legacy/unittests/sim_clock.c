/* sim_clock.c: the one claim the simulation clock rebase stands on.
 *
 * The interpolation alpha is the fraction between two float32 clocks, and it depends only on their
 * difference. The rebase takes the same amount off both so that difference is preserved while the
 * resolution of each is restored. If that subtraction is not exact, the feature does not merely
 * fail to help, it injects a fresh error into the very quantity it exists to protect, and nothing
 * on screen would say so.
 *
 * What decays, in numbers. The simulation target is a float32 accumulating level time in seconds
 * and every frame takes target += frameDelta. One ulp of that accumulator is about 0.95
 * microseconds ten seconds into a level, 15.3 at two hundred seconds and 61 at six hundred, so the
 * alpha ladder precesses as the level runs, and every time it slides past a boundary one substep is
 * drawn across four frames instead of five. Those four advance a quarter of a step each instead of
 * a fifth, which is everything interpolated moving 25 per cent too fast for about 31 ms and then
 * returning.
 *
 * The pair is not guessed at. Both cells are read out of the operands of the alpha computation
 * itself, at 0x004757FF:
 *
 *     D9 05 94928600    fld  [0x00869294]     the simulation target
 *     D8 25 28878600    fsub [0x00868728]     minus the simulation time
 *
 * The three instructions after those add one substep, divide by it and store the quotient as the
 * interpolation alpha, so these are by construction the two clocks alpha is built from rather than
 * two cells that resemble them.
 *
 * Why the offset has to be handed back to the world. bapmap_setWorldClock at 0x0041F0C9 writes the
 * world clock at world+0x54 and derives an integer millisecond tick from the same argument, at
 * fld [ebp+0xC] and fmul [0x004A8214], the cell holding 1000.0f. That world clock is what
 * bapmap_tickMover compares for equality against every mover's own copy at mover+0x30. Moving it
 * costs less than it looks, and the bytes are what settle that: the early return at 0x00409191
 * tests equality only, the delta is clamped to zero at 0x004091A3, and mover+0x30 is restamped at
 * 0x004091BD on both branches, so a world clock that went backwards costs one zero length tick per
 * mover and not a freeze. What it would really cost is every absolute deadline stamped from
 * world+0x54, which the rebase would extend and which would then never expire. So the feature adds
 * the offset back inside that one function, and the world keeps the absolute time it always had.
 *
 * What is left is pure arithmetic, so it is proven here rather than played:
 *
 *   not a power of two    the subtraction rounds, the difference shifts, and alpha steps
 *   larger than the clock the pair goes negative, a state the substep driver never produces itself
 *   fired too early       a rebase every frame for no gain
 *   never fired           the resolution keeps decaying, which is the defect
 */
#include "unittest.h"

#include "sim_clock.h"

#include <math.h>
#include <stdbool.h>

static bool is_power_of_two(double v)
{
    int exponent;

    return v > 0.0 && frexp(v, &exponent) == 0.5;
}

static void below_the_threshold_nothing_happens(void)
{
    /* A level that has just opened has a small clock and a fine resolution. Rebasing there would be
     * work for nothing, and it must not move a clock the engine has only just zeroed: it writes
     * both cells itself when a level opens, at 0x00475621 and 0x0047562B, and an offset carried
     * across that boundary would make the new level's world clock start at the previous level's
     * duration.
     *
     * The threshold is two seconds, which is low enough for the ulp above it to be irrelevant and
     * high enough that the rebase happens about once every two seconds rather than every frame. */
    ut_check(sim_clock_rebase_step(0.0f) == 0.0, "a clock at zero is left alone");
    ut_check(sim_clock_rebase_step(0.5f) == 0.0, "half a second is left alone");
    ut_check(sim_clock_rebase_step(2.0f) == 0.0, "exactly at the threshold is left alone");
}

static void the_step_is_always_a_power_of_two_below_the_clock(void)
{
    float live;

    /* Both properties matter and for different reasons. A power of two is a whole multiple of the
     * ulp of anything at least as large, which is what makes the subtraction exact. Not exceeding
     * the clock is what keeps it positive. */
    for (live = 2.5f; live < 4000.0f; live *= 1.37f) {
        double step = sim_clock_rebase_step(live);

        ut_checkf(step > 0.0, "a clock of %.1f s should rebase", (double)live);
        ut_checkf(is_power_of_two(step), "the step for %.1f s must be a power of two, got %.6f",
                  (double)live, step);
        ut_checkf(step <= (double)live, "the step %.4f must not exceed the clock %.1f",
                  step, (double)live);
    }
}

static void the_difference_survives_bit_for_bit(void)
{
    float live;

    /* This is the check the whole feature rests on. The two clocks are within one simulation step
     * of each other and alpha is built from their difference, so that difference has to come out of
     * the subtraction unchanged, exactly, at every clock value a long level reaches. The comparison
     * below is an equality on purpose: near enough is exactly what is not being claimed. */
    for (live = 2.5f; live < 20000.0f; live *= 1.19f) {
        double step   = sim_clock_rebase_step(live);
        float  target = live + 0.03125f * 0.4f;          /* somewhere inside a substep */
        float  before = target - live;
        float  after;

        if (step == 0.0) {
            continue;
        }
        after = (float)((double)target - step) - (float)((double)live - step);
        ut_checkf(after == before,
                  "the difference at %.1f s changed: %.9f became %.9f", (double)live,
                  (double)before, (double)after);
    }
}

static void rebasing_actually_restores_the_resolution(void)
{
    float  live = 900.0f;                         /* fifteen minutes into a level */
    double step = sim_clock_rebase_step(live);
    float  after;
    float  coarse_before;
    float  coarse_after;

    /* The point of the exercise. The engine adds a frame delta to this clock every frame, and the
     * defect is that the addition rounds by more and more as the clock grows. Adding one frame at
     * 160 fps must move the rebased clock by more than it moves the original. */
    ut_check(step > 0.0, "a clock at fifteen minutes rebases");
    after = (float)((double)live - step);

    coarse_before = (float)((double)live + 0.00625) - live;
    coarse_after  = (float)((double)after + 0.00625) - after;

    ut_checkf(coarse_after > coarse_before,
              "a frame must register better after rebasing: %.9f before, %.9f after",
              (double)coarse_before, (double)coarse_after);
    ut_checkf(after > 0.0f, "and the clock must stay positive, got %.3f", (double)after);
}

static void a_clock_that_keeps_running_stays_bounded(void)
{
    /* Repeated rebasing must hold the live value inside one binade rather than letting it walk
     * away, otherwise the resolution decays again between rebases and the defect returns slowly.
     * Twenty minutes of frames at 160 fps is longer than any level in the game runs.
     *
     * The offset is accumulated in double here because that is how the module carries it. A float32
     * running total would reintroduce the very rounding this removes, one level deep instead of
     * one frame deep. */
    double offset = 0.0;
    float  live   = 2.5f;
    int    frame;

    for (frame = 0; frame < 200000; ++frame) {
        double step;

        live = (float)((double)live + 0.00625);   /* one frame at 160 fps */
        step = sim_clock_rebase_step(live);
        if (step > 0.0) {
            live    = (float)((double)live - step);
            offset += step;
        }
        ut_checkf(live >= 0.0f && live <= 8.0f,
                  "the live clock left its band at frame %d: %.4f", frame, (double)live);
        if (live < 0.0f || live > 8.0f) {
            break;
        }
    }
    ut_check(offset > 1000.0, "and the offset carried the elapsed time away in double");
}

int main(void)
{
    below_the_threshold_nothing_happens();
    the_step_is_always_a_power_of_two_below_the_clock();
    the_difference_survives_bit_for_bit();
    rebasing_actually_restores_the_resolution();
    a_clock_that_keeps_running_stays_bounded();

    return ut_summary("sim_clock");
}
