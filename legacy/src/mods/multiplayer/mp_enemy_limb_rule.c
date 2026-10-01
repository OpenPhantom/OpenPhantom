/* mp_enemy_limb_rule.c: a limb an enemy lost, as a number and a decision. See the header. */
#include "mp_enemy_limb_rule.h"

#include "mp_enemy_pack.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

uint32_t mp_enemy_limb_pack(uint32_t ordinal, bool hidden_before)
{
    return mp_enemy_pack_pair(ordinal, hidden_before ? 1u : 0u, MP_ENEMY_LIMB_MAX_ORDINAL, 1u);
}

bool mp_enemy_limb_unpack(uint32_t packed, uint32_t *ordinal, bool *hidden_before)
{
    uint32_t before = 0;

    if (ordinal == NULL || hidden_before == NULL ||
        !mp_enemy_unpack_pair(packed & 0xFFFFu, ordinal, &before)) {
        return false;
    }
    *hidden_before = before != 0u;
    return true;
}

mp_enemy_limb_verdict_t mp_enemy_limb_verdict(uint32_t ordinal, uint32_t node_count,
                                              uint32_t free_tasks)
{
    if (ordinal == 0u || ordinal >= node_count) {
        return MP_ENEMY_LIMB_OUT_OF_RANGE;
    }
    return free_tasks > MP_ENEMY_LIMB_TASKS_KEPT_FREE ? MP_ENEMY_LIMB_THROW
                                                      : MP_ENEMY_LIMB_HIDE_ONLY;
}
