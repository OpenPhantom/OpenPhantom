/* movie_close.h: the close box while a movie is on screen.
 *
 * The close box arrives as WM_NCLBUTTONDOWN on HTCLOSE, addressed to the game's own window, whose
 * messages a movie's pump leaves alone. Outside a multiplayer session it ends the movie and is put
 * back on the queue at once, as it always was.
 *
 * In a session it ends no movie. The movie is the host's, and on one machine with two windows a
 * click into the other one would otherwise end the movie of every player. The request is not lost
 * for that: it is remembered, and passed on once the movie and any wait for the host are over, so
 * the game closes then as it would have with no movie on screen. With a frame and a close box on
 * the window that means it shuts down, because enhanced_resolution answers WM_CLOSE there; in the
 * frameless window the engine ships in there is no close box to press.
 */
#ifndef MOVIE_CLOSE_H
#define MOVIE_CLOSE_H

#include "movie_rule.h"

#include <windows.h>

#include <stdbool.h>

/* One look at the game window's queue for a close request. True when there is one and it ends the
 * movie, which happens only when `loop` lets the close box end it; the request is then put back as
 * it came. Otherwise a request found is taken, counted into `loop->counts.closes_refused` and
 * remembered, and the answer is false. */
bool movie_close_requested(HWND game_window, movie_loop_t *loop);

/* The request remembered while a session's movie was on screen, passed on exactly once however
 * often the box was pressed, as WM_SYSCOMMAND with SC_CLOSE: the command a completed click on the
 * close box produces. True when one was passed on. Called once the movie and any wait for the host
 * are over, before the game gets its window back. */
bool movie_close_pass_on(void);

#endif /* MOVIE_CLOSE_H */
