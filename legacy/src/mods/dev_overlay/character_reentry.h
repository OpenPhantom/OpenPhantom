/* character_reentry.h: a lock a hook holds while it is inside itself.
 *
 * ==============================================================================================
 * Why a hook needs one at all
 *
 * A detour replaces a function's first bytes with a branch, so the ADDRESS of that function is no
 * longer a way to reach the function: it is a way to reach the hook. A module that detours a draw
 * entry point and then draws something of its own through that same entry point calls itself, and
 * the only thing between that and a stack overflow is whatever state it happens to test on the way
 * in. That state lives in the module, is written by the module, and can be cleared by any other
 * path through the module while the outer call is still running.
 *
 * This is the same test, held at the hook instead, where the recursion actually is. It answers one
 * question and it answers it about the call rather than about the feature: am I already inside
 * this hook.
 *
 * ==============================================================================================
 * No interlocked operations, and that is a statement about this engine
 *
 * The engine draws, simulates and reads input on ONE thread; the two threads this project adds are
 * the movie loader and the music timer, and neither of them can reach a render hook. So the lock is
 * an ordinary counter. Making it interlocked would not make it correct on a second drawing thread,
 * because everything the hooks it guards go on to touch is unsynchronised anyway; it would only
 * make the claim look stronger than it is.
 */
#ifndef CHARACTER_REENTRY_H
#define CHARACTER_REENTRY_H

#include <stdbool.h>
#include <stdint.h>

/* Zero initialised means not held, so a static instance needs no constructor. */
typedef struct character_reentry {
    uint32_t depth;
} character_reentry_t;

/* True when the caller now holds the lock and must call character_reentry_leave(). False means the
 * lock was already held, the caller is inside itself, and it must do nothing but unwind. */
bool character_reentry_enter(character_reentry_t *lock);

/* Gives it back. Safe on a lock that is not held, because a hook that unwinds through an early
 * return is more likely than a hook that counts its own brackets correctly. */
void character_reentry_leave(character_reentry_t *lock);

/* Whether the lock is held, for a module that wants to answer differently rather than not at
 * all. */
bool character_reentry_is_held(const character_reentry_t *lock);

#endif /* CHARACTER_REENTRY_H */
