/* overlay_draw.h: paints the panel into the frame the game is already drawing.
 *
 * There is no window and no second surface. The panel is drawn with the engine's own two
 * dimensional primitives from inside the frame, just before the scene is closed, so it composites
 * with the finished picture the way the game's own letterbox bars do. That is what makes it survive
 * a full screen mode, cost nothing in focus, and blend with real alpha per shape.
 *
 * Everything is in real screen pixels. The proportions are in overlay_layout.c and are multiples of
 * the font's measured height; the entry points are resolved in overlay_sites.c. What is here is
 * colour, order and the shapes themselves.
 */
#ifndef OVERLAY_DRAW_H
#define OVERLAY_DRAW_H

#include <stdbool.h>
#include <stdint.h>

/* The resolution the game's own screens were authored for. Only the glyph scale uses these now: the
 * font layer multiplies by the display over these two, and cancelling that is what gives text at
 * its authored size in real pixels. */
#define OVERLAY_AUTHORED_WIDTH   640.0f
#define OVERLAY_AUTHORED_HEIGHT  480.0f

/* Resolves the drawing entry points and the screen size. False when any is missing, and then the
 * overlay refuses to open rather than becoming an invisible modal state. Says which one failed. */
bool overlay_draw_resolve(void);

/* Which of the font layer's three alignment modes starts a string where it is put. Mode 0 centres
 * it, which is established; the other two are left and right in an order the bytes do not say. */
void overlay_draw_set_align(int32_t mode);

/* The panel's size is owned by dev_menu_size_row.c and asked for while drawing, not set from here. */

/* Paints the current model, and answers whether it could. False means the engine has no display
 * mode to draw into, which the caller treats as "this panel is not being seen" rather than as a
 * frame to skip. */
bool overlay_draw_paint(void);

/* Which row the pointer is over, or -1. The coordinates are the screen pixels the paint uses. */
int32_t overlay_draw_row_at(float x, float y);

/* The row whose slider track is under (x, y), and where along it, 0 at the left end and 1 at the
 * right. -1 when that row has no track or the point is past either end of it.
 *
 * Answered from what the last paint actually drew rather than recomputed, because a track's ends
 * depend on the fitted label and the chip beside it, and a second derivation of that would drift
 * from the first. */
int32_t overlay_draw_slider_at(float x, float y, float *fraction);

/* Where `x` falls along the track drawn for row `index` on the last paint, ignoring the pointer's
 * height entirely. False when that row had no track or has scrolled out of the panel.
 *
 * A drag in progress uses this rather than the hit test above: once a handle has been grabbed it
 * keeps following the pointer even when the hand wanders off the row, which is what every slider
 * does and what stops a drag from jumping to the row below. */
bool overlay_draw_slider_fraction(int32_t index, float x, float *fraction);

/* Which tab the pointer is over, or -1. */
int32_t overlay_draw_tab_at(float x, float y);

/* True when the pointer is over the search field's own box, the same rectangle overlay_draw.c
 * fills and outlines. What a click here focuses, so typing goes into the box only once it has
 * been clicked into rather than the moment the panel opens. */
bool overlay_draw_search_at(float x, float y);

/* The panel's left and top edge in authored units, so a caller can keep a pointer inside it. */
/* What the engine thinks the screen is, which is the space the panel and the pointer share. False
 * before a display mode has been chosen, and then neither is written. */
bool overlay_draw_screen(float *out_width, float *out_height);

float overlay_draw_left(void);
float overlay_draw_top(void);
float overlay_draw_height(void);

#endif /* OVERLAY_DRAW_H */
