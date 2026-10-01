/* overlay_draw.h: paints the panel into the frame the game is already drawing.
 *
 * There is no window and no second surface. The panel is drawn with the engine's own two
 * dimensional primitives from inside the frame, just before the scene is closed, so it composites
 * with the finished picture the way the game's own letterbox bars do. It therefore survives a
 * full screen mode, costs nothing in focus, and blends with real alpha per shape.
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

/* The panel's size is owned by dev_menu_size_row.c and asked for while drawing, not set here. */

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
 * keeps following the pointer even when the hand wanders off the row, as every slider does, and
 * a drag therefore never jumps to the row below. */
bool overlay_draw_slider_fraction(int32_t index, float x, float *fraction);

/* The row whose segment strip is under (x, y), and which of its words, written to `segment`. -1
 * when that row has none or the point is past either end of the strip.
 *
 * Answered from what the last paint drew, the same way a track is and for the same reason: a box
 * is as wide as its own word, and a second derivation of that would land a click on the word
 * beside the one under the pointer. */
int32_t overlay_draw_segment_at(float x, float y, int32_t *segment);

/* The row whose Default button was under the pointer, or -1. Asked before the track below it,
 * because a grab is allowed a little outside each end of the track and that slack reaches into
 * the button. -1 for a track row with no Default, which is one whose row named no standard and
 * one on a panel too narrow to have drawn the button at all. */
int32_t overlay_draw_default_at(float x, float y);

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

/* For marks drawn beside the panel rather than in it, the placement mode's: text whose baseline
 * starts at a pixel, its width and the height of a capital, and the engine's own pointer. Each
 * prepares the font itself, since whatever else the frame drew has left it as it pleased. Nothing
 * is drawn, and 0 is answered, before a display mode. */
void  overlay_draw_note(const char *text, float x, float y, uint32_t argb);
float overlay_draw_note_width(const char *text);
float overlay_draw_note_height(void);
void  overlay_draw_pointer_at(float x, float y);

/* The four marks the panel is made of, exported because overlay_frame.c paints its bands with the
 * same ones. A second copy of any of them is the shape that comes apart: the fill goes through
 * common/screen_fill.c, whose vertices every driver draws, and the two text calls set the font up
 * before they touch it, which is the defect this whole file was written around.
 *
 * `overlay_draw_write_in` centres a string in a band of `height` starting at `y`. The engine's
 * text grows upward from the position it is given, so that position is a baseline and not a top
 * edge, and centring a glyph box of one text height therefore puts the baseline at the BOTTOM of
 * that box. Treating it as a corner put every label at the top of its own band with the rule and
 * the chip under it. */
/* `what` shortened until it fits `room`, ending in two dots when it had to be cut, written into
 * `scratch`. Answers `what` itself when it already fits, so a caller keeps its own string in the
 * ordinary case. Exported because the refusal band above the footer is drawn in
 * overlay_frame.c and would otherwise be the one string in this panel never measured against the
 * room it has. */
const char *overlay_draw_fit(const char *what, float room, char *scratch, size_t scratch_size);

void  overlay_draw_fill(float x0, float y0, float x1, float y1, uint32_t argb);
void  overlay_draw_outlined(float x0, float y0, float x1, float y1, uint32_t border,
                            uint32_t inner);
void  overlay_draw_write_in(const char *what, float x, float y, float height, uint32_t argb);
float overlay_draw_width(const char *what);

#endif /* OVERLAY_DRAW_H */
