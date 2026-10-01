/* mp_own_body.c: where this machine's own body stands. See the header. */
#include "mp_own_body.h"

#include "mp_bank.h"
#include "mp_cells.h"

#include "common/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The object's own position and rotation, which are what a script drives while the player module
 * is stopped. The rotation is three floats and the heading is the second of them, the same one the
 * block keeps at 0x2A0. */
#define BAPOBJ_POSITION 0x18u
#define BAPOBJ_ROTATION 0x3Cu

/* The two module states of a death: dying, and the re-entry's fade and spawn. */
#define HERO_MODULE_DYING      3u
#define HERO_MODULE_RESPAWNING 4u

/* The module state is the engine's own gate for "a player exists and is not parked", and the
 * object has to be there as well: state nought is also what stands between a despawn and the next
 * spawn, and there is nothing to read then.
 *
 * Dying and respawning, the block is the truth: the engine moves nothing then, and a body the
 * re-entry has just spawned stands at the world's origin until its first pose commit. Said here,
 * once, so that the send and every other reader of this body take the same place. */
mp_own_model_t mp_own_body_model(float position[3], float *heading, uint32_t *state)
{
    uintptr_t block  = mp_cells_address(MP_CELL_HERO_BLOCK);
    uint32_t  module = 1u;
    uint32_t  object = 0u;
    float     rotation[3];

    if (block == 0u || !memory_try_read_u32(block + MP_HERO_BLOCK_MODULE_STATE, &module) ||
        module == MP_HERO_MODULE_RUNNING ||
        !memory_try_read_u32(block + MP_HERO_BLOCK_OBJECT, &object) || object == 0u) {
        return MP_OWN_MODEL_NOT_HELD;
    }
    *state = module;
    if (module == HERO_MODULE_DYING || module == HERO_MODULE_RESPAWNING) {
        return MP_OWN_MODEL_DEATH;
    }
    if (!memory_try_read((uintptr_t)object + BAPOBJ_POSITION, position, 3u * sizeof(float)) ||
        !memory_try_read((uintptr_t)object + BAPOBJ_ROTATION, rotation, sizeof rotation)) {
        return MP_OWN_MODEL_UNREAD;
    }
    *heading = rotation[1];
    return MP_OWN_MODEL_READ;
}

bool mp_own_body_place(float out[3], bool *off_the_model)
{
    float    heading = 0.0f;
    uint32_t state   = MP_HERO_MODULE_RUNNING;
    bool     model;

    if (out == NULL || mp_bank_active() != 0u || !mp_cells_hero_position(out)) {
        return false;
    }
    model = mp_own_body_model(out, &heading, &state) == MP_OWN_MODEL_READ;
    if (off_the_model != NULL) {
        *off_the_model = model;
    }
    return true;
}
