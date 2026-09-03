/* authored_fog_row.c: see authored_fog_row.h. */
#include "authored_fog_row.h"

#include "common/ini.h"

#define VIEW_DISTANCE_SECTION "view_distance_fix"
#define AUTHORED_FOG_KEY      "AuthoredFogBand"
#define PIXEL_FOG_KEY         "PixelFog"

bool authored_fog_row_get(void)
{
    /* The band is what the row reads, because it is the half that decides what the fog looks
     * like. The delivery below moves with it rather than being asked about separately: a state
     * where one is set and the other is not is neither of the two answers this row offers. */
    return ini_read_bool(VIEW_DISTANCE_SECTION, AUTHORED_FOG_KEY, false);
}

bool authored_fog_row_set(bool authored)
{
    /* Both halves, because this row is a choice between two complete answers rather than a single
     * knob. The engine's own per-vertex ramp with the band scaled against the draw distance is one
     * of them, and it is what OpenPhantom shipped; the level's authored band delivered per pixel by
     * the device is the other. Writing one without the other would leave a mixture nobody asked
     * for, and the mixture is where the surprises live: the authored band on the per-vertex ramp
     * is the configuration that shimmers, and the scaled band per pixel is neither faithful nor
     * the shipped look.
     *
     * Order matters only in that the band should not be seen scaled while the delivery has already
     * changed, so the band goes first and the delivery follows it. Both are picked up by
     * view_distance_fix on its own once-a-second poll. */
    if (!ini_write_int(VIEW_DISTANCE_SECTION, AUTHORED_FOG_KEY, authored ? 1 : 0)) {
        return false;
    }
    return ini_write_int(VIEW_DISTANCE_SECTION, PIXEL_FOG_KEY, authored ? 1 : 0);
}
