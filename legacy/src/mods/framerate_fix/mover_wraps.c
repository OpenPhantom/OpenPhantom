/* mover_wraps.c: see mover_wraps.h. */
#include "mover_wraps.h"

#include "common/logging.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

typedef struct wrap_entry {
    uintptr_t mover;
    uint32_t  count;
    float     track;
    float     drop;
} wrap_entry_t;

static wrap_entry_t wrap_table[MOVER_WRAPS_SLOTS];

bool mover_wraps_is_wrap(float pose_before, float pose_after, float track)
{
    return (pose_after < pose_before) && (track > 0.0f) &&
           ((pose_before - pose_after) > (track * 0.5f));
}

void mover_wraps_note(uintptr_t mover, float track, float drop)
{
    size_t index;
    size_t free_slot = MOVER_WRAPS_SLOTS;

    if (mover == 0u) {
        return;
    }
    for (index = 0; index < MOVER_WRAPS_SLOTS; ++index) {
        if (wrap_table[index].mover == mover) {
            ++wrap_table[index].count;
            wrap_table[index].track = track;
            wrap_table[index].drop  = drop;
            return;
        }
        if (wrap_table[index].mover == 0u && free_slot == MOVER_WRAPS_SLOTS) {
            free_slot = index;
        }
    }
    /* A full table simply stops taking new names. The busiest offender is almost certainly
     * already in it, because it got there by wrapping often. */
    if (free_slot < MOVER_WRAPS_SLOTS) {
        wrap_table[free_slot].mover = mover;
        wrap_table[free_slot].count = 1u;
        wrap_table[free_slot].track = track;
        wrap_table[free_slot].drop  = drop;
    }
}

void mover_wraps_report(uint32_t total, float substep_travel)
{
    size_t index;
    size_t worst = 0;

    for (index = 1; index < MOVER_WRAPS_SLOTS; ++index) {
        if (wrap_table[index].count > wrap_table[worst].count) {
            worst = index;
        }
    }
    if (wrap_table[worst].count == 0u) {
        return;
    }

    /* The half track rule can only tell a wrap from a reversal while half the track beats one
     * substep of travel. Said out loud next to the numbers, because the failure is silent: the
     * mover is drawn unblended every time it turns round and the count looks like ordinary
     * wrapping. */
    if (substep_travel > 0.0f && wrap_table[worst].track < (2.0f * substep_travel)) {
        log_warning("wraps: one mover wrapped %u times of %u in this window, and its track "
                    "length %.2f is under twice one substep of travel %.2f, so the half track "
                    "rule CANNOT separate a wrap from a reversal for it. It is being drawn "
                    "unblended every time it turns round, which is jitter this DLL is causing "
                    "rather than removing. Last drop %.2f.",
                    (unsigned)wrap_table[worst].count, (unsigned)total,
                    (double)wrap_table[worst].track, (double)substep_travel,
                    (double)wrap_table[worst].drop);
        return;
    }

    log_info("wraps: the busiest mover wrapped %u times of %u in this window, track length %.2f, "
             "last drop %.2f. A drop over half the track is read as a wrap, so a track under "
             "twice one substep of travel would be misread on every reversal",
             (unsigned)wrap_table[worst].count, (unsigned)total,
             (double)wrap_table[worst].track, (double)wrap_table[worst].drop);
}

void mover_wraps_reset(void)
{
    memset(wrap_table, 0, sizeof wrap_table);
}
