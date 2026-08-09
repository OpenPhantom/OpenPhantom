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
 * the process's lifetime. False on ANY failure - no 32-bit VLC found (a 64-bit-only install does
 * not count: a 32-bit process cannot load a 64-bit DLL at all), a required export missing, or
 * libvlc_new itself failing - in which case the caller has nothing to play through and should fall
 * back to the original playback path, the same as every other failure mode in this DLL. */
bool vlc_playback_init(void);

/* Plays `file_path` into `window` (already created and positioned by the caller - this file has no
 * opinion on window management at all, only on what renders inside one) via
 * libvlc_media_player_set_hwnd, and blocks, pumping the calling thread's message queue, until the
 * file ends, Escape is pressed, or libVLC never actually starts decoding within a bounded timeout
 * (protects against hanging forever on a file libVLC silently can't open). Returns false only when
 * libvlc_media_player_play itself refused the request or playback never started at all; anything
 * after playback genuinely begins - reaching the end, Escape, or even a rare mid-playback error this
 * file has no verified ABI to distinguish from a natural end - is treated as handled, matching how
 * the retail player itself just moves on regardless of why a movie stopped.
 *
 * `exclude_from_dispatch`, if not NULL, is a window whose own messages are drained from the queue
 * but never dispatched to its WndProc while this call blocks. Pass NULL when this call's own window
 * is the only thing on the calling thread (nothing else needs excluding). video_overlay.c passes the
 * game's own window here: this function runs in-process, on the game's own thread, synchronously
 * inside the call it replaced, so the game's thread is a UI thread with a live window that this loop
 * would otherwise be pumping (WM_PAINT, WM_ACTIVATEAPP, ...) to that window's own WndProc for the
 * whole length of a movie, which is exactly the risk this parameter exists to remove. */
bool vlc_playback_play_blocking(HWND window, const wchar_t *file_path, HWND exclude_from_dispatch);

#endif /* VLC_PLAYBACK_H */
