/* camera_follow.h: a passive camera follow for strafing, the thing a pad expects and this engine
 * does not do.
 *
 * Why it is needed is a fact about strafe rather than about the camera. strafe_walk.c does
 * not turn the player's heading; that is its whole design. It sets the forward bit and the drive
 * the engine itself would write and then turns the direction the walk comes out in, so the clips,
 * the footsteps and the collision all stay the engine's own. The heading therefore never moves
 * while you strafe.
 *
 * The camera branches on exactly that. bapview_updateCam picks between two yaw arms on the signed
 * per-frame change of the body's target heading, and the hard arm runs iff that change is zero.
 * Strafe holds it at zero, so the camera is bolted to your facing. Walk sideways and it does not
 * move at all, which on a keyboard reads as precision and on a pad reads as the camera asleep.
 *
 * ==============================================================================================
 * This file only answers a number. That is the whole correction
 *
 * The first version wrote the camera's yaw offset cell directly and it fought the player, exactly
 * as camera_sites.h warns it would: "writing the offset without freezing the recentre gives a
 * camera that slides home under the player's hand". The engine's own recentre drags that cell
 * toward zero every frame; pushing it the other way every frame is two writers on one value and it
 * feels like it.
 *
 * The other half of that same warning rules out the obvious repair: "freezing the recentre without
 * the exemption signals leaves a scripted camera permanently rotated". Holding this cell correctly
 * needs the recentre frozen AND authored camera regions handled, and free_look_camera.c already
 * does both, along with the arm select and every release condition. Building a second copy beside
 * it would be the largest piece of duplication in this directory.
 *
 * So nothing here touches the engine. This file damps an angle and answers it, and
 * free_look_camera.c drives the camera with it through the path it already owns.
 */
#ifndef ENHANCED_INPUT_CAMERA_FOLLOW_H
#define ENHANCED_INPUT_CAMERA_FOLLOW_H

#include <stdbool.h>

/* Off unless the ini says otherwise, and refused outright unless strafe is on.
 *
 * The gate is not tidiness. Without strafe the walk never leaves the heading, so the travel angle
 * is always zero and there would be nothing to follow; input_switches.c refuses a switch whose
 * driver cannot run for the same reason.
 *
 * `settle_seconds` is how long 90 % of a gap takes to close in real time, and it is the taste
 * value here. `max_rate_deg_per_second` caps the step so a sudden right angle swings rather than
 * whips. */
/* `strength` is the SHARE of the travel angle the camera takes, and `max_degrees` the ceiling
 * on the result. Both exist because following the angle outright does not read as a passive
 * camera at all: the walk's travel angle reaches a right angle on a held sidestep, and a
 * camera that goes all the way there has turned the sidestep into a turn. The stick is
 * heading-relative, so the direction that moves the player sideways ON SCREEN rotates as the
 * view swings, the player corrects, the correction moves the travel angle, and the view swings
 * further. Nothing in the engine is in conflict there and no cell is contended; the loop
 * closes through the player's hands and reads as fighting. Taking a third of the angle and
 * stopping well short of the point where the mapping inverts leaves the drift readable as
 * drift. */
void camera_follow_configure(bool enabled, bool strafe_enabled, float settle_seconds,
                             float max_rate_deg_per_second, float strength,
                             float max_degrees);

/* One damper step, once per SUBSTEP, taken from phase 7. The damper is time-based, so the feel
 * is the same either way, but the clock matters to anyone reading the rate against it.
 *
 * `travel_degrees` is how far the walk is turned off the heading, zero when the stick is centred,
 * so releasing it is a drift home rather than a separate case. */
void camera_follow_step(float travel_degrees, float frame_seconds);

/* How far off the interpolated heading the camera should sit right now. Zero when this is off, so
 * a caller never has to ask twice. */
float camera_follow_offset_degrees(void);

/* True when this should be driving the camera: on, and free look is not, since free look owns the
 * same hold and the two must never both want it. */
bool camera_follow_wants_camera(void);

/* Forget the accumulated angle, for a release or a level change. */
void camera_follow_reset(void);

#endif /* ENHANCED_INPUT_CAMERA_FOLLOW_H */
