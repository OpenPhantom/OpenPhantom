/* creeping_mover.c: see creeping_mover.h. */
#include "creeping_mover.h"

#include <math.h>

bool creeping_mover_is_creep(float fell)
{
    /* FINITE FIRST, and that order is the point: a comparison against a bound is false for a value
     * that is not a number, so testing the bound alone would answer "not a creep" for a NaN and
     * hand it to the caller as an ordinary carry. */
    if (!isfinite(fell)) {
        return false;
    }
    return fell > 0.0f && fell < CREEPING_MOVER_LIMIT;
}

bool creeping_mover_note(creeping_mover_set_t *set, uint32_t id)
{
    if (set == NULL) {
        return false;
    }
    if (creeping_mover_known(set, id)) {
        return false;
    }
    if (set->count >= CREEPING_MOVER_MAX) {
        return false;
    }
    set->id[set->count++] = id;
    return true;
}

bool creeping_mover_known(const creeping_mover_set_t *set, uint32_t id)
{
    unsigned i;

    if (set == NULL) {
        return false;
    }
    for (i = 0; i < set->count; i++) {
        if (set->id[i] == id) {
            return true;
        }
    }
    return false;
}
