/* rider_floor.h: keeps a character being carried by a mover from being carried into the floor.
 *
 * The second route to the same wrong outcome. This module already stops a character being pushed
 * down through the floor by a CONTACT. This is the same end state reached another way, and it was
 * found in the opening cutscene of the final level, where two characters standing near a mover
 * descend about five centimetres into the ground over the two seconds it runs and stay there until
 * the script repositions them.
 *
 * What is actually happening, measured rather than reasoned. A hardware write watch named the
 * rider carry as the writer. That call does, once per level tick:
 *
 *     riderPos = R * (riderPos - pivot) + pivot + translationDelta
 *
 * and the numbers coming out of it are real work rather than noise: the mover's pose advances
 * about 0.45 of its 29 units of travel every tick, its rotation delta is exactly zero, and its
 * translation delta is a steady -0.0008 in Z. Over the full run that is the five centimetres,
 * and the carry is faithfully applying it. Nothing afterwards puts the character back: the tick
 * re-probes the floor immediately after the carry, but only stores the result, so the descent
 * stands.
 *
 * Why the carry is not suppressed outright. A genuinely descending platform produces those same
 * numbers, and refusing every translation would freeze every rider on every lift in the game. The
 * mover's type is not the test either: the one at fault is a crusher, and so are about fifteen
 * shipped movers with walkable faces that descend, one of them a ninety unit lift.
 *
 * What separates them is the rate. The one at fault creeps down at 0.0008 of a unit a tick and the
 * slowest genuine platform in any shipped level moves forty times faster, with nothing in between.
 * So the carry hook lets the engine's body run, and when the mover is a crusher whose rider moved
 * DOWN by less than the creeping limit it puts the rider's height back and remembers that mover.
 * The ground snap is the second route to the same floor: it would settle the character straight
 * back onto the crusher's polygon, so it is declined for a character standing on a remembered
 * mover while the floor is below their feet. Both halves or neither: either alone leaves the
 * fault in place.
 *
 * An earlier version guarded a different invariant, refusing to leave a rider below the floor
 * their own contact had selected. It never fired: the floor under them descends with them, so by
 * that measure nothing was ever wrong. The contact's signed floor height is still used, but only
 * for the snap's direction test.
 */
#ifndef GROUND_CLIP_FIX_RIDER_FLOOR_H
#define GROUND_CLIP_FIX_RIDER_FLOOR_H

#include <stdbool.h>

/* Optional, like the contact guard beside it: a failure is a named degraded mode rather than a
 * refusal, because the other half of this module is worth having on its own. */
bool rider_floor_install(void);

#endif /* GROUND_CLIP_FIX_RIDER_FLOOR_H */
