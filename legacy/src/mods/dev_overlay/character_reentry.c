/* character_reentry.c: the lock itself. The reasoning is in character_reentry.h. */
#include "character_reentry.h"

#include <stddef.h>

bool character_reentry_enter(character_reentry_t *lock)
{
    if (lock == NULL || lock->depth != 0u) {
        return false;
    }
    lock->depth = 1u;
    return true;
}

/* A leave on a lock that is not held is not an error and is not counted down past zero. A hook
 * that took an early return before it entered would otherwise leave the counter negative and the
 * next real entry would be refused forever, which is the failure this exists to prevent rather
 * than a new way of producing it. */
void character_reentry_leave(character_reentry_t *lock)
{
    if (lock != NULL && lock->depth != 0u) {
        lock->depth = 0u;
    }
}

bool character_reentry_is_held(const character_reentry_t *lock)
{
    return lock != NULL && lock->depth != 0u;
}
