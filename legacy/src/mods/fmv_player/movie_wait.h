/* movie_wait.h: the black picture a client of a multiplayer session waits behind, when its movie
 * ended before the host's.
 *
 * A movie in a session belongs to the host, and a client's copy of it can end first: it began a
 * little later or not at all in step, or the host is slower to load. The client's movie is then
 * over and the host's is not, and the client may not go on into a level the host has not begun.
 * So the window the movie played in turns black with one line in the session's language, and
 * stays until the host's world moves or the session lets the player go. Everything here is window
 * side effects; the question of when to stop is asked through the movie loop.
 */
#ifndef MOVIE_WAIT_H
#define MOVIE_WAIT_H

#include "movie_rule.h"

#include <windows.h>

/* The line to paint while a wait is on screen, NULL otherwise. The overlay's window procedure
 * paints it on WM_PAINT, and paints nothing of its own while it is NULL, which is while libVLC
 * draws. */
const char *movie_wait_text(void);

/* Black over the whole client area of `window`, and `text` centred on it. */
void movie_wait_paint(HWND window, HDC dc, const char *text);

/* Asks `loop->poll` once, and when the host is not done yet and the session still holds the
 * player: shows `loop->wait_text` in `overlay_window`, or straight into `render_window` when there
 * is no overlay of this DLL's own, tells `loop->waits`, and pumps until the host is done or the
 * session lets go. `loop->end` comes back as MOVIE_END_HOST or MOVIE_END_ALONE when it waited, as
 * MOVIE_END_HOST when the host was done at the first question, and unchanged when the session had
 * already let the player go. */
void movie_wait_hold(HWND overlay_window, HWND render_window, HWND game_window,
                     movie_loop_t *loop);

#endif /* MOVIE_WAIT_H */
