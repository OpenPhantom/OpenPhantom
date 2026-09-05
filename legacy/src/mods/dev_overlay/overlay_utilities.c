/* overlay_utilities.c: see overlay_utilities.h. */
#include "overlay_utilities.h"

#include "auto_range_row.h"
#include "dev_menu_size_row.h"
#include "fog_band_row.h"
#include "fog_follow_row.h"
#include "fov_row.h"
#include "menu_extras_row.h"
#include "sensitivity_row.h"
#include "free_look_row.h"
#include "open_key_row.h"
#include "strafe_row.h"
#include "strict_range_row.h"
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
    UTILITIES_STRICT_RANGE,
    UTILITIES_FOG_BAND,
    UTILITIES_FOG_FOLLOW,
    UTILITIES_FOV,
    UTILITIES_FOV_TRACK,
    UTILITIES_FREE_LOOK,
    UTILITIES_STRAFE,
    UTILITIES_SENSITIVITY,
    UTILITIES_SENSITIVITY_TRACK,
    UTILITIES_MENU_EXTRAS,
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
        /* GREYED WHILE THE ROW BELOW IS ON, because the two contradict each other and the one
         * below wins. Strict mode declines the governor outright, so a switch still reading ON
         * would be describing something that is not happening.
         *
         * Its key is deliberately NOT written when that happens. A reader who had the governor on,
         * turns strict on to look at something and turns it off again gets the governor back,
         * rather than finding a setting they never changed has been changed for them. So this row
         * reports the state the game is actually in, and the file keeps the state the reader
         * asked for. */
        copy_label(out->label, "Draw distance follows the frame rate");
        out->available = !strict_range_row_get();
        out->on = out->available && auto_range_row_get();
        return;

    case UTILITIES_STRICT_RANGE:
        /* Named for the trade rather than for the machinery, like the row above it. The frame rate
         * is the cost a reader will actually meet, because the governor is the term that acts in
         * ordinary play; the watchdog only acts above 1.00x, and what it costs when declined is in
         * the ini and in strict_range_row.h rather than in 47 characters. */
        copy_label(out->label, "Keep the draw distance (costs frame rate)");
        out->on = strict_range_row_get();
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

    case UTILITIES_FOV: {
        /* The one row here that can be unavailable. Every other row edits a settings file and works
         * with the DLL that reads it gone; this one needs a width in degrees that only variable_fov
         * can publish, and inventing one would be wrong on some canvas. */
        float degrees;
        char  range[40];

        out->kind = OVERLAY_ROW_VALUE;
        _snprintf(range, sizeof range, "Field of view (%.0f to %.0f)",
                  (double)fov_row_min(), (double)fov_row_max());
        range[sizeof range - 1] = '\0';
        copy_label(out->label, range);
        if (fov_row_get(&degrees)) {
            fill_typed(out, editing_text, fov_row_format, degrees);
        } else {
            out->available = false;
            copy_label(out->value, "");
        }
        return;
    }

    case UTILITIES_FOV_TRACK: {
        float degrees;
        float low  = fov_row_min();
        float high = fov_row_max();

        out->kind = OVERLAY_ROW_SLIDER;
        copy_label(out->label, "");
        if (!fov_row_get(&degrees)) {
            out->available = false;    /* no published base, so nothing to place a handle against */
            return;
        }
        /* Guarded rather than assumed: both ends come out of the file, and somebody who sets them
         * equal would otherwise divide by zero here. */
        out->fraction = (high > low) ? ((degrees - low) / (high - low)) : 0.0f;
        /* CLAMPED FOR DRAWING, while the number on the row above is left honest. ExtraDegrees can
         * be set in the file to a width outside the slider's own ends, and the row should say so
         * rather than pretend; but a fraction outside 0 to 1 would put the handle beyond the track
         * it belongs to, which reads as a slider that has broken rather than a value off the
         * scale. */
        if (out->fraction < 0.0f) {
            out->fraction = 0.0f;
        }
        if (out->fraction > 1.0f) {
            out->fraction = 1.0f;
        }
        return;
    }

    case UTILITIES_FREE_LOOK:
        copy_label(out->label, "Free look");
        out->on = free_look_row_get();
        return;

    case UTILITIES_STRAFE:
        copy_label(out->label, "Strafe");
        out->on = strafe_row_get();
        return;

    case UTILITIES_SENSITIVITY:
        out->kind = OVERLAY_ROW_VALUE;
        /* The name the game's own controls screen gave it, so a reader who has seen that screen
         * recognises this one. */
        copy_label(out->label, "Mouse speed");
        fill_typed(out, editing_text, sensitivity_row_format, sensitivity_row_get());
        return;

    case UTILITIES_SENSITIVITY_TRACK: {
        const float value = sensitivity_row_get();

        out->kind = OVERLAY_ROW_SLIDER;
        copy_label(out->label, "");
        /* No availability test, unlike the field of view: both ends of this one are fixed, so there
         * is nothing to wait for another DLL to publish. */
        out->fraction = (value - SENSITIVITY_MIN) / (SENSITIVITY_MAX - SENSITIVITY_MIN);
        if (out->fraction < 0.0f) {
            out->fraction = 0.0f;
        }
        if (out->fraction > 1.0f) {
            out->fraction = 1.0f;
        }
        return;
    }

    case UTILITIES_MENU_EXTRAS:
        /* Named for what a reader sees rather than for the three widgets, and it says when,
         * because a switch that appears to do nothing is worse than one that explains itself. */
        copy_label(out->label, "Show extra menu options (restart the game)");
        out->on = menu_extras_row_get();
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
           slot == (uint32_t)UTILITIES_FOV ||
           slot == (uint32_t)UTILITIES_SENSITIVITY ||
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
        if (strict_range_row_get()) {
            return false;              /* greyed; the model refuses first, this is the second lock */
        }
        return auto_range_row_set(!auto_range_row_get());
    case UTILITIES_STRICT_RANGE:
        return strict_range_row_set(!strict_range_row_get());
    case UTILITIES_FOG_FOLLOW:
        return fog_follow_row_set(!fog_follow_row_get());
    case UTILITIES_FREE_LOOK:
        return free_look_row_set(!free_look_row_get());
    case UTILITIES_STRAFE:
        return strafe_row_set(!strafe_row_get());
    case UTILITIES_MENU_EXTRAS:
        return menu_extras_row_set(!menu_extras_row_get());
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
    case UTILITIES_FOV:
        return fov_row_parse(text, &parsed) && fov_row_set(parsed);
    case UTILITIES_SENSITIVITY:
        return sensitivity_row_parse(text, &parsed) && sensitivity_row_set(parsed);
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

bool overlay_utilities_slider_set(uint32_t slot, float fraction)
{
    float low;
    float high;

    if (fraction < 0.0f) {
        fraction = 0.0f;
    }
    if (fraction > 1.0f) {
        fraction = 1.0f;
    }
    if ((utilities_slot_t)slot == UTILITIES_SENSITIVITY_TRACK) {
        /* Not rounded to anything, unlike the field of view below: the band is a tenth of a degree
         * wide and the row shows three decimals, so every position along the track is a value
         * somebody can tell apart from the one beside it. */
        return sensitivity_row_set(SENSITIVITY_MIN +
                                   fraction * (SENSITIVITY_MAX - SENSITIVITY_MIN));
    }
    if ((utilities_slot_t)slot != UTILITIES_FOV_TRACK) {
        return false;
    }
    low  = fov_row_min();
    high = fov_row_max();
    if (!(high > low)) {
        return false;
    }
    /* Rounded to whole degrees. The row shows whole degrees, so a drag that set 96.4 would display
     * 96 and then write 96.4 back into the file, and the two would disagree for anyone reading it.
     * A degree is also below what the eye picks out mid-drag. */
    return fov_row_set((float)(int)(low + fraction * (high - low) + 0.5f));
}
