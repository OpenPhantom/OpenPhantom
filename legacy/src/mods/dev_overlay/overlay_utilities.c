/* overlay_utilities.c: see overlay_utilities.h. */
#include "overlay_utilities.h"

#include "auto_range_row.h"
#include "dev_menu_size_row.h"
#include "fog_band_row.h"
#include "fog_follow_row.h"
#include "open_key_row.h"
#include "overlay_key_name.h"
#include "view_range_live_row.h"
#include "view_range_row.h"

#include <stdio.h>
#include <string.h>

/* The slots, in drawn order. Named rather than numbered at the use sites, because the order is a
 * reading decision and whoever changes it should have to change one list. */
typedef enum utilities_slot {
    UTILITIES_VIEW_RANGE = 0,
    UTILITIES_VIEW_RANGE_LIVE,
    UTILITIES_AUTO_RANGE,
    UTILITIES_FOG_BAND,
    UTILITIES_FOG_FOLLOW,
    UTILITIES_DEV_MENU_SIZE,
    UTILITIES_OPEN_KEY
} utilities_slot_t;

_Static_assert((uint32_t)UTILITIES_OPEN_KEY + 1u == OVERLAY_UTILITIES_ROW_COUNT,
               "the slot enum and OVERLAY_UTILITIES_ROW_COUNT have to end together, or the last "
               "row is either built and never drawn or drawn and never built");

static void copy_label(char *out, const char *text)
{
    size_t i;

    if (text == NULL) {
        out[0] = '\0';
        return;
    }
    for (i = 0; i + 1u < OVERLAY_LABEL_MAX && text[i] != '\0'; ++i) {
        out[i] = text[i];
    }
    out[i] = '\0';
}

/* The typed rows all show either what is being typed, with a cursor, or the stored value. Written
 * once because a difference between them in that is the kind of thing a reader notices and cannot
 * explain. */
static void fill_typed(overlay_row_t *out, const char *editing_text,
                       void (*format)(float, char *, size_t), float value)
{
    if (editing_text != NULL) {
        _snprintf(out->value, sizeof out->value, "%s_", editing_text);
    } else {
        format(value, out->value, sizeof out->value);
    }
    out->value[sizeof out->value - 1] = '\0';
}

void overlay_utilities_row(uint32_t slot, const char *editing_text, bool capturing,
                           overlay_row_t *out)
{
    if (out == NULL) {
        return;
    }

    /* Every row here edits a settings file rather than reaching into the running game, so unlike
     * the cheats group none of them can be unavailable for want of a resolved site: they work with
     * no level loaded and whether or not the DLL that reads the setting is installed at all. The
     * one exception below is a row whose setting has nothing to act on, which is a different
     * question from a row that could not be wired up. */
    out->kind      = OVERLAY_ROW_CHEAT;
    out->on        = false;
    out->available = true;
    out->value[0]  = '\0';
    out->expanded  = false;
    out->pending   = false;

    switch ((utilities_slot_t)slot) {
    case UTILITIES_VIEW_RANGE:
        out->kind = OVERLAY_ROW_VALUE;
        /* The accepted range is in the label rather than left for a player to discover by having
           a number refused. */
        copy_label(out->label, "Draw distance (1.0 to 2.5)");
        fill_typed(out, editing_text, view_range_row_format, view_range_row_get());
        return;

    case UTILITIES_VIEW_RANGE_LIVE: {
        /* A note rather than a control, so it cannot be clicked into and cannot be mistaken for
         * something to set. What it reports is the number the game is running, which is not always
         * the number above it: the frame governor lowers that when a scene costs too much, and the
         * cell watchdog lowers it when the draw table or the vertex cache is near overflowing. On
         * Coruscant the watchdog can pin it at 1.00 for a whole level, and the row above then shows
         * a number nothing is using. */
        char text[16];

        out->kind = OVERLAY_ROW_INFO;
        if (view_range_live_row_get(text, sizeof text)) {
            _snprintf(out->label, sizeof out->label, "  in force: %s", text);
        } else {
            copy_label(out->label, "  in force: not reported");
        }
        out->label[sizeof out->label - 1] = '\0';
        return;
    }

    case UTILITIES_AUTO_RANGE:
        copy_label(out->label, "Draw distance follows the frame rate");
        out->on = auto_range_row_get();
        return;

    case UTILITIES_FOG_BAND:
        out->kind = OVERLAY_ROW_VALUE;
        copy_label(out->label, "Fog thickness (0.25 to 1.0)");
        fill_typed(out, editing_text, fog_band_row_format, fog_band_row_get());
        return;

    case UTILITIES_FOG_FOLLOW:
        copy_label(out->label, "Fog follows the draw distance");
        out->on = fog_follow_row_get();
        return;

    case UTILITIES_DEV_MENU_SIZE:
        out->kind = OVERLAY_ROW_VALUE;
        copy_label(out->label, "Dev menu size (0.33 to 4.0)");
        fill_typed(out, editing_text, dev_menu_size_row_format, dev_menu_size_row_get());
        return;

    case UTILITIES_OPEN_KEY:
        out->kind = OVERLAY_ROW_HOTKEY;
        copy_label(out->label, "Key that opens this menu");
        if (capturing) {
            _snprintf(out->value, sizeof out->value, "...");
        } else {
            int32_t vk = open_key_row_get();

            if (vk != 0) {
                overlay_key_name(vk, out->value, sizeof out->value);
            } else {
                /* The default accepts three keys, so naming one would be a lie about the other
                   two. F6 is the one every keyboard has in the same place, so it is the one worth
                   telling a player about. */
                _snprintf(out->value, sizeof out->value, "F6 or ~");
            }
        }
        out->value[sizeof out->value - 1] = '\0';
        return;

    default:
        /* Past the end. Answered as an empty unavailable row rather than left as whatever the
         * caller's struct held: a caller asking for a slot that does not exist has a bug, and a
         * blank row is what makes it visible instead of showing stale text. */
        copy_label(out->label, "");
        out->available = false;
        return;
    }
}

bool overlay_utilities_row_is_value(uint32_t slot)
{
    return slot == (uint32_t)UTILITIES_VIEW_RANGE ||
           slot == (uint32_t)UTILITIES_FOG_BAND ||
           slot == (uint32_t)UTILITIES_DEV_MENU_SIZE;
}

bool overlay_utilities_row_is_key(uint32_t slot)
{
    return slot == (uint32_t)UTILITIES_OPEN_KEY;
}

bool overlay_utilities_toggle(uint32_t slot)
{
    switch ((utilities_slot_t)slot) {
    case UTILITIES_AUTO_RANGE:
        return auto_range_row_set(!auto_range_row_get());
    case UTILITIES_FOG_FOLLOW:
        return fog_follow_row_set(!fog_follow_row_get());
    default:
        return false;
    }
}

bool overlay_utilities_commit(uint32_t slot, const char *text)
{
    float parsed;

    if (text == NULL || text[0] == '\0') {
        return false;
    }

    /* Refused rather than clamped when the text is not a number. Each of these would turn a typing
     * mistake into an extreme: the shortest draw distance, the thickest fog the range allows, or
     * the smallest panel, which is the worst of the three because it shrinks the thing being typed
     * into. */
    switch ((utilities_slot_t)slot) {
    case UTILITIES_VIEW_RANGE:
        return view_range_row_parse(text, &parsed) && view_range_row_set(parsed);
    case UTILITIES_FOG_BAND:
        return fog_band_row_parse(text, &parsed) && fog_band_row_set(parsed);
    case UTILITIES_DEV_MENU_SIZE:
        return dev_menu_size_row_parse(text, &parsed) && dev_menu_size_row_set(parsed);
    default:
        return false;
    }
}

bool overlay_utilities_bind(uint32_t slot, int32_t virtual_key)
{
    if (!overlay_utilities_row_is_key(slot)) {
        return false;
    }
    return open_key_row_set(virtual_key);
}
