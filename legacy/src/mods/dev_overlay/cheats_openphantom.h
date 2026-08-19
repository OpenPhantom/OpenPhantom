/* cheats_openphantom.h: the eight codes this project adds - unlimited ammunition, unlimited
 * health, no fog, invincible NPCs, one-shot NPCs, giant player, tiny player, and free camera.
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
 * Invincible NPCs and one-shot NPCs are the enemy-side counterpart to unlimited health, and share
 * one site rather than getting one each: enemy_receiveDamage (0x00433803) is the single function
 * every NPC's health, at character record +0x38, is ever subtracted through. Invincible NPCs
 * declines that subtraction outright, the same "decline, don't top up" shape as the player's own
 * unlimited health. One-shot NPCs cannot decline the same way - the point is to change the outcome,
 * not skip it - so it forces the write to zero instead, which is exactly what the death gate this
 * function feeds (dismemberment.c's own DEATH GATE, reached only when health <= 0) already treats
 * as lethal. See cheats_openphantom.c's own site comment for the byte evidence, and for why both
 * cheats can share one detour instead of needing their own like ammunition and player health do.
 *
 * Giant player and tiny player share one detour on rdThing_Draw, the function that renders any
 * object at all - the player included, called through exactly one of its two callers for ordinary
 * (non-particle) things. Before letting the original run, this file compares the thing being drawn
 * against the player's own (chased fresh off the player-record global each call, not cached, so
 * this needs no rising-edge bookkeeping of its own the way free camera does) and, only for that one
 * thing, calls a small existing engine function that composes a diagonal scale into a transform
 * matrix - the SAME function retail's own shipped code already calls, elsewhere in this exact
 * function, to apply a permanent 3.0x scale to one specific hardcoded model under a specific cheat-
 * flag condition neither of these two cheats touches or depends on. Nothing here reimplements that
 * math; it calls the engine's own routine with this project's own scale instead of retail's fixed
 * one. Purely visual - the render matrix is rebuilt from the player's real position every frame
 * regardless, so nothing needs restoring when either cheat switches off, and neither touches the
 * player's own collision size, which lives elsewhere entirely. Mutually exclusive by construction -
 * see cheats_openphantom_toggle() - so the panel is never showing one as on while the other's own
 * scale is silently the one being applied.
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
    CHEATS_OWN_INVINCIBLE_NPCS,
    CHEATS_OWN_ONE_SHOT_NPCS,
    CHEATS_OWN_GIANT_PLAYER,
    CHEATS_OWN_TINY_PLAYER,
    CHEATS_OWN_FREECAM,   /* MUST stay last - overlay_model.c's row layout relies on it, and its
                            * own _Static_assert fails the build if this ever stops being true */
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

/* Notches scrolled since the last take, positive away from the player - the exact contract
 * overlay_input_take_wheel_delta() keeps. A function pointer rather than calling that function by
 * name: the wheel is only observable through window messages, which is overlay_input.c's own
 * domain and not something this file can poll for itself the way it already does for keys and the
 * cursor, but linking straight to that file would drag its whole message-hook subsystem into
 * anything that links this one, including the unit test built against the real cheat sources (see
 * unittests/CMakeLists.txt's own "never a stub" rule) - a wheel source it would then have no way
 * to provide. NULL until dev_overlay.c wires the real one in once both cheats_openphantom_install()
 * and overlay_input_install() have run, and free camera treats "no source" exactly like "no
 * scrolling happened yet", which is what it already was for every DLL build before this existed. */
typedef int32_t (*cheats_openphantom_wheel_source_fn_t)(void);
void cheats_openphantom_set_wheel_source(cheats_openphantom_wheel_source_fn_t fn);

#endif /* CHEATS_OPENPHANTOM_H */
