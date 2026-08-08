#ifndef FREE_LOOK_H
#define FREE_LOOK_H

#include "free_look_math.h"
#include "player_sites.h"

#include <stdbool.h>
#include <stdint.h>

/* Horizontal free look: the mouse turns the CAMERA, the body faces where it travels, and the
 * pitch is not touched anywhere.
 *
 * This lives inside enhanced_input rather than in a DLL of its own because it is the same control
 * scheme as mouse look and sideways walking: it replaces the mouse-to-body half of both, it needs
 * the same four resolved sites, and two DLLs would be two claimants for one per-frame mouse bank
 * with no way to arbitrate.
 *
 * The arithmetic is in free_look_math.h and is tested without the game. This header is the engine
 * half only.
 */

/* Reads the configuration. Safe to call before anything is resolved. */
void free_look_load_config(void);

/* Resolves the camera cells and installs the two chained detours.
 *
 * The machinery is installed whether or not the feature is switched on, and that is what makes the
 * control mode a live setting rather than a launch-time choice. While it is off the hooks write
 * nothing at all: the arming gate refuses on the switch itself, so the two camera cells stay the
 * engine's own and the mouse keeps turning the body. The alternative, resolving and detouring at
 * the moment a menu switch is ticked, would patch code from inside a screen loop, and a check box
 * is not a good reason to do that.
 *
 * Must be called after mouse_look_install(): the per-frame drain of the mouse bank is only safe
 * while the bank is live, and whether it is live is not decided until then. Called too early the
 * feature still installs and still works, one substep at a time instead of one frame at a time,
 * and the log says so; it degrades rather than double-counting the mouse.
 *
 * Returns false when anything it cannot run without is missing, in which case nothing has been
 * patched, today's mouse look is untouched, and free look can never be switched on this session. */
bool free_look_install(const player_sites_t *player, bool strafe_enabled);

/* True once the two cells CAN be ours, the sites resolved and the camera update is detoured. It
 * says nothing about whether the feature is switched on. */
bool free_look_is_installed(void);

/* True when free look is the LIVE control mode: installed and switched on. This is the mutual
 * exclusion with the mouse-to-body path, while it is true, nothing else may turn the body with
 * the mouse. */
bool free_look_is_enabled(void);

/* Switches the live control mode. Returns false, changing nothing, when the machinery is not
 * installed: there would be no camera to turn and no honest half of the feature to offer.
 *
 * Switching off drops the arming gate in the same instant, so the very next substep is back to
 * mouse look. There is no restore path to get wrong: the camera update rewrites the recentre rate
 * from its own immediate before every return, so ceasing to write is the whole release, and the
 * engine's own damper swings the camera home behind the player. */
bool free_look_set_enabled(bool enabled);

/* Phase 2, after the original. Returns true when free look has taken this substep, the caller
 * must then apply NO view yaw and NO travel angle of its own, or the mouse would turn the body as
 * well as the camera and the two would cancel out.
 *
 * `strafe` is +1 for right and -1 for left, already inverted per configuration.
 * `stand_mode` gates the forced walk exactly as the sideways walk gates it. */
bool free_look_steer(uint8_t *record, float mouse_step_degrees, float strafe, bool stand_mode,
                     float substep_seconds);

/* Phase 7, BEFORE the original: turn the body toward the direction phase 2 asked for. The write
 * is not undone afterwards, unlike the sideways walk's travel offset this heading change is
 * real and every downstream consumer must see it. Does nothing unless free look took the substep. */
void free_look_integrate(uint8_t *record, float substep_seconds);

/* True while the trigger is held: the body is pointed at the camera and the feet strafe relative
 * to it, which is exactly what free look OFF does. Nothing is twisted in that state, because the
 * weapon already points where the player is looking. */
bool free_look_aim_stance(void);

#endif /* FREE_LOOK_H */
