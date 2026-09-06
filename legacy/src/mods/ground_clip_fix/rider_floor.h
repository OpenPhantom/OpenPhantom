/* rider_floor.h: keeps a character being carried by a mover from being carried into the floor.
 *
 * THE SECOND ROUTE TO THE SAME WRONG OUTCOME. This module already stops a character being pushed
 * down through the floor by a CONTACT. This is the same end state reached another way, and it was
 * found in the opening cutscene of the final level, where two characters standing near a mover
 * descend about five centimetres into the ground over the two seconds it runs and stay there until
 * the script repositions them.
 *
 * WHAT IS ACTUALLY HAPPENING, measured rather than reasoned. A hardware write watch named the rider
 * carry as the writer. That call does, once per level tick:
 *
 *     riderPos = R * (riderPos - pivot) + pivot + translationDelta
 *
 * and the numbers coming out of it are real work rather than noise: the mover's pose advances about
 * 0.45 of its 29 units of travel every tick, its rotation delta is exactly zero, and its translation
 * delta is a steady -0.0008 in Z. Over the full run that is the five centimetres, and the carry is
 * faithfully applying it. Nothing afterwards puts the character back: the tick re-probes the floor
 * immediately after the carry, but only stores the result, so the descent stands.
 *
 * WHY THE CARRY IS NOT SUPPRESSED. Those same numbers are what a genuinely descending platform
 * looks like, and refusing the translation would freeze every rider on every lift in the game. The
 * two cases cannot be told apart from the delta.
 *
 * WHAT SEPARATES THEM IS THE FLOOR. The ground contact already carries the signed height of the
 * selected floor over the character's feet, positive when the floor is ABOVE them. A rider properly
 * riding a platform keeps that at roughly zero, because the floor they stand on moves with them. A
 * rider being carried into static ground sees it go positive and stay there. So the repair does not
 * need to know why the mover reports what it does; it only has to refuse to leave a character below
 * the floor its own contact selected.
 */
#ifndef GROUND_CLIP_FIX_RIDER_FLOOR_H
#define GROUND_CLIP_FIX_RIDER_FLOOR_H

#include <stdbool.h>

/* Optional, like the contact guard beside it: a failure is a named degraded mode rather than a
 * refusal, because the other half of this module is worth having on its own. */
bool rider_floor_install(void);

#endif /* GROUND_CLIP_FIX_RIDER_FLOOR_H */
