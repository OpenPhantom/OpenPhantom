/* look_counts.c: see look_counts.h. */
#include "look_counts.h"

#include <math.h>

/* Truncating one axis, and clamping whatever the cast could not have held.
 *
 * A truncation toward zero is what the carry wants on both sides of centre: it leaves a remainder
 * with the same sign as the movement, so a leftward push banks a leftward fraction and the next
 * count still goes left. Rounding to nearest would alternate the sign of the remainder and make
 * the carry an oscillation rather than a debt. */
static long take_whole(double *owed, double desired)
{
    long whole;

    if (desired > LOOK_COUNTS_MAX) {
        *owed = 0.0;
        return (long)LOOK_COUNTS_MAX;
    }
    if (desired < -LOOK_COUNTS_MAX) {
        *owed = 0.0;
        return -(long)LOOK_COUNTS_MAX;
    }

    whole = (long)desired;
    *owed = desired - (double)whole;
    return whole;
}

bool look_counts_step(look_carry_t *carry, float stick_x, float stick_y, float sensitivity,
                      double seconds, long *out_dx, long *out_dy)
{
    double desired_x;
    double desired_y;

    *out_dx = 0;
    *out_dy = 0;

    /* Written as a NOT so that a gap which is not a number fails it. */
    if (!(seconds > 0.0)) {
        return false;
    }

    desired_x = (double)stick_x * (double)sensitivity * seconds + carry->x;
    desired_y = (double)(-stick_y) * (double)sensitivity * seconds + carry->y;

    if (!isfinite(desired_x) || !isfinite(desired_y)) {
        look_counts_reset(carry);
        return false;
    }

    *out_dx = take_whole(&carry->x, desired_x);
    *out_dy = take_whole(&carry->y, desired_y);

    return *out_dx != 0 || *out_dy != 0;
}

void look_counts_reset(look_carry_t *carry)
{
    carry->x = 0.0;
    carry->y = 0.0;
}
