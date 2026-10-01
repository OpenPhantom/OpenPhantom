/* mp_enemy_block_rule.c: the decisions behind an NPC's blade clang. See the header. */
#include "mp_enemy_block_rule.h"

#include "mp_enemy_pack.h"

#include <stddef.h>

mp_enemy_block_gate_t mp_enemy_block_gate(bool read_before, uint32_t before, bool read_after,
                                          uint32_t after)
{
    if (!read_before || !read_after) {
        return MP_ENEMY_BLOCK_UNREAD;
    }
    return before != after ? MP_ENEMY_BLOCK_PASSED : MP_ENEMY_BLOCK_HELD;
}

uint32_t mp_enemy_block_pack(int32_t kind)
{
    if (kind < 0) {
        return 0u;
    }
    return mp_enemy_pack_one((uint32_t)kind, MP_ENEMY_BLOCK_MAX_KIND);
}

bool mp_enemy_block_unpack(uint32_t field, int32_t *kind)
{
    uint32_t value = 0;

    if (!mp_enemy_unpack_one(field & 0xFFu, &value) || value > MP_ENEMY_BLOCK_MAX_KIND) {
        return false;
    }
    if (kind != NULL) {
        *kind = (int32_t)value;
    }
    return true;
}
