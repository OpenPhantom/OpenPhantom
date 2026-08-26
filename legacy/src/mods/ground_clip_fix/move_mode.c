/* move_mode.c: see move_mode.h. Numbers only, no engine. */
#include "move_mode.h"

#include <stddef.h>

bool move_mode_skips_collision(int32_t move_mode)
{
    return (move_mode & MOVE_MODE_SKIPS_COLLISION_BIT) != 0;
}

/* True when this component would move the character. Written as the negation of "definitely small"
 * rather than as "definitely large", so a NaN answers true: a NaN velocity reaching a move that
 * nothing will test is the worst case available, and clearing it is the safe answer. */
static bool component_moves(float value)
{
    return !(value > -MOVE_MODE_VELOCITY_EPSILON && value < MOVE_MODE_VELOCITY_EPSILON);
}

bool move_mode_move_is_uncontested(int32_t move_mode, const float velocity[3])
{
    if (velocity == NULL) {
        return false;
    }
    if (!move_mode_skips_collision(move_mode)) {
        return false;
    }
    return component_moves(velocity[0]) || component_moves(velocity[1]) ||
           component_moves(velocity[2]);
}
