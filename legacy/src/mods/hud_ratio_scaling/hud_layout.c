#include "hud_layout.h"

#include <math.h>
#include <stdbool.h>

/* The fractions the engine lays the HUD out with. Every one of them is a float constant read out
 * of the executable's data section, so they are written here to the precision the engine has. */
#define BAR_WIDTH_FRACTION    0.2f          /* of the width  */
#define BAR_HEIGHT_FRACTION   0.06666667f   /* of the height */
#define ICON_WIDTH_FRACTION   0.2f          /* of the HEIGHT; this is the one that already fits */

/* The engine's own absolute pixel terms: the bottom row sits one pixel clear of the last scanline,
 * and the weapon icon starts one pixel after the bar it follows. */
#define BOTTOM_MARGIN 1.0f
#define ICON_GAP      1.0f

/* A rectangle counts as a match when it is within this many pixels of the formula. Two is enough
 * for the +-1 the engine itself adds and for float rounding, and tight enough that an unrelated
 * blit cannot pass by accident.
 *
 * The classifier sees rectangles it does not own. The gate it runs under is "the HUD-draw function
 * is on the stack", and any other DLL that chains onto that function draws its own sprites inside
 * exactly that window. Those rectangles arrive here and must fall out as HUD_BLOCK_NONE. That is
 * why each of the four tests below pins three edges of its block rather than one, and why this
 * tolerance must not be widened: the separation that keeps a foreign rectangle out is the same
 * separation that keeps the four blocks apart from each other. The tightest of those is the
 * health bottom against the force bottom, which are 0.06667*H + 1 apart, 33 px at 640x480, and
 * never within twice this tolerance at any display size the caller accepts. */
#define MATCH_TOLERANCE 2.0f

/* At 4:3 the width is exactly this multiple of the height. */
#define AUTHORED_WIDTH_UNITS  4.0f
#define AUTHORED_HEIGHT_UNITS 3.0f

static bool close_to(float value, float expected)
{
    return fabsf(value - expected) <= MATCH_TOLERANCE;
}

static bool screen_is_usable(float screen_width, float screen_height)
{
    return screen_width > 0.0f && screen_height > 0.0f;
}

hud_multipliers_t hud_multipliers(float screen_width, float screen_height,
                                  float scale, bool square)
{
    hud_multipliers_t out;

    out.horizontal = scale;
    out.vertical   = scale;

    if (!square || !screen_is_usable(screen_width, screen_height)) {
        return out;
    }

    /* Written as (4*H)/(3*W) and not as H*(4/3)/W on purpose: at every 4:3 mode 4*H and 3*W are
     * the same integer, so the quotient is exactly 1.0 and the identity holds bit for bit. The
     * other spelling reaches 1.0 only by rounding. */
    out.horizontal = ((AUTHORED_WIDTH_UNITS * screen_height) /
                      (AUTHORED_HEIGHT_UNITS * screen_width)) * scale;
    return out;
}

hud_block_t hud_classify(const hud_rect_t *rect, float screen_width, float screen_height)
{
    float bar_width;
    float bottom_row;
    float force_bottom;

    if (rect == NULL || !screen_is_usable(screen_width, screen_height)) {
        return HUD_BLOCK_NONE;
    }

    bar_width    = screen_width * BAR_WIDTH_FRACTION;
    bottom_row   = screen_height - BOTTOM_MARGIN;
    force_bottom = bottom_row - screen_height * BAR_HEIGHT_FRACTION - BOTTOM_MARGIN;

    /* Escort: the only block anchored to the top, and the only centred one. */
    if (close_to(rect->top, 0.0f) &&
        close_to(rect->bottom, screen_height * BAR_HEIGHT_FRACTION) &&
        close_to(rect->left, (screen_width - bar_width) * 0.5f) &&
        close_to(rect->right - rect->left, bar_width)) {
        return HUD_BLOCK_ESCORT;
    }

    /* Weapon icon: it starts where the bar ends and takes its width from the height. */
    if (close_to(rect->bottom, bottom_row) &&
        close_to(rect->left, bar_width + ICON_GAP) &&
        close_to(rect->right - rect->left, screen_height * ICON_WIDTH_FRACTION)) {
        return HUD_BLOCK_WEAPON;
    }

    /* Health bar: hard against the left edge, on the bottom row, width from the screen width. */
    if (close_to(rect->bottom, bottom_row) &&
        rect->left <= MATCH_TOLERANCE &&
        close_to(rect->right, bar_width)) {
        return HUD_BLOCK_HEALTH;
    }

    /* Force bar: the health bar's rectangle lifted by one bar height, and by one more pixel
     * because the engine subtracts its 1.0 margin a second time on the way up. */
    if (close_to(rect->bottom, force_bottom) &&
        rect->left <= MATCH_TOLERANCE &&
        close_to(rect->right, bar_width)) {
        return HUD_BLOCK_FORCE;
    }

    return HUD_BLOCK_NONE;
}

hud_block_t hud_block_for_number(float x, float screen_width)
{
    if (!(screen_width > 0.0f)) {
        return HUD_BLOCK_NONE;
    }
    if (x > screen_width * BAR_WIDTH_FRACTION) {
        return HUD_BLOCK_WEAPON;
    }
    return HUD_BLOCK_HEALTH;
}

/* The weapon icon's left edge, which is the anchor both the icon and the ammo count are measured
 * from. It is the one term of that block the engine took from the screen width. */
static float icon_left_edge(float screen_width)
{
    return screen_width * BAR_WIDTH_FRACTION + ICON_GAP;
}

hud_rect_t hud_transform(const hud_rect_t *rect, hud_block_t block,
                         float screen_width, float screen_height,
                         float scale, bool square)
{
    hud_rect_t        out = *rect;
    hud_multipliers_t m   = hud_multipliers(screen_width, screen_height, scale, square);

    if (block == HUD_BLOCK_NONE) {
        return out;
    }

    if (block == HUD_BLOCK_ESCORT) {
        /* Centred horizontally, anchored to the top edge. */
        float centre = screen_width * 0.5f;

        out.left   = centre + (rect->left  - centre) * m.horizontal;
        out.right  = centre + (rect->right - centre) * m.horizontal;
        out.top    = rect->top    * m.vertical;
        out.bottom = rect->bottom * m.vertical;
        return out;
    }

    if (block == HUD_BLOCK_WEAPON) {
        /* The two x terms come from different places and therefore need different multipliers:
         * the left edge is 0.2*W+1 and the width is 0.2*H. Correcting the left edge is what
         * carries the icon inwards as the bar in front of it shrinks; leaving the width alone is
         * what keeps the icon square. Exempting the whole block instead opens a hole between the
         * bar and the icon exactly as wide as the bar lost. */
        out.left   = rect->left * m.horizontal;
        out.right  = out.left + (rect->right - rect->left) * m.vertical;
        out.top    = screen_height - (screen_height - rect->top)    * m.vertical;
        out.bottom = screen_height - (screen_height - rect->bottom) * m.vertical;
        return out;
    }

    /* The two bars: anchored to the left edge and to the bottom edge, both x extents from W. */
    out.left   = rect->left  * m.horizontal;
    out.right  = rect->right * m.horizontal;
    out.top    = screen_height - (screen_height - rect->top)    * m.vertical;
    out.bottom = screen_height - (screen_height - rect->bottom) * m.vertical;
    return out;
}

void hud_transform_point(float *x, float *y, hud_block_t block,
                         float screen_width, float screen_height,
                         float scale, bool square)
{
    hud_multipliers_t m;

    if (block == HUD_BLOCK_NONE || x == NULL || y == NULL) {
        return;
    }

    m = hud_multipliers(screen_width, screen_height, scale, square);

    if (block == HUD_BLOCK_ESCORT) {
        float centre = screen_width * 0.5f;

        *x = centre + (*x - centre) * m.horizontal;
        *y = *y * m.vertical;
        return;
    }

    if (block == HUD_BLOCK_WEAPON) {
        /* Measured from the icon's left edge with the icon's own multiplier, so the ammo count
         * stays where it sits inside the icon instead of drifting across it. */
        float left = icon_left_edge(screen_width);

        *x = left * m.horizontal + (*x - left) * m.vertical;
        *y = screen_height - (screen_height - *y) * m.vertical;
        return;
    }

    *x = *x * m.horizontal;
    *y = screen_height - (screen_height - *y) * m.vertical;
}

void hud_glyph_scale(float *horizontal, float *vertical,
                     float screen_width, float screen_height,
                     float scale, bool square)
{
    hud_multipliers_t m;

    if (horizontal == NULL || vertical == NULL) {
        return;
    }

    /* The renderer draws at (sx*W/640, sy*H/480). The bars grow by H/480 under squaring, so for
     * the digits to grow by the same factor the horizontal has to be divided by W/H against the
     * authored aspect, which is precisely the horizontal multiplier the rectangles use. */
    m = hud_multipliers(screen_width, screen_height, scale, square);

    *horizontal = *horizontal * m.horizontal;
    *vertical   = *vertical   * m.vertical;
}

float hud_square_glyph_scale(float horizontal_scale, int width, int height)
{
    if (width <= 0 || height <= 0) {
        return horizontal_scale;
    }
    return horizontal_scale * ((AUTHORED_HEIGHT_UNITS * (float)width) /
                               (AUTHORED_WIDTH_UNITS * (float)height));
}
