/* mover_evenness.c: see mover_evenness.h. */
#include "mover_evenness.h"

#include "common/logging.h"
#include "common/numeric.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

/* Below this a subnode is not really moving and the comparison is noise. A carried character moves
 * 0.045 units in a simulation step and a platform rather more, so a thousandth of a unit in a
 * frame is comfortably beneath anything visible. */
#define STEP_FLOOR 0.001f

/* How far a frame may differ from what its neighbours expect before it counts. Smooth acceleration
 * changes the step by well under this from one frame to the next at any rate the game runs at; a
 * stepped mover changes it by tens of per cent. */
#define UNEVEN_FRACTION 0.05f

static bool     evenness_on;
static uint32_t evenness_judged;
static uint32_t evenness_uneven;
static uint32_t evenness_raw_between;   /* raw frames drawn the frame after a blended one */
static float    evenness_worst;

void mover_evenness_enable(bool enabled)
{
    evenness_on     = enabled;
    evenness_judged      = 0;
    evenness_uneven      = 0;
    evenness_raw_between = 0;
    evenness_worst       = 0.0f;
}

bool mover_evenness_enabled(void)
{
    return evenness_on;
}

void mover_evenness_note(mover_evenness_state_t *state, const float *translation,
                         uint32_t frame_stamp, bool blended)
{
    float dx;
    float dy;
    float dz;
    float step;

    if (!evenness_on || state == NULL || translation == NULL) {
        return;
    }

    /* The second of the two calls this subnode gets in a frame. Ignored, not treated as a gap. */
    if (state->seen && state->stamp == frame_stamp) {
        return;
    }

    if (state->seen && state->stamp + 1u == frame_stamp) {
        if (!blended && state->last_blended) {
            ++evenness_raw_between;
        }
        dx = translation[0] - state->last[0];
        dy = translation[1] - state->last[1];
        dz = translation[2] - state->last[2];
        step = (float)sqrt((double)(dx * dx + dy * dy + dz * dz));

        if (numeric_is_finite(step)) {
            /* The middle of the three is the one judged, against the mean of its neighbours.
             * All three must be real motion, so a mover coming to rest is not condemned for
             * having stopped. */
            if (state->have_before && state->have_middle &&
                state->step_before > STEP_FLOOR && state->step_middle > STEP_FLOOR &&
                step > STEP_FLOOR) {
                float expected = 0.5f * (state->step_before + step);

                if (expected > 0.0f) {
                    float off = state->step_middle - expected;

                    if (off < 0.0f) {
                        off = -off;
                    }
                    off /= expected;

                    ++evenness_judged;
                    if (off > UNEVEN_FRACTION) {
                        ++evenness_uneven;
                    }
                    if (off > evenness_worst) {
                        evenness_worst = off;
                    }
                }
            }
            state->step_before = state->step_middle;
            state->have_before = state->have_middle;
            state->step_middle = step;
            state->have_middle = true;
        }
    } else {
        /* Not consecutive: the subnode went off screen or has just appeared, and a gap is not a
         * step. The chain starts again rather than reporting the gap as motion. */
        state->have_before = false;
        state->have_middle = false;
    }

    state->last[0] = translation[0];
    state->last[1] = translation[1];
    state->last[2] = translation[2];
    state->stamp   = frame_stamp;
    state->seen    = true;
    state->last_blended = blended;
}

void mover_evenness_counts(uint32_t *out_judged, uint32_t *out_uneven)
{
    if (out_judged != NULL) {
        *out_judged = evenness_judged;
    }
    if (out_uneven != NULL) {
        *out_uneven = evenness_uneven;
    }
}

void mover_evenness_report(void)
{
    if (!evenness_on || evenness_judged == 0u) {
        return;
    }
    log_info("mover evenness: %u of %u judged frames disagreed with their neighbours by more than "
             "5 per cent, worst %.1f per cent, and %u frames drew a raw pose straight after a "
             "blended one. Only meaningful at a steady frame rate: a drawn object correctly moves "
             "further on a longer frame, so an uncapped run reads badly with nothing wrong",
             (unsigned)evenness_uneven, (unsigned)evenness_judged,
             (double)evenness_worst * 100.0, (unsigned)evenness_raw_between);
    evenness_judged      = 0;
    evenness_uneven      = 0;
    evenness_raw_between = 0;
    evenness_worst       = 0.0f;
}
