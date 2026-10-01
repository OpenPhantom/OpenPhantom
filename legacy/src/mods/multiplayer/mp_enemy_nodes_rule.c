/* mp_enemy_nodes_rule.c: hidden nodes and meshes in the engine's saved form. See the header. */
#include "mp_enemy_nodes_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

_Static_assert(MP_ENEMY_NODES_MAX == MP_ENEMY_NODES_WORDS * 32u,
               "the saved form is two words of thirty two entries");

static uint32_t bounded(uint32_t count)
{
    return count < MP_ENEMY_NODES_MAX ? count : MP_ENEMY_NODES_MAX;
}

mp_enemy_nodes_mask_t mp_enemy_nodes_rule_fold(const int32_t *entries, uint32_t count)
{
    mp_enemy_nodes_mask_t mask;
    uint32_t              index;

    memset(&mask, 0, sizeof mask);
    if (entries == NULL) {
        return mask;
    }
    for (index = 0; index < bounded(count); ++index) {
        if (entries[index] != 0) {
            mask.word[index / 32u] |= 1u << (index & 31u);
        }
    }
    return mask;
}

bool mp_enemy_nodes_rule_bit(const mp_enemy_nodes_mask_t *mask, uint32_t index)
{
    return mask != NULL && index < MP_ENEMY_NODES_MAX &&
           (mask->word[index / 32u] & (1u << (index & 31u))) != 0u;
}

uint32_t mp_enemy_nodes_rule_count(const mp_enemy_nodes_mask_t *mask)
{
    uint32_t count = 0;
    uint32_t index;

    for (index = 0; index < MP_ENEMY_NODES_MAX; ++index) {
        count += mp_enemy_nodes_rule_bit(mask, index) ? 1u : 0u;
    }
    return count;
}

uint32_t mp_enemy_nodes_rule_first(const mp_enemy_nodes_mask_t *mask)
{
    uint32_t index;

    for (index = 0; index < MP_ENEMY_NODES_MAX; ++index) {
        if (mp_enemy_nodes_rule_bit(mask, index)) {
            return index;
        }
    }
    return MP_ENEMY_NODES_MAX;
}

bool mp_enemy_nodes_rule_past(const mp_enemy_nodes_mask_t *mask, uint32_t count)
{
    uint32_t index;

    for (index = count; index < MP_ENEMY_NODES_MAX; ++index) {
        if (mp_enemy_nodes_rule_bit(mask, index)) {
            return true;
        }
    }
    return false;
}

mp_enemy_nodes_step_t mp_enemy_nodes_rule_step(const mp_enemy_nodes_mask_t *want,
                                               const mp_enemy_nodes_mask_t *here, uint32_t count)
{
    mp_enemy_nodes_step_t step;
    uint32_t              index;

    memset(&step, 0, sizeof step);
    if (want == NULL || here == NULL) {
        return step;
    }
    for (index = 0; index < bounded(count); ++index) {
        bool wanted = mp_enemy_nodes_rule_bit(want, index);

        if (wanted == mp_enemy_nodes_rule_bit(here, index)) {
            continue;
        }
        if (wanted) {
            step.hide.word[index / 32u] |= 1u << (index & 31u);
        } else {
            step.show.word[index / 32u] |= 1u << (index & 31u);
        }
    }
    return step;
}
