/* view_range_live_row.c: see view_range_live_row.h. */
#include "view_range_live_row.h"

#include "overlay_host_value.h"
#include "view_range_row.h"

#include "common/ini.h"
#include "common/text.h"

#include <stdio.h>

#define VIEW_DISTANCE_SECTION "view_distance_fix"
#define LIVE_KEY              "EffectiveViewRange"

bool view_range_live_row_get(char *out, size_t size)
{
    char  buffer[32];
    float value = 0.0f;

    if (out == NULL || size == 0u) {
        return false;
    }
    /* A client of a running session whose host's draw distance is the target: view_distance_fix
     * does not write the key then, because the ini is the player's own and the host's value is
     * only for the session, so what it applied is read from the record it files instead. */
    if (overlay_host_view_range_in_force(&value)) {
        text_format(out, size, "%.2fx", (double)value);
        return true;
    }
    if (!ini_read_string(VIEW_DISTANCE_SECTION, LIVE_KEY, "", buffer, sizeof(buffer)) ||
        buffer[0] == '\0') {
        return false;
    }
    /* The draw distance row's parser, reached rather than repeated: both read the same shape of
       number out of the same file, and it is the one that does not depend on the locale's idea of
       a decimal point. */
    if (!view_range_row_parse(buffer, &value) || !(value > 0.0f)) {
        return false;
    }
    text_format(out, size, "%.2fx", (double)value);
    return true;
}
