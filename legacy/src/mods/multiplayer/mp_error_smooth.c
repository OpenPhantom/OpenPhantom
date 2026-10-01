/* mp_error_smooth.c: the offset, its decay, and the snap that gives up on hiding a big jump. */
#include "mp_error_smooth.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>

static float length3(const float v[3])
{
    return (float)sqrt((double)(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]));
}

void mp_error_smooth_init(mp_error_smooth_t *smooth, float unit)
{
    if (unit <= 0.0f) {
        unit = 1.0f;
    }
    smooth->offset[0]  = 0.0f;
    smooth->offset[1]  = 0.0f;
    smooth->offset[2]  = 0.0f;
    smooth->small_error = unit;
    smooth->large_error = 4.0f * unit;
    smooth->snap_error  = 40.0f * unit;
    smooth->slow_decay  = 0.90f;
    smooth->fast_decay  = 0.80f;
    smooth->settle      = unit / 64.0f;
}

bool mp_error_smooth_correct(mp_error_smooth_t *smooth, const float delta[3])
{
    float distance = length3(delta);

    if (distance > smooth->snap_error) {
        /* Too far to hide: a clean cut looks better than a body sliding across the level. */
        smooth->offset[0] = 0.0f;
        smooth->offset[1] = 0.0f;
        smooth->offset[2] = 0.0f;
        return false;
    }

    /* The drawn position was old = corrected + old_offset. After the jump the authoritative is
     * corrected + delta, so to keep the drawn position put `offset = old_offset - delta`. */
    smooth->offset[0] -= delta[0];
    smooth->offset[1] -= delta[1];
    smooth->offset[2] -= delta[2];
    return true;
}

void mp_error_smooth_decay(mp_error_smooth_t *smooth)
{
    float distance = length3(smooth->offset);
    float factor;
    int   axis;

    if (distance <= smooth->settle) {
        smooth->offset[0] = 0.0f;
        smooth->offset[1] = 0.0f;
        smooth->offset[2] = 0.0f;
        return;
    }

    /* The decay factor is slow for a small offset and fast for a large one, blended linearly
     * between the two error thresholds, so the catch-up speeds up smoothly with the error rather
     * than in two steps. */
    if (distance <= smooth->small_error) {
        factor = smooth->slow_decay;
    } else if (distance >= smooth->large_error) {
        factor = smooth->fast_decay;
    } else {
        float span = smooth->large_error - smooth->small_error;
        float t    = (span > 0.0f) ? (distance - smooth->small_error) / span : 1.0f;

        factor = smooth->slow_decay + (smooth->fast_decay - smooth->slow_decay) * t;
    }

    for (axis = 0; axis < 3; ++axis) {
        smooth->offset[axis] *= factor;
    }
}

void mp_error_smooth_render(const mp_error_smooth_t *smooth, const float position[3], float out[3])
{
    out[0] = position[0] + smooth->offset[0];
    out[1] = position[1] + smooth->offset[1];
    out[2] = position[2] + smooth->offset[2];
}

float mp_error_smooth_magnitude(const mp_error_smooth_t *smooth)
{
    return length3(smooth->offset);
}
