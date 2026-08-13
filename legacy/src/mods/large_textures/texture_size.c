#include "texture_size.h"

#include <stdbool.h>
#include <stdint.h>

static bool is_power_of_two(uint32_t value)
{
    /* Zero has no bit set, so the mask test on its own would call it a power of two, and zero is
     * the value an ini file produces most easily by accident. */
    return value != 0u && (value & (value - 1u)) == 0u;
}

texture_size_verdict_t texture_size_check(int32_t requested, uint32_t original,
                                          uint32_t low, uint32_t high)
{
    /* Signed comparisons, and the range before anything else. A negative value is below the floor
     * rather than an enormous size, and it never reaches the power of two test as an unsigned
     * number that happens to have one bit set. */
    if (requested < (int32_t)low) {
        return TEXTURE_SIZE_BELOW_MINIMUM;
    }
    if (requested > (int32_t)high) {
        return TEXTURE_SIZE_ABOVE_MAXIMUM;
    }
    if (!is_power_of_two((uint32_t)requested)) {
        return TEXTURE_SIZE_NOT_POWER_OF_TWO;
    }
    /* Measured against what the engine holds, not against the floor. They are the same number for
     * both keys today, and treating them as one would write the site with the value it already has
     * the moment either range moves. */
    if ((uint32_t)requested == original) {
        return TEXTURE_SIZE_UNCHANGED;
    }
    return TEXTURE_SIZE_APPLY;
}

uint32_t texture_size_world_page_bytes(uint32_t axis)
{
    return axis * axis;
}

uint32_t texture_size_effective_ceiling(uint32_t applied_ceiling, uint32_t original_ceiling)
{
    return (applied_ceiling != 0u) ? applied_ceiling : original_ceiling;
}
