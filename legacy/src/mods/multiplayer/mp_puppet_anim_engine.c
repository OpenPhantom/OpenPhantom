/* mp_puppet_anim_engine.c: the swing table's clip column, read out of the engine into the
 * puppet's exclusion set.
 *
 * Left mp_puppet_anim.c on 2026-09-04, when the fixes after a review put
 * that file past its size note. What lives here is the one read of engine data the animation
 * decisions stand on that is neither a track nor a clip player: the table the swing starter
 * indexes, whose first word per row is the clip a swing plays and which the state path therefore
 * never starts. It is read on every reset rather than once at resolve, because the dev overlay's
 * character profile rewrites the column in place, and an exclusion read before the profile would
 * let the state path start the profile's swing clip beside the swing event.
 */
#include "mp_puppet_anim.h"

#include "mp_cells.h"

#include "common/logging.h"
#include "common/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The swing table row stride; the clip is the row's first word. */
#define SWING_ROW_BYTES 0x20u

/* The first read, or its refusal, is said once; a later read that finds the column changed says
 * so as well, because that is the profile at work and not a fault. */
static bool swing_logged;

void mp_puppet_anim_read_swing_table(mp_puppet_anim_exclusions_t *exclusions)
{
    uintptr_t table = mp_cells_address(MP_CELL_SWING_TABLE);
    uint16_t  clips[MP_PUPPET_ANIM_SWING_ROWS];
    size_t    row;

    if (exclusions == NULL) {
        return;
    }
    if (table == 0) {
        exclusions->swing_count = 0;
        if (!swing_logged) {
            swing_logged = true;
            log_warning("the swing table cell is unknown, so the puppet's state path cannot tell "
                        "a swing clip from any other and may start one twice; degraded, not "
                        "disabled");
        }
        return;
    }
    for (row = 0; row < MP_PUPPET_ANIM_SWING_ROWS; ++row) {
        uint32_t clip = 0;

        if (!memory_try_read(table + row * SWING_ROW_BYTES, &clip, sizeof clip) || clip > 0xFFFFu) {
            exclusions->swing_count = 0;
            if (!swing_logged) {
                swing_logged = true;
                log_warning("swing table row %u at %08X does not read as a clip ordinal (%08X); "
                            "the exclusion is left empty", (unsigned)row,
                            (unsigned)(table + row * SWING_ROW_BYTES), (unsigned)clip);
            }
            return;
        }
        clips[row] = (uint16_t)clip;
    }
    if (exclusions->swing_count != 0u &&
        memcmp(clips, exclusions->swing_clip, sizeof clips) != 0) {
        log_info("the swing table's clips changed since the last read; the puppet's state path "
                 "follows the table as it stands now");
    }
    memcpy(exclusions->swing_clip, clips, sizeof clips);
    exclusions->swing_count = MP_PUPPET_ANIM_SWING_ROWS;
    if (!swing_logged) {
        swing_logged = true;
        log_info("the puppet's state path leaves the swing table's %u clips to the swing event",
                 (unsigned)MP_PUPPET_ANIM_SWING_ROWS);
    }
}
