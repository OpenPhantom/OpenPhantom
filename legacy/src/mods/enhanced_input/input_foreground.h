/* input_foreground.h: does this process own the foreground, asked cheaply enough to ask per packet.
 *
 * Why this is a question at all
 *
 * The raw mouse reader registers RIDEV_INPUTSINK, and it has to: it owns a message-only window
 * that can never be activated, so a foreground-only registration on that window would deliver
 * nothing. INPUTSINK is what makes the reader work, and it is also what makes it keep working
 * after the player has alt-tabbed away. Windows goes on delivering the device's movement, the
 * reader goes on accumulating it, and the view goes on turning inside a game the player is no
 * longer looking at.
 *
 * The retail engine does not behave that way. It opens its DirectInput devices FOREGROUND, so
 * Windows unacquires them the moment the window goes to the background and the engine reads
 * nothing. Gating on the foreground therefore restores what the engine did rather than changing
 * it, which is why there is no setting for it: the alternative is a bug, not a preference.
 *
 * Why the process and not a window
 *
 * The test is whether the foreground window belongs to THIS PROCESS, not whether it is the game's
 * own window. That is deliberate. Asking about a specific window would mean answering "which
 * window is the game's" a second time in a second DLL, and the answer to that already has an owner
 * elsewhere that this one cannot call. The process test needs no such answer, and it is the right
 * question anyway: a modal dialog or a movie window owned by the game is still the game in front.
 */
#ifndef INPUT_FOREGROUND_H
#define INPUT_FOREGROUND_H

#include <stdbool.h>

/* True while a window of this process is the foreground window.
 *
 * Cheap enough to call on every raw input packet, which at a 1000 Hz mouse is a thousand times a
 * second: the answer is cached and the two window calls behind it are made at most once every few
 * milliseconds. A cached answer can therefore be one poll interval stale, which costs at most a
 * few milliseconds of movement on the frame the foreground changes and corrects itself on the
 * next poll. That is the trade this exists to make; a caller that needs the exact instant of the
 * change wants a message, not this.
 *
 * The cache is two aligned 32-bit cells written without a lock. It is called from the raw input
 * thread today. If a second thread ever calls it, the worst that can happen is that both poll in
 * the same interval, because neither cell can be read torn on this target and a redundant poll
 * returns the same answer. */
bool input_foreground_is_ours(void);

#endif /* INPUT_FOREGROUND_H */
