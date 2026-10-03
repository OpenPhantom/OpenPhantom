/* mp_scene_scope.c: where a scene's actor stands, read out of the engine. See the header.
 *
 * The engine's own reader of the field: the actor's position in move_chaseDrive 0x00429DF4, and
 * enemy_hostOnPlayer 0x004377EE, which seeds it from the body. */
#include "mp_scene_scope.h"

#include "mp_cells.h"

#include "common/memory.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool mp_scene_scope_actor(uintptr_t actor, float at[3])
{
    return actor != 0u && at != NULL &&
           memory_try_read(actor + MP_CHARACTER_POS, at, 3u * sizeof(float)) &&
           isfinite(at[0]) && isfinite(at[1]) && isfinite(at[2]);
}
