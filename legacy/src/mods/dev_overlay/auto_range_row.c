/* auto_range_row.c: see auto_range_row.h. */
#include "auto_range_row.h"

#include "common/ini.h"

#define VIEW_DISTANCE_SECTION "view_distance_fix"
#define AUTO_RANGE_KEY        "FrameBackoff"

bool auto_range_row_get(void)
{
    /* The default here is the shipped default in view_distance_fix, and the two have to stay in
     * step: a row that reads OFF while the feature is ON would be worse than no row. */
    return ini_read_bool(VIEW_DISTANCE_SECTION, AUTO_RANGE_KEY, false);
}

bool auto_range_row_set(bool enabled)
{
    return ini_write_int(VIEW_DISTANCE_SECTION, AUTO_RANGE_KEY, enabled ? 1 : 0);
}
