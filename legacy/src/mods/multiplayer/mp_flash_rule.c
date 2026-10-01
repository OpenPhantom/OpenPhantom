/* mp_flash_rule.c: whether a detonation is one the player would see. */
#include "mp_flash_rule.h"

#include <math.h>
#include <stddef.h>

#define DEG_TO_RAD 0.01745329252f

bool mp_flash_is_seen(const float eye[3], float heading_deg, const float at[3], float near_units)
{
    float dx, dy, dz, flat, along;

    if (eye == NULL || at == NULL) {
        return true;
    }
    dx = at[0] - eye[0];
    dy = at[1] - eye[1];
    dz = at[2] - eye[2];
    if (dx * dx + dy * dy + dz * dz <= near_units * near_units) {
        return true;
    }
    flat = (float)sqrt((double)(dx * dx + dy * dy));
    if (flat <= 0.0f) {
        return true;   /* straight above or below him is as good as on top of him */
    }
    along = (dx * -(float)sin((double)(heading_deg * DEG_TO_RAD)) +
             dy * (float)cos((double)(heading_deg * DEG_TO_RAD))) / flat;
    return along >= MP_FLASH_IN_VIEW_COS;
}
