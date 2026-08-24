/* projectile_cleanup_fix.h: a safety net for the engine's own generic ballistic-physics list, so a
 * settled entry that never gets cleaned up cannot accumulate forever.
 *
 * FUN_004524b9 (0x004524b9), the list's own per-tick update loop: every entry is removed one of two
 * ways, an external "please clean me up" flag (bit 0x40000000 of the entry's own flags word), or
 * immediately on its first hit, but ONLY for an entry that does not bounce/persist on impact (flag
 * bit 0x2 clear). Bit 0x40000000 is set from exactly one place outside this function: FUN_00454aa1
 * (0x00454aa1), an event-driven "on contact" handler this function itself registers, fired by
 * various collision/physics routines elsewhere in the binary. It is not a per-tick poll of any
 * kind; it only ever runs in response to an actual collision event.
 *
 * THE ACTUAL MECHANISM, found by a later, deeper investigation than the one that first closed this
 * field report: a persisting entry that collides has a probabilistic "stick" outcome (a random roll
 * at 0x00452c8c against a threshold at 0x004a8790). On a stick, FUN_004524b9 zeroes BOTH the
 * entry's velocity (+0x2c/+0x30/+0x34) AND its acceleration (+0x38/+0x3c/+0x40), and clears the
 * persist bit. Because the next tick's candidate position is computed purely from velocity and
 * acceleration, zeroing both means the entry's position never changes again, so it never collides
 * again, so it never generates the contact event that is the only thing that can flag it for
 * removal. It goes permanently inert. This is an EVENT-STARVATION bug, not a position-staleness
 * check: nothing in the traced call graph (FUN_004524b9, FUN_0045492c, FUN_00453cd2, FUN_004535af,
 * FUN_00454aa1) compares a remembered position against a current one, and nothing there treats a
 * moving surface differently from a static one. An earlier draft of this comment guessed at a
 * position-unchanged-for-N-seconds check as the cause; that guess is refuted by this later reading
 * of the actual code and is recorded here only so the correction is visible, not carried forward.
 *
 * One piece is still unresolved: FUN_004524b9 also invokes a per-type callback (entry+0xC0, sourced
 * from the spawn template table at &DAT_004b5710 + type*0x44 + 0x3C) unconditionally every tick for
 * every entry. Which concrete function is installed there for this debris type, and whether it
 * plays any further role, was not traced.
 *
 * Measured live: entries were observed to accumulate to 44-57 during a single fight at the two lift
 * platforms this field report is originally about, instead of draining within a second or two the
 * way the same combat does everywhere else in the level. This was first shipped scoped to just the
 * five placements it was field-confirmed at, on the reasoning that the mechanism's not being
 * moving-surface-specific was plausible but unconfirmed. A later full playthrough refuted that
 * caution directly: the list piled up to 89 entries at multiple points scattered across the level,
 * correlating with 80 measured hitches in one session, at droid deaths nowhere near either lift.
 * The mechanism was never actually lift-specific; the fix below is scoped to the whole list now
 * because the field report it was scoped against turned out to be the whole level.
 *
 * THE FIX. Rather than repair the starvation at its source (a change to shared physics/collision
 * code used by every ballistic effect in the game, including live blaster bolts, and not worth the
 * risk without first resolving the callback above), this sets the SAME removal flag FUN_004524b9
 * already watches for, directly, on any list entry that BOTH persists on impact (flag bit 0x2 set,
 * the same flag that is the whole reason an entry can get stuck in the first place) AND is old
 * enough (0.1s) that no legitimate settled debris would still be waiting for a collision that is
 * never coming. The engine's own removal code, proven correct and already running, never touched,
 * does the rest on its very next pass over the list.
 *
 * THE PERSIST CHECK IS NOT OPTIONAL. Widening the fix from five known placements to the whole list
 * was first tried with only the age check, no persist-bit check at all, on the reasoning that "a
 * blaster bolt has never been observed to need more than a couple of seconds" so a 0.1s threshold
 * seemed generous. That reasoning was never actually tested against a live bolt: the 0.1s value
 * was tuned entirely while the fix was ALSO gated by position, and a flying bolt leaves a 12-unit
 * radius in far less than 0.1s regardless of its own lifetime, so the age check alone had never
 * once been exercised against something still in flight. Without the position gate, EVERY
 * projectile on the list, live blaster bolts included, is normally still there and still flying
 * well past 0.1s of age, and the age-only version force-removed all of them a tenth of a second
 * after creation. Field-reported directly: enemies could no longer hit the player, because their
 * own shots vanished just past the muzzle. The persist-bit check restricts this fix to exactly the
 * entries that can actually get stuck, the same category the field report was always about, and
 * leaves ordinary projectiles, which are already removed correctly on their first hit, completely
 * untouched regardless of age. */
#ifndef PROJECTILE_CLEANUP_FIX_H
#define PROJECTILE_CLEANUP_FIX_H

#include <stdbool.h>

void projectile_cleanup_fix_install(bool enabled);

#endif /* PROJECTILE_CLEANUP_FIX_H */
