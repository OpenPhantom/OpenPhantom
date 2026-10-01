/* overlay_menu_extras.c: see overlay_menu_extras.h. */
#include "overlay_menu_extras.h"

#include "menu_extras_row.h"
#include "overlay_row_fill.h"

#include "common/text.h"

/* The fold's own text, one row per line. What the switch adds, where, and the two things a
 * player needs to know before pressing it: it takes a restart, and nothing is lost without it. */
static const char *const MENU_EXTRAS_LINES[OVERLAY_MENU_EXTRAS_LINE_COUNT] = {
    "Adds this patch's settings to the",
    "  game's own options screens",
    "Controls: free look, strafe and",
    "  the mouse speed slider",
    "Video: the field of view slider",
    "Takes effect after a restart",
    "Everything here works without it"
};

static bool fold_open;   /* the row is showing its lines */

uint32_t overlay_menu_extras_row_count(void)
{
    return OVERLAY_MENU_EXTRAS_LINE_FIRST + (fold_open ? OVERLAY_MENU_EXTRAS_LINE_COUNT : 0u);
}

void overlay_menu_extras_reset(void)
{
    fold_open = false;   /* folds closed on every open, same as the groups do */
}

void overlay_menu_extras_row(uint32_t slot, overlay_row_t *out)
{
    if (out == NULL) {
        return;
    }
    overlay_row_defaults(out);
    switch (slot) {
    case OVERLAY_MENU_EXTRAS_SWITCH_SLOT:
        /* Named for what a reader sees rather than for the three widgets, and it says when,
         * because a switch that appears to do nothing is worse than one that explains itself. */
        overlay_row_label(out->label, "Show extra menu options (restart the game)");
        out->on = menu_extras_row_get();
        return;

    case OVERLAY_MENU_EXTRAS_SUMMARY_SLOT:
        /* A fold on one row, the same shape as the free camera's "how to fly": the marker is a
         * character in the label, so the drawer needs nothing new to draw it. */
        out->kind = OVERLAY_ROW_INFO;
        overlay_row_label(out->label, fold_open ? "- What this adds" : "+ What this adds");
        out->expanded = fold_open;
        return;

    default:
        if (fold_open && slot >= OVERLAY_MENU_EXTRAS_LINE_FIRST &&
            slot < OVERLAY_MENU_EXTRAS_LINE_FIRST + OVERLAY_MENU_EXTRAS_LINE_COUNT) {
            char line[OVERLAY_LABEL_MAX];

            out->kind = OVERLAY_ROW_INFO;
            text_format(line, sizeof line, "    %s",
                        MENU_EXTRAS_LINES[slot - OVERLAY_MENU_EXTRAS_LINE_FIRST]);
            overlay_row_label(out->label, line);
            return;    /* a nested line, not a gate; never clicked either way */
        }
        overlay_row_label(out->label, "");    /* past the end; a blank shows the caller's bug */
        out->available = false;
        return;
    }
}

bool overlay_menu_extras_toggle(uint32_t slot)
{
    switch (slot) {
    case OVERLAY_MENU_EXTRAS_SWITCH_SLOT:
        return menu_extras_row_set(!menu_extras_row_get());
    case OVERLAY_MENU_EXTRAS_SUMMARY_SLOT:
        fold_open = !fold_open;
        return true;
    default:
        return false;    /* a line is a note */
    }
}
