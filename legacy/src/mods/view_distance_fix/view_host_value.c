/* view_host_value.c: the host's value or this machine's own, for the three keys a session's host
 * decides in this DLL. See the header.
 */
#include "view_host_value.h"

#include "view_settings.h"

#include "common/numeric.h"

#include <stdbool.h>
#include <stddef.h>

/* A host value is taken only when it is one this DLL would have run itself. The multiplayer's
 * codec and the shared record refuse anything else already; asking again here keeps a record of
 * another build from putting a draw distance past the clamp into the engine. */
static bool inside(float value, float minimum, float maximum)
{
    return numeric_is_finite(value) && value >= minimum && value <= maximum;
}

view_choice_t view_host_pick_range(bool pinned, bool host_named, float host_value,
                                   float own_value)
{
    view_choice_t choice;

    choice.from_host = false;
    if (pinned) {
        choice.value = VIEW_SETTINGS_RANGE_MIN;
        return choice;
    }
    if (host_named && inside(host_value, VIEW_SETTINGS_RANGE_MIN, VIEW_SETTINGS_RANGE_MAX)) {
        choice.value     = host_value;
        choice.from_host = true;
        return choice;
    }
    choice.value = numeric_clamp(own_value, VIEW_SETTINGS_RANGE_MIN, VIEW_SETTINGS_RANGE_MAX);
    return choice;
}

view_choice_t view_host_pick_fog_band(bool host_named, float host_value, float own_value)
{
    view_choice_t choice;

    if (host_named && inside(host_value, VIEW_SETTINGS_FOG_BAND_MIN, VIEW_SETTINGS_FOG_BAND_MAX)) {
        choice.value     = host_value;
        choice.from_host = true;
        return choice;
    }
    choice.value     = numeric_clamp(own_value, VIEW_SETTINGS_FOG_BAND_MIN,
                                     VIEW_SETTINGS_FOG_BAND_MAX);
    choice.from_host = false;
    return choice;
}

bool view_host_pick_authored(bool host_named, float host_value, bool own_value, bool *from_host)
{
    bool host_says = host_named && (host_value == 0.0f || host_value == 1.0f);

    if (from_host != NULL) {
        *from_host = host_says;
    }
    return host_says ? host_value == 1.0f : own_value;
}

/* Half of the hundredth the note and the ini print, so two values that print alike are one. */
#define SHOWN_HALF_STEP 0.005f

bool view_host_shows_the_same(float scale, float shown)
{
    return scale > shown - SHOWN_HALF_STEP && scale < shown + SHOWN_HALF_STEP;
}

bool view_host_writes_effective(bool host_range_in_force, float scale, float *published)
{
    if (published == NULL) {
        return false;
    }
    if (host_range_in_force) {
        *published = -1.0f;
        return false;
    }
    if (*published >= 0.0f && view_host_shows_the_same(scale, *published)) {
        return false;
    }
    *published = scale;
    return true;
}
