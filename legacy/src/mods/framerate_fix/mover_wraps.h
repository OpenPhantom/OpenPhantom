/* mover_wraps.h: has this mover just run off the end of its track and back to the start.
 *
 * A free running track runs its pose up to the track length and then jumps back to the start, and
 * that step is a jump however small the resulting angle looks. A mover that has just done it is
 * drawn unblended for one step, because its two samples sit at opposite ends of the track and a
 * blend between them sweeps it across the level.
 *
 * The verdict is a drop of more than half the track length. A wrap subtracts a whole track and a
 * reversal subtracts at most one substep of travel, so half a track lies between them.
 *
 * That argument has a floor. It separates the two only while half the track is larger than one
 * substep of travel, so a mover on a short enough track has every ordinary reversal read as a
 * wrap and is drawn unblended each time it turns round. Nothing in the shipped levels is close to
 * that floor, and it is written down because the arithmetic gives no sign when it is crossed: the
 * mover simply stops being smoothed and looks like the fault this file exists to avoid.
 */
#ifndef MOVER_WRAPS_H
#define MOVER_WRAPS_H

#include <stdbool.h>

/* `track` of zero or less means the mover has no track to wrap, so nothing is a wrap. */
bool mover_wraps_is_wrap(float pose_before, float pose_after, float track);

#endif /* MOVER_WRAPS_H */
