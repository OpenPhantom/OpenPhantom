/* overlay_layout.c: the panel's proportions.
 *
 * The multiples below are the only taste in the feature, and they are collected here so the whole
 * panel can be loosened or tightened in one place. Each one carries the reason it is that number.
 */
#include "overlay_layout.h"

#include "overlay_model.h"

#include <stdint.h>

/* The glyph box is one H, so a row of 1.375H carries 0.375H of leading, three pixels above and
 * three below at the normal size. Two words of this font stop merging at about four pixels of
 * separation, so six is one comfortable step past that and nothing more. Wider than this and a
 * list of single words starts reading as paragraphs, which is what made it feel sparse. */
#define ROW_H       1.375f

/* A band holds one word. It is a band, not a room. */
#define TITLE_H     1.75f
#define TAB_H       1.75f      /* the same as the title, so the top reads as one block of chrome */
#define SEARCH_H    2.00f   /* the field inside is 1.5H, a quarter H above and below */
#define FIELD_H     1.50f
#define CAP_H       0.25f   /* the accent bar: with one font size, weight comes from this */
#define BOTTOM_PAD  0.50f
#define EDGE_PAD    0.75f

/* The fixed furniture beside a name: its indent, the smallest gap worth bridging to the state, the
 * widest state chip, and the right hand padding. */
#define FURNITURE   8.00f
#define WIDTH_MIN  24.00f
#define WIDTH_MAX  32.00f

/* A tab is its word plus one H of padding on each side. */
#define TAB_PAD     1.00f
#define TAB_GAP     0.25f

/* Away from the middle of the screen, which is where the character and the aim are, and where the
 * point of a cheat panel is to watch the effect of a toggle while it is being toggled. Anchoring
 * also means the panel only ever grows downward: centred, unfolding a group moves every row that
 * is already on screen. */
#define ANCHOR      2.00f

static layout_t built;

static float clampf(float value, float low, float high)
{
    if (value < low)  { return low; }
    if (value > high) { return high; }
    return value;
}

void overlay_layout_build(float text_height, float content_width, uint32_t row_count,
                          const float *tab_widths, float screen_width, float screen_height)
{
    layout_t out;
    float    x;
    uint32_t i;

    out.text_h = (text_height > 0.0f) ? text_height : 12.0f;
    out.rule = (out.text_h / 16.0f < 1.0f) ? 1.0f : (float)(int32_t)(out.text_h / 16.0f + 0.5f);

    out.width = clampf(content_width + FURNITURE * out.text_h,
                       WIDTH_MIN * out.text_h, WIDTH_MAX * out.text_h);

    out.cap_h = CAP_H * out.text_h;
    out.title_h = TITLE_H * out.text_h;                 /* the band's bottom, cap included */
    out.tabs_top = out.title_h;
    out.tab_h = TAB_H * out.text_h;
    out.search_top = out.tabs_top + out.tab_h;
    out.search_h = SEARCH_H * out.text_h;
    out.rows_top = out.search_top + out.search_h;
    out.row_h = ROW_H * out.text_h;
    out.height = out.rows_top + (float)row_count * out.row_h + BOTTOM_PAD * out.text_h;

    x = EDGE_PAD * out.text_h;
    for (i = 0; i < OVERLAY_LAYOUT_TABS; ++i) {
        const float word = (tab_widths != NULL && tab_widths[i] > 0.0f)
                         ? tab_widths[i] : out.text_h * 4.0f;

        out.tab_x0[i] = x;
        out.tab_w[i] = word + TAB_PAD * 2.0f * out.text_h;
        x += out.tab_w[i] + TAB_GAP * out.text_h;
    }

    out.left = ANCHOR * out.text_h;
    out.top = ANCHOR * out.text_h;
    /* A panel that would not fit is pushed back inside rather than drawn off the edge. */
    if (out.left + out.width > screen_width) {
        out.left = screen_width - out.width - out.text_h;
    }
    if (out.top + out.height > screen_height) {
        out.top = screen_height - out.height - out.text_h;
    }
    if (out.left < 0.0f) { out.left = 0.0f; }
    if (out.top < 0.0f)  { out.top = 0.0f; }

    built = out;
}

const layout_t *overlay_layout(void)
{
    return &built;
}

float overlay_draw_height(void)
{
    return built.height;
}

float overlay_draw_left(void)
{
    return built.left;
}

float overlay_draw_top(void)
{
    return built.top;
}

/* The pointer tests read the layout the last paint built rather than recomputing, so what was drawn
 * and what can be clicked can never be a frame apart. Both refuse a zero band instead of dividing
 * by it, which is the state before the first paint. */
int32_t overlay_draw_row_at(float x, float y)
{
    const float top = built.top + built.rows_top;
    int32_t     index;

    if (x < built.left || x > built.left + built.width) {
        return -1;
    }
    if (y < top || !(built.row_h > 0.0f)) {
        return -1;
    }
    index = (int32_t)((y - top) / built.row_h);
    if (index < 0 || (uint32_t)index >= overlay_model_row_count()) {
        return -1;
    }
    return index;
}

/* The inactive tab is drawn without a fill, but it is still a target, so the hit test uses the
 * padded span the layout gave it rather than anything visible. */
int32_t overlay_draw_tab_at(float x, float y)
{
    const float top = built.top + built.tabs_top;
    uint32_t    i;

    if (y < top || y >= top + built.tab_h) {
        return -1;
    }
    for (i = 0; i < OVERLAY_LAYOUT_TABS; ++i) {
        const float x0 = built.left + built.tab_x0[i];

        if (x >= x0 && x < x0 + built.tab_w[i]) {
            return (int32_t)i;
        }
    }
    return -1;
}

/* The field's own box, the same rectangle overlay_draw.c fills and outlines: search_top plus a
 * quarter H above and below the 1.5H field it draws. Kept here rather than duplicated by eye in
 * two files, for the same reason the row and tab tests are here rather than in the painter. */
bool overlay_draw_search_at(float x, float y)
{
    const float right = built.left + built.width;
    const float fy0 = built.top + built.search_top + (built.search_h - 1.5f * built.text_h) * 0.5f;
    const float fy1 = fy0 + 1.5f * built.text_h;
    const float fx0 = built.left + EDGE_PAD * built.text_h;
    const float fx1 = right - EDGE_PAD * built.text_h;

    return x >= fx0 && x < fx1 && y >= fy0 && y < fy1;
}
