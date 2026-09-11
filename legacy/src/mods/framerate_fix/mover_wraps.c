/* mover_wraps.c: see mover_wraps.h. */
#include "mover_wraps.h"

#include <stdbool.h>

bool mover_wraps_is_wrap(float pose_before, float pose_after, float track)
{
    return (pose_after < pose_before) && (track > 0.0f) &&
           ((pose_before - pose_after) > (track * 0.5f));
}
