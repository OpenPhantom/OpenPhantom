/* move_mode.c: see move_mode.h. Numbers only, no engine. */
#include "move_mode.h"

#include <stddef.h>

bool move_mode_skips_collision(int32_t move_mode)
{
    return (move_mode & MOVE_MODE_SKIPS_COLLISION_BIT) != 0;
}

/* True when these two differ by more than rounding. Written as the negation of "definitely the
 * same" rather than as "definitely different", so a NaN on either side answers true and the caller
 * restores the value it knows. */
static bool component_changed(float before, float after)
{
    float delta = after - before;

    return !(delta > -MOVE_MODE_VELOCITY_EPSILON && delta < MOVE_MODE_VELOCITY_EPSILON);
}

bool move_mode_contact_pushed(int32_t move_mode, const float before[3], const float after[3])
{
    if (before == NULL || after == NULL) {
        return false;
    }
    if (!move_mode_skips_collision(move_mode)) {
        return false;
    }
    return component_changed(before[0], after[0]) || component_changed(before[1], after[1]) ||
           component_changed(before[2], after[2]);
}
