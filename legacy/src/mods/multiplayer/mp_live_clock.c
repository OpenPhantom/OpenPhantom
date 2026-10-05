/* mp_live_clock.c: the time in which the engine could answer. See the header. */
#include "mp_live_clock.h"

#include <stddef.h>

uint32_t mp_live_clock_look(mp_live_clock_t *clock, uint32_t steps, bool level_runs, bool parked,
                            uint32_t wall_ms)
{
    uint32_t passed;
    bool     held;

    if (clock == NULL) {
        return 0u;
    }
    if (!clock->known) {
        clock->known   = true;
        clock->steps   = steps;
        clock->wall_ms = wall_ms;
        return clock->ms;
    }

    /* A difference, so a wall clock that has wrapped is still read right. */
    passed         = (uint32_t)(wall_ms - clock->wall_ms);
    clock->wall_ms = wall_ms;

    if (steps != clock->steps || !level_runs) {
        clock->steps    = steps;
        clock->still_ms = 0u;
    } else if (clock->still_ms <= MP_LIVE_CLOCK_GAP_MS) {
        /* Only "past the gap" is ever asked of it, so one long look saturates it and a hold of
         * any length cannot wrap it. */
        clock->still_ms = passed > MP_LIVE_CLOCK_GAP_MS ? MP_LIVE_CLOCK_GAP_MS + 1u
                                                         : clock->still_ms + passed;
    }

    held = level_runs && (parked || clock->still_ms > MP_LIVE_CLOCK_GAP_MS);
    if (held) {
        if (!clock->holding) {
            ++clock->holds;
        }
        clock->held_ms += passed;
    } else {
        clock->ms += passed;
    }
    clock->holding = held;
    return clock->ms;
}
