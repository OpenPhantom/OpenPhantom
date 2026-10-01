/* overlay_fog.c: see overlay_fog.h. */
#include "overlay_fog.h"

#include "cheats_no_fog.h"
#include "fog_band_row.h"
#include "fog_follow_row.h"
#include "overlay_host_value.h"
#include "overlay_kit.h"

/* The drawn slots, in order. session_lock.c takes every one of them but the first: the band and
 * what it follows write `[view_distance_fix]` keys a session's host hands out, while "No fog"
 * writes this mod's own section. A NUMBER entry in the table below is two of these. */
typedef enum fog_slot {
    FOG_NO_FOG = 0,
    FOG_BAND,
    FOG_BAND_TRACK,
    FOG_FOLLOW
} fog_slot_t;

_Static_assert((uint32_t)FOG_FOLLOW + 1u == OVERLAY_FOG_ROW_COUNT,
               "the slot enum and OVERLAY_FOG_ROW_COUNT have to end together, or the last "
               "row is either built and never drawn or drawn and never built");

/* The cheat offers a flip and no setter, so the state the table asks for is reached by flipping
 * when it is not already there. The only caller ever asks for the other one. */
static bool no_fog_set(bool on)
{
    return (on != cheats_no_fog_is_on()) ? cheats_no_fog_toggle() : true;
}

static bool fog_band_now(float *out)
{
    *out = fog_band_row_get();
    return true;
}

/* What Default puts back: FOG_BAND_DEFAULT, what the game runs at with the key absent. It holds
 * the same number as FOG_BAND_MAX and means a different thing, which is why fog_band_row.h gives
 * it a name of its own: the default has moved four times and the maximum has not moved at all. */
static bool fog_band_standard(float *out)
{
    *out = FOG_BAND_DEFAULT;
    return true;
}

/* On a client of a running session the host decides the band and whether it follows the draw
 * distance, and the two rows the session has taken say so in their chips. "No fog" is this
 * machine's own and has no host value. */
static bool fog_band_host_word(char *out, size_t size, bool *on)
{
    float value;

    (void)on;   /* a number has no switch to report */
    return overlay_host_value(HOST_SETTING_FOG_BAND_SCALE, &value) &&
           overlay_host_number_word(value, fog_band_row_format, out, size);
}

/* Inverted like the row itself: the key is AuthoredFogBand, and the band follows the draw distance
 * while it is 0. */
static bool fog_follow_host_word(char *out, size_t size, bool *on)
{
    float authored;

    if (!overlay_host_value(HOST_SETTING_AUTHORED_FOG_BAND, &authored)) {
        return false;
    }
    *on = authored == 0.0f;
    return overlay_host_switch_word(*on, out, size);
}

static const overlay_kit_entry_t ROWS[] = {
    /* The one row here that is a cheat by origin, and the one that can be unavailable: it needs
     * its engine site, where the rows under it only write a file. */
    { .type       = OVERLAY_KIT_TOGGLE,
      .label      = "No fog",
      .get_on     = cheats_no_fog_is_on,
      .set_on     = no_fog_set,
      .offered    = cheats_no_fog_is_available },

    /* A hundredth, the precision the row's formatter shows, so the drag and the text beside it
     * agree; a fiftieth was tried and could not reach 0.25, this row's own minimum. The full
     * account is at the draw distance in overlay_picture.c. */
    { .type       = OVERLAY_KIT_NUMBER,
      .label      = "Fog thickness (0.25 to 1.0)",
      .number     = fog_band_now,
      .set_number = fog_band_row_set,
      .format     = fog_band_row_format,
      .parse      = fog_band_row_parse,
      .minimum    = FOG_BAND_MIN,
      .maximum    = FOG_BAND_MAX,
      .step       = 0.01f,
      .coarse     = 0.10f,
      .standard   = fog_band_standard,
      .host_word  = fog_band_host_word },

    { .type       = OVERLAY_KIT_TOGGLE,
      .label      = "Fog follows the draw distance",
      .get_on     = fog_follow_row_get,
      .set_on     = fog_follow_row_set,
      .host_word  = fog_follow_host_word }
};

#define ROW_ENTRIES ((uint32_t)(sizeof ROWS / sizeof ROWS[0]))

uint32_t overlay_fog_row_count(void)
{
    return overlay_kit_count(ROWS, ROW_ENTRIES);
}

void overlay_fog_row(uint32_t slot, const char *editing_text, overlay_row_t *out)
{
    overlay_kit_fill(ROWS, ROW_ENTRIES, slot, editing_text, out);
}

bool overlay_fog_toggle(uint32_t slot)
{
    return overlay_kit_activate(ROWS, ROW_ENTRIES, slot);
}

bool overlay_fog_commit(uint32_t slot, const char *text)
{
    return overlay_kit_commit(ROWS, ROW_ENTRIES, slot, text);
}

bool overlay_fog_slider_set(uint32_t slot, float fraction)
{
    return overlay_kit_slider(ROWS, ROW_ENTRIES, slot, fraction);
}

bool overlay_fog_slider_value(uint32_t slot, float fraction, char *out, size_t size)
{
    return overlay_kit_value_at(ROWS, ROW_ENTRIES, slot, fraction, out, size);
}

/* The numbers behind that track, straight off the table entry; see overlay_kit.h. */
bool overlay_fog_slider_limits(uint32_t slot, overlay_number_t *out)
{
    return overlay_kit_limits(ROWS, ROW_ENTRIES, slot, out);
}
