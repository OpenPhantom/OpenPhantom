/* hud_layout.h: the arithmetic that resizes a HUD block, with no engine in it.
 *
 * The HUD is four blocks. Every rectangle is in framebuffer pixels and every one of them is
 * rebuilt from the live screen size on every frame, so nothing here has to be invalidated when
 * the display mode changes. Written out, with W and H the framebuffer size:
 *
 *     health bar     x 1 .. 0.2*W           y 0.93333*H .. H-1
 *     force bar      the same x             y 0.93333*H-0.06667*H-1 .. H-2-0.06667*H
 *     weapon icon    x 0.2*W+1 .. +0.2*H    y 0.93333*H-1 .. H-1
 *     escort bar     x centred, 0.2*W wide  y 0 .. 0.06667*H
 *
 * The force bar subtracts 1.0 TWICE. Each of its two edges is built as
 * "the matching health edge - 0.06667*H - 1", and the health bottom edge is already H-1, so the
 * force bottom lands on H-2-0.06667*H, not on H-1-0.06667*H. It is one pixel, and it is the
 * difference between a recogniser that has its whole tolerance left and one that has half of it.
 *
 * Two things follow from the table, and they are the whole reason this file exists.
 *
 * The bars stretch, the icon does not. A bar is 0.2*W wide and 0.06667*H tall, so its shape
 * depends on the aspect ratio: 4:1 at 4:3, 5.33:1 at 16:9. The weapon icon takes its WIDTH from
 * the height and therefore keeps its shape everywhere, but its LEFT EDGE still comes from the
 * width, so the icon is not exempt from the correction, it just needs a different multiplier on
 * each of the two terms.
 *
 * Nothing gets bigger relative to the screen. Every extent is a fixed fraction, so the HUD is
 * always the same share of the picture no matter the resolution. On a large screen that reads as
 * small. A single scale factor applied to every extent, about the edge each block is anchored
 * to, so nothing walks off screen, is the knob for that.
 *
 * The unit is a multiplier on what the engine already computed, never a re-derivation from the
 * authored 640x480 constants. That is what makes the identity exact: the engine's own fixed-pixel
 * terms (the left edge at 1, the bottom at H-1, the 1-pixel gap between bar and icon) are carried
 * through untouched whenever the multiplier is 1.
 *
 * A rectangle is only ever changed when it matches one of the four formulas above. Anything else
 * passes through untouched, and that is not only about the engine: the sprite blitter draws far
 * more than the HUD, and another DLL that chains onto the HUD-draw function can push its own
 * rectangles through the same gate.
 */
#ifndef HUD_LAYOUT_H
#define HUD_LAYOUT_H

#include <stdbool.h>

typedef enum hud_block {
    HUD_BLOCK_NONE = 0,   /* not one of ours, leave it alone */
    HUD_BLOCK_HEALTH,     /* health bar: bottom left, both x extents from W */
    HUD_BLOCK_FORCE,      /* force bar: above the health bar, same x, Jedi heroes only */
    HUD_BLOCK_WEAPON,     /* weapon icon: left edge from W, width from H */
    HUD_BLOCK_ESCORT      /* escort/boss bar: top centre, width from W */
} hud_block_t;

typedef struct hud_rect {
    float left;
    float right;
    float top;
    float bottom;
} hud_rect_t;

/* The two multipliers the whole feature is built from. One is applied to every extent the engine
 * derived from the screen WIDTH, the other to every extent it derived from the HEIGHT. */
typedef struct hud_multipliers {
    float horizontal;
    float vertical;
} hud_multipliers_t;

/* horizontal = (square ? 4H/3W : 1) * scale,  vertical = scale.
 * 4H/3W is exactly 1.0 at 4:3 because 4H and 3W are then the same integer, so "square" is the
 * identity there by construction rather than by rounding. On a 5:4 screen it is 1.0667, i.e.
 * squaring makes the bars WIDER there, not narrower.
 * An unusable screen size skips the width correction and applies the scale only. */
hud_multipliers_t hud_multipliers(float screen_width, float screen_height,
                                  float scale, bool square);

/* Recognises a rectangle by the formula that produced it. `screen_width` and `screen_height` are
 * the framebuffer size the engine laid it out with. Returns HUD_BLOCK_NONE for everything else,
 * including a foreign rectangle that reached the classifier through the same gate. */
hud_block_t hud_classify(const hud_rect_t *rect, float screen_width, float screen_height);

/* Which block a HUD number belongs to. There are exactly two numbers: the health value centred on
 * the health bar, and the ammo count centred on the weapon icon. The bar ends at 0.2*W and the
 * icon starts one pixel further right, so the x coordinate alone decides. */
hud_block_t hud_block_for_number(float x, float screen_width);

/* Returns the rectangle to draw, each block scaled about the edge it is anchored to. Identity at
 * scale 1.0 with squaring off, and identity at scale 1.0 on a 4:3 screen either way. */
hud_rect_t hud_transform(const hud_rect_t *rect, hud_block_t block,
                         float screen_width, float screen_height,
                         float scale, bool square);

/* The same transform applied to a point, for the numbers drawn inside the blocks. A point in the
 * weapon block is measured from the icon's left edge, so it follows the icon rather than sliding
 * across it. */
void hud_transform_point(float *x, float *y, hud_block_t block,
                         float screen_width, float screen_height,
                         float scale, bool square);

/* The glyph scale for text drawn INSIDE the HUD. The renderer multiplies the caller's pair by
 * (W/640, H/480), so this pair is what makes the digits grow by exactly the factor the bars grow
 * by instead of by the width ratio. Identity at scale 1.0 with squaring off. */
void hud_glyph_scale(float *horizontal, float *vertical,
                     float screen_width, float screen_height,
                     float scale, bool square);

/* The glyph rule for text drawn ANYWHERE ELSE: sy that makes a glyph square. The renderer
 * multiplies the caller's scale by (W/640, H/480), so a uniform pair still draws 4/3 too wide at
 * 16:9. This one raises the vertical rather than lowering the horizontal, because the menus
 * already pass a hand-corrected horizontal and lowering it would make menu text smaller than it
 * has ever been. Identity at 4:3 by construction. */
float hud_square_glyph_scale(float horizontal_scale, int width, int height);

#endif /* HUD_LAYOUT_H */
