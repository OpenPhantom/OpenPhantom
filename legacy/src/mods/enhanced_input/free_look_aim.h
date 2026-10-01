/* free_look_aim.h: the shot under free look, on the three detours of the attack path.
 *
 * Taken out of free_look.c along a seam of its own when that file reached the hard limit. The
 * body faces where the feet go under free look, so a shot built along the body would leave along
 * the travel and not the look; these three detours, on Plr_StartFire, the fire handler and
 * Plr_AutoAim, arm the aim snap and resolve the engine's target lock into the body's frame. The
 * account of each, and of the order they install in, is with the code.
 */
#ifndef FREE_LOOK_AIM_H
#define FREE_LOOK_AIM_H

#include "free_look_internal.h"

/* Places the three detours, each optional and each failure a named degraded mode, and keeps the
 * state pointer for the hooks. Called once from free_look_install after the camera half stands. */
void free_look_aim_install(free_look_state_t *state);

#endif /* FREE_LOOK_AIM_H */
