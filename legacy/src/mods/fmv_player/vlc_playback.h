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
 * and creating one shared instance for the life of the process, ALL OF IT on a background thread,
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

/* Letterbox (false, and the default) keeps the movie's own shape and puts black bars wherever the
 * screen does not match it. Stretch (true) pulls the picture out to fill the window exactly.
 *
 * The source movies are roughly 4:3 and almost no screen is, so one of the two has to happen and
 * neither is right for everyone. Call it before the first movie. It applies to every movie after
 * that and needs no re-encoding, so it also works on files converted before this setting existed.
 * A libVLC without the export it needs stays on letterbox and says so once. */
void vlc_playback_set_stretch(bool stretch);

/* WHICH VIDEO OUTPUT LIBVLC USES. An empty name, which is the default, leaves libVLC to choose, so
 * an installation that says nothing behaves exactly as it always has.
 *
 * This exists for the same Wine defect video_overlay.h describes: the cutscenes play and the menu
 * that follows is black, with the game still running, its sound audible and its frames still being
 * drawn. What that header assumed was the WINDOW turned out not to be: drawing the movie into a
 * child of the game's own window, which adds no window at all at the X11 level, made no difference.
 *
 * What is left is the DEVICE. libVLC's default video output on Windows is Direct3D, so playing a
 * movie creates a SECOND Direct3D device while the engine is holding an exclusive mode one through
 * dxwrapper. That is window independent, which is exactly the shape of what was measured. An output
 * that paints through GDI instead creates no device and cannot take the engine's away.
 *
 * Passed straight to libVLC as --vout=NAME, so the names are libVLC's own: "gdi" for the plain
 * Windows blitter, "opengl" or "gl", "direct3d9", "direct3d11". A name libVLC does not know makes
 * it fall back to its own choice rather than fail, so a typo costs a wrong output and not a movie.
 *
 * Must be called before vlc_playback_init_async: the argument is read once, when the instance is
 * created on the worker thread. */
void vlc_playback_set_video_output(const char *name);

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
 * original path. Anything after it genuinely began, whether the end of the file, Escape, or a
 * mid-playback error this file has no verified ABI to tell apart from a natural end, counts as
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
 * the one message that BEGINS a close, the close box as WM_NCLBUTTONDOWN/HTCLOSE, which is then
 * removed and immediately re-posted, so the request survives to be honoured by the engine's own
 * window procedure once this returns. Alt+F4 was handled here too and is not any more; see
 * vlc_playback.c for what went wrong with it.
 * Without it the game cannot be closed until the movie ends, which for the credits is minutes and
 * which the retail Bink path does not do. WM_QUIT needs no handling here: it is a thread message
 * with no window, a scoped peek never retrieves it, and it stays queued for the game's own pump.
 *
 * What none of this touches, because it has been claimed here before and is worth stating plainly:
 * SENT messages. The MSG that PeekMessageW fills in only ever carries queued messages, while a
 * sent message, WM_ACTIVATEAPP, WM_SETCURSOR, WM_SETFOCUS, WM_WINDOWPOSCHANGED and the rest, is
 * delivered by the system straight to the target window procedure from inside the PeekMessageW
 * call itself and never appears in the MSG at all. So an Alt-Tab during a movie does reach the
 * engine's window procedure, re-entrantly, on the thread that is parked here, and no peek filter
 * of any shape changes that. The reason this design holds is that the overlay is in-process, so
 * Windows raises no WM_ACTIVATEAPP for it becoming topmost; that is one reason, not two. */
bool vlc_playback_play_blocking(HWND window, const wchar_t *file_path, HWND game_window);

#endif /* VLC_PLAYBACK_H */
