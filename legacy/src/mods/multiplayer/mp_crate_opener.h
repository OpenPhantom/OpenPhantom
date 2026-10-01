/* mp_crate_opener.h: whether a call of the engine's opener is a push block sinking.
 *
 * Layer 2. The engine sinks a push block by retyping it and then opening it, through the same
 * opener every door and lift goes through, and the map's hull on that opener would report the
 * sink as a trigger of this player's. A client's copy of the block is still a push block, where
 * the opener has nothing to do, so the event helped nobody, and once push blocks are the host's
 * state a client sinks its copy itself when the host's note says so.
 *
 * Its own file because the map's hull asks it, and the map is built into tests that carry none of
 * the push block binding. mp_crate.c arms it once its calls stand.
 */
#ifndef MULTIPLAYER_MP_CRATE_OPENER_H
#define MULTIPLAYER_MP_CRATE_OPENER_H

#include <stdbool.h>
#include <stdint.h>

/* From now on the question below is answered; before, every opener call is a trigger as ever. */
void mp_crate_opener_arm(void);

/* True, and counted, for an opener call on a push block whose kind is no longer 7. */
bool mp_crate_opener_is_a_sink(uint32_t world, int32_t index);

/* How many opener calls were a sink and not sent. */
uint32_t mp_crate_opener_sinks(void);

#endif /* MULTIPLAYER_MP_CRATE_OPENER_H */
