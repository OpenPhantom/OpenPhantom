/* pixel_fog_row.c: see pixel_fog_row.h. */
#include "pixel_fog_row.h"

#include "common/ini.h"

#define VIEW_DISTANCE_SECTION "view_distance_fix"
#define PIXEL_FOG_KEY         "PixelFog"

bool pixel_fog_row_get(void)
{
    /* The default here is the shipped default in view_distance_fix, and the two have to stay in
     * step: a row that reads off while the feature is on would be worse than no row. */
    return ini_read_bool(VIEW_DISTANCE_SECTION, PIXEL_FOG_KEY, true);
}

bool pixel_fog_row_set(bool per_pixel)
{
    return ini_write_int(VIEW_DISTANCE_SECTION, PIXEL_FOG_KEY, per_pixel ? 1 : 0);
}
