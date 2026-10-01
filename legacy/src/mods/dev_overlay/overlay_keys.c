/* overlay_keys.c: see overlay_keys.h. */
#include "overlay_keys.h"

#include "overlay_choice.h"
#include "overlay_layout.h"
#include "overlay_model.h"
#include "overlay_slider.h"

#include <stdbool.h>
#include <stdint.h>

/* The keys, spelled out rather than pulled from a system header, the way the window procedure
 * beside this file already spells the ones it compares. */
#define KEY_RETURN          0x0D
#define KEY_PAGE_UP         0x21
#define KEY_PAGE_DOWN       0x22
#define KEY_END             0x23
#define KEY_HOME            0x24
#define KEY_LEFT            0x25
#define KEY_UP              0x26
#define KEY_RIGHT           0x27
#define KEY_DOWN            0x28

/* Keeping the selected row on screen is overlay_layout_reveal_selection(): it sits there because
 * the window can shrink under a selection that has not moved at all, which the keys never see. */
static void move_selection(int32_t rows)
{
    const uint32_t visible = overlay_layout()->visible_rows;

    /* From no selection the first press lands on the top row on screen rather than at the top of
     * a list the player may have scrolled a long way down. */
    overlay_model_move_selection(rows, overlay_model_scroll(visible));
    overlay_layout_reveal_selection();
}

/* The track a number row is about: the row itself when it IS the track, and the line under it
 * when it is the number above one. -1 for a number row with no track at all, which is what
 * leaves the dev menu size row out of this: its ends depend on the picture it is drawn in and
 * a pull on it would move the track being pulled.
 *
 * An unavailable row has no track either, so Left on a greyed number changes the tab like Left
 * on any other row that cannot be acted on, rather than being swallowed to no effect. */
static int32_t track_of(int32_t at, const overlay_row_t *row)
{
    overlay_row_t below;

    if (!row->available) {
        return -1;
    }
    if (row->kind == OVERLAY_ROW_SLIDER) {
        return at;
    }
    if (row->kind == OVERLAY_ROW_VALUE &&
        overlay_model_row((uint32_t)at + 1u, &below) &&
        below.kind == OVERLAY_ROW_SLIDER && below.available) {
        return at + 1;
    }
    return -1;
}

/* See overlay_keys.h. Left on a folded heading, right on an open one, and either at the end of
 * a row of words all fall through to the tab, which is what makes holding one direction walk
 * the tabs rather than stopping on the first row that answers. A number row does NOT fall
 * through at either end of its track: holding a direction there would run the value to the end
 * and then walk off the tab. */
bool overlay_keys_sideways(int32_t at, int32_t by, bool coarse)
{
    overlay_row_t row;
    int32_t       track;

    if (at < 0 || !overlay_model_row((uint32_t)at, &row)) {
        return false;
    }
    if (row.kind == OVERLAY_ROW_GROUP) {
        if (row.expanded != (by < 0)) {
            return false;
        }
        (void)overlay_model_activate((uint32_t)at);
        return true;
    }
    /* The same reading of "this row is a strip of words" the paint and the panel's width use,
     * which is a row of segments that can be USED. One that cannot has no words to walk, so the
     * key falls through to the tab, the same as it does on every other row nobody can act on. */
    if (overlay_choice_is_strip(&row)) {
        const int32_t to = overlay_choice_step(&row, by);

        return to >= 0 && overlay_choice_pick(&row, (uint32_t)to);
    }
    track = track_of(at, &row);
    if (track < 0) {
        return false;
    }
    /* Through overlay_slider.c, which is the one way any hand in this panel reaches the
     * settings file. Another write path here would be exactly the thing that file was
     * written to put an end to. */
    (void)overlay_slider_nudge(track, by, coarse);
    return true;
}

/* The selection's own, with the tab as what is left. */
static void sideways(int32_t by, bool coarse)
{
    int32_t tab;

    if (overlay_keys_sideways(overlay_model_selected(), by, coarse)) {
        return;
    }
    tab = (int32_t)overlay_model_tab() + by;
    if (tab >= 0 && tab < (int32_t)OVERLAY_TAB_COUNT) {
        overlay_model_set_tab((overlay_tab_t)tab);
    }
}

bool overlay_keys_navigate(int32_t virtual_key, bool panel_visible, bool coarse)
{
    /* One place, before anything is looked at. The panel is open but not drawn while the free
     * camera flies and while the placement mode runs, and in both of those the player is looking
     * at the world: a key that moved a selection there would move one nobody can see, and Return
     * would act on it. Handing the key back rather than swallowing it leaves it exactly where it
     * was before any of this existed, which is with the panel's own lock below. */
    if (!panel_visible) {
        return false;
    }

    switch (virtual_key) {
    case KEY_UP:
    case KEY_DOWN:
        /* The arrows move the selection and the list follows it, rather than moving the list
         * under a panel that has no current row at all, which is what they used to do. */
        move_selection((virtual_key == KEY_UP) ? -1 : 1);
        break;

    case KEY_LEFT:
    case KEY_RIGHT:
        sideways((virtual_key == KEY_LEFT) ? -1 : 1, coarse);
        break;

    case KEY_HOME:
    case KEY_END:
        overlay_model_set_selected((virtual_key == KEY_HOME)
                                       ? 0 : (int32_t)overlay_model_row_count() - 1);
        overlay_layout_reveal_selection();
        break;

    case KEY_RETURN: {
        const int32_t at = overlay_model_selected();

        if (at >= 0) {
            (void)overlay_model_activate((uint32_t)at);
        }
        break;
    }

    case KEY_PAGE_UP:
    case KEY_PAGE_DOWN: {
        /* A page is what is on screen less one row, so the row you were reading stays in view and
         * there is no gap to lose your place in. The selection goes with it when there is one, so
         * a page and an arrow do not disagree about where the player is. */
        const uint32_t visible = overlay_layout()->visible_rows;
        const int32_t  page = (visible > 1u) ? (int32_t)(visible - 1u) : 1;

        if (overlay_model_selected() >= 0) {
            move_selection((virtual_key == KEY_PAGE_UP) ? -page : page);
        } else {
            overlay_model_scroll_by((virtual_key == KEY_PAGE_UP) ? -page : page);
        }
        break;
    }

    default:
        return false;
    }
    /* Every arm above changes what is on screen, so the list is built again before the next
     * paint reads it. */
    overlay_model_rebuild();
    return true;
}
