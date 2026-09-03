/* view_range_live_row.c: see view_range_live_row.h. */
#include "view_range_live_row.h"

#include "view_range_row.h"

#include "common/ini.h"

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
    (void)_snprintf(out, size - 1u, "%.2fx", (double)value);
    out[size - 1u] = '\0';
    return true;
}
