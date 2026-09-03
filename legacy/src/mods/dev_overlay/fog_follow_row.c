/* fog_follow_row.c: see fog_follow_row.h. */
#include "fog_follow_row.h"

#include "common/ini.h"

#define VIEW_DISTANCE_SECTION "view_distance_fix"
#define AUTHORED_FOG_KEY      "AuthoredFogBand"

bool fog_follow_row_get(void)
{
    /* Inverted, and the default inverts with it: the shipped AuthoredFogBand is 0, so the row
     * reads on, which is the state view_distance_fix is in with the key absent. */
    return !ini_read_bool(VIEW_DISTANCE_SECTION, AUTHORED_FOG_KEY, false);
}

bool fog_follow_row_set(bool follow)
{
    return ini_write_int(VIEW_DISTANCE_SECTION, AUTHORED_FOG_KEY, follow ? 0 : 1);
}
