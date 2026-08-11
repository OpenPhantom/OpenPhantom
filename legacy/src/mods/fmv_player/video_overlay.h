/* video_overlay.h: a borderless window over the game, playing one file through libVLC
 * (vlc_playback.c) and blocking until it ends. Runs entirely in-process now - see video_overlay.c's
 * header comment for the two prior designs this replaces and why.
 *
 * Everything in here is Win32 window side effects - so, like vlc_playback.c, none of it is
 * unit-testable without the game actually running. See fmv_player.c for the byte-evidenced call
 * site that decides WHEN this runs; this file has no engine dependency at all.
 */
#ifndef VIDEO_OVERLAY_H
#define VIDEO_OVERLAY_H

#include <stdbool.h>
#include <stddef.h>   /* wchar_t */

/* Registers this file's window class (cheap, synchronous) and starts loading libVLC on its own
 * thread (vlc_playback_init_async() - see vlc_playback.h for why that is not synchronous here).
 * Call once, as early as possible; returns false only if the window class itself could not be
 * registered, which is checkable immediately. Whether libVLC is actually ready yet is a separate
 * question - see video_overlay_is_ready(). */
bool video_overlay_start_async_init(void);

/* Non-blocking. True only once libVLC has finished loading in the background and is ready to play
 * something. A caller that sees false has nothing to play through yet this call. */
bool video_overlay_is_ready(void);

/* Finds the game's own top-level window (EnumWindows, no engine signature needed), creates a
 * borderless popup owned by it and sized to its current client rect, plays `file_path` into that
 * window through libVLC, and blocks until playback ends, Escape is pressed, or libVLC never starts.
 * Returns false on ANY failure (no game window found, the window could not be created, or libVLC
 * itself failed), in which case nothing is left on screen and the caller is expected to fall back to
 * the original playback path. */
bool video_overlay_play_blocking(const wchar_t *file_path);

#endif /* VIDEO_OVERLAY_H */
