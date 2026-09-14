/* overlay_fog.c: see overlay_fog.h. */
#include "overlay_fog.h"

#include "cheats_no_fog.h"
#include "fog_band_row.h"
#include "fog_follow_row.h"
#include "overlay_row_fill.h"

/* The slots, in drawn order: the switch that removes the fog, then how thick it is, then what
 * that thickness is measured against. */
typedef enum fog_slot {
    FOG_NO_FOG = 0,
    FOG_BAND,
    FOG_BAND_TRACK,
    FOG_FOLLOW
} fog_slot_t;

_Static_assert((uint32_t)FOG_FOLLOW + 1u == OVERLAY_FOG_ROW_COUNT,
               "the slot enum and OVERLAY_FOG_ROW_COUNT have to end together, or the last "
               "row is either built and never drawn or drawn and never built");

void overlay_fog_row(uint32_t slot, const char *editing_text, overlay_row_t *out)
{
    if (out == NULL) {
        return;
    }
    overlay_row_defaults(out);
    switch ((fog_slot_t)slot) {
    case FOG_NO_FOG:
        /* The one row here that is a cheat by origin, and the one that can be unavailable: it
         * needs its engine site, where the rows under it only write a file. */
        overlay_row_label(out->label, "No fog");
        out->on = cheats_no_fog_is_on();
        out->available = cheats_no_fog_is_available();
        return;

    case FOG_BAND:
        out->kind = OVERLAY_ROW_VALUE;
        overlay_row_label(out->label, "Fog thickness (0.25 to 1.0)");
        overlay_row_typed(out, editing_text, fog_band_row_format, fog_band_row_get());
        return;

    case FOG_BAND_TRACK:
        out->kind = OVERLAY_ROW_SLIDER;
        overlay_row_label(out->label, "");
        out->fraction = (fog_band_row_get() - FOG_BAND_MIN) / (FOG_BAND_MAX - FOG_BAND_MIN);
        overlay_row_clamp_fraction(out);
        return;

    case FOG_FOLLOW:
        overlay_row_label(out->label, "Fog follows the draw distance");
        out->on = fog_follow_row_get();
        return;

    default:
        overlay_row_label(out->label, "");    /* past the end; a blank shows the caller's bug */
        out->available = false;
        return;
    }
}

bool overlay_fog_toggle(uint32_t slot)
{
    switch ((fog_slot_t)slot) {
    case FOG_NO_FOG:
        return cheats_no_fog_toggle();
    case FOG_FOLLOW:
        return fog_follow_row_set(!fog_follow_row_get());
    default:
        return false;
    }
}

bool overlay_fog_commit(uint32_t slot, const char *text)
{
    float parsed;

    if (text == NULL || text[0] == '\0' || (fog_slot_t)slot != FOG_BAND) {
        return false;
    }
    /* Refused, not clamped, when the text is not a number: a typing mistake would otherwise
     * become the thickest fog the range allows. */
    return fog_band_row_parse(text, &parsed) && fog_band_row_set(parsed);
}

bool overlay_fog_slider_set(uint32_t slot, float fraction)
{
    float value;

    if ((fog_slot_t)slot != FOG_BAND_TRACK) {
        return false;
    }
    if (fraction < 0.0f) {
        fraction = 0.0f;
    }
    if (fraction > 1.0f) {
        fraction = 1.0f;
    }
    /* Rounded to a hundredth, the precision the row's formatter shows, so the drag and the text
     * beside it agree; a fiftieth was tried and could not reach 0.25, this row's own minimum. The
     * full account is at the draw distance's slider in overlay_picture.c. */
    value = FOG_BAND_MIN + fraction * (FOG_BAND_MAX - FOG_BAND_MIN);
    return fog_band_row_set((float)((int)(value * 100.0f + 0.5f)) / 100.0f);
}
