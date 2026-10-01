/* overlay_layout.c: the panel's proportions.
 *
 * The multiples below are the only taste in the feature, and they are collected here so the whole
 * panel can be loosened or tightened in one place. Each one carries the reason it is that number.
 */
#include "overlay_layout.h"

#include "overlay_look.h"
#include "overlay_model.h"
#include "overlay_notice.h"

#include <stdint.h>

/* The glyph box is one H, so a row of 1.875H carries 0.875H of leading, seven pixels above and
 * seven below at the normal size, which is about two millimetres of gap between one row's text and
 * the next on a desktop monitor.
 *
 * It was 1.375H, chosen when this tab held five cheats and a list of single words did start
 * reading as paragraphs once the leading went past that. Nineteen rows in two groups is a
 * different problem: there the eye has to track along a row to its chip and back down to the next
 * one, and the tighter spacing made neighbouring rows hard to tell apart. Lines between the rows
 * were tried first and looked worse than the problem.
 *
 * In H rather than pixels, so it scales with the dev menu size like everything else here: raising
 * that setting widens this gap in proportion rather than leaving it fixed while the text grows. */
#define ROW_H       1.875f

/* A band holds one word. It is a band, not a room. */
#define TITLE_H     1.75f
#define TAB_H       1.75f      /* the same as the title, so the top reads as one block of chrome */
/* The footer, the same height as the title band, so the list is framed by two bands of one size.
 * It carries key caps, and a cap is a box around a word: the word's own H plus a quarter above and
 * below, which is why it is not the 1.5H a bare line of text would want. */
#define FOOT_H      1.75f
#define SEARCH_H    2.00f   /* the field inside is 1.5H, a quarter H above and below */
/* The refusal band, the same height as the title and the footer: it holds one sentence, and a
 * band holds one word. It costs this only while something stands in it and nothing at all
 * otherwise, which is the whole of what keeps the rows where they were.
 *
 * It sits at the BOTTOM, directly above the footer. Under the search box it would push `rows_top`
 * down by 0.93 of a row the moment it appeared, and a refusal appears exactly when somebody has
 * just clicked something and is about to click it again: that second click would land on the row
 * above the one aimed at, with nothing but the fill under a pointer that never moved to say so.
 * Down here `rows_top` does not move at all, every row keeps the y it was drawn at, and what the
 * band takes is the LAST row that fits, which is the row furthest from the hand. */
#define NOTICE_H    1.75f
#define FIELD_H     1.50f
#define CAP_H       0.25f   /* the accent bar: with one font size, weight comes from this */
#define BOTTOM_PAD  0.50f

/* The furniture beside a name AND its chip: the name's indent, the smallest gap worth bridging
 * between the two, one H of padding inside the chip, and the right hand padding. It is
 * NAME_X + GUTTER_MIN + 1 + OVERLAY_EDGE_PAD from overlay_look.h, and it is exact rather
 * than an estimate.
 *
 * It was 8.00 and it included "the widest state chip" as part of that guess, and that left a
 * label clipped after the width ceiling was raised: a chip reading "RUN" costs about 2.4H, one
 * reading "auto 1.00x" costs nearly 9, and no single number covers both. The caller now measures
 * the widest chip as well and hands both in, so what is left here is only the part that really is
 * fixed. */
#define FURNITURE   6.00f
#define WIDTH_MIN  24.00f

/* The ceiling on the panel's width, and it is a ceiling rather than a size: a tab whose longest
 * label is short still gets a narrow panel. It was 32, which was enough while the widest label was
 * a cheat name, and then rows arrived whose labels are half again as long and the longest of them
 * were drawn clipped, with the end of the sentence replaced by an ellipsis in the middle of the
 * panel.
 *
 * Raised to fit the longest name and chip this panel can produce together, with the unit test for
 * that in unittests/overlay_model.c so text written past it fails a build rather than reaching a
 * screenshot. */
#define WIDTH_MAX  46.00f

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

/* See the header. The scroll is nudged by exactly as much as the selection has left the window by,
 * so nothing here has to know how long the list is. */
void overlay_layout_reveal_selection(void)
{
    const uint32_t visible = built.visible_rows;
    const int32_t  at = overlay_model_selected();
    uint32_t       first;

    if (at < 0 || visible == 0u) {
        return;
    }
    first = overlay_model_scroll(visible);
    if ((uint32_t)at < first) {
        overlay_model_scroll_by(at - (int32_t)first);
    } else if ((uint32_t)at >= first + visible) {
        overlay_model_scroll_by((int32_t)((uint32_t)at - (first + visible - 1u)));
    }
}

void overlay_layout_build(float text_height, float content_width, uint32_t row_count,
                          const float *tab_widths, float screen_width, float screen_height)
{
    const uint32_t was_visible = built.visible_rows;
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
    /* The rows begin under the search box whether or not anything stands in the refusal band:
     * the band is below them now, see NOTICE_H. */
    out.rows_top = out.search_top + out.search_h;
    /* Asked, not passed in, because everything that draws this panel and everything that clicks
     * on it has to get the same answer, and a caller that forgot the argument would be a layout
     * that disagreed with the paint by one band. */
    out.notice_h = (overlay_notice_text() != NULL) ? NOTICE_H * out.text_h : 0.0f;
    out.row_h = ROW_H * out.text_h;
    out.foot_h = FOOT_H * out.text_h;
    /* The rows that fit, and the panel is built around THAT rather than around the row count.
     * One text height is kept back at each end so the panel never sits flush against the screen
     * edge, which is the same margin the repositioning below uses. At least one row is always
     * shown: a panel with no rows at all would be a worse answer than a cramped one.
     *
     * The footer is part of the chrome here, which is the whole of why it cannot be a thing the
     * painter adds: every row below the one that no longer fits is reached by scrolling, and the
     * hit test counts rows from `rows_top` by `row_h`, so a band subtracted anywhere else puts
     * what is drawn and what can be clicked a row apart. The refusal band is charged in the same
     * sum and for the same reason; what it takes is rows off the END of the list, and the hit
     * test follows because it refuses anything past `visible_rows`. */
    {
        const float chrome    = out.rows_top + out.notice_h + BOTTOM_PAD * out.text_h + out.foot_h;
        const float available = screen_height - 2.0f * out.text_h - chrome;
        uint32_t    fits      = 1u;

        if (available > 0.0f && out.row_h > 0.0f) {
            fits = (uint32_t)(available / out.row_h);
            if (fits < 1u) {
                fits = 1u;
            }
        }
        out.visible_rows = (row_count < fits) ? row_count : fits;
    }

    /* Under the last row and the padding, and hard against the footer: bands in this panel abut,
     * and the padding belongs between text and a band rather than between two bands. */
    out.notice_top = out.rows_top + (float)out.visible_rows * out.row_h + BOTTOM_PAD * out.text_h;
    out.height = out.notice_top + out.notice_h + out.foot_h;
    out.foot_top = out.height - out.foot_h;

    x = OVERLAY_EDGE_PAD * out.text_h;
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
    /* The window just got smaller, which is what a refusal band appearing does: one row fewer
     * fits, and the row that no longer fits is the last one. A selection sitting there would be
     * off the bottom of a panel it is still the current row of, and nothing would pull it back
     * until the next arrow key. Only on a SHRINK: running this on every build would undo the
     * wheel, which scrolls the list away from the selection on purpose. */
    if (out.visible_rows < was_visible) {
        overlay_layout_reveal_selection();
    }
}

const layout_t *overlay_layout(void)
{
    return &built;
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
    /* Only the rows on screen are targets, and the first of them is wherever the list is scrolled
     * to. Without the second test a click below the last drawn row would land on whatever row the
     * arithmetic ran on to. */
    if (index < 0 || (uint32_t)index >= built.visible_rows) {
        return -1;
    }
    index += (int32_t)overlay_model_scroll(built.visible_rows);
    if ((uint32_t)index >= overlay_model_row_count()) {
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
    const float fx0 = built.left + OVERLAY_EDGE_PAD * built.text_h;
    const float fx1 = right - OVERLAY_EDGE_PAD * built.text_h;

    return x >= fx0 && x < fx1 && y >= fy0 && y < fy1;
}
