/* free_look_follow.h: the passive camera, one damped step toward the body per drawn frame.
 *
 * Lifted out of free_look_camera.c, which publishes free look's wanted yaw and does nothing else.
 * This decides what that yaw should drift toward when the player is not driving it, which is a
 * separate question with its own clock, its own settings and its own reasons.
 */
#ifndef ENHANCED_INPUT_FREE_LOOK_FOLLOW_H
#define ENHANCED_INPUT_FREE_LOOK_FOLLOW_H

#include "free_look_internal.h"

/* Called once per rendered frame from the camera update, after the player's own look input has been
 * taken and before the offset is computed, with the interpolated heading that frame is publishing
 * against. Moves `state->camera_yaw` alone; writes no engine cell. */
void free_look_follow_step(free_look_state_t *state, float interpolated);

#endif /* ENHANCED_INPUT_FREE_LOOK_FOLLOW_H */
