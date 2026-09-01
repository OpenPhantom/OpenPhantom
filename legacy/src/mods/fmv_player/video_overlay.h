/* video_overlay.h: a borderless window over the game, playing one file through libVLC and blocking
 * until it ends.
 *
 * Everything in here is Win32 window side effects, so, like vlc_playback.c, none of it is
 * observable to a unit test without a running game and a real video file. movie_path.c is where
 * the part that can be checked went. See fmv_player.c for the byte-evidenced call site that
 * decides WHEN this runs; the only engine memory this file touches is the drawn menu cursor, and
 * that is menu_cursor_cells.c's business, not this one's.
 */
#ifndef VIDEO_OVERLAY_H
#define VIDEO_OVERLAY_H

#include <stdbool.h>
#include <stddef.h>   /* wchar_t */

/* Registers this file's window class, resolves the drawn menu cursor's cells, and STARTS bringing
 * libVLC up on a thread of its own. Call once, as early as possible: the load is the slow part and
 * nothing on the game's thread waits for it.
 *
 * False only when the window class itself could not be registered, which is the one half that is
 * answerable immediately. Whether libVLC is actually up yet is a separate question with a separate
 * answer below. */
bool video_overlay_start_async_init(void);

/* Non-blocking. True only once libVLC has finished loading in the background AND succeeded. A
 * caller that sees false has nothing to play through for this one movie and should use the
 * original path; the next movie asks again. */
bool video_overlay_is_ready(void);

/* Non-blocking, and what separates the two reasons for that false: still loading, which is
 * temporary, from no usable libVLC at all, which lasts the session. */
bool video_overlay_is_still_loading(void);

/* Finds the game's own top-level window, creates a borderless popup owned by it and sized to its
 * current client rect, plays `file_path` into that window through libVLC, and blocks until
 * playback ends, Escape is pressed, or libVLC never starts.
 *
 * False on any failure, with the reason logged, in which case nothing is left on screen and the
 * caller is expected to fall back to the original playback path. */
bool video_overlay_play_blocking(const wchar_t *file_path);

/* WHICH WINDOW LIBVLC DRAWS INTO. The default is "popup" on Windows and "child" under Wine, and
 * neither needs setting: the ini is here to override a machine that disagrees.
 *
 * WHAT "child" IS ACTUALLY FOR, because it is not what it was written for. It was built to test
 * whether a second full screen window was what turned the menus black under Wine. It was NOT: that
 * was libVLC's Direct3D video output taking the engine's exclusive mode device away, and it is
 * fixed by the video output setting in vlc_playback.h instead. This looked like a dead end.
 *
 * It turned out to be the fix for a different defect. Under Wine a real Escape did not skip a
 * movie, while the controller's synthesised one did, and three separate ways of READING the key
 * failed because the key was never reaching the process at all. A top level window is mapped as a
 * real X11 window, an X11 window manager focuses a newly mapped one, and the HWND behind it is
 * WS_EX_NOACTIVATE, so Wine held the focus for a window that refuses activation and every keystroke
 * went nowhere for as long as the movie lasted. A child is drawn inside its parent and is not an
 * X11 window at all, so nothing is ever mapped and the focus never moves.
 *
 *   "popup"  a full screen WS_POPUP owned by the game window. Correct on Windows and the default
 *            there: it is what has always shipped and what every Windows release was tested with.
 *   "child"  a WS_CHILD inside the game's own window. The default under Wine. FIELD CONFIRMED on
 *            Linux Mint together with the gdi video output: the movies play and Escape skips them.
 *   "game"   no window of ours at all: libVLC is handed the game's own window. Never needed in the
 *            end, kept because it costs two lines and is the only remaining thing to try if a
 *            machine turns up that neither of the above suits.
 *
 * An unrecognised name is refused and logged rather than guessed at, and the platform default is
 * kept. */
void video_overlay_set_surface_mode(const char *mode);

#endif /* VIDEO_OVERLAY_H */
