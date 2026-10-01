/* mp_hud.h: the live scoreboard, drawn into the picture the engine is already making.
 *
 * A deathmatch whose score can only be found in a log file is not one. This is the panel that
 * puts it on screen, and the whole of it is three decisions: when to draw, where to draw, and
 * what to draw. The second is in this file, the third is mp_board.c, and the first is the reason
 * the file exists at all.
 *
 * WHEN. Module message 0x15 is the first statement of the engine's frame end: the world, the
 * heads up display and the subtitles are already in the picture, the fade is drawn by a module
 * listening for that same message, and the scene is closed about five hundred bytes further down
 * the same function. A broadcast walks the registry from its tail towards its head, and this
 * feature's second node was relinked to the head when it was installed, so it hears 0x15 LAST of
 * every module in the game. That is the whole trick: no patch, no detour, no call site taken away
 * from anybody, and the panel lands on top of the fade rather than under it.
 *
 * Why not the call that closes the scene. Redirecting it is the other obvious instant and it does
 * not chain: the shared redirect refuses to read a call whose current target is outside the host
 * image, so the second module to want that call gets nothing. Whichever mod loads first would own
 * the panel and the other would be silently absent, depending on which switches are set in an ini
 * file. A module message has no such owner.
 *
 * Once per frame, and it is checked. The panel's body is translucent, so drawing it twice in one
 * frame is visible as a darker panel rather than as nothing. The field census that established
 * this instant counted one more 0x15 than it counted frames and recorded a maximum of two in one
 * frame, and neither number was explained at the time. So the arrivals are counted against the
 * frames, a second arrival inside one frame is refused and counted, and the report says how often
 * that happened.
 *
 * The dead player is the reason this is not a pause screen. The engine refuses to open its pause
 * menu while the player is dead, and dead is exactly what somebody is during the seconds they
 * want to read the table. So the panel comes up for a player who is waiting to come back without
 * anybody pressing anything.
 *
 * What is deliberately not here is the final table on the screen the game shows between levels.
 * That is a screen of the frontend and belongs with the screens, not with a panel painted into a
 * running frame; this file is the named place for the decision, and the round's own result line
 * is already what such a screen would print.
 */
#ifndef MULTIPLAYER_MP_HUD_H
#define MULTIPLAYER_MP_HUD_H

#include <stdbool.h>

/* Resolves the drawing surface and reads the configured key. False when the surface is not
 * complete, in which case the panel stays off and the report names the site that was missing.
 * Safe to call twice; the second call answers what the first decided. */
bool mp_hud_install(void);

/* Every module message the head node hears. It acts on one of them and returns at once for the
 * rest, which is what lets the caller stay a plain fan out. */
void mp_hud_note_module_message(int message);

/* Once per drawn frame, from the frame hook, AFTER the frame the message above drew into. It is
 * what makes a frame a frame for the guard, and it is where the key is sampled: two instances of
 * the game share one keyboard, so the sample is refused unless this window has the focus. */
void mp_hud_pump(void);

/* What the panel did, for the run report: whether it ever drew, and when it did not, why. */
void mp_hud_report(void);

#endif /* MULTIPLAYER_MP_HUD_H */
