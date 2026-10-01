/* fullscreen_toggle.h: Alt and Enter, back and forth between a window and the whole screen.
 *
 * The dev panel can already choose any window mode, and the settings file has always been able to.
 * Neither is what a player reaches for while playing. Alt+Enter is the gesture every windowed game
 * has had for twenty five years, and a player who is in a window and wants the screen back should
 * not have to open a panel to say so.
 *
 * ==============================================================================================
 * What it toggles between
 *
 * Borderless at the size of the monitor on one side, and whatever mode was in force before on the
 * other. It remembers rather than assuming, because the four modes are not interchangeable: a
 * player who set up a resizable window and pressed this twice should get their resizable window
 * back, not the fixed one.
 *
 * What it does NOT do is change the engine's resolution. The picture is scaled to the window
 * either way, so this changes how much of the screen the game covers and nothing about what is
 * being rendered. Going to fullscreen this way is not the same as setting a fullscreen
 * resolution, and it costs nothing to come back from.
 *
 * ==============================================================================================
 * Why Alt has to be held, and why the key is a setting anyway
 *
 * Enter on its own is a key the game uses. The modifier makes it safe, and it also makes it the
 * gesture people already know, so it is not optional and not configurable. Which key
 * Alt is held with is configurable, because a keyboard layout or a conflicting overlay is not
 * something this can predict.
 *
 * Alt is bound in the engine's own key table, so whatever Alt does will also happen on the way
 * through. That is true of Alt+Enter in every game of this era and is not worth fighting.
 */
#ifndef FULLSCREEN_TOGGLE_H
#define FULLSCREEN_TOGGLE_H

#include <stdbool.h>

/* Reads its own key from the settings file rather than being handed one, which is the same thing
 * window_poll.c does and for the same reason: enhanced_resolution.c is at its size limit, and a
 * setting that is only ever read by one file does not need to travel through the file that reads
 * every other setting. Read once, at install: rebinding the key takes a restart. */
bool fullscreen_toggle_install(void);

/* Polled by window_poll alongside everything else, so this file needs no timer of its own. */
void fullscreen_toggle_poll(void);

#endif /* FULLSCREEN_TOGGLE_H */
