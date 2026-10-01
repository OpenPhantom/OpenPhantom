/* mp_lifecycle.h: hulls over the five player lifecycle functions that bypass the bank swap.
 *
 * A player pointer swap reaches 110 functions at once, and exactly five do not follow it, because
 * they address the hero block by its absolute address: the savegame writer and reader, the
 * respawn, the spawn (whose block clear is the dangerous one: it wipes bank 0 whatever the
 * pointer says) and the despawn. Each gets a chained detour whose only job is to bracket the
 * original with the bank's block loan, so that a call landing while a bank is swapped in operates
 * on the active bank coherently instead of on two players at once.
 *
 * While no bank is swapped in, every hull is a pass-through, and today that is every call there
 * is: a byte census found no path from the phase pipeline into any of the five, and their five
 * VAs appear nowhere in the image as data, so all their callers are ten known call sites in four
 * lifecycle and script functions, which all run with bank 0 active. The hulls are what makes a
 * deliberate call on a swapped bank possible later, when the second body is spawned into one.
 *
 * Every hook keeps the original's calling convention and return value. The savegame pair returns
 * a status word the caller tests, and a hull that dropped it would corrupt saving silently.
 */
#ifndef MULTIPLAYER_MP_LIFECYCLE_H
#define MULTIPLAYER_MP_LIFECYCLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Installs the five hulls. Counts rather than all-or-nothing: a site that did not resolve is
 * reported and skipped, and the report says how many stand. Idempotent. */
size_t mp_lifecycle_install(void);

bool mp_lifecycle_installed(void);

/* Who hears a respawn asked for, with the address the call returns to and the call's own three
 * arguments, before it is begun. A script's warp, the cheats' hero swap and this feature's own
 * re-entry all come through the same function, and only the return address tells them apart; a
 * warp's target is the position and heading it names. `at` is the engine's own three floats, read
 * and not kept. Called from inside the engine, so it counts, notes and at most writes a line.
 * Answers whether the respawn's hull stands, which is whether it will ever be called. NULL hears
 * nobody. */
typedef void (*mp_lifecycle_respawn_listener_t)(uintptr_t caller, int32_t hero, const float *at,
                                                float heading);
bool mp_lifecycle_set_respawn_listener(mp_lifecycle_respawn_listener_t listener);

/* How often each hull was entered, and how often while a bank was swapped in. The second number
 * being nonzero is expected only once deliberate bank calls exist; until then it is a finding. */
void mp_lifecycle_report(const char *why);

#endif /* MULTIPLAYER_MP_LIFECYCLE_H */
