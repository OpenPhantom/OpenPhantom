/* strict_range_row.c: see strict_range_row.h. */
#include "strict_range_row.h"

#include "common/ini.h"

#define VIEW_DISTANCE_SECTION "view_distance_fix"
#define STRICT_RANGE_KEY      "StrictViewRange"

bool strict_range_row_get(void)
{
    /* The default here is the shipped default in view_distance_fix, and the two have to stay in
     * step: a row that reads OFF while the feature is ON would be worse than no row. */
    return ini_read_bool(VIEW_DISTANCE_SECTION, STRICT_RANGE_KEY, false);
}

bool strict_range_row_set(bool strict)
{
    return ini_write_int(VIEW_DISTANCE_SECTION, STRICT_RANGE_KEY, strict ? 1 : 0);
}
