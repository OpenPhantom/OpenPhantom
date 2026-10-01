/* mover_blend.c: what a moving door looks like between two simulation steps.
 *
 * All of the arithmetic and none of the engine, in a file of its own because every way it can be
 * wrong is silent in review and loud on screen:
 *
 *   a scale destroyed      the mover snaps to its unscaled size on every drawn frame and grows
 *                          back on the next simulation step, which reads as a flickering model
 *   a guard too tight      a subnode scaled by half fails a raw dot product every frame and is
 *                          never smoothed, so the feature installs and does nothing for it
 *   a guard too loose      a track wrapping from its end back to its start is drawn as a sweep
 *                          across the level rather than as the jump it is
 *   a basis left skewed    rows blended one at a time stop being perpendicular, and a basis that
 *                          is not orthogonal shears whatever it transforms
 *   not the identity at 1  the behaviour at the authored rate stops being a fixed point, which is
 *                          the one property this feature is not allowed to lose
 *
 * ============================ Why a direction and a length, separately ========================
 *
 * The obvious implementation orthonormalises the blended matrix in place. That is wrong here in
 * both directions at once, and the reason is that a subnode's 3x3 is usually a pure rotation but
 * not always: the mover track header carries a bit that composes a scale into the matrix, so a row
 * can legitimately be longer or shorter than one.
 *
 * A raw dot product between two scaled rows is not a cosine. A subnode scaled by half trips a
 * 0.707 rejection on every frame and is never smoothed, while one scaled by three can never trip
 * it however far it turns. And re-orthonormalising a lerped matrix sets every row to unit length,
 * so a scaled model snaps to its unscaled size on every drawn frame and grows back on the next
 * simulation step.
 *
 * Splitting the two apart fixes both without a special case: the cosine is taken between unit
 * directions and is a real cosine, and the length rides along as its own linear quantity. For a
 * pure rotation the lengths are 1 at both ends and the arithmetic is exactly what it would have
 * been anyway.
 *
 * The direction blend is a normalised lerp rather than a spherical one. Over one simulation step
 * the angle is small and the difference between the chord and the arc is far below what a renderer
 * can show; it grows with the square of the angle, and the rejection above bounds that angle.
 *
 * The assumption this rests on is that the scale multiplies the ROWS. If it multiplies the columns
 * instead, the blend is still continuous and still exact at both ends, but the intermediate is a
 * mix rather than the exact answer. Check that before reading a wobble here as a defect in the
 * lerp.
 */
#include "mover_blend.h"

#include <math.h>
#include <string.h>

static float dot3(const float *a, const float *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static float length3(const float *v)
{
    return (float)sqrt((double)dot3(v, v));
}

static void scale3(float *out, const float *v, float factor)
{
    out[0] = v[0] * factor;
    out[1] = v[1] * factor;
    out[2] = v[2] * factor;
}

static void lerp3(float *out, const float *from, const float *to, float alpha)
{
    out[0] = from[0] + (to[0] - from[0]) * alpha;
    out[1] = from[1] + (to[1] - from[1]) * alpha;
    out[2] = from[2] + (to[2] - from[2]) * alpha;
}

/* Gram-Schmidt over the three blended directions.
 *
 * Blending each row on its own keeps none of them exactly perpendicular to the others, and a basis
 * that is not orthogonal shears whatever it transforms. Over one step the error is tiny and this
 * removes it rather than arguing about how tiny. Row 0 is taken as authoritative because it is the
 * one the rejection test has already vouched for first. */
static bool orthonormalise(float rows[MOVER_ROW_COUNT][3])
{
    float length;
    int   index;
    int   axis;

    for (index = 0; index < MOVER_ROW_COUNT; ++index) {
        int previous;

        for (previous = 0; previous < index; ++previous) {
            float projection = dot3(rows[index], rows[previous]);

            for (axis = 0; axis < 3; ++axis) {
                rows[index][axis] -= rows[previous][axis] * projection;
            }
        }

        length = length3(rows[index]);
        if (!(length > MOVER_ROW_LENGTH_MINIMUM)) {
            /* The rows were linearly dependent, so there is no basis to recover. */
            return false;
        }
        scale3(rows[index], rows[index], 1.0f / length);
    }
    return true;
}

bool mover_blend_world(float *out,
                       const float *previous,
                       const float *current,
                       float alpha,
                       float translation_limit,
                       int *out_reason)
{
    float previous_direction[MOVER_ROW_COUNT][3];
    float current_direction[MOVER_ROW_COUNT][3];
    float blended[MOVER_ROW_COUNT][3];
    float previous_length[MOVER_ROW_COUNT];
    float current_length[MOVER_ROW_COUNT];
    int   row;
    int   axis;
    int   reason = MOVER_BLEND_OK;

    /* The caller draws `current` whenever this returns false, so writing it first means every
     * rejection below leaves a complete and correct result rather than a half built one. */
    memcpy(out, current, MOVER_WORLD_FLOATS * sizeof(float));

    if (!(alpha >= 0.0f) || !(alpha <= MOVER_BLEND_WEIGHT_MAX)) {
        /* Written as NOTs so that a weight which is not a number lands here rather than sailing
         * through: every comparison against a NaN is false.
         *
         * The upper bound was 1.0 and that was a defect, not a safeguard. See the note at
         * MOVER_BLEND_WEIGHT_MAX: it silently refused every pose two later modes produced, so
         * each of them drew the newest pose instead and reproduced the stepping it was written to
         * remove. Both ends are now inclusive, and zero draws the earlier sample, which is a real
         * answer rather than a rejection. */
        reason = MOVER_BLEND_WEIGHT_RANGE;
        goto refused;
    }

    if (alpha == 1.0f) {
        /* Kept, and it is the one case that must not go through the arithmetic below. The caller
         * draws `current` on a refusal, which is the newer sample byte for byte, and the alpha
         * reaches exactly one on every frame at 32 frames a second. Running it through the lerp
         * instead would be correct to within rounding and would stop being the identity at the
         * rate the game was authored for, because `from + (to - from) * 1.0` rounds twice and the
         * rotation rows are renormalised on the way. */
        reason = MOVER_BLEND_IDENTITY;
        goto refused;
    }

    for (row = 0; row < MOVER_ROW_COUNT; ++row) {
        const float *from = previous + row * 3;
        const float *to   = current  + row * 3;

        previous_length[row] = length3(from);
        current_length[row]  = length3(to);
        if (!(previous_length[row] > MOVER_ROW_LENGTH_MINIMUM) ||
            !(current_length[row]  > MOVER_ROW_LENGTH_MINIMUM)) {
            reason = MOVER_BLEND_ROW_LENGTH;
            goto refused;
        }
        scale3(previous_direction[row], from, 1.0f / previous_length[row]);
        scale3(current_direction[row],  to,   1.0f / current_length[row]);

        if (!(dot3(previous_direction[row], current_direction[row])
              >= MOVER_ROTATION_COS_MINIMUM)) {
            reason = MOVER_BLEND_ROTATION;   /* a discontinuity, not motion */
            goto refused;
        }
    }

    if (translation_limit > 0.0f) {
        float delta[3];

        for (axis = 0; axis < 3; ++axis) {
            delta[axis] = current[MOVER_TRANSLATION + axis] - previous[MOVER_TRANSLATION + axis];
        }
        if (!(length3(delta) <= translation_limit)) {
            reason = MOVER_BLEND_TRANSLATION;
            goto refused;
        }
    }

    for (row = 0; row < MOVER_ROW_COUNT; ++row) {
        lerp3(blended[row], previous_direction[row], current_direction[row], alpha);
    }
    if (!orthonormalise(blended)) {
        reason = MOVER_BLEND_BASIS;
        goto refused;
    }

    for (row = 0; row < MOVER_ROW_COUNT; ++row) {
        float length = previous_length[row] + (current_length[row] - previous_length[row]) * alpha;

        scale3(out + row * 3, blended[row], length);
    }
    lerp3(out + MOVER_TRANSLATION,
          previous + MOVER_TRANSLATION,
          current  + MOVER_TRANSLATION,
          alpha);

    if (out_reason != NULL) {
        *out_reason = MOVER_BLEND_OK;
    }
    return true;

    /* One exit for every rejection, so a guard cannot be added later that forgets to name itself.
     * The weight guard above did exactly that: it returned directly, so its counter never moved
     * and the one guard whose silence had already cost two play sessions was the one guard that
     * could not be seen. `out` already holds `current` from the top of the function, so the
     * caller has what it draws on a refusal and there is nothing to unwind here. */
refused:
    if (out_reason != NULL) {
        *out_reason = reason;
    }
    return false;
}
