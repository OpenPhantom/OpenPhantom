/* menu_island_clip.h: a menu may only draw where a menu can erase.
 *
 * ==============================================================================================
 * The defect this repairs, measured on a 3840x2160 session
 *
 * With MenuKeepsResolution=1 the menus run as a 640x480 island in the middle of the display mode
 * instead of bolting the mode down to 640x480. The menu toolkit draws its widgets through the
 * engine's textured-sprite blitter, which clips against the SCREEN, but it repairs itself in
 * canvas coordinates that are hard-clipped to 640x480, so any sprite that reaches past the
 * island's edge is drawn once and can never be erased. The hovered button's glow does exactly
 * that: authored at canvas x=7 with a halo wider than its plate, it pokes a few pixels past the
 * island's left border, and every hover stamps a blue smear onto the border that stays until the
 * screen closes. At real 640x480 the screen edge IS the canvas edge and the rasterizer cropped
 * that halo for free, which is why the retail arrangement never showed it.
 *
 * The repair clamps the sprite rectangle to the island, but only while the engine's own "the
 * menu's widgets are drawing" flag is up, so the gameplay HUD, the frozen pause backdrop and
 * everything else that shares the blitter passes through untouched. Details, byte evidence and
 * the honest limitations (a clamped sprite is squashed by the poked-out fraction where the retail
 * rasterizer cropped it, and a partly drawn sprite is left alone entirely) are at the top of
 * menu_island_clip.c.
 * ============================================================================================ */
#ifndef MENU_ISLAND_CLIP_H
#define MENU_ISLAND_CLIP_H

#include <stdbool.h>

/* The menu toolkit's whole world. The engine centres this island by adding
 * g_menuOrigin = ((W-640)/2, (H-480)/2) in the draw path and the hit test, and clips every
 * canvas blit against exactly these two numbers. */
#define MENU_ISLAND_WIDTH  640
#define MENU_ISLAND_HEIGHT 480

/* The arithmetic alone, exposed so the unit test can drive it without a game.
 *
 * Clamps the rectangle in place to [island_left, island_left+640] x [island_top, island_top+480].
 * Returns false when the rectangle lies entirely outside the island, which means the draw should
 * be skipped; the engine's own 640x480 canvas clip would have dropped it the same way. A
 * rectangle whose bounds are reversed or unordered (NaN included) is not ours to judge and is
 * passed through untouched with a true return. */
bool menu_island_clip_rect(float *left, float *right, float *top, float *bottom,
                           float island_left, float island_top);

/* True when the blitter's `fill` argument means "draw all of it", which is the only case in which
 * clamping the rectangle is the same operation the retail screen edge performed. Exposed for the
 * test; menu_island_clip.c's header explains what the blitter does with any other value. */
bool menu_island_clip_fill_is_whole(float fill);

/* Resolves its two sites and installs the sprite-blitter detour. Returns true only when the
 * clamp is really in force. Call AFTER window_fit_install(): the island origin is derived from
 * window_fit_current_mode_size(), and that accessor is resolved there. */
bool menu_island_clip_install(bool enabled);

#endif /* MENU_ISLAND_CLIP_H */
