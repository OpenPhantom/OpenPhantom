/* cheats_openphantom.h: the four codes this project adds - unlimited ammunition, unlimited
 * health, no fog, and free camera.
 *
 * The first two work the same way and it is the smallest way there is. The engine spends
 * ammunition and applies damage through one short function each, and while a cheat is on its
 * detour returns without calling the original. Nothing is written into the player's record, no
 * counter is topped up and no timer is held, so switching a cheat off leaves the game in a state
 * it could have reached by itself.
 *
 * That matters more than it sounds. Refilling ammunition every frame would fight the pickup code,
 * change what the HUD flashes and survive a save; declining to subtract does none of those.
 *
 * No fog is a different shape, because there is nothing to decline: fog is not spent, it is a bit
 * in the loaded level's own record, read fresh every frame by the renderer rather than cached
 * anywhere this project could detour instead. See cheats_no_fog.c for why it is a per-frame force
 * rather than a single write, and why turning it back off does not try to restore what a level
 * authored.
 *
 * Free camera is a different shape again, and does not move the player at all: it freezes the
 * whole simulation (one flag the engine's own fixed-timestep driver already checks every frame,
 * found rather than added) and drives the camera object directly through a chained detour on its
 * own per-frame update, the same site enhanced_input's free-look feature already detours. An
 * earlier attempt at this - noclip, letting the player walk through walls and fly - kept hitting
 * player-physics bugs (falling through unmodelled floors, a ledge pre-check, the mode-dispatch
 * that gates the collision hook, a pitch-redirect attempt that sank the player into ordinary
 * floors) that a camera untethered from the player's own state machine does not have, because the
 * player is not moving at all. Free camera replaced it outright rather than living alongside it.
 * See cheats_openphantom.c's own site comment for the byte evidence.
 *
 * Free camera's own mouse look claims the cursor for as long as it is on, which locks the player
 * out of both the dev panel and the game's own pause menu at once - there is no cursor left to
 * click either with. A bound exit hotkey is the way out: a physical key, read directly the same
 * way E/Q already are, that only means anything while free camera is on, and turns it off on its
 * own without needing the mouse at all. See the panel's own hotkey row (OVERLAY_ROW_HOTKEY in
 * overlay_model.c) for how it gets bound. Free camera refuses to turn on at all until one is
 * bound - see cheats_openphantom_toggle()'s own gate - because turning it on without one is a
 * door with no handle on the inside.
 */
#ifndef CHEATS_OPENPHANTOM_H
#define CHEATS_OPENPHANTOM_H

#include <stdbool.h>
#include <stdint.h>

typedef enum cheats_own_id {
    CHEATS_OWN_UNLIMITED_AMMO = 0,
    CHEATS_OWN_UNLIMITED_HEALTH,
    CHEATS_OWN_NO_FOG,
    CHEATS_OWN_FREECAM,
    CHEATS_OWN_COUNT
} cheats_own_id_t;

/* Places both detours. Answers false if neither could be placed; a single one that resolved is
 * kept and reported, because half of this feature is still worth having and the log says which
 * half. Idempotent. */
bool cheats_openphantom_install(void);

/* The name shown in the panel, and NULL for an id out of range. */
const char *cheats_openphantom_name(cheats_own_id_t id);

/* Whether the site this cheat needs actually resolved. A cheat that never armed is shown but
 * cannot be switched, which is honest: the alternative is a row that ticks and does nothing. */
bool cheats_openphantom_is_available(cheats_own_id_t id);

bool cheats_openphantom_is_on(cheats_own_id_t id);

/* Flips one and answers the new state. A cheat whose site did not resolve stays off, and free
 * camera specifically also stays off with no exit hotkey bound - see cheats_openphantom.c. */
bool cheats_openphantom_toggle(cheats_own_id_t id);

/* The virtual key that turns free camera back off while it is on. 0 means unbound, in which case
 * cheats_openphantom_toggle() refuses to turn CHEATS_OWN_FREECAM on at all. */
int32_t cheats_openphantom_freecam_hotkey(void);
void cheats_openphantom_freecam_set_hotkey(int32_t virtual_key);

#endif /* CHEATS_OPENPHANTOM_H */
