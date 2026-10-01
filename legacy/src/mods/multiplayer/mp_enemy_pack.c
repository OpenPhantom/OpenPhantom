/* mp_enemy_pack.c: putting something that may not be there into a delta coded field. */
#include "mp_enemy_pack.h"

#include <stddef.h>

/* The offset field is a byte wide in both users, so the largest value it can carry is 254: 255
 * would come back as 0 and read as nothing. */
#define BYTE_MAX 254u

uint32_t mp_enemy_pack_one(uint32_t value, uint32_t max)
{
    if (max > BYTE_MAX || value > max) {
        return 0u;
    }
    return value + 1u;
}

bool mp_enemy_unpack_one(uint32_t packed, uint32_t *value)
{
    uint32_t low = packed & 0xFFu;

    if (low == 0u) {
        return false;
    }
    if (value != NULL) {
        *value = low - 1u;
    }
    return true;
}

uint32_t mp_enemy_pack_pair(uint32_t low, uint32_t high, uint32_t low_max, uint32_t high_max)
{
    uint32_t packed = mp_enemy_pack_one(low, low_max);

    if (packed == 0u || high_max > 0xFFu || high > high_max) {
        return 0u;
    }
    return packed | ((high & 0xFFu) << 8);
}

bool mp_enemy_unpack_pair(uint32_t packed, uint32_t *low, uint32_t *high)
{
    if (!mp_enemy_unpack_one(packed, low)) {
        return false;
    }
    if (high != NULL) {
        *high = (packed >> 8) & 0xFFu;
    }
    return true;
}
