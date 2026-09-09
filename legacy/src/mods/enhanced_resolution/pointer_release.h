/* pointer_release.h: a key that hands the mouse pointer back to Windows.
 *
 * ==============================================================================================
 * Why a window with a frame needs this at all
 *
 * Two separate things hold the pointer inside the game, and switching off the one that belongs to
 * this project is not enough.
 *
 * The first is ours, focus_guard's ClipCursor, and it yields to THIS: it stops confining while the
 * pointer is released and confines again the moment it is handed back. Tying it to the window mode
 * instead was tried and was wrong, because it left the pointer loose for the whole session, and the
 * clip is what stops it escaping during fast mouse movement. The engine's warp fires on mouse
 * messages, and a movement quick enough crosses the window edge between two of them.
 *
 * The second is the engine's, and it is not a clip. control_recentreMouse warps the pointer back
 * to the middle of the client area on every mouse message, which is how a 1999 engine reads a
 * relative mouse: it moves the pointer to a known place, and the distance the next message arrives
 * from is the delta. cursor_anchor.c repairs where that centre is when the window has moved, but
 * repairing it does not make it stop. The pointer can never be walked out of the window, because
 * every move that would carry it towards the edge is answered by a warp back to the middle.
 *
 * The engine also holds SetCapture while it is the active application, so even a pointer that did
 * escape would send its clicks to the game rather than to the close button under it.
 *
 * ==============================================================================================
 * What this does
 *
 * One key toggles. While released, the warp is skipped and the capture is dropped, so the pointer
 * behaves like any other Windows pointer: it leaves the window, it reaches the title bar and the
 * buttons on it, and it clicks what is under it. Pressing the key again gives the pointer back to
 * the game.
 *
 * The engine is told nothing. It sees no mouse messages while the pointer is outside its window
 * and is not capturing, so no delta accumulates and the view does not lurch when the pointer comes
 * back. That is the reason this suppresses the warp rather than moving the pointer somewhere: a
 * warp the engine did not ask for is a delta it will believe.
 *
 * The pointer is invisible over the client area, because the engine answers WM_SETCURSOR with
 * SetCursor(NULL) and returns 1. It becomes the ordinary system arrow the moment it crosses onto
 * the frame, which is where it is needed, so it is not worth fighting the engine for the few
 * hundred pixels in between.
 */
#ifndef POINTER_RELEASE_H
#define POINTER_RELEASE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct pointer_release_config {
    /* A Windows virtual key code. Zero switches the feature off entirely, and then not one line of
     * it runs: no key is polled and the pointer keeps the behaviour every release has shipped. */
    int32_t key;
} pointer_release_config_t;

bool pointer_release_install(const pointer_release_config_t *config);

/* Rebinds the key while the game is running. 0 is accepted and switches the release off, which
 * also gives the pointer straight back rather than stranding it outside a window whose key no
 * longer exists. False when nothing was installed to rebind. */
bool pointer_release_set_key(int32_t virtual_key);

/* True while the pointer belongs to Windows rather than to the game. cursor_anchor.c asks this
 * before re-centring; the mechanism is no more than that. Safe to call before install, and false
 * then, so the caller needs no guard of its own. */
bool pointer_release_is_active(void);

#endif /* POINTER_RELEASE_H */
