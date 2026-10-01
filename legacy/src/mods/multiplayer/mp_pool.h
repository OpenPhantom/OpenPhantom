/* mp_pool.h: the reserve latch, and the only door through which this feature takes a pool slot.
 *
 * The engine's object pool serves every body, bolt, corpse, muzzle flash and severed limb from the
 * same 255 slots, three of the engine's own seven allocator callers dereference a NULL return
 * without checking it, and the assert handler that would have caught them is NULL in the retail
 * build. So an exhausted pool is not a missing effect, it is an access violation inside engine
 * code, and the crash names neither the pool nor whoever took the last slot.
 *
 * The latch in front of that is deliberately one-sided: it can only refuse OUR allocations, never
 * the engine's. A multiplayer body is the one load this tree adds on purpose, so it is the one
 * load that can politely decline while the engine's own shots and gibs keep being served. The
 * reserve is the number of slots the latch insists stay free for the engine after any allocation
 * of ours.
 *
 * The reserve is a risk reducer and not a proof, and the header is the place to say so: the worst
 * measured engine demand is 252 shots arising in a single substep, which no reserve on a 255 slot
 * pool can cover. What the latch does guarantee is that when that day comes, the slots this
 * feature holds are bounded and every one of them was checked on the way out.
 */
#ifndef MULTIPLAYER_MP_POOL_H
#define MULTIPLAYER_MP_POOL_H

#include "mp_pool_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 64 is the reserve kept back in the engine's pool, sized against bursts of
 * engine-spawned shots: a burst can want 252 slots in a single substep, and the worst standing
 * demand counted was 508 against the 255 slots with bodies and severed pieces kept, so on the
 * retail pool no reserve is a proof and this one is sized against ordinary bursts. A quiet level
 * measured 27 of 255 slots live, so 64 still leaves roughly 160 grantable in ordinary play. The
 * maximum keeps at least a quarter of the retail pool usable: a reserve at or past the capacity
 * would refuse every allocation forever and read like a broken build. */
#define MP_POOL_RESERVE_DEFAULT 64u
#define MP_POOL_RESERVE_MAX     192u

/* The decision on its own is mp_pool_may_take (mp_pool_rule.h), pure, so a test can drive it over
 * its boundaries without a game and the other tables that decide on a pool ask the same rule. */

/* Clamps to MP_POOL_RESERVE_MAX and says so when it had to. Call once at install; the default
 * stands without a call. */
void mp_pool_configure(uint32_t reserve);

uint32_t mp_pool_reserve(void);

/* One slot out of the engine's pool, or NULL, and NULL is an answer rather than an accident: the
 * refusal is counted and logged with the occupancy that caused it. `what` names the purpose in
 * that log line. The caller owns the slot until mp_pool_give. */
void *mp_pool_take(const char *what);

void mp_pool_give(void *thing);

/* Slots taken through the latch and not yet given back. */
uint32_t mp_pool_outstanding(void);

uint32_t mp_pool_refusals(void);

/* The gate test for the latch itself: fill the pool to the last slot with the engine's own
 * allocator, watch the latch refuse at zero free and again inside the reserve band, then give
 * everything back and watch an ordinary allocation succeed. Runs entirely inside one call, so
 * nothing engine-side ever sees the pool full. Skips with a warning when the allocator sites did
 * not resolve. */
void mp_pool_provoke_full(void);

void mp_pool_report(const char *why);

#endif /* MULTIPLAYER_MP_POOL_H */
