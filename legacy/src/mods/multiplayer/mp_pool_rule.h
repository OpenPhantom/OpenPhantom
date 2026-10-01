/* mp_pool_rule.h: whether one more slot may be taken out of a pool, as a rule of its own.
 *
 * Pure, so that every table that decides on a pool asks the same question the engine pools' latch
 * asks (mp_pool.h), without a game in the process: the host's table of NPC copies decides from a
 * census, the latch from the engine's own counts.
 */
#ifndef MULTIPLAYER_MP_POOL_RULE_H
#define MULTIPLAYER_MP_POOL_RULE_H

#include <stdbool.h>
#include <stdint.h>

/* True when taking one slot still leaves at least `reserve` free. A live count at or past the
 * capacity is refused as well: that is a corrupt walk, not a full pool, and neither deserves a
 * slot. */
bool mp_pool_may_take(uint32_t live, uint32_t capacity, uint32_t reserve);

#endif /* MULTIPLAYER_MP_POOL_RULE_H */
