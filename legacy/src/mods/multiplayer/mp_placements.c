/* mp_placements.c: the level's placement table. See the header for why it is one file.
 */
#include "mp_placements.h"

#include "common/memory.h"

/* The band of class ids the engine's own contact handler treats as "walk into it and it is
 * yours". Read off the branch in Plr_PickUp: below it are the enemies and the props, above it are
 * the scripted classes that a touch does nothing to.
 *
 * It is also the band a PLAYER's body may never be given, for exactly this reason: two players
 * with bodies in it would pick each other up and delete each other. */
#define PICKUP_CLASS_FIRST 0x0Au
#define PICKUP_CLASS_LAST  0x1Bu

bool mp_placements_table(uintptr_t *world, uint32_t *count, uint32_t *table)
{
    uintptr_t cell = mp_cells_address(MP_CELL_LEVEL);
    uint32_t  value = 0;

    if (cell == 0u || count == NULL || table == NULL) {
        return false;
    }
    if (!memory_try_read_u32(cell, &value) || value == 0u) {
        return false;   /* no level open: a menu, and not a fault */
    }
    if (world != NULL) {
        *world = (uintptr_t)value;
    }
    if (!memory_try_read((uintptr_t)value + MP_LEVEL_PLACEMENT_COUNT, count, sizeof *count) ||
        *count > MP_PLACEMENTS_COUNT_LIMIT) {
        return false;
    }
    return memory_try_read((uintptr_t)value + MP_LEVEL_PLACEMENTS, table, sizeof *table) &&
           *table != 0u;
}

bool mp_placements_read_at(uintptr_t address, mp_placement_t *out)
{
    uint16_t slots[4];

    if (out == NULL || address == 0u) {
        return false;
    }
    out->address      = address;
    out->drives_mover = false;
    if (!memory_try_readable(address, MP_PLACEMENTS_READ_BYTES) ||
        !memory_try_read(address + MP_PLACEMENT_FLAGS, &out->flags, sizeof out->flags) ||
        !memory_try_read(address + MP_PLACEMENT_CLASS_ID, &out->class_id, sizeof out->class_id) ||
        !memory_try_read(address + MP_PLACEMENT_SPAWN_STATE, &out->state, sizeof out->state) ||
        !memory_try_read(address + MP_PLACEMENT_MOVER_SLOTS, slots, sizeof slots)) {
        return false;
    }
    /* The slots lie inside the extent the readable check already covers: 0x94 plus eight is
     * 0x9C, and the check runs to 0xCC for the spawn state. */
    out->drives_mover = slots[0] != 0u || slots[1] != 0u || slots[2] != 0u || slots[3] != 0u;
    return true;
}

bool mp_placements_read(uint32_t table, uint32_t index, mp_placement_t *out)
{
    uint32_t placement = 0;

    if (table == 0u) {
        return false;
    }
    if (!memory_try_read((uintptr_t)table + (uintptr_t)index * 4u, &placement, sizeof placement)) {
        return false;
    }
    return mp_placements_read_at((uintptr_t)placement, out);
}

bool mp_placements_reach_at(uintptr_t address, mp_placement_reach_t *out)
{
    if (out == NULL || address == 0u || !memory_try_readable(address, MP_PLACEMENTS_READ_BYTES)) {
        return false;
    }
    /* All three lie inside the extent the readable check covers: the position ends at 0xB8. */
    return memory_try_read(address + MP_PLACEMENT_WAKE_RANGE, &out->wake, sizeof out->wake) &&
           memory_try_read(address + MP_PLACEMENT_DEACT_RANGE, &out->keep, sizeof out->keep) &&
           memory_try_read(address + MP_PLACEMENT_POSITION, out->position, sizeof out->position);
}

bool mp_placements_is_pickup(int32_t class_id)
{
    return class_id >= (int32_t)PICKUP_CLASS_FIRST && class_id <= (int32_t)PICKUP_CLASS_LAST;
}
