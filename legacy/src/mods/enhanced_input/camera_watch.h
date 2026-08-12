#ifndef CAMERA_WATCH_H
#define CAMERA_WATCH_H

#include "camera_sites.h"

#include <stdbool.h>

/* Notices a camera that swings on its own, and says which half of the arithmetic did it.
 *
 * Why this exists as an instrument rather than as a fix. A user report of "the camera sometimes
 * whips right round, only while walking straight, and I cannot see a pattern" has been chased twice
 * from the wrong end. The first attempt blamed the mouse limiter, and the field log then showed the
 * bolt that would have caught such a sample firing exactly ZERO times. Guessing again would cost
 * another round trip.
 *
 * The camera's yaw is one addition and the engine performs it in one place:
 *
 *     cameraYaw = wrap360( interp(headPrevious, headCurrent, substepAlpha) + cameraYawOffset )
 *
 * plus, on the frames mouse look leads the camera, a term this DLL adds after that composition.
 * So a jump has exactly three possible sources, and one line printed at the instant it happens
 * separates them for good:
 *
 *   * the INTERPOLATED HEADING moved, the body turned, or the two-deep history is not two clean
 *     substep samples, or the alpha left (0,1] and the term extrapolated;
 *   * the OFFSET moved, something wrote the cell: free look, the engine's own recentre, or
 *     another mod;
 *   * the VIEW LEAD moved, which is the hand, and which is the only one of the three that is
 *     supposed to move on an ordinary frame.
 *
 * It is quiet by construction. It prints only when a single frame moves the camera further than a
 * threshold no ordinary frame reaches, and it stops after a fixed number of lines, so a healthy
 * session logs nothing at all and a pathological one cannot fill a disk.
 */

/* Reads its own configuration and remembers the cells. `sites` must be fully resolved; the watch
 * silently does nothing when the camera object could not be found, because there is nothing to
 * watch and saying so once per frame would be worse than silence. */
void camera_watch_install(const camera_sites_t *sites);

/* Call once per rendered frame IMMEDIATELY BEFORE the camera update runs, and once IMMEDIATELY
 * AFTER it. The split is not tidiness, it is the difference between a line that composes and a line
 * that does not: the camera update REWRITES the yaw offset at its own tail, so an offset read after
 * it is not the offset the yaw was built from. The first call records the inputs the update is
 * about to use; the second reads the yaw it produced and prints them together, so
 * `wrap360(interp + offset) == yaw` is a check the reader can actually perform. */
void camera_watch_before_update(void);
void camera_watch_sample(void);

/* The degrees mouse look's per-frame view lead added to the drawn yaw after the update composed it,
 * or zero on a frame that added none.
 *
 * Without it this instrument would report its own colleague as the third writer it exists to catch:
 * the composition it prints would be short by exactly the lead, on every frame the hand is moving,
 * and the reader would be sent looking for a mod that is not there. */
void camera_watch_note_lead(float degrees);

#endif /* CAMERA_WATCH_H */
