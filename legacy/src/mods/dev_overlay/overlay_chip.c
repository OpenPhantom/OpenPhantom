/* overlay_chip.c: see overlay_chip.h. */
#include "overlay_chip.h"

#include "overlay_choice.h"
#include "overlay_draw.h"
#include "overlay_model.h"
#include "overlay_reason.h"

#include "cheats_openphantom.h"
#include "cheats_original.h"
#include "cheats_original_actions.h"

#include <stdbool.h>
#include <stdint.h>

/* Folds `widest` up to whichever of it and every name in [0, count) is largest. Shared because the
 * Original tab now measures two sources rather than one, and a loop copied twice is two chances to
 * change only one of them. */
static float widen_over(float widest, uint32_t count, const char *(*name_of)(uint32_t))
{
    uint32_t i;

    for (i = 0; i < count; ++i) {
        const float width = overlay_draw_note_width(name_of(i));

        if (width > widest) {
            widest = width;
        }
    }
    return widest;
}

static const char *original_toggle_name(uint32_t i)
{
    static char label[OVERLAY_LABEL_MAX];   /* measured at once, never kept */

    cheats_original_label(i, label, sizeof label);
    return label;
}

static const char *original_action_name(uint32_t i)
{
    return cheats_original_actions_name((cheats_action_id_t)i);
}

static const char *openphantom_name(uint32_t i)
{
    return cheats_openphantom_name((cheats_own_id_t)i);
}

/* The width the panel has to find room for, measured from the rows that are ON SCREEN rather than
 * from any one source's list.
 *
 * It used to ask the cheat sources for their names, which was right while every row in a tab was a
 * cheat and quietly wrong afterwards: the rows this panel adds itself, the settings and the
 * actions and the folded notes, were never measured, so the panel was sized to fit the names it
 * knew about and drew the rest clipped. Reported as an ellipsis in the middle of four rows.
 *
 * Asking the model instead means a label added anywhere is measured by the fact of being drawn,
 * which is the only version of this that cannot fall out of step again. The cheat lists are still
 * measured underneath it so that a folded tab is no narrower than an open one, which keeps the
 * panel from changing width as groups are opened. */
float overlay_chip_widest_name(void)
{
    float    widest = 0.0f;
    uint32_t count  = overlay_model_row_count();
    uint32_t i;

    if (overlay_model_tab() == OVERLAY_TAB_ORIGINAL) {
        widest = widen_over(widest, cheats_original_count(), original_toggle_name);
        widest = widen_over(widest, (uint32_t)CHEATS_ACTION_COUNT, original_action_name);
    } else {
        widest = widen_over(widest, (uint32_t)CHEATS_OWN_COUNT, openphantom_name);
    }

    for (i = 0; i < count; ++i) {
        overlay_row_t row;

        if (!overlay_model_row(i, &row)) {
            continue;
        }
        /* A group heading is indented less than a row is, so it needs less room for the same
         * text; measuring it against a row's indent is the safe direction to be wrong in. */
        if (overlay_draw_note_width(row.label) > widest) {
            widest = overlay_draw_note_width(row.label);
        }
    }
    return widest;
}

/* A hotkey row, and a value row too, get the action's own chip: both are buttons that start
 * something rather than plain switches. Neither falls through to "RUN", because either kind
 * always carries a value of its own; that arm of the word below is kept for ACTION, which needs
 * it.
 *
 * One function because the word and the colour are two answers to one question and used to be two
 * copies of one conditional in two files. The same pair had already come apart once: the tidy-up
 * that noticed it merged the WORD and left the colour where it was. */
bool overlay_chip_is_button(const overlay_row_t *row)
{
    return row->kind == OVERLAY_ROW_ACTION || row->kind == OVERLAY_ROW_HOTKEY ||
           row->kind == OVERLAY_ROW_VALUE;
}

/* The word in a row's chip. One function because two places need it and they have to agree: the
 * drawing below, and the measurement above it that sizes the panel to fit. They were two copies of
 * the same conditional for one commit, which is one commit longer than that is safe.
 *
 * A row that is not available reads its REASON rather than a flat `n/a`, which was one word for a
 * site that never resolved, a row held back on purpose, a row another row decides and a running
 * multiplayer session. A reason nobody named still answers `n/a`, so nothing that said it before
 * says anything else now. A heading says whatever the model put on it and nothing if it put
 * nothing, since a heading is not a control and has no state of its own to fall back on.
 *
 * The two choice kinds answer with nothing at all while they can be used. What they are is drawn
 * beside the name and not in a chip: a mark for one entry of a list, the words themselves for a
 * row of segments. A chip there would spell the same state a second time, and the word it would
 * carry, ON, is the panel's word for a switch. A choice that cannot be used still reads its
 * reason, which is the one thing about it that is not on the row already.
 *
 * One kind of taken row reads a value after all: on a client of a running session, a row whose
 * setting the host decides carries the host's value, and that is what this machine runs. The word
 * `session` there said why the row could not be used and hid the one number a player on that
 * machine is asking about. The row stays greyed and refuses a press; only the chip changes. */
const char *overlay_chip_word(const overlay_row_t *row)
{
    if (row->kind == OVERLAY_ROW_GROUP) {
        return row->value;
    }
    if (!row->available) {
        return (row->host_value && row->value[0] != 0) ? row->value
                                                       : overlay_reason_word(row->reason);
    }
    if (row->kind == OVERLAY_ROW_CHOICE || row->kind == OVERLAY_ROW_SEGMENT) {
        return "";
    }
    return row->pending       ? "QUEUED"
         : row->value[0] != 0 ? row->value    /* e.g. the graphics detail level */
         : overlay_chip_is_button(row) ? "RUN"
         : row->on            ? "ON" : "OFF";
}

/* The widest CHIP the tab can show, which the panel has to find room for beside the widest name.
 *
 * Measured rather than assumed, because the assumption was wrong by a factor of four. The layout
 * used to fold "the widest state chip" into one fixed allowance, which fits a chip reading "RUN"
 * and does not fit one reading "auto 1.00x", so the longest label was drawn clipped even after the
 * width ceiling was raised for it. The words are chosen exactly as the row drawing below chooses
 * them, so what is measured is what is drawn. */
float overlay_chip_widest(void)
{
    float    widest = 0.0f;
    uint32_t count  = overlay_model_row_count();
    uint32_t i;

    for (i = 0; i < count; ++i) {
        overlay_row_t row;
        float         w;

        if (!overlay_model_row(i, &row)) {
            continue;
        }
        if (row.kind == OVERLAY_ROW_INFO || (row.kind == OVERLAY_ROW_GROUP && row.value[0] == 0)) {
            continue;              /* full width text, with nothing on the right to make room for */
        }
        /* A track row is measured by what stands at the end of its track: the number and the
         * Default, with the room between and around them. The word below answers the number
         * alone, so without this the panel would be sized as though the Default were not
         * there and overlay_number_fit() would drop it on every screen there is.
         *
         * Two text heights, not a string of spaces: the drawing leaves half a height between
         * the two and gives the button a height of padding, and a guess at how wide a space
         * is would be a second answer to that. */
        if (row.kind == OVERLAY_ROW_SLIDER && row.available) {
            overlay_number_t limits;

            if (overlay_model_slider_limits(i, &limits) && limits.has_standard) {
                w = overlay_draw_note_width(row.value) +
                    overlay_draw_note_width(OVERLAY_DEFAULT_WORD) +
                    2.0f * overlay_draw_note_height();
                if (w > widest) {
                    widest = w;
                }
                continue;
            }
        }
        /* A row of segments is measured by its WORDS, which stand where a chip would and are
         * wider than any chip on the tab. The word above answers nothing for it, so without this
         * the panel would be sized as though the widest row of the entity spawner group were its
         * name alone. */
        if (overlay_choice_is_strip(&row)) {
            char strip[OVERLAY_CHOICE_SEGMENTS_MAX * (OVERLAY_LABEL_MAX + 2u)];

            if (overlay_choice_strip(&row, strip, sizeof strip)) {
                w = overlay_draw_note_width(strip);
                if (w > widest) {
                    widest = w;
                }
                continue;
            }
        }
        /* A heading that carries a word is measured like any other chip. It used to be skipped
         * outright, which was right while headings never carried one and is exactly the mistake
         * the comment above this function describes: a chip nobody measured is a chip drawn over
         * the name beside it. */
        w = overlay_draw_note_width(overlay_chip_word(&row));
        if (w > widest) {
            widest = w;
        }
    }
    return widest;
}

