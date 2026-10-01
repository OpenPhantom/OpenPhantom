/* mover_wraps.c: see mover_wraps.h. */
#include "mover_wraps.h"

#include <math.h>
#include <stdbool.h>

bool mover_wraps_is_wrap(float pose_before, float pose_after, float track)
{
    return (pose_after < pose_before) && (track > 0.0f) &&
           ((pose_before - pose_after) > (track * 0.5f));
}

float mover_wraps_angle_degrees(float cosine)
{
    if (!(cosine < 1.0f)) {
        return 0.0f;                 /* also catches NaN, as no turn at all */
    }
    if (cosine <= -1.0f) {
        return 180.0f;
    }
    return (float)(acos((double)cosine) * (180.0 / 3.14159265358979323846));
}

/* Unlike its ordinary self: off by more than half the ordinary figure, and by more than the floor
 * in absolute terms, so a mover that ordinarily does not move at all is not called a reset for a
 * hundredth of a unit of rounding. */
static bool differs(float wrap, float ordinary, float floor_value)
{
    float allowed = ordinary * 0.5f;

    if (allowed < floor_value) {
        allowed = floor_value;
    }
    return !(fabsf(wrap - ordinary) <= allowed);   /* NaN reads as differing */
}

bool mover_wraps_is_reset(float wrap_step, float wrap_angle,
                          float ordinary_step, float ordinary_angle, bool have_ordinary)
{
    if (!have_ordinary) {
        return true;
    }
    return differs(wrap_step, ordinary_step, MOVER_WRAP_STEP_FLOOR) ||
           differs(wrap_angle, ordinary_angle, MOVER_WRAP_ANGLE_FLOOR);
}
