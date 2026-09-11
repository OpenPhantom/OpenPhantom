/* mover_wraps.h: has this mover just run off the end of its track, and did that move it.
 *
 * A free running track runs its pose up to the track length and then back to the start. The first
 * question is whether that happened, and the answer is a drop of more than half the track length:
 * a wrap subtracts a whole track and a reversal subtracts at most one substep of travel, so half a
 * track lies between them. That argument has a floor. It separates the two only while half the
 * track is larger than one substep of travel, so a mover on a short enough track has every
 * ordinary reversal read as a wrap. Nothing in the shipped levels is close to that floor.
 *
 * The second question is what the wrap did to the geometry, and it is a different question with a
 * different answer for two kinds of track. A track that RESETS, a piston running out and snapping
 * back, puts its two samples at opposite ends of the path, and a blend between them sweeps the
 * mover backwards across a substep; it has to be drawn unblended for that one step. A track that
 * LOOPS, a rotor or a belt whose end is its beginning, moves across the wrap exactly as it moves
 * on any other tick, and drawing it unblended is a hitch on the fastest moving thing in the scene,
 * once every loop. Measured on Coruscant: eight movers on a 29 unit track wrapping four times a
 * second each, twenty-three wraps a second between them, every one held raw for a substep.
 *
 * A fixed geometric threshold cannot tell the two apart, because a small reset and a large loop
 * step look alike. Each mover measured against itself can: across the wrap a loop takes a step
 * the size of its ordinary step and turns by its ordinary angle, and a reset does not. So the
 * verdict compares the wrap's step and turn with the mover's own last ordinary tick, and calls it
 * a reset when either differs by more than half of the ordinary figure, with a floor under each
 * so that a stationary or slow mover is not judged on noise.
 */
#ifndef MOVER_WRAPS_H
#define MOVER_WRAPS_H

#include <stdbool.h>

/* `track` of zero or less means the mover has no track to wrap, so nothing is a wrap. */
bool mover_wraps_is_wrap(float pose_before, float pose_after, float track);

/* Below these an ordinary step is too small to compare against, and the wrap has to move or turn
 * by at least this much on its own to count as a reset: half a world unit, ten degrees. */
#define MOVER_WRAP_STEP_FLOOR   0.5f
#define MOVER_WRAP_ANGLE_FLOOR 10.0f

/* Whether the wrap moved the mover unlike its own ordinary tick. Steps are translation lengths in
 * world units; angles are the turn of the first basis row in degrees, which the caller derives
 * from the row cosine. `have_ordinary` false means no ordinary tick has been seen for this
 * subnode yet, and then the answer is a reset, because an unblended step is the safe side. */
bool mover_wraps_is_reset(float wrap_step, float wrap_angle,
                          float ordinary_step, float ordinary_angle, bool have_ordinary);

/* The turn between two unit rows, in degrees, from their dot product. Clamped, so a cosine a hair
 * outside [-1, 1] from rounding is 0 or 180 rather than not a number. */
float mover_wraps_angle_degrees(float cosine);

#endif /* MOVER_WRAPS_H */
