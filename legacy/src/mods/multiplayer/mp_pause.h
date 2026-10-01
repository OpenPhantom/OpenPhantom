/* mp_pause.h: the pause menu of a session opens over a running world.
 *
 * In a session nobody may stop the world for everybody, so the pause key does not reach the
 * engine's pause there. The key hook's one call of sys_pause is repointed while a transport stands
 * and put back when it comes down; single player never sees the repointed call. What runs instead
 * drives the engine's own pause menu without the latch, without the SUSPEND broadcast and without
 * the simulation gate, so the menu's own loop pumps the world's frame, substeps included, the way
 * the shipped game already does on a slow machine and in its cheat console. This player's body
 * goes on being simulated; only its input is held, through mp_input and the session note.
 *
 * The menu closes itself when the player dies, a dialogue or scene lock rises, the level ends or
 * the session does, and every way out, the player's own answers and those four, ends in the one
 * exit, which writes the cells the engine's own pause writes for the same answer.
 *
 * The rules are mp_pause_rule; this file is their binding to the engine.
 */
#ifndef MULTIPLAYER_MP_PAUSE_H
#define MULTIPLAYER_MP_PAUSE_H

#include <stdbool.h>
#include <stdint.h>

/* A transport went up: the call is repointed, the first time after its sites are bound and
 * proved. False when it cannot be, said once; the pause stays the engine's own then. */
bool mp_pause_arm(void);

/* The transport comes down: a menu that is up closes for it, and the call is put back. */
void mp_pause_disarm(void);

/* One drawn frame, from the bridge's frame pump: the world under an open menu is looked at, and a
 * close reason is latched. */
void mp_pause_frame(void);

/* A screen reads its navigation code. While a close reason stands, a read with no code of the
 * player's answers a cancel, so the menu and whatever screen is on top of it leave. */
int32_t mp_pause_nav(int32_t code);

/* The player list's frame over a session's pause menu. True when it ran the world's frame; false
 * leaves the list to its own pump, which is also the answer over every other screen. */
bool mp_pause_pump_world(void);

void mp_pause_report(void);

#endif /* MULTIPLAYER_MP_PAUSE_H */
