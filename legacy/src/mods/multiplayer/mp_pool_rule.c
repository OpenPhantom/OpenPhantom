/* mp_pool_rule.c: the pool rule. See the header. */
#include "mp_pool_rule.h"

#include <stdbool.h>
#include <stdint.h>

bool mp_pool_may_take(uint32_t live, uint32_t capacity, uint32_t reserve)
{
    if (capacity == 0u || live >= capacity) {
        return false;   /* full, or a walk that answered nonsense; neither deserves a slot */
    }
    return (capacity - live) > reserve;
}
