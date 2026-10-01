/* mp_timeline.c: the render tick, advanced by one per substep and regulated in eighths.
 *
 * The order inside one substep is fixed by the caller: every arrival of the substep is noted
 * first, then advance runs once. The lag is therefore measured against the newest tick the
 * substep could know, and a substep that received nothing measures one tick less than the one
 * before it, which is what makes a single lost sample invisible: the render still moves by one,
 * the lag dips by one and comes back with the next arrival, and no streak reaches eight.
 *
 * The same ordering is what lets the target lag be measured rather than given. A substep that
 * received nothing is exactly one tick of buffer spent, so the longest run of such substeps is
 * the buffer the stream demands, and both quantities the measurement needs are already here.
 */
#include "mp_timeline.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* The signed reading of a tick difference, valid while the true span is under half the range,
 * which is two years at this rate. */
static int32_t ticks_between(uint32_t later, uint32_t earlier)
{
    return (int32_t)(later - earlier);
}

static uint32_t clamp_target(uint32_t target)
{
    if (target < MP_TIMELINE_TARGET_MIN) {
        return MP_TIMELINE_TARGET_MIN;
    }
    if (target > MP_TIMELINE_TARGET_MAX) {
        return MP_TIMELINE_TARGET_MAX;
    }
    return target;
}

static void clear_dip_window(mp_timeline_t *timeline)
{
    memset(timeline->dip_bucket, 0, sizeof timeline->dip_bucket);
    timeline->bucket_left = MP_TIMELINE_LAG_BUCKET;
}

void mp_timeline_init(mp_timeline_t *timeline, uint32_t target_lag)
{
    /* Both fields the reset reads have to be set before it runs, because it keeps them. */
    timeline->init_target = clamp_target(target_lag);
    timeline->auto_lag    = false;
    mp_timeline_reset(timeline);
}

void mp_timeline_reset(mp_timeline_t *timeline)
{
    uint32_t target    = timeline->init_target;
    bool     automatic = timeline->auto_lag;

    /* Everything but the two settings starts over, including the counters and the measurement:
     * a reset is a new peer, and the target it starts from is the one the caller chose. */
    memset(timeline, 0, sizeof *timeline);
    timeline->init_target = target;
    timeline->target_lag  = target;
    timeline->auto_lag    = automatic;
    timeline->slew_step   = MP_TIMELINE_EIGHTHS;
    timeline->bucket_left = MP_TIMELINE_LAG_BUCKET;

    /* Nothing is given back before a whole window has been measured. */
    timeline->lag_lock = MP_TIMELINE_LAG_WINDOW;
}

void mp_timeline_note_received(mp_timeline_t *timeline, uint32_t tick)
{
    if (!timeline->have_newest) {
        timeline->have_newest = true;
        timeline->newest_tick = tick;
        timeline->first_tick  = tick;
        return;
    }
    if (ticks_between(tick, timeline->newest_tick) > 0) {
        timeline->newest_tick = tick;
    }
}

/* The deepest dip over the newest buckets, the one being filled included, so a dip counts in the
 * substep it happens rather than once its bucket has rolled. */
static uint32_t deepest_dip(const mp_timeline_t *timeline, uint32_t buckets)
{
    uint32_t deepest = 0u;
    uint32_t bucket;

    for (bucket = 0u; bucket < buckets; ++bucket) {
        if (deepest < timeline->dip_bucket[bucket]) {
            deepest = timeline->dip_bucket[bucket];
        }
    }
    return deepest;
}

/* The target those dips ask for: one tick more than the deepest of them. A stream that never dips
 * needs one tick, which is the floor, because the render still has to have the sample it is about
 * to show. */
static uint32_t demand_over(const mp_timeline_t *timeline, uint32_t buckets)
{
    return clamp_target(deepest_dip(timeline, buckets) + 1u);
}

/* Once per substep while the render is running: how far this substep's lag fell short of the
 * target, written into the newest bucket, and the buckets rolled on when one is full.
 *
 * A HALT is deliberately not fed in here, and that was measured rather than assumed. Feeding it
 * took a single four substep stall to the ceiling of six: the render halts, resumes, finds the
 * sender still stopped, halts again, and each halt asks for one more tick than the last. A stall
 * is the sender having stopped, not the line jittering, and the honest answer to it is the halt
 * and the resync that already exist. The dip alone still rises for real jitter, because the lag
 * can fall to minus one before a step would pass the newest sample. */
static void measure_dip(mp_timeline_t *timeline, int32_t lag)
{
    int32_t  shortfall = (int32_t)timeline->target_lag - lag;
    uint32_t dip;
    uint32_t bucket;

    if (shortfall <= 0) {
        dip = 0u;
    } else {
        dip = (uint32_t)shortfall;
    }
    if (timeline->dip_bucket[0] < dip) {
        timeline->dip_bucket[0] = dip;
    }

    --timeline->bucket_left;
    if (timeline->bucket_left != 0u) {
        return;
    }
    for (bucket = MP_TIMELINE_LAG_BUCKETS - 1u; bucket != 0u; --bucket) {
        timeline->dip_bucket[bucket] = timeline->dip_bucket[bucket - 1u];
    }
    timeline->dip_bucket[0] = 0u;
    timeline->bucket_left   = MP_TIMELINE_LAG_BUCKET;
}

/* The target moved. The machinery above it is left alone on purpose: a running slew, the rest
 * after one and a halt all still mean what they meant, and only the two streaks are cleared,
 * because they were counted against a target that has gone. */
static void retarget(mp_timeline_t *timeline, uint32_t target)
{
    timeline->target_lag  = target;
    timeline->high_streak = 0u;
    timeline->low_streak  = 0u;
    timeline->lag_lock    = MP_TIMELINE_LAG_LOCK;
}

/* Up to what the recent window demands at once, down by one tick when the whole window agrees
 * and the bar has run out. */
static void choose_target(mp_timeline_t *timeline)
{
    uint32_t recent = demand_over(timeline, MP_TIMELINE_LAG_RECENT);

    if (recent > timeline->target_lag) {
        /* Through the bar as well: raising only ever moves toward what was just measured and
         * stops at the ceiling, so it cannot pump, and an underrun costs more than the lag. */
        retarget(timeline, recent);
        ++timeline->target_changes;
        return;
    }
    if (timeline->lag_lock != 0u) {
        --timeline->lag_lock;
        return;
    }
    if (demand_over(timeline, MP_TIMELINE_LAG_BUCKETS) < timeline->target_lag) {
        retarget(timeline, timeline->target_lag - 1u);
        ++timeline->target_changes;
    }
}

void mp_timeline_set_auto_lag(mp_timeline_t *timeline, bool enabled)
{
    if (timeline->auto_lag == enabled) {
        return;
    }
    timeline->auto_lag = enabled;
    if (enabled) {
        /* The window starts empty and the bar is a whole one, so the first decision rests on a
         * window that was measured rather than on the zeros of one that was not. */
        clear_dip_window(timeline);
        timeline->lag_lock = MP_TIMELINE_LAG_WINDOW;
        return;
    }
    if (timeline->target_lag != timeline->init_target) {
        /* Switching off is a caller's decision rather than a measurement, so it is not counted
         * with the moves the regulation made. */
        retarget(timeline, timeline->init_target);
    }
}

static void end_regulation(mp_timeline_t *timeline)
{
    timeline->high_streak = 0u;
    timeline->low_streak  = 0u;
    timeline->slew_left   = 0u;
    timeline->slew_step   = MP_TIMELINE_EIGHTHS;
    timeline->cooldown    = 0u;
}

/* The render put a target lag behind the newest tick, with the regulation cleared: the warm-up's
 * end and the rebase after a stall are the same move. */
static void rebase(mp_timeline_t *timeline)
{
    timeline->render_tick   = timeline->newest_tick - timeline->target_lag;
    timeline->render_phase8 = 0u;
    timeline->halted        = false;
    end_regulation(timeline);
}

/* How many eighths this substep advances by. A running slew has decided already; the cooldown
 * after one keeps the streaks at zero so that a decision needs eight fresh substeps; otherwise
 * the lag is judged against the band and a full streak starts a slew, whose first step is taken
 * at once. */
static uint32_t step_eighths(mp_timeline_t *timeline, int32_t lag)
{
    int32_t target = (int32_t)timeline->target_lag;

    if (timeline->slew_left != 0u) {
        --timeline->slew_left;
        if (timeline->slew_left == 0u) {
            timeline->cooldown = MP_TIMELINE_COOLDOWN;
        }
        return timeline->slew_step;
    }
    if (timeline->cooldown != 0u) {
        --timeline->cooldown;
        timeline->high_streak = 0u;
        timeline->low_streak  = 0u;
        return MP_TIMELINE_EIGHTHS;
    }
    if (lag > target + (int32_t)MP_TIMELINE_BAND) {
        ++timeline->high_streak;
        timeline->low_streak = 0u;
    } else if (lag < target - (int32_t)MP_TIMELINE_BAND) {
        ++timeline->low_streak;
        timeline->high_streak = 0u;
    } else {
        timeline->high_streak = 0u;
        timeline->low_streak  = 0u;
    }
    if (timeline->high_streak >= MP_TIMELINE_STREAK) {
        timeline->slew_step = MP_TIMELINE_EIGHTHS + 1u;
        ++timeline->skipped;
    } else if (timeline->low_streak >= MP_TIMELINE_STREAK) {
        timeline->slew_step = MP_TIMELINE_EIGHTHS - 1u;
        ++timeline->inserted;
    } else {
        return MP_TIMELINE_EIGHTHS;
    }
    timeline->high_streak = 0u;
    timeline->low_streak  = 0u;
    timeline->slew_left   = MP_TIMELINE_SLEW - 1u;   /* this substep is the first of the eight */
    return timeline->slew_step;
}

bool mp_timeline_advance(mp_timeline_t *timeline)
{
    int32_t  lag;
    uint32_t eighths;
    uint32_t next_tick;
    uint32_t next_phase;

    if (!timeline->have_newest) {
        return false;
    }

    /* The measurement needs a render tick to measure against, so it starts once the warm-up has
     * produced one. The first version needed none and ran during the warm-up as well, so a peer
     * that delivered one tick and went quiet before the warm-up finished grew a gap for as long
     * as the silence lasted; that case is now simply not measured. Past a halt it stops: there
     * the sender has stopped rather than jittered, and a stall that sized the buffer would hold
     * the target up long after it ended. */
    if (timeline->warm && !timeline->halted) {
        measure_dip(timeline, ticks_between(timeline->newest_tick, timeline->render_tick) - 1);
        if (timeline->auto_lag) {
            choose_target(timeline);
        }
    }

    if (!timeline->warm) {
        if (ticks_between(timeline->newest_tick, timeline->first_tick) <
            (int32_t)timeline->target_lag) {
            return false;
        }
        timeline->warm = true;
        rebase(timeline);
        return true;
    }
    if (timeline->halted) {
        /* The render stands until the sender is a whole target ahead again; resuming is a
         * rebase in effect and counted as one, though the render does not move. It does not jump
         * on the resumption even when the sender resumed with a burst (the engine's frame clamp
         * lets it catch up four substeps in one frame), so a lag of four or five is left for the
         * slews. A sender running under the engine's ten frames a second clamp produces fewer
         * than thirty two ticks a second for as long as it does; the receiver then halts and
         * resumes repeatedly rather than follow with slews, and every resumption counts here. */
        if (ticks_between(timeline->newest_tick, timeline->render_tick) <
            (int32_t)timeline->target_lag) {
            return false;
        }
        timeline->halted = false;
        ++timeline->resyncs;
        return true;
    }

    /* The lag is judged as the render will show it this substep, the newest tick less the tick
     * a plain step reaches, so a steady stream measures the target itself, one lost sample
     * measures one under it, and two in a row are what start a streak. */
    lag = ticks_between(timeline->newest_tick, timeline->render_tick) - 1;
    if (lag > (int32_t)(timeline->target_lag + MP_TIMELINE_RESYNC_LAG)) {
        rebase(timeline);
        ++timeline->resyncs;
        return true;
    }

    eighths    = timeline->render_phase8 + step_eighths(timeline, lag);
    next_tick  = timeline->render_tick + eighths / MP_TIMELINE_EIGHTHS;
    next_phase = eighths % MP_TIMELINE_EIGHTHS;

    /* The render never passes the newest sample: a step that would is cut at the sample, and
     * the timeline halts there rather than extrapolate a body whose input it does not have. */
    if (ticks_between(next_tick, timeline->newest_tick) > 0 ||
        (next_tick == timeline->newest_tick && next_phase != 0u)) {
        timeline->render_tick   = timeline->newest_tick;
        timeline->render_phase8 = 0u;
        timeline->halted        = true;
        ++timeline->halts;
        end_regulation(timeline);
        return false;
    }
    timeline->render_tick   = next_tick;
    timeline->render_phase8 = (uint8_t)next_phase;
    return true;
}

bool mp_timeline_render_known(const mp_timeline_t *timeline)
{
    return timeline->warm;
}

uint32_t mp_timeline_render_tick(const mp_timeline_t *timeline)
{
    return timeline->render_tick;
}

uint8_t mp_timeline_render_phase8(const mp_timeline_t *timeline)
{
    return timeline->render_phase8;
}

uint32_t mp_timeline_newest_tick(const mp_timeline_t *timeline)
{
    return timeline->have_newest ? timeline->newest_tick : 0u;
}

int32_t mp_timeline_lag(const mp_timeline_t *timeline)
{
    if (!timeline->warm) {
        return 0;
    }
    return ticks_between(timeline->newest_tick, timeline->render_tick);
}

bool mp_timeline_halted(const mp_timeline_t *timeline)
{
    return timeline->halted;
}

uint32_t mp_timeline_inserted(const mp_timeline_t *timeline) { return timeline->inserted; }
uint32_t mp_timeline_skipped(const mp_timeline_t *timeline)  { return timeline->skipped; }
uint32_t mp_timeline_resyncs(const mp_timeline_t *timeline)  { return timeline->resyncs; }
uint32_t mp_timeline_halts(const mp_timeline_t *timeline)    { return timeline->halts; }

uint32_t mp_timeline_target_lag(const mp_timeline_t *timeline) { return timeline->target_lag; }
bool     mp_timeline_auto_lag(const mp_timeline_t *timeline)   { return timeline->auto_lag; }

uint32_t mp_timeline_dip(const mp_timeline_t *timeline)
{
    return deepest_dip(timeline, MP_TIMELINE_LAG_BUCKETS);
}

uint32_t mp_timeline_target_changes(const mp_timeline_t *timeline)
{
    return timeline->target_changes;
}
