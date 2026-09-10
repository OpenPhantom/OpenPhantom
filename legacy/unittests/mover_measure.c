/* mover_measure.c: the instrument that has to settle an argument two derivations got wrong.
 *
 * Two ways of deciding when a mover should be drawn were worked out from the engine's clocks,
 * built, and played, and both were worse than the arithmetic they replaced. So the quantities
 * underneath them are going to be read out of a running game, and an instrument that is about to
 * be trusted for that had better not have a way of quietly reporting nothing.
 *
 * The two failures worth checking are both silent. A range that has seen nothing has to say so
 * rather than print whatever sentinel it started from, because a reader cannot tell an unfilled
 * range from a real one that happened to sit there. And one value that is not a number, arriving
 * anywhere in a six hundred frame window, must not take the whole window with it: every
 * comparison against a NaN is false, so a range that let one in would report it as both bounds
 * and the session would have to be played again.
 */
#include "unittest.h"

#include "mover_measure.h"

#include <math.h>
#include <string.h>

static void set3(float *v, float x, float y, float z)
{
    v[0] = x;
    v[1] = y;
    v[2] = z;
}

int main(void)
{
    mover_measurement_t measurement;
    mover_range_t       range;
    float               low;
    float               high;

    ut_section("a window that has seen nothing");

    mover_measure_reset(&measurement);
    mover_measure_bounds(&measurement.interval, &low, &high);
    ut_check(low == 0.0f && high == 0.0f,
             "an unfilled range answers zero to zero rather than its own sentinel");
    ut_check(measurement.ticks == 0 && measurement.draws == 0,
             "and the counters start at nothing");

    ut_section("ordinary observations");

    mover_measure_reset(&measurement);
    range = measurement.interval;
    mover_measure_note(&range, 0.0333f);
    mover_measure_bounds(&range, &low, &high);
    ut_near(low, 0.0333, 0.00001, "one observation is both bounds, low");
    ut_near(high, 0.0333, 0.00001, "and high");

    mover_measure_note(&range, 0.0167f);
    mover_measure_bounds(&range, &low, &high);
    ut_near(low, 0.0167, 0.00001, "a smaller one moves the low bound");
    ut_near(high, 0.0333, 0.00001, "and leaves the high one alone");

    mover_measure_note(&range, 0.0500f);
    mover_measure_bounds(&range, &low, &high);
    ut_near(low, 0.0167, 0.00001, "a larger one leaves the low bound");
    ut_near(high, 0.0500, 0.00001, "and moves the high one");

    ut_section("a negative observation is data, not an error");

    /* The substep alpha is not guaranteed to stay inside its interval right after a rate change,
     * so the elapsed time computed from it can come out slightly negative. That is a measurement
     * worth seeing rather than one worth hiding, and it is exactly the sort of thing this window
     * is being run to find out. */
    mover_measure_reset(&measurement);
    range = measurement.elapsed;
    mover_measure_note(&range, -0.002f);
    mover_measure_bounds(&range, &low, &high);
    ut_near(low, -0.002, 0.00001, "a negative elapsed time is recorded as it stands");

    ut_section("values that would swallow the window");

    mover_measure_reset(&measurement);
    range = measurement.phase;
    mover_measure_note(&range, 0.25f);
    mover_measure_note(&range, (float)NAN);
    mover_measure_bounds(&range, &low, &high);
    ut_near(low, 0.25, 0.00001,
            "a value that is not a number is ignored and the low bound survives it");
    ut_near(high, 0.25, 0.00001, "and so does the high bound");

    mover_measure_note(&range, (float)INFINITY);
    mover_measure_bounds(&range, &low, &high);
    ut_near(high, 0.25, 0.00001, "an endless value is ignored as well");

    /* The order matters as much as the value: arriving first, a NaN would have set both bounds
     * and marked the range as filled, and every later comparison against it would have been
     * false, so the range would have reported a NaN for the whole window. */
    mover_measure_reset(&measurement);
    range = measurement.phase;
    mover_measure_note(&range, (float)NAN);
    mover_measure_bounds(&range, &low, &high);
    ut_check(low == 0.0f && high == 0.0f,
             "a range whose only observation was not a number still reads as empty");

    mover_measure_note(&range, 0.5f);
    mover_measure_bounds(&range, &low, &high);
    ut_near(low, 0.5, 0.00001, "and it accepts the next real observation normally");

    ut_section("a window that has been reported");

    mover_measure_reset(&measurement);
    mover_measure_note(&measurement.alpha, 0.75f);
    measurement.ticks = 320u;
    measurement.draws = 15000u;
    mover_measure_reset(&measurement);
    mover_measure_bounds(&measurement.alpha, &low, &high);
    ut_check(low == 0.0f && high == 0.0f && measurement.ticks == 0 && measurement.draws == 0,
             "a reset window forgets its ranges and its counters together");

    ut_section("nothing to report about");

    mover_measure_note(NULL, 1.0f);
    mover_measure_reset(NULL);
    mover_measure_report(NULL, 600u);
    mover_measure_bounds(NULL, &low, &high);
    ut_check(low == 0.0f && high == 0.0f,
             "every entry point survives being handed nothing, which a diagnostic must");

    ut_section("the drawn evenness test, driven with motion whose answer is known");

    /* The instrument that judges every mover change from here on, so its own failures are worth
     * pinning down first. The version before this one reported "0 of 0 judged frames" from a real
     * play session and the fault was not in the game: the draw calls both the matrix multiply and
     * the rigid invert for each subnode, this DLL redirects both, and the second call of a frame
     * was being treated as a break in the three frame chain. Nothing was ever judged. */
    {
        mover_drawn_t drawn;
        float         at[3];
        uint32_t      frame;
        int           i;

        /* Perfectly even motion must score nothing, whatever the speed. */
        mover_measure_reset(&measurement);
        memset(&drawn, 0, sizeof drawn);
        set3(at, 0.0f, 0.0f, 0.0f);
        for (frame = 1u; frame <= 40u; ++frame) {
            at[0] = (float)frame * 0.05f;
            mover_measure_drawn(&measurement, &drawn, at, frame);
        }
        ut_checkf(measurement.steps_judged > 30u,
                  "even motion is judged on %u frames", (unsigned)measurement.steps_judged);
        ut_checkf(measurement.steps_uneven == 0u,
                  "and none of them disagrees with its neighbours, worst %.2f per cent",
                  (double)measurement.worst_unevenness * 100.0);

        /* The same, with each frame drawn twice, as the engine does it. The
         * second call must be ignored rather than break the chain. */
        mover_measure_reset(&measurement);
        memset(&drawn, 0, sizeof drawn);
        for (frame = 1u; frame <= 40u; ++frame) {
            at[0] = (float)frame * 0.05f;
            mover_measure_drawn(&measurement, &drawn, at, frame);
            mover_measure_drawn(&measurement, &drawn, at, frame);
        }
        ut_checkf(measurement.steps_judged > 30u,
                  "drawn twice a frame, as the engine does it, still judges %u frames",
                  (unsigned)measurement.steps_judged);
        ut_check(measurement.steps_uneven == 0u, "and still finds nothing uneven");

        /* Smooth acceleration must also score nothing. The metric this replaced condemned it,
         * because it divided the largest step by the smallest. */
        mover_measure_reset(&measurement);
        memset(&drawn, 0, sizeof drawn);
        at[0] = 0.0f;
        for (frame = 1u; frame <= 40u; ++frame) {
            at[0] += 0.02f + (float)frame * 0.002f;
            mover_measure_drawn(&measurement, &drawn, at, frame);
        }
        ut_checkf(measurement.steps_uneven == 0u,
                  "a mover accelerating smoothly scores nothing, worst %.2f per cent",
                  (double)measurement.worst_unevenness * 100.0);

        /* And the case it exists for: one frame that jumps and the frame that holds after it.
         * Both disagree with their neighbours, so both are counted. */
        mover_measure_reset(&measurement);
        memset(&drawn, 0, sizeof drawn);
        at[0] = 0.0f;
        for (i = 1; i <= 40; ++i) {
            at[0] += (i == 20) ? 0.15f : ((i == 21) ? 0.0015f : 0.05f);
            mover_measure_drawn(&measurement, &drawn, at, (uint32_t)i);
        }
        ut_checkf(measurement.steps_uneven >= 2u,
                  "a frame that jumps and the frame that holds are both counted, %u of %u",
                  (unsigned)measurement.steps_uneven, (unsigned)measurement.steps_judged);
        ut_checkf(measurement.worst_unevenness > 0.5f,
                  "and the worst disagreement is large, %.0f per cent",
                  (double)measurement.worst_unevenness * 100.0);
    }

    return ut_summary("mover measure");
}
