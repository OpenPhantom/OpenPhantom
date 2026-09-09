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

bool creeping_mover_note(creeping_mover_set_t *set, uint32_t id, const void *mover)
{
    unsigned i;

    if (set == NULL) {
        return false;
    }
    for (i = 0; i < set->count; i++) {
        if (set->entries[i].id != id) {
            continue;
        }
        if (set->entries[i].mover == mover) {
            return false;                     /* the same mover, already being refused */
        }
        /* The same number on a different mover, which means a level has opened since. The old one
         * cannot come back, so its slot is taken rather than a second one spent on the same id;
         * that also keeps a long session from filling the table with dead levels. Reported as new,
         * because on this level it is. */
        set->entries[i].mover = mover;
        return true;
    }
    if (set->count >= CREEPING_MOVER_MAX) {
        return false;
    }
    set->entries[set->count].id    = id;
    set->entries[set->count].mover = mover;
    ++set->count;
    return true;
}

bool creeping_mover_known(const creeping_mover_set_t *set, uint32_t id, const void *mover)
{
    unsigned i;

    if (set == NULL) {
        return false;
    }
    for (i = 0; i < set->count; i++) {
        if (set->entries[i].id == id && set->entries[i].mover == mover) {
            return true;
        }
    }
    return false;
}
