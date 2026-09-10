/* mover_interpolation.h: draw doors, lifts and platforms between the simulation steps.
 *
 * The engine already interpolates every object in its own draw list, and has since 1999. Movers
 * are not in that list. They advance on the world clock, which only the substep loop writes, so at
 * a high frame rate four frames in five draw a door exactly where the previous frame did and the
 * fifth moves it a whole simulation step at once. Their speed is right and their pacing is right;
 * what is missing is the picture in between.
 *
 * Nothing here writes into a record the simulation reads. The drawn matrix is passed to the matrix
 * product as an argument, so an interpolated copy in a local is all that is needed, which is the
 * same convention the engine's own object draw follows.
 */
#ifndef MOVER_INTERPOLATION_H
#define MOVER_INTERPOLATION_H

#include <stdbool.h>

/* `translation_limit` is the furthest a mover may legitimately travel in one simulation step; a
 * larger step is a teleport or a track wrapping round, and is drawn as the jump it is. Zero
 * disables that test and leaves only the rotation guard.
 *
 * `weight_mode` decides WHEN the mover is drawn rather than how far it may move, and the reasoning
 * behind the three answers sits with the arithmetic in mover_weight.h. An unrecognised value takes
 * the default rather than switching the feature off, because a mover drawn at a slightly wrong
 * moment is a smaller fault than a mover that steps. */
/* `wrap_veto` keeps the pose-based track wrap test, which marks a wrapping mover unusable for
 * about two frames. mover_wraps.h has the case against it: for a looping track the wrap is not a
 * discontinuity in space, and a real one is caught by the blend's own geometric guards. */
void mover_interpolation_install(bool enabled, float translation_limit, int weight_mode,
                                 bool wrap_veto);

/* Called once per rendered frame. Emits the instrument line every so often, so that a build which
 * installed but never interpolated anything says so instead of looking like it worked. */
void mover_interpolation_sample(void);

#endif /* MOVER_INTERPOLATION_H */
