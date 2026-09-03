/* fog_fov_row.c: see fog_fov_row.h. */
#include "fog_fov_row.h"

#include "fog_follow_row.h"

#include "common/ini.h"

#define VIEW_DISTANCE_SECTION "view_distance_fix"
#define FOG_FOLLOW_FOV_KEY    "FogFollowFov"

bool fog_fov_row_available(void)
{
    /* Nothing to scale unless the band is being scaled at all. */
    return fog_follow_row_get();
}

bool fog_fov_row_get(void)
{
    /* The default here is the shipped default in view_distance_fix, and the two have to stay in
     * step: a row that reads off while the feature is on would be worse than no row. */
    return ini_read_bool(VIEW_DISTANCE_SECTION, FOG_FOLLOW_FOV_KEY, true);
}

bool fog_fov_row_set(bool follow)
{
    if (!fog_fov_row_available()) {
        return false;
    }
    return ini_write_int(VIEW_DISTANCE_SECTION, FOG_FOLLOW_FOV_KEY, follow ? 1 : 0);
}
