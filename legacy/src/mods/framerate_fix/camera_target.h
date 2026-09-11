/* camera_target.h: the camera's two substep samples stay two substeps while the player rides.
 *
 * The camera follows a PAIR of samples of the player's position, the one from the last substep and
 * the one before it, and once a rendered frame it interpolates between them with the substep
 * alpha.
 * That is the engine's own smoothing and it is right. The pair is rotated by one setter,
 * bapview_setCamTarget at 0x004184CC: previous takes current, current takes the new position.
 *
 * The player tick calls that setter twice in a substep whenever the player stands on a mover. The
 * rider carry feeds it straight after moving him, and the ground publish a few phases later feeds
 * it again with the same position. Two rotations with one value leave previous equal to current,
 * the interpolation returns the newest sample, and the camera moves in 32 Hz steps for as long as
 * the ride lasts, smoothed by the anchor ease into a sawtooth. On foot there is one call and the
 * pair is sound.
 *
 * Measured on the Coruscant platform at 72 frames a second: the platform's drawn position advanced
 * 0.0205 units every frame, the two camera samples were identical on every frame, and the camera
 * advanced 0.014 on frames with a substep in them and 0.026 on frames without. The platform is
 * the object that moves with the camera, so it is where a camera wobble shows; the background
 * pans too fast to show it and the riders wobble with the camera. At the authored 30 fps a substep
 * and a frame were the same thing and none of this could be seen, which is how it shipped.
 *
 * The repair lets the pair rotate once per substep. A second call inside the same substep still
 * updates the current sample, the heading, the turn rate and the ground block as the engine's own
 * body does; only the previous sample and the previous heading are put back to what they were
 * before the call. Nothing else the setter writes changes and no other reader is touched.
 */
#ifndef CAMERA_TARGET_H
#define CAMERA_TARGET_H

#include <stdbool.h>

/* Installs the once-per-substep rule, or declines with a reason in the log. Needs the substep
 * counter, which framerate_fix resolves before this runs. */
void camera_target_install(bool enabled);

#endif /* CAMERA_TARGET_H */
