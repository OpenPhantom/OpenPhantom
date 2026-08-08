/* cursor_anchor.h: keep the engine's pointer re-centring anchored to the window we moved.
 *
 * Why this belongs to the resolution fix and not to the input fix
 *
 * The engine creates its window at X = 0, Y = 0 with style 0x80000000 (WS_POPUP, no frame) and
 * NEVER moves it: both of its own SetWindowPos calls carry SWP_NOMOVE and only ever resize. So in
 * the shape the engine ships in, the client area starts at screen (0,0), and the arithmetic that
 * re-centres the mouse pointer, which compares CLIENT coordinates against the SCREEN coordinates
 * it then warps to, is accidentally correct.
 *
 * window_fit.c is the only code in this tree that moves that window. The moment it puts the window
 * on a monitor whose origin is not (0,0), the engine warps the pointer to a screen point that is
 * not inside its own client area, and on a second monitor at -1920,0 that point is on the OTHER
 * display: the pointer leaves the game window and stays there.
 *
 * The DLL that breaks the assumption is the one that has to repair it, and it must repair it in
 * the same session and without a runtime dependency on any other feature DLL. That is why this
 * file sits next to window_fit.c.
 *
 * What it is not: this is not the confinement. The engine's own confinement is "warp the pointer
 * back to the middle of the client area on every mouse message"; this file repairs the coordinate
 * space that warp works in and nothing else. It calls neither ClipCursor nor SetCapture, so it
 * holds no state that could survive an Alt-Tab or a crash and has nothing to release on exit, and
 * it refuses to warp at all while the foreground window belongs to another process.
 *
 * The repair is necessary and it is not sufficient. A warp only happens when a mouse message
 * arrives, and a pointer that crosses the window edge between two messages stops producing them,
 * so a window smaller than the screen, or a screen with a second monitor next to it, leaks the
 * pointer no matter which space the warp is computed in. Holding it inside the client area is
 * focus_guard.c's job, because that needs process-global state and therefore needs a release path
 * for every way the foreground can be taken away.
 */
#ifndef CURSOR_ANCHOR_H
#define CURSOR_ANCHOR_H

#include <stdbool.h>
#include <stdint.h>

/* Resolves both sites, installs the detours and logs which branch it took. `enabled` is the
 * feature's ini gate. Returns true only when the pointer is really anchored from now on.
 *
 * `window_is_moved` changes NOTHING about the behaviour and only what the install line claims. It
 * has to, because the claim is otherwise false in the common case: on a window left at screen 0,0
 * this repair is the identity, bit for bit, so it is insurance and not the thing holding the
 * pointer in. Saying "the pointer is anchored" without that distinction would send the next reader
 * of the log looking here for a fault that lives somewhere else. */
bool cursor_anchor_install(bool enabled, bool window_is_moved);

/* ---- the two pure helpers, exposed because they are the arithmetic that can be tested ------- *
 * A window message packs its client coordinates into one dword as two SIGNED 16-bit values, and
 * the sign is load-bearing here: once the warp lands in the right place, a pointer pushed past the
 * left or top edge of the client area reports a NEGATIVE coordinate. Getting that wrong turns a
 * small movement to the left into a coordinate near 65000 and the anchor would fight itself. */
int  cursor_anchor_signed_word(uint32_t value);

/* True when the packed client point is exactly the (320,240) the engine treats as "centred",
 * the test its early-out makes, which is also the echo of our own warp. */
bool cursor_anchor_point_is_centre(uint32_t packed_client_point);

#endif /* CURSOR_ANCHOR_H */
