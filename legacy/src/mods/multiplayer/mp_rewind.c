/* mp_rewind.c: the ring by tick, the window clamp, and the interpolation between two samples. */
#include "mp_rewind.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void mp_rewind_init(mp_rewind_t *rewind)
{
    size_t i;

    rewind->have_newest = false;
    rewind->newest_tick = 0u;
    for (i = 0; i < MP_REWIND_HISTORY; ++i) {
        rewind->used[i] = false;
    }
}

static bool tick_newer(uint32_t lhs, uint32_t rhs)
{
    return (lhs != rhs) && ((uint32_t)(lhs - rhs) < 0x80000000u);
}

void mp_rewind_store(mp_rewind_t *rewind, uint32_t tick, const float position[3])
{
    size_t slot = (size_t)(tick % MP_REWIND_HISTORY);

    rewind->used[slot]        = true;
    rewind->tick[slot]        = tick;
    rewind->position[slot][0] = position[0];
    rewind->position[slot][1] = position[1];
    rewind->position[slot][2] = position[2];

    if (!rewind->have_newest || tick_newer(tick, rewind->newest_tick)) {
        rewind->have_newest = true;
        rewind->newest_tick = tick;
    }
}

static const float *sample_at(const mp_rewind_t *rewind, uint32_t tick)
{
    size_t slot = (size_t)(tick % MP_REWIND_HISTORY);

    if (!rewind->used[slot] || rewind->tick[slot] != tick) {
        return NULL;   /* the slot holds a different tick, or none */
    }
    return rewind->position[slot];
}

bool mp_rewind_oldest_allowed(const mp_rewind_t *rewind, uint32_t *tick)
{
    if (!rewind->have_newest) {
        return false;
    }
    *tick = rewind->newest_tick - MP_REWIND_MAX_TICKS;
    return true;
}

bool mp_rewind_sample(const mp_rewind_t *rewind, uint32_t from_tick, float alpha, float out[3])
{
    uint32_t     oldest;
    const float *a;
    const float *b;
    int          axis;

    if (!rewind->have_newest) {
        return false;
    }

    /* Clamp the request into the window: no further back than the cap, no further forward than the
     * newest sample. A request outside is answered at the edge rather than refused. */
    oldest = rewind->newest_tick - MP_REWIND_MAX_TICKS;
    if (tick_newer(oldest, from_tick)) {
        from_tick = oldest;
        alpha     = 0.0f;
    }
    if (tick_newer(from_tick, rewind->newest_tick)) {
        from_tick = rewind->newest_tick;
        alpha     = 0.0f;
    }
    if (from_tick == rewind->newest_tick) {
        alpha = 0.0f;   /* nothing to interpolate toward past the newest */
    }

    a = sample_at(rewind, from_tick);
    if (a == NULL) {
        return false;   /* the bracketing sample is gone; caller uses the current position */
    }
    if (alpha <= 0.0f) {
        out[0] = a[0];
        out[1] = a[1];
        out[2] = a[2];
        return true;
    }

    b = sample_at(rewind, from_tick + 1u);
    if (b == NULL) {
        out[0] = a[0];
        out[1] = a[1];
        out[2] = a[2];
        return true;   /* no next sample to blend toward; hold the one we have */
    }
    if (alpha > 1.0f) {
        alpha = 1.0f;
    }
    for (axis = 0; axis < 3; ++axis) {
        out[axis] = a[axis] + (b[axis] - a[axis]) * alpha;
    }
    return true;
}
