/* diag_projectiles.c: how many entries are alive on the engine's own projectile list right now.
 *
 * FUN_004524b9 (0x004524b9), decompiled during the field report investigation this file supports:
 * a `do { ... local_10 = local_c; } while (true)` loop over a global linked list, head at
 * DAT_00872fb8, next pointer at the record's own +0x00, walked once per simulation step for every
 * live entry regardless of what kind of object it turns out to be. The record carries gravity,
 * velocity, a lifetime timer and a bounce/impact callback - the shape of a generic ballistic
 * physics object, not something the engine's own naming ties to "projectile" specifically. Blaster
 * bolts are the confirmed inhabitant; whether anything else (a dropped weapon, a settling ragdoll)
 * shares the same list is exactly the question this measures rather than assumes.
 *
 * Each entry runs its OWN world-collision trace every tick for as long as it is alive, and an
 * earlier stage of the same investigation measured that trace to be dramatically more expensive
 * against the dense geometry of an enclosed lift shaft than the same trace in open space. A count
 * that spikes at the moment a droid dies on a lift platform, rather than at the moment it is
 * created or fought, points straight at whatever the death handler feeds into this same list.
 *
 * No detour, no signature: DAT_00872fb8 is a fixed global this build's own FUN_004524b9 reads as a
 * literal absolute operand, not a relocatable target, and this is a plain periodic read, nothing
 * hooked. */
#include "diag_projectiles.h"

#include "diag_install.h"
#include "diag_log.h"

#include "common/frame_hook.h"
#include "common/logging.h"
#include "common/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PROJECTILE_LIST_HEAD_ADDRESS 0x00872FB8u
#define PROJECTILE_NEXT_OFFSET       0x00u
#define PROJECTILE_POSITION_OFFSET   0x04u   /* float[3], world x/y/z */

/* A cap on the walk itself, not a belief about how many can really be alive: a corrupted or
 * cyclic list must not turn a periodic report into a hang. */
#define PROJECTILE_WALK_MAX 8192u

/* Short on purpose, unlike the other censuses' 200 frames: at the frame rates this investigation
 * is chasing (single digits during the worst of a stall), 200 frames is tens of seconds between
 * reports, which is longer than the whole event this exists to catch. */
#define PROJECTILE_REPORT_EVERY_FRAMES 30u

/* Past this many live entries, the report also names the first few by position, because a count
 * on its own does not say whether they are spread across the level (normal, many fights at once)
 * or stacked on top of each other (the shape a stuck or repeatedly-retriggered spawn would take). */
#define PROJECTILE_SAMPLE_THRESHOLD 10u
#define PROJECTILE_SAMPLE_COUNT     5u

typedef struct projectile_census {
    bool     armed;
    bool     per_frame;
    uint32_t frame_count;
} projectile_census_t;

static projectile_census_t projectile_census;

static void projectile_census_tick(void)
{
    uint32_t entry;
    uint32_t count;
    uint32_t sampled;

    if (!projectile_census.armed) {
        return;
    }
    ++projectile_census.frame_count;
    if (projectile_census.frame_count < PROJECTILE_REPORT_EVERY_FRAMES) {
        return;
    }
    projectile_census.frame_count = 0;

    if (!memory_read(PROJECTILE_LIST_HEAD_ADDRESS, &entry, sizeof(entry))) {
        return;
    }

    count = 0;
    sampled = 0;
    while (entry != 0 && count < PROJECTILE_WALK_MAX) {
        if (count < PROJECTILE_SAMPLE_THRESHOLD || sampled >= PROJECTILE_SAMPLE_COUNT) {
            /* not yet over threshold, or already sampled enough this report - just keep counting */
        } else {
            float position[3];

            if (memory_read((uintptr_t)entry + PROJECTILE_POSITION_OFFSET, position,
                            sizeof(position))) {
                diag_log_write("prj    #%u at (%.1f, %.1f, %.1f)", (unsigned)count,
                               (double)position[0], (double)position[1], (double)position[2]);
                ++sampled;
            }
        }

        {
            uint32_t next;

            if (!memory_read((uintptr_t)entry + PROJECTILE_NEXT_OFFSET, &next, sizeof(next))) {
                break;
            }
            entry = next;
        }
        ++count;
    }

    diag_log_write("prj  census: %u live entries on the projectile list%s", (unsigned)count,
                   count >= PROJECTILE_WALK_MAX ? " (hit the walk cap, list may be longer or "
                                                   "cyclic)" : "");
}

int diag_projectiles_install(int projectiles_level)
{
    if (projectiles_level <= 0) {
        return 0;
    }

    projectile_census.armed     = true;
    projectile_census.per_frame = frame_hook_add(projectile_census_tick);

    log_info("projectile list census armed at fixed global %08X, reporting every %u frames%s",
             (unsigned)PROJECTILE_LIST_HEAD_ADDRESS, (unsigned)PROJECTILE_REPORT_EVERY_FRAMES,
             projectile_census.per_frame
                 ? ""
                 : ". The per-frame hook is NOT available, so nothing will ever be reported");
    return projectile_census.per_frame ? 1 : 0;
}
