/* mp_session_over.h: when the session ends, everybody goes back to the title screen.
 *
 * The rule this file carries out is deliberately the small one: there is no host migration. If the
 * host is gone, the session is gone, and every client returns to the menu. Nobody is promoted,
 * nothing is resumed, and the level is not held open in the hope that somebody comes back.
 *
 * Why it needs a module at all. Ending a session is not one write. The engine has to be told to
 * leave the level, and eleven modules hold state that outlives it, a puppet standing in a world
 * that is about to be freed, a round that never stops running, a re-entry rule that keeps a dead
 * player's death suppressed forever. Until this file existed, NONE of that was cleared on
 * anything except a peer ARRIVAL, so a session that ended simply left its wreckage standing.
 *
 * The worst of those, and the reason this is not cosmetic: mp_damage's survival switch stays on
 * after the far side is gone. A client that then dies is not respawned (the anchor it would be
 * put beside is a ghost), gets no death screen (the switch suppressed it), and cannot open the
 * pause menu (the key hook at 0x0043F603 opens it only while the outcome cell reads 2 and the
 * player is alive). It hangs, with Alt+F4 as the only way out. Clearing the switch is one line
 * here and it is the line that fixes that.
 *
 * How the engine is told, and why it is these two cells in this order: the campaign round runs
 * `while (g_levelOutcome == 2) sys_frame();` and then branches on what it finds. Outcome 3 is
 * "level complete", which fades, sends the level-end message, and moves on to the NEXT level;
 * `g_restorePending` is what turns that into "back to the title screen" instead. The engine's own
 * "leave level" choice in the pause menu writes exactly this pair, so this is not a new path
 * through the engine, it is the existing one driven from here. The order of the two writes is
 * not load bearing: the engine's own choice writes them the other way round, nothing re-enters
 * the engine between the two writes, and the restore flag is read only after the fade.
 *
 * SIZE NOTE: under 300 lines, no seam.
 */
#ifndef MULTIPLAYER_MP_SESSION_OVER_H
#define MULTIPLAYER_MP_SESSION_OVER_H

#include "mp_exit.h"
#include "mp_lobby.h"

#include <stdbool.h>

/* Once per drawn frame, from the frame pump. It asks the bridge whether the session is over and
 * acts on the answer; until one is, it costs two comparisons.
 *
 * It runs from the FRAME pump rather than from a substep because the substeps stop and the frames
 * do not: the blocking menu loops and the death screen all keep drawing, and a player sitting in
 * one of those is exactly who needs to be told that the session has gone.
 *
 * What it does not cover is a host vanishing during the client's own level load. The frame hook
 * does not run there either, and the rule below asks for a level to be running, which a loading
 * one is not. Nothing is lost by that: a load ends, and the first frame after it asks again. */
void mp_session_over_tick(void);

/* Host only, and the other half of the promise: say so before leaving. A host that walks back to
 * its own title screen does not close its socket, and the frame hook and the 30 ms thread timer
 * both keep running there, so keepalives keep going out and the far side's 30 s connected timeout
 * never fires; without this, its clients play on alone in a world with no authority. Wired to the
 * engine's own "a new game begins" broadcast, which is the message the campaign sends on its way
 * to the title screen. */
void mp_session_over_announce(void);

/* What ended it, or MP_LOBBY_OVER_NO while nothing has. For the screen. */
mp_lobby_over_t mp_session_over_reason(void);

/* The line to put in front of the player, in the menus' language. NULL for MP_LOBBY_OVER_NO. */
const char *mp_session_over_text(mp_lobby_over_t reason);

/* Cleared when a fresh session starts, so a second session in the same process does not open with
 * the first one's farewell still on the screen. */
void mp_session_over_forget(void);

/* The numbers, for the bridge's report. */
void mp_session_over_report(void);

/* ==============================================================================================
 * The one exit (mp_exit.h has the rules). Every way out of a session calls it, and nothing else
 * takes a session down.
 * ============================================================================================ */

/* The first half, at once: the host says the session is over, the world the session left standing
 * is cleared, and the reason a player is owed is kept for the title. The second half follows from
 * the pumps. Idempotent while one is under way; nothing for the loopback and with no transport. */
void mp_session_over_exit(mp_exit_why_t why);

/* The second half, from both pumps: the transport comes down once no level stands and the last
 * word is delivered or a second has passed. */
void mp_session_over_exit_tick(void);

/* The second half now, for a caller that is about to put a new transport up. */
void mp_session_over_finish_now(void);

/* Whether an exit's first half is said and its second still waits: a level that begins now is one
 * this side began on its own, and nothing of the session is to be put on it. */
bool mp_session_over_exit_waits(void);

/* A level has begun, module message 5: remembered for the title door, or the end of an exit that
 * was still waiting when this side began a level of its own. */
void mp_session_over_note_level_begin(void);

/* The title screen is on show. Takes a session with nothing left to be down through the title
 * door, and answers the sentence a player is owed about an ending that was not their choice, once,
 * or NULL. `menu_armed` says the menu put the transport up. */
const char *mp_session_over_at_title(bool menu_armed);

/* Every far body down, while the world holding them stands. */
void mp_session_over_take_bodies_down(void);

/* The report the second half prints before the transport goes, and the listener that gives back
 * what the arming wrote once it has gone. */
void mp_session_over_set_listeners(void (*report)(const char *why), void (*unarmed)(void));

#endif /* MULTIPLAYER_MP_SESSION_OVER_H */
