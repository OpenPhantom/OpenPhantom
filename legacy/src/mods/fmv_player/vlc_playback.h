/* vlc_playback.h: plays one file into an already-created window through a 32-bit libVLC, loaded at
 * run time rather than linked against an SDK.
 *
 * There is no libVLC SDK anywhere in this project's build, on purpose: LoadLibraryW and
 * GetProcAddress against a libvlc.dll found at run time mean nobody building this project needs
 * libVLC headers or an import library, and a machine without a usable one degrades to the Bink
 * fallback exactly like every other missing-dependency case here. vlc_locate.c decides where that
 * DLL comes from.
 *
 * This replaces a Media Foundation backend that flickered black through six different fixes. The
 * one thing every one of those attempts shared was MFPlay and its EVR renderer; VLC, standalone,
 * played the same converted file with no flicker at all, which is why its engine was tried rather
 * than a seventh round of window management around MFPlay.
 */
#ifndef VLC_PLAYBACK_H
#define VLC_PLAYBACK_H

#include <windows.h>

#include <stdbool.h>

/* Starts locating a 32-bit libVLC, loading libvlccore.dll and libvlc.dll out of it, resolving the
 * handful of exports this file calls, pointing VLC_PLUGIN_PATH at that install's plugins folder,
 * and creating one shared instance for the life of the process - ALL OF IT ON A BACKGROUND THREAD,
 * started once and never joined.
 *
 * Call as early as possible, at install time, so it has the most time to finish before the first
 * movie needs it. It is idempotent; a second call does nothing. It returns nothing because there
 * is nothing to report yet: whether the load worked is a separate question, asked later and
 * without blocking, through vlc_playback_is_ready().
 *
 * On failure the log says which: no 32-bit libVLC anywhere, a DLL that would not load, a missing
 * export, or libvlc_new itself refusing. Nothing is left loaded and VLC_PLUGIN_PATH is removed
 * again, so a failed load leaves nothing behind that another component could pick up. It is
 * REMOVED rather than restored: no original is saved, so a process that had exported that variable
 * before this DLL ran does not get it back. */
void vlc_playback_init_async(void);

/* Non-blocking. True only once the background load above has both FINISHED and SUCCEEDED; false
 * while it is still running, if it was never started, and if it finished with a failure. A caller
 * that sees false has nothing to play through for this one movie and should use the original
 * playback path, the same as every other failure mode in this DLL. It never waits, so asking on
 * the game's own thread costs nothing. */
bool vlc_playback_is_ready(void);

/* Non-blocking, and the reason the false above is not one situation but two. True only while the
 * background load is still running, which is temporary and worth telling a caller about because
 * the next movie may well succeed. A load that never started, or that finished and failed, is
 * false here as well as in is_ready, and is permanent for this session. A caller that reports
 * "still loading" for that case would be naming a cause that is not true, once per movie. */
bool vlc_playback_is_still_loading(void);

/* Plays `file_path` into `window` and blocks until the file ends, Escape is pressed, or libVLC
 * never starts decoding within a bounded timeout. The window is the caller's: this file has no
 * opinion on window management, only on what renders inside one.
 *
 * Returns false only when playback never began, which is the caller's signal to fall back to the
 * original path. Anything after it genuinely began - the end of the file, Escape, or a rare
 * mid-playback error this file has no verified ABI to tell apart from a natural end - counts as
 * handled, matching how the retail player itself moves on regardless of why a movie stopped.
 *
 * The pump is SCOPED TO `window`, the overlay's own handle, rather than run over the whole thread
 * queue. This call runs in-process on the game's own thread, so the game window's posted traffic
 * sits on that same queue, and an unscoped peek retrieves it. PM_REMOVE takes a message off the
 * queue whether or not it is then dispatched, so a loop that retrieved the game's messages and
 * declined to dispatch them would not be DEFERRING them, it would be DISCARDING them: every
 * WM_MOUSEMOVE made during a movie thrown away rather than delivered late, and the engine coming
 * out of each movie having silently missed all of it. Scoping the peek is what stops that. The
 * game's traffic simply waits, in order, for its own pump to resume.
 *
 * `game_window`, if not NULL, is looked at but never emptied. It is peeked with PM_NOREMOVE for
 * the two messages that BEGIN a close - Alt+F4 as WM_SYSKEYDOWN/VK_F4, the close box as
 * WM_NCLBUTTONDOWN/HTCLOSE - and only one of those is then removed and immediately re-posted, so
 * the request survives to be honoured by the engine's own window procedure once this returns.
 * Without it the game cannot be closed until the movie ends, which for the credits is minutes and
 * which the retail Bink path does not do. WM_QUIT needs no handling here: it is a thread message
 * with no window, a scoped peek never retrieves it, and it stays queued for the game's own pump.
 *
 * What none of this touches, because it has been claimed here before and is worth stating plainly:
 * SENT messages. The MSG that PeekMessageW fills in only ever carries queued messages, while a
 * sent message - WM_ACTIVATEAPP, WM_SETCURSOR, WM_SETFOCUS, WM_WINDOWPOSCHANGED and the rest - is
 * delivered by the system straight to the target window procedure from inside the PeekMessageW
 * call itself and never appears in the MSG at all. So an Alt-Tab during a movie does reach the
 * engine's window procedure, re-entrantly, on the thread that is parked here, and no peek filter
 * of any shape changes that. The reason this design holds is that the overlay is in-process, so
 * Windows raises no WM_ACTIVATEAPP for it becoming topmost; that is one reason, not two. */
bool vlc_playback_play_blocking(HWND window, const wchar_t *file_path, HWND game_window);

#endif /* VLC_PLAYBACK_H */
