/* cheats_openphantom.h: the nine codes this project adds: unlimited ammunition, unlimited
 * health, no fog, invincible NPCs, one-shot NPCs, giant player, tiny player, jump boost, and free
 * camera.
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
 * unlimited health. One-shot NPCs cannot decline the same way, because the point is to change the
 * outcome, not skip it, so it forces the write to zero instead, which is exactly what the death
 * gate this function feeds (dismemberment.c's own DEATH GATE, reached only when health <= 0)
 * already treats as lethal. See cheats_openphantom.c's own site comment for the byte evidence,
 * and for why both cheats can share one detour instead of needing their own like ammunition and
 * player health do.
 *
 * Giant player and tiny player share one detour on rdThing_Draw, the function that renders any
 * object at all, the player included, called through exactly one of its two callers for ordinary
 * (non-particle) things. Before letting the original run, this file compares the thing being drawn
 * against the player's own (chased fresh off the player-record global each call, not cached, so
 * this needs no rising-edge bookkeeping of its own the way free camera does) and, only for that one
 * thing, calls a small existing engine function that composes a diagonal scale into a transform
 * matrix, the SAME function retail's own shipped code already calls, elsewhere in this exact
 * function, to apply a permanent 3.0x scale to one specific hardcoded model under a specific cheat-
 * flag condition neither of these two cheats touches or depends on. Nothing here reimplements that
 * math; it calls the engine's own routine with this project's own scale instead of retail's fixed
 * one. Purely visual; the render matrix is rebuilt from the player's real position every frame
 * regardless, so nothing needs restoring when either cheat switches off, and neither touches the
 * player's own collision size, which lives elsewhere entirely. Mutually exclusive by construction,
 * see cheats_openphantom_toggle(), so the panel is never showing one as on while the other's own
 * scale is silently the one being applied.
 *
 * Jump boost multiplies the vertical velocity the engine's own jump-entry code writes, rather than
 * reimplementing a jump. There are two sites, not one: mode 6 ("Jump") and mode 7 ("Jedi Jump")
 * each have their own entry function, and both write the same player-record field (+0xB4, right
 * after the already-confirmed +0xB0 ramped-speed field) with the same flat velocity value read
 * from a per-character table; the difference is only which characters route through which
 * function (Obi-Wan and Qui-Gon go through Jedi Jump for an ORDINARY jump, everyone else through
 * plain Jump; see cheats_openphantom.c's own site comments for the byte evidence). Both hooks call
 * the original unconditionally first, since the jump must still happen exactly as retail built it,
 * then, only while this cheat is on, read the velocity the original just wrote and scale it up in
 * place. Gravity integration afterward is untouched and needs no separate handling: a bigger launch
 * velocity fed into the same linear decay simply produces a higher arc. Either site resolving is
 * enough to offer the cheat; if only one does, whichever characters route through the other
 * function jump at their normal height, and the log says which half is covered, the same
 * "half a feature is still worth having" reasoning this file already documents for install().
 *
 * The scale itself is a number, not just a switch, and the dev panel's own row for it (see
 * overlay_model.c) shows the value it would multiply by right now and lets a player click in and
 * type a different one; cheats_openphantom_jump_boost_scale()/_set_scale() below are that row's
 * whole connection to this file, read and written fresh on every panel rebuild the same way every
 * other live value this panel shows already is, never cached anywhere else.
 *
 * A higher jump is also a longer fall, so this cheat also suppresses three things retail's own
 * ground-contact code can do to a long fall, for exactly as long as it is switched on; neither is
 * a cheat of its own, no row, no separate toggle. The first is the fixed ten-point landing damage
 * every ordinary hard landing already risks; it goes through this file's own damage hook rather
 * than around it, so Unlimited health still wins if both happen to be on at once, the same as it
 * already does against everything else that can hurt the player. The second, found only after a
 * field report that a high enough boosted jump was ending in a death screen and a level reload
 * rather than a damage tick: retail ALSO force-kills the player outright, unconditionally and with
 * no health check anywhere in the path, either after two seconds airborne or after falling farther
 * than a second, larger distance ceiling, both fixed thresholds a sufficiently boosted jump's
 * longer, higher fall reaches on its own. A third, found the same way after death stopped and the
 * camera turned out to be the next thing a survived big fall exposed: retail's own dramatic-fall
 * camera, which pitches down to watch the player from above and, because nothing in its own
 * landing path ever expected a fall this big to be survived, never lets go afterward. All three
 * fire from the same "this fall just became significant" transition, all three are suppressed the
 * same way, and all three stop mattering the instant jump boost switches back off. See
 * cheats_openphantom.c's own site comment next to SIG_PLAYER_GROUND_CONTACT for the full
 * mechanism and every call site.
 *
 * Free camera is a different shape again, and does not move the player at all: it freezes the
 * whole simulation (one flag the engine's own fixed-timestep driver already checks every frame,
 * found rather than added) and drives the camera object directly through a chained detour on its
 * own per-frame update, the same site enhanced_input's free-look feature already detours. An
 * earlier attempt at this, noclip, letting the player walk through walls and fly, kept hitting
 * player-physics bugs (falling through unmodelled floors, a ledge pre-check, the mode-dispatch
 * that gates the collision hook, a pitch-redirect attempt that sank the player into ordinary
 * floors) that a camera untethered from the player's own state machine does not have, because the
 * player is not moving at all. Free camera replaced it outright rather than living alongside it.
 * See cheats_openphantom.c's own site comment for the byte evidence.
 *
 * Free camera's own mouse look claims the cursor for as long as it is on, which locks the player
 * out of both the dev panel and the game's own pause menu at once; there is no cursor left to
 * click either with. A bound key is the way out: a physical key, read directly the same way E/Q
 * already are, that only means anything while free camera is on, and ends the flight on its own
 * without needing the mouse at all. It also brings the player to wherever the camera is, which is
 * what the camera is usually being flown for; F4 ends the flight without moving anybody, and is
 * fixed rather than bindable because a fallback that always means the same thing is worth more
 * than one more thing to configure. See the panel's own hotkey row (OVERLAY_ROW_HOTKEY in
 * overlay_model.c) for how the bindable one gets set. Free camera refuses to turn on at all until
 * it is bound, see cheats_openphantom_toggle()'s own gate, because turning it on without one is
 * a door with no handle on the inside.
 *
 * "Skip to next level" is not a toggle either, and not one of the nine cheats above: a debug-only
 * action row, for iterating on a specific level without replaying everything before it. It writes
 * DAT_00881368 (cheats_original_actions.c's own OP_CREDITS_VAR, exposed read-only from there) to
 * the SAME value the level's own exit trigger writes, script opcode 0x606, sub-command 1, which
 * is what main_game_movie_sequencer_loop (the campaign driver) reads as "level complete" and acts
 * on: broadcast, advance its own level index, load the next entry off its own table. See
 * cheats_openphantom.c's own site comment for the full call chain this was traced through.
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
    CHEATS_OWN_JUMP_BOOST,
    CHEATS_OWN_FREECAM,   /* MUST stay last; overlay_model.c's row layout relies on it, and its
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

/* Jump boost's own multiplier; see this file's own header comment above for what it is applied
 * to. The getter always answers something usable, even before the cheat's own sites have resolved
 * or if they never do; the setter clamps into a fixed sane range rather than trusting whatever a
 * player typed, since this is fed straight into a real physics quantity rather than merely stored. */
float cheats_openphantom_jump_boost_scale(void);
void cheats_openphantom_jump_boost_set_scale(float scale);

/* Flips one and answers the new state. A cheat whose site did not resolve stays off, and free
 * camera specifically also stays off with no key bound. See cheats_openphantom.c. */
bool cheats_openphantom_toggle(cheats_own_id_t id);

/* The virtual key that ends the flight AND brings the player to the camera. 0 means unbound, in
 * which case cheats_openphantom_toggle() refuses to turn CHEATS_OWN_FREECAM on at all. */
int32_t cheats_openphantom_freecam_hotkey(void);
void cheats_openphantom_freecam_set_hotkey(int32_t virtual_key);

/* Suppresses the five consequences of falling for a few seconds, the same five jump boost already
 * suppresses while it is on: the damage, the airborne-too-long death, the fall-distance death and
 * the two camera latches. Granted by the free camera teleport, because arriving at a camera that
 * was flying is a fall the player did not choose to take. Ends on the first landing. */
void cheats_openphantom_grant_fall_grace(void);

/* Forgets whatever the current fall was, so the next level starts with the immunity jump boost
 * normally gives. Called on a level skip: see the comment at the call site for why a level that is
 * left in mid air otherwise kills the player on arrival in the next one. */
void cheats_openphantom_reset_fall_state(void);

/* Switches jump boost off across a level change and puts it back afterwards.
 *
 * Suspend remembers whether it was on and turns it off; resume turns it back on only if suspend
 * was the thing that turned it off, so a player who switched it off themselves during the
 * transition stays switched off. Both are no-ops when there is nothing to do, so they are safe to
 * call on every level change whatever the state. */
void cheats_openphantom_suspend_jump_boost(void);
void cheats_openphantom_resume_jump_boost(void);

/* Notches scrolled since the last take, positive away from the player, the exact contract
 * overlay_input_take_wheel_delta() keeps. A function pointer rather than calling that function by
 * name: the wheel is only observable through window messages, which is overlay_input.c's own
 * domain and not something this file can poll for itself the way it already does for keys and the
 * cursor, but linking straight to that file would drag its whole message-hook subsystem into
 * anything that links this one, including the unit test built against the real cheat sources (see
 * unittests/CMakeLists.txt's own "never a stub" rule), a wheel source it would then have no way
 * to provide. NULL until dev_overlay.c wires the real one in once both cheats_openphantom_install()
 * and overlay_input_install() have run, and free camera treats "no source" exactly like "no
 * scrolling happened yet", which is what it already was for every DLL build before this existed. */
typedef int32_t (*cheats_openphantom_wheel_source_fn_t)(void);
void cheats_openphantom_set_wheel_source(cheats_openphantom_wheel_source_fn_t fn);

/* "Skip to next level"; see this file's own header comment above for the mechanism. False until
 * cheats_original_actions.c's own DAT_00881368 resolution has run and succeeded; this feature
 * resolves nothing of its own. */
bool cheats_openphantom_end_level_is_available(void);

/* Fires it once. False when unavailable; never fails silently the way a straight write would. */
bool cheats_openphantom_end_level_invoke(void);

#endif /* CHEATS_OPENPHANTOM_H */
