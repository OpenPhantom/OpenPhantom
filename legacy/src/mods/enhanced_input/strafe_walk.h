#ifndef STRAFE_WALK_H
#define STRAFE_WALK_H

#include "player_record.h"

#include <stdbool.h>
#include <stdint.h>

/* Walking sideways, by driving the engine's own walk and then turning the direction it comes out
 * in. Nothing here moves the player directly. */

/* The angle the walk is turned off the body's heading, in degrees, positive to the LEFT.
 *
 *   `strafe`     +1 right, -1 left, 0 none
 *   `forward`    the player's OWN forward axis: +1, -1 or 0, 0 even when a walk is about to be
 *                forced on their behalf, which is what makes a lone sideways key a right angle
 *   `drive_sign` +1 when the drive is forward, -1 when it is backward
 *
 * Pure, and one of the two pieces of this DLL that can be checked without the game running. */
float strafe_walk_travel_offset(float strafe, float forward, float drive_sign);

/* One damper step, and the second piece that is checkable offline.
 *
 * `current` moves toward `target` so that 90 % of any gap is closed in `settle_seconds` of REAL
 * time, whatever the substep is worth, which is why the substep is an argument and not a
 * compile-time constant. The step is additionally limited to `max_rate_deg_per_second` of travel,
 * so a large gap cannot produce a first-substep spike.
 *
 * Returns `target` exactly once the remaining gap falls inside the dead band, so the value lands
 * on its target and never hunts around it.
 *
 * `settle_seconds <= 0` means "no damping": the target is returned unchanged.
 * A substep that is not positive returns `current`: no time passed, so nothing moves. */
float strafe_walk_damp_step(float current, float target, float substep_seconds,
                            float settle_seconds, float max_rate_deg_per_second);

/* The engine entry the body turn goes through, whether the body is turned at all, and how the
 * angle is damped. Passing NULL for the entry leaves the walk working with the body left facing
 * the way it always did. */
void strafe_walk_bind(set_node_yaw_fn_t set_node_yaw, bool turns_body,
                      float settle_seconds, float max_rate_deg_per_second);

/* Tell the engine a forward walk is under way, in its own two fields and with its own arithmetic:
 * the forward bit the clip selector branches on, and the drive Plr_Steer writes for a fully
 * deflected axis. Everything else, the walk and run clips, the footsteps, the speed caps and
 * their ramp, the acceleration and the collision, then follows for free, because it is the
 * engine walking rather than this DLL shoving.
 *
 * `clear_backward` additionally takes the backward bit off. Only a control scheme that turns the
 * body to face its travel may ask for that; see the reason at the implementation. */
void strafe_walk_force_forward(uint8_t *record, bool clear_backward);

/* Phase 2, after the original: tell the engine a walk is under way when only a sideways key is
 * held, turn the body to face the way it will travel, and answer the angle phase 7 must send the
 * displacement out at. The answer is the DAMPED angle, and the same number reaches the body and
 * the displacement, the feet have to go where the body points. */
float strafe_walk_drive(uint8_t *record, float strafe, float substep_seconds);

/* Either thunk, on a substep that does not drive the walk: bring the latched body angle home.
 *
 * The root node is a latch, and the walk is driven in Stand only, so without this the last angle
 * written in Stand stays on the model for the whole of a jump, a sabre swing or a swim. Call it on
 * every substep that does not call strafe_walk_drive; it does nothing at all once the angle has
 * reached zero, so it never writes to a node it does not already own.
 *
 * Returns true while the angle is still non-zero, i.e. while it still has to be called.
 * A NULL record drops the angle without writing: there is no body left to write to. */
bool strafe_walk_release(uint8_t *record, float substep_seconds);

/* Forget the latched angle without writing anything. For a setting change, where the caller has
 * decided that the feature stops here. */
void strafe_walk_reset(void);

/* Phase 7, after the original: put heading and the facing vector back to what the engine itself
 * would have written, undoing the offset the displacement was built with. */
void strafe_walk_restore_heading(uint8_t *record, float heading_before);

#endif /* STRAFE_WALK_H */
