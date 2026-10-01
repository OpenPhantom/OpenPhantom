/* npc_range.c: the NPC activation radius a scale may reach. */
#include "npc_range.h"

#include <stddef.h>

float npc_range_scaled(float active, float removal, float scale, bool *capped)
{
    float scaled;
    float share;
    float result;

    if (capped != NULL) {
        *capped = false;
    }
    if (!(active > 0.0f)) {
        return active;
    }
    scaled = active * scale;
    if (!(removal > 0.0f)) {
        return scaled;
    }
    share = removal * NPC_RANGE_REMOVAL_SHARE;
    if (scaled <= share) {
        return scaled;
    }
    /* The authored radius is the floor even where it already reaches the removal radius: the 12
     * placements authored that way behave as they shipped, and the scale is what is taken back. */
    result = (share > active) ? share : active;
    if (result > scaled) {
        result = scaled;
    }
    if (capped != NULL) {
        *capped = result < scaled;
    }
    return result;
}
