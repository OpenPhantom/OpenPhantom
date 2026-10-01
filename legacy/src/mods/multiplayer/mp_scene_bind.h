/* mp_scene_bind.h: what a scene's gathering reads of the engine and calls in it.
 *
 * Layer 2, the one place the gathering knows an address. The host's state machine and a client's
 * mirror (mp_scene_host.c, mp_scene_client.c) decide; this reads the cells their rules are asked
 * about and calls the engine's own fade.
 *
 *   The FADE is the one the engine's respawn uses: fxfade_startTintOpaque out with its hold, the
 *   cell its draw sets once the fade is done, and the same call back in. A fade out holds the
 *   screen black until the next tint starts, so whoever starts one owes the fade in on every way
 *   out; the seat flow in mp_scene_flow is what keeps that count.
 *
 *   The MODES a player may be moved out of are the three the engine's own grab parks a player
 *   from, read out of that function's three compares, so a player is moved exactly where the engine
 *   would have taken him for a scene itself.
 *
 *   Whether the player STANDS is the engine's live player test, through the world's anchor, which
 *   calls it for the same question: one predicate for one state, or the two fall apart.
 *
 * Installed on the session's way in, never when the DLL loads. Nothing here writes the engine; the
 * fade is called, and the placement of a body is mp_start's.
 */
#ifndef MULTIPLAYER_MP_SCENE_BIND_H
#define MULTIPLAYER_MP_SCENE_BIND_H

#include "mp_scene_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The hero's window of the campaign bank: bytes 6 to 11, bits 48 to 95. */
#define MP_SCENE_QUEST_WINDOW_BYTES 6u

/* Resolves the fade, its cell, the three modes and the story cells. Idempotent; says once what it
 * found and, where something did not resolve, what the gathering does without it. */
bool mp_scene_bind_install(void);

/* Whether the fade and the modes resolved, without which nobody is moved. */
bool mp_scene_bind_ready(void);

/* The engine's own fade: out to black and held, back in and let go. Nothing when unresolved. */
void mp_scene_bind_fade_out(float seconds);
void mp_scene_bind_fade_in(float seconds);

/* Whether the engine's tint says the fade it last started has run its time. */
bool mp_scene_bind_fade_done(void);

/* Whether this machine's own player may be moved for a scene now (mp_scene_may_move). Refused
 * while a bank window is open, when the player record is a far body's. */
mp_scene_move_t mp_scene_bind_may_move(void);

/* Whether this machine's own player stands, by the engine's own live player test. A test that did
 * not resolve counts as standing, so a hold's second and a half still runs out. */
bool mp_scene_bind_player_stands(void);

/* Whether a scene runs on this machine's own cells (mp_scene_running). */
bool mp_scene_bind_running(void);

/* The player module's state now, 1 running; false where it did not read. */
bool mp_scene_bind_module_state(uint32_t *state);

/* This machine's own player: where its record stands and which way it faces. */
bool mp_scene_bind_own_pose(float position[3], float *heading);

/* The hero's window of the campaign bank, as it stands now. */
bool mp_scene_bind_quest_window(uint8_t out[MP_SCENE_QUEST_WINDOW_BYTES]);

/* Where the cheats' hero swap's call to the respawn returns to, nought where it did not prove. */
uintptr_t mp_scene_bind_swap_return(void);

#endif /* MULTIPLAYER_MP_SCENE_BIND_H */
