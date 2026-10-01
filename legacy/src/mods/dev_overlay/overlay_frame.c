/* overlay_frame.c: see overlay_frame.h. */
#include "overlay_frame.h"

#include "cheats_original_actions.h"
#include "overlay_draw.h"
#include "overlay_input.h"
#include "overlay_legend.h"
#include "overlay_look.h"
#include "overlay_model.h"
#include "overlay_notice.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* What the panel is called, and what the two tabs are called.
 *
 * "Cheatmenu" named the one thing it held when it was written; it holds the cheats, the spawner,
 * the free camera, the picture, the controls and this patch's own settings now, and a player
 * looking for any of those was looking under the wrong word.
 *
 * The tabs are named for the two halves of the project. */
#define FRAME_TITLE "Dev menu"

static const char *const TAB_TITLE[OVERLAY_TAB_COUNT] = { "Original", "OpenPhantom" };

/* The model counts the tabs and the layout stores their spans. Two constants, and nothing but this
 * keeps them the same number. */
_Static_assert((uint32_t)OVERLAY_TAB_COUNT == OVERLAY_LAYOUT_TABS,
               "the model and the layout disagree about how many tabs there are");

void overlay_frame_tab_widths(float *out, uint32_t count)
{
    uint32_t i;

    if (out == NULL) {
        return;
    }
    for (i = 0; i < count && i < (uint32_t)OVERLAY_TAB_COUNT; ++i) {
        out[i] = overlay_draw_width(TAB_TITLE[i]);
    }
}

/* The body, its frame, the accent cap and the title. */
static void paint_frame(const layout_t *lay, float left, float right)
{
    /* --- the body, its frame, and the accent cap ------------------------------------------------
     * The cap is the answer to a title with no weight: with one font at one size weight cannot come
     * from the type, so it comes from a saturated bar directly above it, and it is the top border
     * as well, so it costs nothing. */
    overlay_draw_fill(left, lay->top, right, lay->top + lay->height, C_PANEL_BODY);
    overlay_draw_fill(left, lay->top, right, lay->top + lay->cap_h, C_ACCENT);
    overlay_draw_fill(left, lay->top, left + lay->rule, lay->top + lay->height, C_BORDER);
    overlay_draw_fill(right - lay->rule, lay->top, right, lay->top + lay->height, C_BORDER);
    overlay_draw_fill(left, lay->top + lay->height - lay->rule, right, lay->top + lay->height,
                      C_BORDER);

    /* --- the title ------------------------------------------------------------------------- */
    overlay_draw_fill(left + lay->rule, lay->top + lay->cap_h, right - lay->rule,
                      lay->top + lay->title_h, C_BAND_TITLE);
    overlay_draw_fill(left, lay->top + lay->title_h - lay->rule, right, lay->top + lay->title_h,
                      C_RULE);
    overlay_draw_write_in(FRAME_TITLE, left + OVERLAY_EDGE_PAD * lay->text_h, lay->top + lay->cap_h,
                          lay->title_h - lay->cap_h, C_ACCENT);
    {
        /* "Esc closes" used to stand here and has gone: the footer says it now, beside the three
         * other keys that also do something, and one sentence in two places is the pair that
         * drifts apart. What is left is the thing that is true only sometimes.
         *
         * A queued swap outranks nothing now; it is the only hint this band has. The row itself
         * already reads QUEUED, so this is confirming what closing does rather than introducing
         * new information. Kept short rather than naming the character: the panel's width is sized
         * to fit the longest ROW label, not this hint plus the title both fitting the band
         * together, and a name-carrying hint here would be the one string in the whole panel that
         * was never checked against that budget. */
        const char *hint = (cheats_original_actions_pending_label() != NULL)
                          ? "Close applies the queued swap" : NULL;

        if (hint != NULL) {
            overlay_draw_write_in(hint,
                                  right - OVERLAY_EDGE_PAD * lay->text_h -
                                      overlay_draw_width(hint),
                                  lay->top + lay->cap_h, lay->title_h - lay->cap_h,
                                  C_ROW_TEXT_DIM);
        }
    }
}

static void paint_tabs(const layout_t *lay, float left, float right)
{
    uint32_t i;

    /* --- the tabs. The inactive one gets NO fill, and without that absence they do not read as
     * tabs: one rectangle among words is unambiguously the selected one at any scene brightness,
     * while a second rectangle both competes with the first and converges on it as the game gets
     * brighter. --- */
    overlay_draw_fill(left, lay->top + lay->search_top - lay->rule, right,
                      lay->top + lay->search_top, C_RULE);
    for (i = 0; i < OVERLAY_LAYOUT_TABS; ++i) {
        const bool  active = ((uint32_t)overlay_model_tab() == i);
        const float x0 = left + lay->tab_x0[i];
        const float y0 = lay->top + lay->tabs_top;

        if (active) {
            overlay_draw_fill(x0, y0, x0 + lay->tab_w[i], y0 + lay->tab_h, C_TAB_ON_FILL);
            overlay_draw_fill(x0, y0 + lay->tab_h - lay->rule * 3.0f, x0 + lay->tab_w[i],
                              y0 + lay->tab_h, C_ACCENT);
        }
        overlay_draw_write_in(TAB_TITLE[i], x0 + lay->text_h, y0, lay->tab_h,
                              active ? C_TAB_ON_TEXT : C_TAB_OFF_TEXT);
    }
}

static void paint_search(const layout_t *lay, float left, float right)
{
    const char *typed;

    /* --- the search field. Opaque with a light border rather than a translucent black rectangle:
     * the same shape, and the difference between a well and a hole.
     *
     * Typing only reaches this box once it has been clicked into, and the input layer's focus
     * gate is what holds that. The border and the caret say so: the accent border
     * and the caret both only appear once a click actually landed here, so the box never LOOKS
     * ready to type into before it is. --- */
    {
        const bool  focused = overlay_input_search_focused();
        const float fy0 = lay->top + lay->search_top + (lay->search_h - 1.5f * lay->text_h) * 0.5f;
        const float fy1 = fy0 + 1.5f * lay->text_h;
        const float tx = left + (OVERLAY_EDGE_PAD + 0.5f) * lay->text_h;
        float       caret;

        overlay_draw_outlined(left + OVERLAY_EDGE_PAD * lay->text_h, fy0,
                              right - OVERLAY_EDGE_PAD * lay->text_h, fy1,
                              focused ? C_ACCENT : C_FIELD_BORDER, C_FIELD_FILL);
        typed = overlay_model_search();
        if (typed[0] != 0) {
            overlay_draw_write_in(typed, tx, fy0, fy1 - fy0, C_TYPED);
            caret = tx + overlay_draw_width(typed) + lay->rule * 2.0f;
        } else {
            overlay_draw_write_in("Search", tx + 0.5f * lay->text_h, fy0, fy1 - fy0,
                                  C_PLACEHOLDER);
            caret = tx;
        }
        if (focused) {
            overlay_draw_fill(caret, fy0 + 0.25f * lay->text_h, caret + lay->rule * 2.0f,
                              fy0 + 1.25f * lay->text_h, C_ACCENT);
        }
    }
    overlay_draw_fill(left, lay->top + lay->rows_top - lay->rule, right,
                      lay->top + lay->rows_top, C_RULE);
}

/* What the panel last turned down, in the band directly above the footer. Nothing at all when
 * nothing stands, and the layout has given it no height either, so the list ends where it ended.
 *
 * It draws no rule of its own: the footer's own top rule is the line between the two, which is
 * this panel's rule everywhere, that a separator is a pixel row OF a band and not a gap.
 *
 * The sentence goes through the same fit the rows' labels go through. Without it this would be
 * the one string in the panel never measured against the width it is drawn in, and a sentence
 * written a few characters too long would run off the right edge onto the game behind.
 *
 * A refusal is drawn in the warning colour and a confirmation in the ordinary text colour. Not in
 * the green: green says a switch is on in this panel, and a sentence that says a key was saved
 * is not a switch. The kind is asked of the notice, which stored it with the sentence. */
static void paint_notice(const layout_t *lay, float left, float right)
{
    const char *said = overlay_notice_text();
    const float y    = lay->top + lay->notice_top;
    const float x    = left + OVERLAY_EDGE_PAD * lay->text_h;
    char        scratch[OVERLAY_NOTICE_MAX + 4];

    if (said == NULL || !(lay->notice_h > 0.0f)) {
        return;
    }
    overlay_draw_fill(left + lay->rule, y, right - lay->rule, y + lay->notice_h, C_BAND_TITLE);
    overlay_draw_write_in(overlay_draw_fit(said, right - OVERLAY_EDGE_PAD * lay->text_h - x,
                                           scratch, sizeof scratch),
                          x, y, lay->notice_h, overlay_notice_confirms() ? C_ROW_TEXT : C_WARN);
}

void overlay_frame_paint_top(const layout_t *lay, float left, float right)
{
    paint_frame(lay, left, right);
    paint_tabs(lay, left, right);
    paint_search(lay, left, right);
}

/* A key cap: a framed box with the key's word in it, the same shape as the search field one band
 * up and for the same reason. No sprite and no second font, because there is neither; the frame
 * is the outlined fill everything else here is drawn with. Answers where it ended. */
#define CAP_PAD 0.4f    /* inside a cap, each side, in text heights */

static float paint_cap(const layout_t *lay, float x, float y, const char *word)
{
    const float pad = CAP_PAD * lay->text_h;
    const float box = overlay_draw_width(word) + 2.0f * pad;
    const float top = y + (lay->foot_h - 1.5f * lay->text_h) * 0.5f;

    overlay_draw_outlined(x, top, x + box, top + 1.5f * lay->text_h, C_FIELD_BORDER, C_CHIP_OFF);
    overlay_draw_write_in(word, x + pad, top, 1.5f * lay->text_h, C_ROW_TEXT);
    return x + box;
}

void overlay_frame_paint_foot(const layout_t *lay, float left, float right)
{
    const float y    = lay->top + lay->foot_top;
    const float room = (right - left) - 2.0f * OVERLAY_EDGE_PAD * lay->text_h;
    const float gap  = 0.5f * lay->text_h;
    /* While a key row waits, every cap would be taken as that row's key; see overlay_legend.h. */
    const char *prompt = overlay_legend_prompt(overlay_model_is_capturing_hotkey());
    char        tally[32];
    float       box[OVERLAY_LEGEND_KEYS];
    float       caps = 0.0f;
    float       words = 0.0f;
    float       x;
    uint32_t    shown;
    uint32_t    i;

    /* The refusal band sits between the last row and this one, so it is painted here: after the
     * list, where nothing of the list can land on it, and before the footer, whose own top rule
     * is the line between the two. */
    paint_notice(lay, left, right);

    overlay_legend_right(tally, sizeof tally, overlay_model_heading_count(),
                         overlay_model_row_count());

    /* Measured before anything is drawn, because what is drawn depends on what fits. The panel is
     * as wide as its longest row needs and is clamped against nothing else, so this band gives up
     * its own parts rather than asking for width the rows did not ask for. */
    for (i = 0; i < OVERLAY_LEGEND_KEYS; ++i) {
        const overlay_legend_key_t *key = overlay_legend_key(i);

        /* The same box paint_cap() draws: the word plus its padding on both sides. */
        box[i] = overlay_draw_width(key->cap) + 2.0f * CAP_PAD * lay->text_h;
        caps += box[i] + gap;
        words += overlay_draw_width(key->what) + gap * 0.5f;
    }

    overlay_draw_fill(left + lay->rule, y, right - lay->rule, y + lay->foot_h, C_BAND_TITLE);
    overlay_draw_fill(left, y, right, y + lay->rule, C_RULE);

    x = left + OVERLAY_EDGE_PAD * lay->text_h;
    if (prompt != NULL) {
        /* Where the caps stand, in the row text colour because it is the one thing to do now,
         * and the tally beside it only where the rule the caps follow leaves room for it. */
        overlay_draw_write_in(prompt, x, y, lay->foot_h, C_ROW_TEXT);
        if (overlay_legend_fit(room, overlay_draw_width(prompt), 0.0f,
                               overlay_draw_width(tally)) != OVERLAY_LEGEND_BARE) {
            overlay_draw_write_in(tally,
                                  right - OVERLAY_EDGE_PAD * lay->text_h -
                                      overlay_draw_width(tally),
                                  y, lay->foot_h, C_ROW_TEXT_DIM);
        }
        return;
    }
    {
        const overlay_legend_fit_t fit =
            overlay_legend_fit(room, caps, words, overlay_draw_width(tally));

        /* Even the caps alone are not promised to fit, and the engine clips nothing: a cap
         * drawn past the right edge lands on the game behind the panel. */
        shown = overlay_legend_caps_that_fit(room, gap, box);
        for (i = 0; i < shown; ++i) {
            const overlay_legend_key_t *key = overlay_legend_key(i);

            x = paint_cap(lay, x, y, key->cap);
            if (fit == OVERLAY_LEGEND_FULL) {
                x += gap * 0.5f;
                overlay_draw_write_in(key->what, x, y, lay->foot_h, C_ROW_TEXT_DIM);
                x += overlay_draw_width(key->what);
            }
            x += gap;
        }
        if (fit != OVERLAY_LEGEND_BARE) {
            overlay_draw_write_in(tally,
                                  right - OVERLAY_EDGE_PAD * lay->text_h -
                                      overlay_draw_width(tally),
                                  y, lay->foot_h, C_ROW_TEXT_DIM);
        }
    }
}
