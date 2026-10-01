/* mp_clock.c: ticks counted, server time estimated, and a blend target that refuses to invent.
 *
 * ============================== Wrap is arithmetic, not an event ===============================
 *
 * Every tick and every millisecond in here is an unsigned 32 bit counter that wraps, and every
 * comparison and distance is taken by subtracting first and reading the difference as signed.
 * That order is what makes the wrap disappear: the difference of two counters is correct modulo
 * 2^32, and as long as the true span between them is under half the range, the signed reading of
 * it is the true span with its sign. Half the range is 35 minutes for microseconds, 24 days for
 * milliseconds and over two years for ticks, all far above what any quantity here spans. The one
 * place a span can genuinely exceed its unit's range, a client asking for an estimate hours after
 * the last snapshot, is refused with a stated limit instead of being computed wrongly.
 *
 * ================================ One base, not an accumulator ================================
 *
 * The server time estimate is recomputed from the newest snapshot every time it is asked for,
 * never accumulated across calls. An accumulator would collect the rounding of every call into a
 * drift that no single reading reveals; a recomputation's error is bounded by one snapshot's
 * arrival jitter forever, no matter how long the session runs. The price is that the estimate
 * jumps by the jitter of each newest snapshot, and the buffer exists to make exactly that
 * invisible.
 *
 * ================================== The bracket over a gap ====================================
 *
 * The interpolation pair is searched among the ticks actually received, not assumed adjacent.
 * When every second snapshot is lost the bracket spans two ticks and alpha runs through the gap
 * at half speed, which is why a buffer of three intervals makes a lost snapshot invisible: the
 * render moment stays behind the newest received tick, so there is always a far side to blend to.
 */
#include "mp_clock.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* True when a comes strictly before b in wrapped tick order. */
static bool tick_before(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) < 0;
}

void mp_clock_init(mp_clock_t *clock)
{
    uint32_t index;

    clock->host_tick        = 0u;
    clock->have_snapshot    = false;
    clock->newest_tick      = 0u;
    clock->newest_arrival_ms = 0u;
    clock->buffer_intervals = MP_CLOCK_BUFFER_DEFAULT_INTERVALS;
    clock->history_count    = 0u;
    for (index = 0u; index < MP_CLOCK_HISTORY; ++index) {
        clock->history[index] = 0u;
    }
    clock->have_arrival     = false;
    clock->have_interval    = false;
    clock->last_arrival_ms  = 0u;
    clock->mean_interval_us = 0u;
    clock->jitter_us        = 0u;
}

void mp_clock_host_substep(mp_clock_t *clock)
{
    ++clock->host_tick;
}

uint32_t mp_clock_host_tick(const mp_clock_t *clock)
{
    return clock->host_tick;
}

/* Keeps the history ascending in wrapped order. Refuses a duplicate, and refuses a tick older
 * than everything a full history holds, because keeping it would evict a newer one that the
 * bracket search may still need. */
static bool history_insert(mp_clock_t *clock, uint32_t tick)
{
    uint32_t at;
    uint32_t index;

    for (at = 0u; at < clock->history_count; ++at) {
        if (clock->history[at] == tick) {
            return false;
        }
        if (tick_before(tick, clock->history[at])) {
            break;
        }
    }

    if (clock->history_count == MP_CLOCK_HISTORY) {
        if (at == 0u) {
            return false;
        }
        /* Evict the oldest; everything below the insertion point moves down one. */
        for (index = 1u; index < at; ++index) {
            clock->history[index - 1u] = clock->history[index];
        }
        clock->history[at - 1u] = tick;
        return true;
    }

    for (index = clock->history_count; index > at; --index) {
        clock->history[index] = clock->history[index - 1u];
    }
    clock->history[at] = tick;
    ++clock->history_count;
    return true;
}

/* The arrival spacing estimator: the smoothed mean with a gain of an eighth and the mean absolute
 * deviation with a gain of a quarter, the pair every smoothed round trip estimator has used since
 * early TCP, chosen because they are proven at exactly this job and cost two divisions. A gap
 * longer than a second is an outage rather than jitter and is clamped, so one stall does not
 * poison the estimate for the next minute. */
static void arrival_observe(mp_clock_t *clock, uint32_t arrival_ms)
{
    uint32_t delta_ms;
    uint32_t sample_us;
    int32_t  difference;
    int32_t  deviation;

    if (!clock->have_arrival) {
        clock->have_arrival    = true;
        clock->last_arrival_ms = arrival_ms;
        return;
    }

    delta_ms = arrival_ms - clock->last_arrival_ms;
    clock->last_arrival_ms = arrival_ms;

    if (delta_ms > 1000u) {
        delta_ms = 1000u;
    }
    sample_us = delta_ms * 1000u;

    if (!clock->have_interval) {
        clock->have_interval    = true;
        clock->mean_interval_us = sample_us;
        clock->jitter_us        = 0u;
        return;
    }

    difference = (int32_t)(sample_us - clock->mean_interval_us);
    clock->mean_interval_us = (uint32_t)((int32_t)clock->mean_interval_us + difference / 8);

    deviation = (difference < 0) ? -difference : difference;
    clock->jitter_us = (uint32_t)((int32_t)clock->jitter_us
                                  + (deviation - (int32_t)clock->jitter_us) / 4);
}

bool mp_clock_snapshot(mp_clock_t *clock, uint32_t server_tick, uint32_t arrival_ms)
{
    if (!history_insert(clock, server_tick)) {
        return false;
    }

    /* Every kept snapshot is an arrival; a reordered one still tells the truth about spacing. */
    arrival_observe(clock, arrival_ms);

    /* Only a NEWER tick moves the estimate base. A late straggler goes into the history above,
     * where the bracket search can still use it, but rebasing on it would step the estimate
     * backwards by however long it sat in the network. */
    if (!clock->have_snapshot || tick_before(clock->newest_tick, server_tick)) {
        clock->have_snapshot     = true;
        clock->newest_tick       = server_tick;
        clock->newest_arrival_ms = arrival_ms;
    }
    return true;
}

bool mp_clock_set_buffer(mp_clock_t *clock, uint32_t intervals)
{
    if (intervals < MP_CLOCK_BUFFER_MIN_INTERVALS || intervals > MP_CLOCK_BUFFER_MAX_INTERVALS) {
        return false;
    }
    clock->buffer_intervals = intervals;
    return true;
}

uint32_t mp_clock_buffer(const mp_clock_t *clock)
{
    return clock->buffer_intervals;
}

bool mp_clock_estimate(const mp_clock_t *clock, uint32_t local_ms,
                       uint32_t *tick, uint32_t *frac_us)
{
    uint32_t elapsed_ms;
    uint32_t elapsed_us;

    if (tick == NULL || frac_us == NULL) {
        return false;
    }
    if (!clock->have_snapshot) {
        return false;
    }

    /* A caller clock a few milliseconds behind the newest arrival is ordinary: a snapshot lands
     * between the caller reading its clock and asking. That is elapsed zero, not an error. */
    elapsed_ms = local_ms - clock->newest_arrival_ms;
    if ((int32_t)elapsed_ms < 0) {
        elapsed_ms = 0u;
    }
    if (elapsed_ms > MP_CLOCK_ESTIMATE_LIMIT_MS) {
        return false;
    }

    elapsed_us = elapsed_ms * 1000u;
    *tick    = clock->newest_tick + elapsed_us / MP_CLOCK_US_PER_TICK;
    *frac_us = elapsed_us % MP_CLOCK_US_PER_TICK;
    return true;
}

bool mp_clock_interpolation(const mp_clock_t *clock, uint32_t local_ms, mp_clock_interp_t *out)
{
    uint32_t estimate_tick;
    uint32_t frac_us;
    uint32_t render_tick;
    uint32_t from = 0u;
    uint32_t to = 0u;
    bool     have_from = false;
    bool     have_to = false;
    uint32_t index;
    uint32_t position_us;
    uint32_t span_us;

    if (out == NULL) {
        return false;
    }
    out->from_tick = 0u;
    out->to_tick   = 0u;
    out->alpha     = 0.0f;

    if (!mp_clock_estimate(clock, local_ms, &estimate_tick, &frac_us)) {
        return false;
    }

    /* One buffer interval is one tick at this protocol's rate; the fraction rides along
     * unchanged, so the render moment is render_tick plus frac_us into it. */
    render_tick = estimate_tick - clock->buffer_intervals;

    /* from is the largest received tick at or before the render moment, to the smallest strictly
     * after it. The history is ascending, so one pass finds both. A tick equal to render_tick is
     * at or before the moment even when the fraction is zero, which is what keeps alpha at zero
     * instead of one when the moment lands exactly on a snapshot. */
    for (index = 0u; index < clock->history_count; ++index) {
        if ((int32_t)(clock->history[index] - render_tick) <= 0) {
            from      = clock->history[index];
            have_from = true;
        } else {
            to      = clock->history[index];
            have_to = true;
            break;
        }
    }

    /* No from: the render moment lies before everything the history holds, which right after
     * joining is the warm up, the buffer not yet filled. No to: underrun, the far side of the
     * blend does not exist yet and producing one would be extrapolation. */
    if (!have_from || !have_to) {
        return false;
    }

    position_us = (render_tick - from) * MP_CLOCK_US_PER_TICK + frac_us;
    span_us     = (to - from) * MP_CLOCK_US_PER_TICK;

    out->from_tick = from;
    out->to_tick   = to;
    out->alpha     = (float)position_us / (float)span_us;

    /* In exact arithmetic the position is strictly inside the span, so alpha is below one. Across
     * a very wide gap the single precision division can still round the quotient up onto 1.0, and
     * the promise to the caller is [0,1), so the promise is enforced here rather than assumed of
     * the rounding. */
    if (out->alpha >= 1.0f) {
        out->alpha = 0.99999994f;
    }
    return true;
}

int32_t mp_clock_pace_decide(mp_clock_pace_t *pace, uint32_t fill_ticks)
{
    int32_t decision = 0;

    if (fill_ticks < MP_CLOCK_PACE_TARGET_MIN_TICKS) {
        ++pace->low_streak;
        pace->high_streak = 0u;
    } else if (fill_ticks > MP_CLOCK_PACE_TARGET_MAX_TICKS) {
        ++pace->high_streak;
        pace->low_streak = 0u;
    } else {
        pace->low_streak  = 0u;
        pace->high_streak = 0u;
    }

    /* The cooldown outranks the streaks: a correction that has been made needs a round trip to
     * show up in the reported fill, and deciding again before seeing it corrects twice. The
     * streaks keep counting underneath, so a condition that persists through the cooldown fires
     * again on the first report after it. */
    if (pace->cooldown > 0u) {
        --pace->cooldown;
        return 0;
    }

    if (pace->low_streak >= MP_CLOCK_PACE_STREAK) {
        decision = 1;
    } else if (pace->high_streak >= MP_CLOCK_PACE_STREAK) {
        decision = -1;
    }

    if (decision != 0) {
        pace->low_streak  = 0u;
        pace->high_streak = 0u;
        pace->cooldown    = MP_CLOCK_PACE_COOLDOWN;
    }
    return decision;
}

bool mp_clock_arrival_stats(const mp_clock_t *clock, uint32_t *mean_us, uint32_t *jitter_us)
{
    if (mean_us == NULL || jitter_us == NULL) {
        return false;
    }
    if (!clock->have_interval) {
        return false;
    }
    *mean_us   = clock->mean_interval_us;
    *jitter_us = clock->jitter_us;
    return true;
}

/* A tick is 125/4 ms exactly. The remainder of the division by four is the tick count modulo
 * four, so the only possible fractions are .0, .25, .5 and .75 ms; adding two before dividing
 * rounds them to nearest with the half rounding up. */
uint32_t mp_clock_ticks_to_ms(uint32_t ticks)
{
    return (ticks * 125u + 2u) / 4u;
}

/* The inverse: ticks are ms times 4/125. The numerator is a multiple of four, so it is never
 * exactly half of 125 and there is no tie to break; adding 62 before dividing rounds to nearest. */
uint32_t mp_clock_ms_to_ticks(uint32_t ms)
{
    return (ms * 4u + 62u) / 125u;
}
