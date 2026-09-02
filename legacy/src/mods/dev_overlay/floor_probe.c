/* floor_probe.c: the engine's own "what is the floor under this point", wrapped for two callers.
 *
 * bapmap_probeFloor is named and typed by j0nny's decomp (include/bapmap.h section 7, body at
 * bp/bapmap.c:1866):
 *
 *   void bapmap_probeFloor(const vec3 *pos, groundContact *out)
 *
 *   groundContact +0x00  f32 dist   the selected floor's SIGNED height over the probe point.
 *                                   Positive is a floor ABOVE the point, negative is a drop below
 *                                   it, and it is SEEDED to 3.4e38 and only replaced when a floor
 *                                   polygon is actually found under the probe's own x/y. The whole
 *                                   block is 0x88 bytes.
 *
 * ITS DOWNWARD REACH IS UNLIMITED, which is what makes it usable as "is there anything under this
 * point at all". The one range limit in its acceptance rule, 1.0 unit, applies to floors ABOVE the
 * feet - the step-up case - and not to drops, so a floor a thousand units below is still found.
 * That was read out of the decompiled body rather than assumed; getting it backwards would have
 * made every high drop read as a void.
 */
#include "floor_probe.h"

#include "common/logging.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* Address free; measured ONE match. The prologue's own frame size and the [ebp+0xc] / +0x18 walk
 * of the second argument are what make it unique - there is no shorter distinctive run here. */
static const uint8_t SIG_PROBE_FLOOR[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x7C, 0x57,
    0xC7, 0x45, 0xC8, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x45, 0x0C, 0x8B, 0x48, 0x18, 0x89, 0x4D, 0xA4,
    0x83, 0x7D, 0xA4, 0x00, 0x74, 0x1E
};

#define GROUND_CONTACT_SIZE 0x88u
#define PROBE_NO_FLOOR      3.0e38f   /* the engine seeds dist to 3.4e38 for "nothing found" */

typedef void (__cdecl *probe_floor_fn_t)(const float *pos, void *out);

static probe_floor_fn_t probe_floor;
static bool             probe_floor_resolved;

floor_probe_result_t floor_probe_below(const float *position, float *out_drop)
{
    unsigned char ground[GROUND_CONTACT_SIZE];
    float         dist;

    /* Resolved lazily rather than at install time: both callers only ever ask while a cheat of
     * theirs is actually in use, so an executable that never opens the panel never pays for it. */
    if (!probe_floor_resolved) {
        probe_floor_resolved = true;
        probe_floor = (probe_floor_fn_t)(uintptr_t)signature_find_unique(SIG_PROBE_FLOOR, NULL,
                                                                         sizeof SIG_PROBE_FLOOR);
        log_info("the floor probe %s", (probe_floor != NULL)
                     ? "resolved: falls and teleports can both be asked about"
                     : "did NOT resolve, so both callers keep their old behaviour");
    }
    if (probe_floor == NULL || position == NULL) {
        return FLOOR_PROBE_UNAVAILABLE;
    }

    memset(ground, 0, sizeof ground);
    probe_floor(position, ground);
    memcpy(&dist, ground, sizeof dist);

    /* Not compared with 3.4e38 exactly: a sentinel that is merely enormous, or a NaN out of a
     * position that has already gone bad, both have to read as "nothing to land on". */
    if (dist > PROBE_NO_FLOOR || dist < -PROBE_NO_FLOOR || dist != dist) {
        return FLOOR_PROBE_NONE;
    }

    if (out_drop != NULL) {
        /* Signed the other way round for the caller: they asked how far DOWN it is. A positive
         * dist means the floor is above the point, which is not a drop at all, so it reads zero. */
        *out_drop = (dist < 0.0f) ? -dist : 0.0f;
    }
    return FLOOR_PROBE_FOUND;
}
