/* overlay_picture.c: see overlay_picture.h. */
#include "overlay_picture.h"

#include "auto_range_row.h"
#include "fov_row.h"
#include "overlay_row_fill.h"
#include "strict_range_row.h"
#include "subtitle_size_row.h"
#include "view_range_live_row.h"
#include "view_range_row.h"

#include "common/text.h"

/* The slots, in drawn order. The draw distance first, because it is the setting a player came
 * looking for, with a note under it saying what the game is actually running and the two gates
 * that decide who else may lower it; then the field of view; then the subtitle size, the one row
 * here that is not the 3-D picture. Three of these are slider tracks, each on its own line so the
 * handle never covers the number it sets. The fog has a group of its own, drawn under this one. */
typedef enum picture_slot {
    PICTURE_VIEW_RANGE = 0,
    PICTURE_VIEW_RANGE_TRACK,
    PICTURE_VIEW_RANGE_LIVE,
    PICTURE_AUTO_RANGE,
    PICTURE_STRICT_RANGE,
    PICTURE_FOV,
    PICTURE_FOV_TRACK,
    PICTURE_SUBTITLE_SIZE,
    PICTURE_SUBTITLE_SIZE_TRACK
} picture_slot_t;

_Static_assert((uint32_t)PICTURE_SUBTITLE_SIZE_TRACK + 1u == OVERLAY_PICTURE_ROW_COUNT,
               "the slot enum and OVERLAY_PICTURE_ROW_COUNT have to end together, or the last "
               "row is either built and never drawn or drawn and never built");

/* The draw distance: the value, its slider, the number in force, and the two switches that
 * decide who else may lower it. True when `slot` was one of these. */
static bool draw_distance_row(uint32_t slot, const char *editing_text, overlay_row_t *out)
{
    switch ((picture_slot_t)slot) {
    case PICTURE_VIEW_RANGE:
        out->kind = OVERLAY_ROW_VALUE;
        /* The accepted range is in the label rather than left for a player to discover by having
           a number refused. */
        overlay_row_label(out->label, "Draw distance (1.0 to 2.5)");
        overlay_row_typed(out, editing_text, view_range_row_format, view_range_row_get());
        return true;

    case PICTURE_VIEW_RANGE_TRACK:
        out->kind = OVERLAY_ROW_SLIDER;
        overlay_row_label(out->label, "");
        /* Both ends are compile-time constants here, unlike the field of view, whose ends come
         * out of the settings file. So there is no divide-by-zero to guard and no way for a
         * player to set them equal. */
        out->fraction = (view_range_row_get() - VIEW_RANGE_MIN) /
                        (VIEW_RANGE_MAX - VIEW_RANGE_MIN);
        overlay_row_clamp_fraction(out);
        return true;

    case PICTURE_VIEW_RANGE_LIVE: {
        /* A note rather than a control, so it cannot be clicked into and cannot be mistaken for
         * something to set. What it reports is the number the game is running, which is not always
         * the number above it: the frame governor lowers that when a scene costs too much, and the
         * cell watchdog lowers it when the draw table or the vertex cache is near overflowing. On
         * Coruscant the watchdog can pin it at 1.00 for a whole level, and the row above then shows
         * a number nothing is using. */
        char text[16];

        out->kind = OVERLAY_ROW_INFO;
        if (view_range_live_row_get(text, sizeof text)) {
            text_format(out->label, sizeof out->label, "  in force: %s", text);
        } else {
            overlay_row_label(out->label, "  in force: not reported");
        }
        out->label[sizeof out->label - 1] = '\0';
        return true;
    }

    case PICTURE_AUTO_RANGE:
        /* Greyed while the row below is on, because the two contradict each other and the one
         * below wins. Strict mode declines the governor outright, so a switch still reading ON
         * would be describing something that is not happening.
         *
         * Its key is deliberately NOT written when that happens. A reader who had the governor on,
         * turns strict on to look at something and turns it off again gets the governor back,
         * rather than finding a setting they never changed has been changed for them. So this row
         * reports the state the game is actually in, and the file keeps the state the reader
         * asked for. */
        overlay_row_label(out->label, "Draw distance follows the frame rate");
        out->available = !strict_range_row_get();
        out->on = out->available && auto_range_row_get();
        return true;

    case PICTURE_STRICT_RANGE:
        /* Named for the trade rather than for the machinery, like the row above it. The frame rate
         * is the cost a reader will actually meet, because the governor is the term that acts in
         * ordinary play; the watchdog only acts above 1.00x, and what it costs when declined is in
         * the ini and in strict_range_row.h rather than in 47 characters. */
        overlay_row_label(out->label, "Keep the draw distance (costs frame rate)");
        out->on = strict_range_row_get();
        return true;

    default:
        return false;
    }
}

/* The field of view, the value and its slider, and the subtitle size with its own. True when
 * `slot` was one of these. */
static bool view_row(uint32_t slot, const char *editing_text, overlay_row_t *out)
{
    switch ((picture_slot_t)slot) {
    case PICTURE_FOV: {
        /* The one row here that can be unavailable. Every other row edits a settings file and works
         * with the DLL that reads it gone; this one needs a width in degrees that only variable_fov
         * can publish, and inventing one would be wrong on some canvas. */
        float degrees;
        char  range[40];

        out->kind = OVERLAY_ROW_VALUE;
        text_format(range, sizeof range, "Field of view (%.0f to %.0f)",
                    (double)fov_row_min(), (double)fov_row_max());
        overlay_row_label(out->label, range);
        if (fov_row_get(&degrees)) {
            overlay_row_typed(out, editing_text, fov_row_format, degrees);
        } else {
            out->available = false;
            overlay_row_label(out->value, "");
        }
        return true;
    }

    case PICTURE_FOV_TRACK: {
        float degrees;
        float low  = fov_row_min();
        float high = fov_row_max();

        out->kind = OVERLAY_ROW_SLIDER;
        overlay_row_label(out->label, "");
        if (!fov_row_get(&degrees)) {
            out->available = false;    /* no published base, so nothing to place a handle against */
            return true;
        }
        /* Guarded rather than assumed: both ends come out of the file, and somebody who sets them
         * equal would otherwise divide by zero here. */
        out->fraction = (high > low) ? ((degrees - low) / (high - low)) : 0.0f;
        /* ExtraDegrees can be set in the file to a width outside the slider's own ends; see
         * overlay_row_clamp_fraction for why the row keeps the honest number and the handle does
         * not. */
        overlay_row_clamp_fraction(out);
        return true;
    }

    case PICTURE_SUBTITLE_SIZE:
        out->kind = OVERLAY_ROW_VALUE;
        /* Named for what it changes rather than for the key it writes, with the band in the label
         * so it need not be found by having a value refused. */
        overlay_row_label(out->label, "Subtitle size (0.50 to 3.0)");
        overlay_row_typed(out, editing_text, subtitle_size_row_format, subtitle_size_row_get());
        return true;

    case PICTURE_SUBTITLE_SIZE_TRACK: {
        const float value = subtitle_size_row_get();

        out->kind = OVERLAY_ROW_SLIDER;
        overlay_row_label(out->label, "");
        /* No availability test: both ends are fixed, so unlike the field of view nothing has to be
         * published by another DLL first. With enhanced_resolution absent the drag writes a key
         * nothing reads, which is how every cross-DLL row here already behaves. */
        out->fraction = (value - SUBTITLE_SIZE_MIN) / (SUBTITLE_SIZE_MAX - SUBTITLE_SIZE_MIN);
        overlay_row_clamp_fraction(out);
        return true;
    }

    default:
        return false;
    }
}

void overlay_picture_row(uint32_t slot, const char *editing_text, overlay_row_t *out)
{
    if (out == NULL) {
        return;
    }
    overlay_row_defaults(out);
    if (draw_distance_row(slot, editing_text, out) || view_row(slot, editing_text, out)) {
        return;
    }

    /* Past the end. Answered as an empty unavailable row rather than left as whatever the
     * caller's struct held: a caller asking for a slot that does not exist has a bug, and a
     * blank row makes that bug visible instead of showing stale text. */
    overlay_row_label(out->label, "");
    out->available = false;
}

bool overlay_picture_toggle(uint32_t slot)
{
    switch ((picture_slot_t)slot) {
    case PICTURE_AUTO_RANGE:
        if (strict_range_row_get()) {
            return false;            /* greyed; the model refuses first, this is the second lock */
        }
        return auto_range_row_set(!auto_range_row_get());
    case PICTURE_STRICT_RANGE:
        return strict_range_row_set(!strict_range_row_get());
    default:
        return false;
    }
}

bool overlay_picture_commit(uint32_t slot, const char *text)
{
    float parsed;

    if (text == NULL || text[0] == '\0') {
        return false;
    }

    /* Refused rather than clamped when the text is not a number. Each of these would turn a typing
     * mistake into an extreme: the shortest draw distance, the narrowest view or the smallest
     * subtitles. */
    switch ((picture_slot_t)slot) {
    case PICTURE_VIEW_RANGE:
        return view_range_row_parse(text, &parsed) && view_range_row_set(parsed);
    case PICTURE_FOV:
        return fov_row_parse(text, &parsed) && fov_row_set(parsed);
    case PICTURE_SUBTITLE_SIZE:
        return subtitle_size_row_parse(text, &parsed) && subtitle_size_row_set(parsed);
    default:
        return false;
    }
}

bool overlay_picture_slider_wants_full_rate(uint32_t slot)
{
    return (picture_slot_t)slot == PICTURE_FOV_TRACK;
}

/* Rounded to a HUNDREDTH, the precision the rows' own formatters show (%.2f). Without it a drag
 * writes more decimals than the text beside it displays and the two disagree about what was set.
 *
 * A fiftieth was tried and is wrong, because the grid has to contain both ends of every row that
 * uses it. The fog thickness, which shares the grid from its own group, starts at 0.25, which is
 * not a multiple of a fiftieth, so dragging fully left rounded up to 0.26 and the documented
 * minimum could not be reached at all. Caught in a log, not in a test. */
static float on_hundredths(float low, float high, float fraction)
{
    float value = low + fraction * (high - low);

    return (float)((int)(value * 100.0f + 0.5f)) / 100.0f;
}

bool overlay_picture_slider_set(uint32_t slot, float fraction)
{
    float low;
    float high;

    if (fraction < 0.0f) {
        fraction = 0.0f;
    }
    if (fraction > 1.0f) {
        fraction = 1.0f;
    }
    switch ((picture_slot_t)slot) {
    case PICTURE_VIEW_RANGE_TRACK:
        return view_range_row_set(on_hundredths(VIEW_RANGE_MIN, VIEW_RANGE_MAX, fraction));
    case PICTURE_SUBTITLE_SIZE_TRACK:
        return subtitle_size_row_set(on_hundredths(SUBTITLE_SIZE_MIN, SUBTITLE_SIZE_MAX,
                                                   fraction));
    case PICTURE_FOV_TRACK:
        low  = fov_row_min();
        high = fov_row_max();
        if (!(high > low)) {
            return false;
        }
        /* Rounded to whole degrees. The row shows whole degrees, so a drag that set 96.4 would
         * display 96 and then write 96.4 back into the file, and the two would disagree for anyone
         * reading it. A degree is also below what the eye picks out mid-drag. */
        return fov_row_set((float)(int)(low + fraction * (high - low) + 0.5f));
    default:
        return false;
    }
}
