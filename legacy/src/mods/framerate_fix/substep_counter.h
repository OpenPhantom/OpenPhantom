/* substep_counter.h: the engine's substep counter, for anything that has to tell one simulation
 * step from the next.
 *
 * The simulation runs on a fixed 1/32 s step and the game draws once per rendered frame, so
 * several frames usually share a step and, below 32 frames a second, one frame spans several
 * steps. Any feature that remembers something per step needs to know which step it is looking at,
 * and the engine keeps that count itself at the tail of the substep loop.
 *
 * Three features in this DLL need it, and none of them should infer it. The facing latch withholds
 * a completion on one step in N. The rider blend remembers where each object was a step ago, and
 * its first version counted steps by watching the substep alpha drop, which is wrong in both
 * directions: a frame spanning two steps advances the count once, and at exactly 32 frames a
 * second the alpha barely moves, so the drop that marks the boundary may not arrive at all. The
 * camera target pair lets the engine's two samples rotate once per step, however many times the
 * setter is called within it.
 *
 * So the count is read rather than reconstructed, and this file owns the one resolution of it.
 *
 * It is not a pure step index, and a caller that assumed it was would be wrong in a menu. The sim
 * loop advances it once per substep, and swmenu_render at 0x45DC47 advances it again once per
 * frame for as long as a Swift menu is on screen; it is also seeded to 1000 rather than to zero.
 * So it is a monotonic token that happens to count substeps exactly while the game is being
 * played, which is where all three callers use it. Only differences are ever taken, so the seed
 * does not matter, and a caller must be able to survive a difference that is larger than the
 * number of steps that really ran: the statistics window has measured 320 advances over 600
 * frames with the simulation not moving at all.
 */
#ifndef SUBSTEP_COUNTER_H
#define SUBSTEP_COUNTER_H

#include <stdbool.h>
#include <stdint.h>

/* Finds the counter. Safe to call more than once: the second call answers from what the first
 * found, so two features can each ask without either having to know about the other. False means
 * the pattern or its operand did not check out, and it is logged with the reason. */
bool substep_counter_resolve(void);

/* The current step. False means there is nothing to read, which is a separate answer from step
 * zero: a caller that treated an unresolved counter as step 0 would see every step as the same
 * one and never move anything forward. */
bool substep_counter_read(uint32_t *out_step);

/* Where the counter was found, or 0. For a log line that says which cell was used. */
uintptr_t substep_counter_cell(void);

#endif /* SUBSTEP_COUNTER_H */
