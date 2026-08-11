/* vlc_playback.h: plays one file into an already-created window through a 32-bit libVLC install
 * found on the machine, loaded at runtime rather than linked against an SDK - see vlc_playback.c
 * for why (there is no build-time libVLC dependency anywhere in this project as a result).
 *
 * This replaces host_main.c's original Media Foundation/MFPlay backend outright, at the user's own
 * direction after MFPlay flickered or minimized the game window through six different fixes (five
 * in-process, one process-isolated) that each ruled out a real hypothesis without finding the right
 * one - see host_main.c's own header comment for that full chronology. The one thing every MFPlay
 * attempt shared was MFPlay/EVR itself; VLC, standalone, was independently confirmed to play the
 * exact same converted file with zero flicker on the reporting machine, which is the whole reason
 * to try its engine specifically rather than keep iterating on window management around MFPlay.
 */
#ifndef VLC_PLAYBACK_H
#define VLC_PLAYBACK_H

#include <windows.h>

#include <stdbool.h>

/* Locates a 32-bit VLC install (checks the conventional Program Files (x86) path first, then the
 * installer's own registry key as a fallback for a non-default install location), loads
 * libvlccore.dll and libvlc.dll from it, resolves the handful of exports this file calls, points
 * VLC_PLUGIN_PATH at that install's plugins folder, and creates one shared libvlc_instance_t for
 * the process's lifetime - all of it on a background thread, started once and never joined by the
 * caller. Call as early as possible (fmv_player_install() time), so it has the most time to
 * finish before the first movie needs it. Idempotent; a second call is a no-op. */
void vlc_playback_init_async(void);

/* Non-blocking. True only once the background init above has both finished AND succeeded; false
 * while it is still running, if it was never started, or if it finished with a failure (no 32-bit
 * VLC found, a required export missing, or libvlc_new() itself failing). Never waits - a caller
 * that sees false here has nothing to play through yet and should fall back to the original
 * playback path for this one movie, the same as every other failure mode in this DLL. */
bool vlc_playback_is_ready(void);

/* Plays `file_path` into `window` (already created and positioned by the caller - this file has no
 * opinion on window management at all, only on what renders inside one) via
 * libvlc_media_player_set_hwnd, and blocks, pumping only `window`'s own messages, until the file
 * ends, Escape is pressed, or libVLC never actually starts decoding within a bounded timeout
 * (protects against hanging forever on a file libVLC silently can't open). Returns false only when
 * libvlc_media_player_play itself refused the request or playback never started at all; anything
 * after playback genuinely begins - reaching the end, Escape, or even a rare mid-playback error this
 * file has no verified ABI to distinguish from a natural end - is treated as handled, matching how
 * the retail player itself just moves on regardless of why a movie stopped.
 *
 * The pump is scoped to `window` (PeekMessageW's hWnd argument, not NULL): this runs in-process, on
 * the game's own thread, so an unscoped peek would also retrieve the game window's own messages -
 * WM_SETCURSOR, WM_ACTIVATE, WM_MOUSEMOVE - off the SAME queue. PM_REMOVE takes a message off the
 * queue whether or not it is dispatched, so a caller that then chose not to dispatch those would not
 * be deferring them, it would be discarding them, and the game window would come out of every movie
 * having silently missed whichever activation and cursor messages happened to arrive during it. This
 * function has no reason to touch that window's messages at all, so it no longer retrieves them. */
bool vlc_playback_play_blocking(HWND window, const wchar_t *file_path);

#endif /* VLC_PLAYBACK_H */
