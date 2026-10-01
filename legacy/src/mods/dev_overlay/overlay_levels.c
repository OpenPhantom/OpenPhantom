/* overlay_levels.c: see overlay_levels.h. */
#include "overlay_levels.h"

#include "cheats_openphantom.h"
#include "overlay_row_fill.h"
#include "start_level.h"
#include "start_level_row.h"

#include "common/text.h"

static bool list_open;

/* The list names a level by its file, the name in the LEVEL folder with the first letter up:
 * Fedship, Swamp, Espa. That is the name a modder works with, and the one the level's own
 * title, "Wherever this is" for the first, does not give. With the table unreadable the number
 * stands alone. */
static void level_name(int level, char *out, uint32_t out_size)
{
    start_level_stem(level, out, out_size);
    if (out[0] == '\0') {
        text_format(out, out_size, "level %d", level);
        return;
    }
    if (out[0] >= 'a' && out[0] <= 'z') {
        out[0] = (char)(out[0] - 'a' + 'A');
    }
}

/* Level one is written as 0, the game's own new game, so a file that has never been written to
 * already names it. Both the row that lights and the row that picks go through this, so what is
 * stored and what is compared cannot be two spellings of one level. */
static int as_written(int level)
{
    return level == 1 ? 0 : level;
}

uint32_t overlay_levels_row_count(void)
{
    return OVERLAY_LEVELS_ENTRY_FIRST + (list_open ? OVERLAY_LEVELS_ENTRY_COUNT : 0u);
}

void overlay_levels_reset(void)
{
    list_open = false;
}

void overlay_levels_row(uint32_t slot, overlay_row_t *out)
{
    if (out == NULL) {
        return;
    }
    overlay_row_defaults(out);
    switch (slot) {
    case OVERLAY_LEVELS_SKIP_SLOT:
        out->kind = OVERLAY_ROW_ACTION;
        /* It HAS been played since this row was added, so the label no longer says it has not.
         * It stays marked as a debug tool, which is still true: it is a jump to the next level
         * with none of the bookkeeping the game itself does on the way out of one. */
        overlay_row_label(out->label, "Skip to next level (debug)");
        out->available = cheats_openphantom_end_level_is_available();
        return;

    case OVERLAY_LEVELS_START_SLOT:
        /* Opens the list; the chip shows the choice, as the window group's size row does. */
        out->kind = OVERLAY_ROW_ACTION;
        overlay_row_label(out->label, list_open ? "New game starts at (pick one)"
                                                : "New game starts at");
        out->available = start_level_is_available();
        start_level_row_value(out->value, sizeof out->value);
        return;

    default:
        if (list_open && slot >= OVERLAY_LEVELS_ENTRY_FIRST &&
            slot < OVERLAY_LEVELS_ENTRY_FIRST + OVERLAY_LEVELS_ENTRY_COUNT) {
            int  level = (int)(slot - OVERLAY_LEVELS_ENTRY_FIRST) + 1;
            int  chosen = start_level_row_get();
            char name[OVERLAY_LABEL_MAX];
            char line[OVERLAY_LABEL_MAX];

            /* Marked for the chosen level, read the way the picking below writes it. One entry
             * of a list rather than a switch: what a new game starts at is one of these, and a
             * green ON beside the other seventeen said each of them was a setting of its own. */
            out->kind = OVERLAY_ROW_CHOICE;
            out->available = start_level_is_available();
            out->on = as_written(chosen) == as_written(level);
            level_name(level, name, sizeof name);
            text_format(line, sizeof line, "    %2d  %s", level, name);
            overlay_row_label(out->label, line);
            return;
        }
        overlay_row_label(out->label, "");    /* past the end; a blank shows the caller's bug */
        out->available = false;
        return;
    }
}

bool overlay_levels_toggle(uint32_t slot)
{
    switch (slot) {
    case OVERLAY_LEVELS_SKIP_SLOT:
        return cheats_openphantom_end_level_invoke();
    case OVERLAY_LEVELS_START_SLOT:
        list_open = !list_open;
        return true;
    default:
        if (list_open && slot >= OVERLAY_LEVELS_ENTRY_FIRST &&
            slot < OVERLAY_LEVELS_ENTRY_FIRST + OVERLAY_LEVELS_ENTRY_COUNT) {
            /* Picking closes the list, so the choice reads back on the row above at once. */
            int level = (int)(slot - OVERLAY_LEVELS_ENTRY_FIRST) + 1;

            list_open = false;
            return start_level_row_set(as_written(level));
        }
        return false;
    }
}
