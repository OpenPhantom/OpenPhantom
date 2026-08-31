/* menu_loading_bar.h: move the level loading bar with the scaled menu canvas.
 *
 * ==============================================================================================
 * What this is for
 *
 * The loading screen is a menu like any other and its picture scales with the canvas, but the
 * progress bar drawn on top of it is not a widget. menu_progressStep draws it by hand, straight
 * into the 3-D layer, at coordinates it works out as `g_uiOrigin + an authored pixel offset`:
 *
 *     black backdrop   (originX + 0x186, originY + 0x15e) size 0xf0 by 0x54
 *     the two sprites  (originX + 0x1be, originY + 400)   size 128 by 32
 *     the percentage   (originX + 0x1fe, originY + 0x17c), set once by ui_progress
 *
 * g_uiOrigin is the same menu origin menu_scale recentres, so those offsets are canvas units and
 * every one of them has to scale with the canvas. Without this they stay at their authored size in
 * the top left corner, because scaling the canvas to fill the screen drives the origin to 0,0 and
 * there is nothing left to carry them down and right.
 *
 * WHAT IS ALREADY RIGHT AND IS NOT TOUCHED HERE. The text SIZE, because ui_progress sets the glyph
 * scale from g_menuTextScale, which menu_scale already grows with the canvas. The picture behind
 * it, because it is an ordinary SW_PIC. Only the ten position and size numbers are wrong.
 */
#ifndef MENU_LOADING_BAR_H
#define MENU_LOADING_BAR_H

#include <stdbool.h>
#include <stdint.h>

/* Scales the bar's geometry to a canvas of `canvas_width` by `canvas_height` pixels.
 *
 * Pass the canvas menu_scale_canvas reports, so the two can never disagree, and call this AFTER
 * menu_scale_install for the same reason the cursor cage is installed after it. A 640x480 canvas
 * means the scale is not in force and this does nothing at all.
 *
 * Returns true when the geometry was scaled, false when it was left alone, whether because there
 * was nothing to do or because a site did not resolve. A failure here costs the bar's position and
 * nothing else, so it is never worth declining anything larger over. */
bool menu_loading_bar_install(int32_t canvas_width, int32_t canvas_height);

#endif /* MENU_LOADING_BAR_H */
