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

/* Locates a 32-bit libVLC install and registers this file's window class. Call once, before the
 * first video_overlay_play_blocking(); safe to call from whichever thread ends up playing movies,
 * which for this engine is always the same thread that calls the retail movie player. */
bool video_overlay_init(void);

/* Finds the game's own top-level window (EnumWindows, no engine signature needed), creates a
 * borderless popup owned by it and sized to its current client rect, plays `file_path` into that
 * window through libVLC, and blocks until playback ends, Escape is pressed, or libVLC never starts.
 * Returns false on ANY failure (no game window found, the window could not be created, or libVLC
 * itself failed), in which case nothing is left on screen and the caller is expected to fall back to
 * the original playback path. */
bool video_overlay_play_blocking(const wchar_t *file_path);

#endif /* VIDEO_OVERLAY_H */
