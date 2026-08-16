/* overlay_input.h: the open key, the pointer, and what this layer can and cannot hold.
 *
 * Everything arrives through one detour, on the function the engine registers to see its own window
 * messages. That is the same place the shipped game opens its cheat console from, and it is the
 * only place where a key can be taken before the game has acted on it.
 *
 * Swallowing a message stops the keys the game reads FROM messages, which is the pause and the menu
 * keys. It is not the input lock, and believing it was cost this feature a round: movement, turning
 * and firing are polled from the device and never touch the queue. input_freeze holds those.
 *
 * The engine's modal cell sits in the matched bytes and is read as proof this is the right
 * function. It is deliberately not written: raising it makes this hook answer "not handled".
 */
#ifndef OVERLAY_INPUT_H
#define OVERLAY_INPUT_H

#include <stdbool.h>
#include <stdint.h>

/* Places the detour and reads the modal cell out of the matched bytes. False when the site did not
 * resolve, and then nothing is patched and the overlay never opens. */
bool overlay_input_install(void);

/* The virtual key that opens the panel. Zero, the default, accepts whichever key sits below Escape
 * on this keyboard, which is the caret on a German layout and the backtick on a British one. */
void overlay_input_set_key(int32_t virtual_key);

/* Whether the panel is open right now. The frame hook asks this to decide whether to paint. */
bool overlay_input_is_open(void);

/* Closes the panel and releases the player. Called from the paint when the frame it would draw into
 * is not the one the player sees, so a level ending, a cutscene or a movie cannot leave the game
 * held with nothing on screen. */
void overlay_input_close(void);

/* Moves the panel's pointer to wherever the system cursor is, mapped into the picture the engine
 * draws. Called once per painted frame. */
void overlay_input_update_pointer(void);

/* Where that pointer is, in the screen pixels the panel is laid out in. */
void overlay_input_pointer(float *out_x, float *out_y);

/* Whether the search box is the thing typing reaches right now. False the moment the panel opens
 * and after every close, true only once a click has landed inside the field itself. The painter
 * asks this to show which state the field is in. */
bool overlay_input_search_focused(void);

#endif /* OVERLAY_INPUT_H */
