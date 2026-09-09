/* window_close.h: make the window's close box end the game, because the engine refuses to.
 *
 * The engine's window procedure answers WM_CLOSE with zero and does nothing else. It never reaches
 * DefWindowProc and it never reaches the shutdown, which is deliberate: the window the engine gives
 * itself is WS_POPUP with no close box at all, so the only thing that could ever send it a close
 * was Alt+F4, and swallowing it stopped a stray keypress ending a level. Giving the window a frame
 * gave it a button the engine has never had a handler for.
 *
 * This answers that message and runs the engine's own teardown, sys_shutdown, which is the same
 * function sys_main calls when the game exits normally. It is NOT a synthesised quit: nothing is
 * skipped, and the settings file is unaffected either way because this game writes its settings
 * when they change rather than on the way out.
 *
 * Two details of how it gets there are not incidental.
 *
 * A frame later, not inside the message. Freeing the world from inside a window procedure means
 * doing it while a message is dispatched from inside a frame that is still running.
 *
 * And the process ends inside it rather than returning. The engine only calls that teardown from
 * sys_main, with nothing above it but WinMain; this reaches it from inside a frame, so returning
 * would carry on running a level that no longer exists. What is given up by leaving there is the C
 * runtime's exit handlers, after the game's own shutdown has already run in full.
 *
 * There is a cleaner path in principle, which is to make the front end return its own quit answer
 * so the game unwinds to sys_main by itself. It was looked at and is not available: that answer is
 * a LOCAL in the front end's frame, not a global anything can set, so there is nothing to write.
 */
#ifndef WINDOW_CLOSE_H
#define WINDOW_CLOSE_H

#include <stdbool.h>
#include <stdint.h>

bool window_close_install(void);


/* Polled by window_poll, so this file needs no frame hook of its own. */
void window_close_poll(void);

#endif /* WINDOW_CLOSE_H */
