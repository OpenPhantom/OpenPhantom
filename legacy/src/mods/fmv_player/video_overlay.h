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

#endif /* VIDEO_OVERLAY_H */
