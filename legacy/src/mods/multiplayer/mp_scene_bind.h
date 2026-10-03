/* mp_scene_bind.h: what the host's scene reads of the engine and calls in it.
 *
 * Layer 2, the one place the host's scene knows an address. The host's state machine and the
 * release of what a scene holds (mp_scene_host.c, mp_scene_free.c) decide; this reads the cells
 * their rules are asked about and calls the engine's own fade and its setter of the input mode.
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
 * fade and the input mode's setter are called, and the placement of a body is mp_start's.
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
 * found and, where something did not resolve, what the host's scene does without it. */
bool mp_scene_bind_install(void);

/* Whether the fade and the modes resolved, without which nobody is moved. */
bool mp_scene_bind_ready(void);

/* The engine's own fade: out to black and held, back in and let go. Nothing when unresolved. */
void mp_scene_bind_fade_out(float seconds);
void mp_scene_bind_fade_in(float seconds);

/* Whether the engine's tint says the fade it last started has run its time. */
bool mp_scene_bind_fade_done(void);

/* The fade as its two addresses, for the module that gives the engine's re-entry its fade back:
 * the routine a tint is started through and the cell that reads 1 once the tint has run its time.
 * False, with neither written, where the fade did not resolve. */
bool mp_scene_bind_fade_sites(uintptr_t *start_routine, uintptr_t *done_cell);

/* Whether this machine's own player may be moved for a scene now (mp_scene_may_move). Refused
 * while a bank window is open, when the player record is a far body's. */
mp_scene_move_t mp_scene_bind_may_move(void);

/* Whether this machine's own player stands, by the engine's own live player test. A test that did
 * not resolve counts as standing, so a hold's second and a half still runs out. */
bool mp_scene_bind_player_stands(void);

/* Whether a scene runs on this machine's own cells (mp_scene_running). */
bool mp_scene_bind_running(void);

/* Whether this machine's own player module stands as the engine's grab leaves it: nought, the
 * running state in its store, and a body. The module half of mp_scene_bind_running. The
 * developer menu and the free camera leave it so as well, with no actor driving the body. */
bool mp_scene_bind_parked(void);

/* Whether a menu of the engine is on show. The session's pause is the engine's own pause menu,
 * so it is one of them. A menu's close puts back the input mode its open found. False where
 * the cell did not resolve. */
bool mp_scene_bind_menu_open(void);

/* Whether the menu on show and the gun's mode can be told at all on this executable. Whoever
 * gives a lock or a camera back asks these first: "no menu" and "no gun" out of a site that did
 * not resolve would let it take a lock from under an open menu, or the camera from a gun. */
bool mp_scene_bind_menu_known(void);
bool mp_scene_bind_gun_known(void);

/* Once a frame, on the host and on a client alike: a menu of the engine that closed over a lock
 * which fell while it was open has put back the lock's input mode over a lock of nought, and
 * nothing is left to set it back (mp_scene_menu_left_the_input_held). The mode is set to play
 * through the setter the menu's own close called. */
void mp_scene_bind_after_a_menu(void);

/* The engine's input mode as it stands, out of the cell the menu's own two calls name; -1 where
 * that cell is not bound or does not read. */
int32_t mp_scene_bind_input_mode(void);

/* Sets the input mode to play through the engine's own setter, the call a menu's close hands its
 * remembered mode back through. The setter turns back at once when the mode is play already.
 * False, and nothing called, where it is not bound. Whether the mode may be set is the caller's
 * to know: a lock that stands and a menu that is open each own the mode they set. */
bool mp_scene_bind_set_play(void);

/* This machine's own player module, the engine's store of a parked one and whether the player
 * has a body, out of one read of the record. Whatever does not read answers as a running module
 * with nothing stored and no body, and so does everything inside a bank window, where the
 * record is a far body's. */
void mp_scene_bind_module_cells(uint32_t *module, uint32_t *store, bool *has_body);

void mp_scene_bind_report(void);

/* Whether this machine's own player rides the tripod gun now. False where the gun's mode did not
 * resolve. */
bool mp_scene_bind_mode_is_gun(void);

/* Whether a scene may take this machine's own player the hard way now: the engine's respawn with
 * his own hero onto his seat, the way out of any mode the teleport may not move. Only with the
 * module running and a body, the mode read and neither death nor the tripod gun, whose camera and
 * turret the respawn would leave behind, the health above nought, which a respawn does not write,
 * and no re-entry of the respawn module under way. Always false where the gun's mode did not
 * resolve, because then a gun cannot be told from any other mode. */
bool mp_scene_bind_hard_way_open(void);

/* The player module's state now, 1 running; false where it did not read. */
bool mp_scene_bind_module_state(uint32_t *state);

/* This machine's own player: where its record stands and which way it faces. */
bool mp_scene_bind_own_pose(float position[3], float *heading);

/* The hero's window of the campaign bank, as it stands now. */
bool mp_scene_bind_quest_window(uint8_t out[MP_SCENE_QUEST_WINDOW_BYTES]);

/* Where the cheats' hero swap's call to the respawn returns to, nought where it did not prove. */
uintptr_t mp_scene_bind_swap_return(void);

#endif /* MULTIPLAYER_MP_SCENE_BIND_H */
