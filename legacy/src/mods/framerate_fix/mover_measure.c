/* mover_measure.c: see mover_measure.h. */
#include "mover_measure.h"

#include "common/logging.h"

#include <math.h>
#include <string.h>

void mover_measure_reset(mover_measurement_t *measurement)
{
    if (measurement == NULL) {
        return;
    }
    memset(measurement, 0, sizeof *measurement);
}

void mover_measure_note(mover_range_t *range, float value)
{
    if (range == NULL || !isfinite(value)) {
        return;
    }
    if (!range->seen) {
        range->low  = value;
        range->high = value;
        range->seen = true;
        return;
    }
    if (value < range->low)  { range->low  = value; }
    if (value > range->high) { range->high = value; }
}

void mover_measure_bounds(const mover_range_t *range, float *out_low, float *out_high)
{
    if (out_low == NULL || out_high == NULL) {
        return;
    }
    if (range == NULL || !range->seen) {
        *out_low  = 0.0f;
        *out_high = 0.0f;
        return;
    }
    *out_low  = range->low;
    *out_high = range->high;
}

void mover_measure_report(const mover_measurement_t *measurement, uint32_t frames)
{
    float interval_low;
    float interval_high;
    float elapsed_low;
    float elapsed_high;
    float phase_low;
    float phase_high;
    float alpha_low;
    float alpha_high;
    float age_low;
    float age_high;
    float advance_low;
    float advance_high;
    float wrap_low;
    float wrap_high;

    if (measurement == NULL) {
        return;
    }
    mover_measure_bounds(&measurement->interval, &interval_low, &interval_high);
    mover_measure_bounds(&measurement->elapsed, &elapsed_low, &elapsed_high);
    mover_measure_bounds(&measurement->phase, &phase_low, &phase_high);
    mover_measure_bounds(&measurement->alpha, &alpha_low, &alpha_high);
    mover_measure_bounds(&measurement->pose_age, &age_low, &age_high);
    mover_measure_bounds(&measurement->alpha_advance, &advance_low, &advance_high);
    mover_measure_bounds(&measurement->wrap_advance, &wrap_low, &wrap_high);

    /* What each number has to be for the rejected arithmetic to have been right, said next to
     * what it actually was, so the line answers the question on its own.
     *
     * Interval is how much world time one move covered. At 60 frames a second against a 32 Hz
     * simulation it should sit between one and two frames, 16.7 to 33.3 ms.
     *
     * Ticks per frame is how often a mover integrates at all. One per mover per frame would mean
     * the world clock advances on every frame, and the whole premise was that it does not.
     *
     * Elapsed is the render time since a mover last moved, and is the quantity both rejected
     * modes were built on. It should reach about one frame. Near zero throughout would mean the
     * alpha is not changing between the two places it is read, so the measurement rather than the
     * arithmetic is what is broken.
     *
     * Phase is elapsed over interval and should sweep the whole way from 0 to 1. A phase pinned
     * at one end explains a mover drawn always at the same pose, so it is stepping. */
    log_info("mover measurement over %u frames: %u tick calls (%.2f per frame, which IS the mover "
             "count) of which %u integrated (%.3f of them, wanted about 0.53 at 60 fps against a "
             "32 Hz simulation) | pose age %.0f to %.0f frames, where anything above zero means "
             "the mover is drawn before it is ticked and every quantity below is that much "
             "older "
             "than assumed | interval %.2f to %.2f ms | elapsed %.2f to %.2f ms, and a floor at "
             "-14.58 is the alpha's own change from one frame to the next rather than time since "
             "the mover moved | phase %.3f to %.3f | alpha %.3f to %.3f, advancing %.4f to "
             "%.4f per frame where 0.5333 is exact at 60 fps | of %u frames measured, %u "
             "advanced more than 1 per cent past exact and %u more than 5 per cent | ACROSS A "
             "STEP BOUNDARY, where the sample pair rolls on, %u frames advancing %.4f to %.4f "
             "with %u past 1 per cent and %u past 5 per cent, the half of the frames "
             "a fault would live in | %u draws",
             (unsigned)frames, (unsigned)measurement->calls,
             (frames != 0) ? (double)measurement->calls / (double)frames : 0.0,
             (unsigned)measurement->ticks,
             (measurement->calls != 0)
                 ? (double)measurement->ticks / (double)measurement->calls : 0.0,
             (double)age_low, (double)age_high,
             (double)interval_low * 1000.0, (double)interval_high * 1000.0,
             (double)elapsed_low * 1000.0, (double)elapsed_high * 1000.0,
             (double)phase_low, (double)phase_high,
             (double)alpha_low, (double)alpha_high,
             (double)advance_low, (double)advance_high,
             (unsigned)measurement->advance_frames,
             (unsigned)measurement->advance_over_1pc,
             (unsigned)measurement->advance_over_5pc,
             (unsigned)measurement->wrap_frames,
             (double)wrap_low, (double)wrap_high,
             (unsigned)measurement->wrap_over_1pc,
             (unsigned)measurement->wrap_over_5pc,
             (unsigned)measurement->draws);

}

/* The alpha is the same for every mover in a frame, so its per-frame step is a property of the
 * frame. Sampling it per pose would report the same value thousands of times and hide nothing. */
static float measure_last_frame_alpha;
static bool  measure_have_frame_alpha;

void mover_measure_frame(mover_measurement_t *measurement, float alpha)
{
    float advance;
    bool  wrapped;

    if (measurement == NULL || !isfinite(alpha)) {
        return;
    }
    if (!measure_have_frame_alpha) {
        measure_last_frame_alpha = alpha;
        measure_have_frame_alpha = true;
        return;
    }

    /* Two kinds of frame, and BOTH have to be counted. On a frame where the alpha rises the
     * advance is the difference. On a frame where it falls, a new simulation step began and the
     * sample pair rolled on, so the drawn position advanced by the rest of the old step plus the
     * start of the new one, which is one plus the difference.
     *
     * An earlier version skipped the second kind as bookkeeping. Those are the frames where the
     * pair rolls, which is the most likely place for a jump, and they are 320 of every 600 at
     * 60 fps: it measured the easy 47 per cent and then reported the motion as uniform. */
    wrapped = (alpha < measure_last_frame_alpha);
    advance = wrapped ? (1.0f + alpha - measure_last_frame_alpha)
                      : (alpha - measure_last_frame_alpha);
    measure_last_frame_alpha = alpha;

    mover_measure_note(wrapped ? &measurement->wrap_advance : &measurement->alpha_advance,
                       advance);
    if (wrapped) {
        ++measurement->wrap_frames;
        if (advance > MOVER_ADVANCE_OVER_1PC) { ++measurement->wrap_over_1pc; }
        if (advance > MOVER_ADVANCE_OVER_5PC) { ++measurement->wrap_over_5pc; }
    } else {
        ++measurement->advance_frames;
        if (advance > MOVER_ADVANCE_OVER_1PC) { ++measurement->advance_over_1pc; }
        if (advance > MOVER_ADVANCE_OVER_5PC) { ++measurement->advance_over_5pc; }
    }
}

void mover_measure_tick(mover_measurement_t *measurement, float interval)
{
    if (measurement == NULL) {
        return;
    }
    ++measurement->ticks;
    mover_measure_note(&measurement->interval, interval);
}

void mover_measure_draw(mover_measurement_t *measurement, float alpha, float elapsed,
                        float interval, uint32_t pose_age)
{
    if (measurement == NULL) {
        return;
    }
    ++measurement->draws;
    mover_measure_note(&measurement->pose_age, (float)pose_age);
    mover_measure_note(&measurement->alpha, alpha);
    mover_measure_note(&measurement->elapsed, elapsed);
    if (interval > 0.0f) {
        mover_measure_note(&measurement->phase, elapsed / interval);
    }
}

/* Below this a subnode is not really moving and the comparison is noise rather than jitter. A
 * carried character moves 0.045 units in a step and a platform rather more, so a thousandth of a
 * unit in a frame is comfortably beneath anything visible. */
#define DRAWN_STEP_FLOOR 0.001f

/* How far a frame may differ from what its neighbours expect before it counts as uneven. Smooth
 * acceleration changes the step by well under this from one frame to the next at any rate the game
 * runs at; a stepped mover changes it by tens of per cent. */
#define DRAWN_UNEVEN_FRACTION 0.05f

void mover_measure_drawn(mover_measurement_t *measurement, mover_drawn_t *drawn,
                         const float *translation, uint32_t frame_stamp)
{
    float dx;
    float dy;
    float dz;
    float step;

    if (measurement == NULL || drawn == NULL || translation == NULL) {
        return;
    }

    /* Twice per subnode per frame, and that broke the first version of this. The draw
     * calls both the matrix multiply and the rigid invert for each subnode, and this DLL redirects
     * both, so the second call arrives with the frame stamp it already recorded. Treating that as
     * a break in the chain reset the three frame history on every second call and the window
     * reported nothing judged at all. */
    if (drawn->seen && drawn->stamp == frame_stamp) {
        return;
    }

    if (drawn->seen && drawn->stamp + 1u == frame_stamp) {
        dx = translation[0] - drawn->last[0];
        dy = translation[1] - drawn->last[1];
        dz = translation[2] - drawn->last[2];
        step = (float)sqrt((double)(dx * dx + dy * dy + dz * dz));

        if (isfinite(step)) {
            /* The middle of the three is the one being judged, against the mean of the one
             * before it and this one. All three must be real motion, so a mover coming to rest
             * is not condemned for the frame it stops on. */
            if (drawn->have_before && drawn->have_middle &&
                drawn->step_before > DRAWN_STEP_FLOOR &&
                drawn->step_middle > DRAWN_STEP_FLOOR &&
                step > DRAWN_STEP_FLOOR) {
                float expected = 0.5f * (drawn->step_before + step);

                if (expected > 0.0f) {
                    float off = drawn->step_middle - expected;

                    if (off < 0.0f) {
                        off = -off;
                    }
                    off /= expected;
                    ++measurement->steps_judged;
                    if (off > DRAWN_UNEVEN_FRACTION) {
                        ++measurement->steps_uneven;
                    }
                    if (off > measurement->worst_unevenness) {
                        measurement->worst_unevenness = off;
                    }
                }
            }
            drawn->step_before = drawn->step_middle;
            drawn->have_before = drawn->have_middle;
            drawn->step_middle = step;
            drawn->have_middle = true;
        }
    } else {
        /* Not consecutive: the subnode went off screen or has just appeared, and a gap is not a
         * step. The chain starts again rather than reporting the gap as motion. */
        drawn->have_before = false;
        drawn->have_middle = false;
    }

    drawn->last[0] = translation[0];
    drawn->last[1] = translation[1];
    drawn->last[2] = translation[2];
    drawn->stamp   = frame_stamp;
    drawn->seen    = true;
}
