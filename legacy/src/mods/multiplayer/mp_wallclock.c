/* mp_wallclock.c: the performance counter as milliseconds since the first call. */
#include "mp_wallclock.h"

#include <windows.h>

#include <stdbool.h>
#include <stdint.h>

typedef struct mp_wallclock_state {
    bool     based;
    int64_t  base;        /* the counter at the first call */
    int64_t  frequency;   /* counts per second, fixed for the life of the process */
    uint32_t last_ms;     /* the last value handed out, so the clock is never seen to go back */
} mp_wallclock_state_t;

static mp_wallclock_state_t clock_state;

uint32_t mp_wallclock_ms(void)
{
    LARGE_INTEGER now;
    int64_t       elapsed;
    uint32_t      ms;

    QueryPerformanceCounter(&now);
    if (!clock_state.based) {
        LARGE_INTEGER frequency;

        QueryPerformanceFrequency(&frequency);
        clock_state.based     = true;
        clock_state.base      = now.QuadPart;
        clock_state.frequency = frequency.QuadPart > 0 ? frequency.QuadPart : 1;
        clock_state.last_ms   = 0u;
        return 0u;
    }

    /* Whole milliseconds, computed so the division never overflows: the seconds first, then the
     * remainder scaled up, so a counter at ten megahertz never overflows a sixty four bit product
     * for the life of any session. The counter itself is documented not to go backwards across
     * cores on any system this runs on, and the last value guards the one case where it did. */
    elapsed = now.QuadPart - clock_state.base;
    if (elapsed < 0) {
        return clock_state.last_ms;
    }
    ms = (uint32_t)((elapsed / clock_state.frequency) * 1000 +
                    (elapsed % clock_state.frequency) * 1000 / clock_state.frequency);
    if ((int32_t)(ms - clock_state.last_ms) < 0) {
        return clock_state.last_ms;
    }
    clock_state.last_ms = ms;
    return ms;
}
