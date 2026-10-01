/* mp_clock.c: the network clock, driven over its edges rather than its middle.
 *
 * What has to hold is not that one snapshot produces one blend target. It is that the counters
 * wrap without an event, that a lost snapshot leaves a gap the blend carries across, that a late
 * straggler fills its gap without stepping the estimate backwards, that the estimate does not
 * drift no matter how long the session runs, that alpha stays inside [0,1) at every millisecond
 * of a sweep, that the clock refuses before the first snapshot and on underrun instead of
 * extrapolating, and that the pace decision cannot be made to oscillate by a fill that does.
 */
#include "unittest.h"

#include "mp_clock.h"

#include <stdbool.h>
#include <stdint.h>

/* The true arrival instant of snapshot i on the exact 31.25 ms cadence, as an integer millisecond
 * clock reports it: rounded down. Exact whenever i is a multiple of four. */
static uint32_t cadence_ms(uint32_t base, uint32_t index)
{
    return base + (index * 125u) / 4u;
}

static void check_conversions(void)
{
    ut_section("tick and millisecond spans convert with stated rounding");

    ut_check(mp_clock_ticks_to_ms(0u) == 0u, "zero ticks is zero milliseconds");
    ut_check(mp_clock_ticks_to_ms(1u) == 31u, "one tick, 31.25 ms, rounds down to 31");
    ut_check(mp_clock_ticks_to_ms(2u) == 63u, "two ticks, 62.5 ms, is a half and rounds up to 63");
    ut_check(mp_clock_ticks_to_ms(3u) == 94u, "three ticks, 93.75 ms, rounds up to 94");
    ut_check(mp_clock_ticks_to_ms(4u) == 125u, "four ticks is exactly 125 ms");
    ut_check(mp_clock_ticks_to_ms(32u) == 1000u, "one second of ticks is exactly one second");

    ut_check(mp_clock_ms_to_ticks(0u) == 0u, "zero milliseconds is zero ticks");
    ut_check(mp_clock_ms_to_ticks(15u) == 0u, "15 ms is 0.48 ticks and rounds down");
    ut_check(mp_clock_ms_to_ticks(16u) == 1u, "16 ms is 0.512 ticks and rounds up");
    ut_check(mp_clock_ms_to_ticks(1000u) == 32u, "one second is exactly 32 ticks");
    ut_check(mp_clock_ms_to_ticks(mp_clock_ticks_to_ms(128u)) == 128u,
             "a whole second multiple survives the round trip exactly");
}

static void check_host_counting(void)
{
    mp_clock_t clock;

    ut_section("the host counts substeps and wraps like any counter");

    mp_clock_init(&clock);
    ut_check(mp_clock_host_tick(&clock) == 0u, "a fresh host has completed no substep");
    mp_clock_host_substep(&clock);
    mp_clock_host_substep(&clock);
    ut_check(mp_clock_host_tick(&clock) == 2u, "two substeps are tick two");

    clock.host_tick = 0xFFFFFFFFu;
    mp_clock_host_substep(&clock);
    ut_check(mp_clock_host_tick(&clock) == 0u,
             "the tick after the last representable one is zero, an ordinary increment");
}

static void check_refusal_before_snapshots(void)
{
    mp_clock_t        clock;
    mp_clock_interp_t interp;
    uint32_t          tick = 0u;
    uint32_t          frac = 0u;

    ut_section("before the first snapshot there is nothing to estimate or blend");

    mp_clock_init(&clock);
    ut_check(!mp_clock_estimate(&clock, 1000u, &tick, &frac),
             "the estimate is refused, not invented");
    ut_check(!mp_clock_interpolation(&clock, 1000u, &interp),
             "and so is the blend target");

    ut_check(mp_clock_snapshot(&clock, 500u, 1000u), "the first snapshot is taken");
    ut_check(!mp_clock_interpolation(&clock, 1000u, &interp),
             "one snapshot is still refused: the render moment lies a whole buffer before it and "
             "there is nothing there to blend from");
}

static void check_estimate(void)
{
    mp_clock_t clock;
    uint32_t   tick = 0u;
    uint32_t   frac = 0u;

    ut_section("the estimate is the newest tick plus locally elapsed time");

    mp_clock_init(&clock);
    (void)mp_clock_snapshot(&clock, 100u, 5000u);

    ut_check(mp_clock_estimate(&clock, 5000u, &tick, &frac) && tick == 100u && frac == 0u,
             "at the arrival instant the estimate is the snapshot's own tick");
    ut_check(mp_clock_estimate(&clock, 5031u, &tick, &frac) && tick == 100u && frac == 31000u,
             "31 ms later it is still inside the same tick, 31000 microseconds in");
    ut_check(mp_clock_estimate(&clock, 5032u, &tick, &frac) && tick == 101u && frac == 750u,
             "32 ms later it has crossed into the next tick, 750 microseconds in");
    ut_check(mp_clock_estimate(&clock, 4990u, &tick, &frac) && tick == 100u && frac == 0u,
             "a caller clock a few ms behind the arrival reads as elapsed zero, not as an error");
    ut_check(!mp_clock_estimate(&clock, 5000u + MP_CLOCK_ESTIMATE_LIMIT_MS + 1u, &tick, &frac),
             "an hour past the newest snapshot the estimate is refused with a stated limit");
}

static void check_interpolation_and_alpha_bounds(void)
{
    mp_clock_t        clock;
    mp_clock_interp_t interp;
    uint32_t          index;
    uint32_t          last_ms;
    uint32_t          in_bounds = 0u;
    uint32_t          ordered = 0u;

    ut_section("the blend target under a healthy stream, and alpha over a full sweep");

    mp_clock_init(&clock);
    for (index = 0u; index <= 15u; ++index) {
        (void)mp_clock_snapshot(&clock, 200u + index, cadence_ms(10000u, index));
    }
    last_ms = cadence_ms(10000u, 15u);

    ut_check(mp_clock_interpolation(&clock, last_ms, &interp)
             && interp.from_tick == 212u && interp.to_tick == 213u,
             "with the default buffer of three the render moment is three ticks in the past");
    ut_near(interp.alpha, 0.0, 0.0001, "and lands exactly on a snapshot, so alpha is zero");

    for (index = 0u; index <= 93u; ++index) {
        if (!mp_clock_interpolation(&clock, last_ms + index, &interp)) {
            continue;
        }
        if (interp.alpha >= 0.0f && interp.alpha < 1.0f) {
            ++in_bounds;
        }
        if ((int32_t)(interp.from_tick - interp.to_tick) < 0) {
            ++ordered;
        }
    }
    ut_check(in_bounds == 94u,
             "at every millisecond of the 94 ms window alpha is at least zero and below one");
    ut_check(ordered == 94u, "and the from tick is strictly before the to tick every time");
}

static void check_buffer_setter(void)
{
    mp_clock_t clock;

    ut_section("the buffer takes two or three intervals and refuses the rest");

    mp_clock_init(&clock);
    ut_check(mp_clock_buffer(&clock) == 3u, "the default is three intervals");
    ut_check(mp_clock_set_buffer(&clock, 2u), "two intervals, the LAN floor, is accepted");
    ut_check(!mp_clock_set_buffer(&clock, 1u), "one interval is refused");
    ut_check(mp_clock_buffer(&clock) == 2u, "and the refusal leaves the standing value alone");
    ut_check(!mp_clock_set_buffer(&clock, 4u), "four intervals is refused too");
    ut_check(mp_clock_set_buffer(&clock, 3u), "three is accepted again");
}

static void check_snapshot_loss(void)
{
    mp_clock_t        clock;
    mp_clock_interp_t interp;
    uint32_t          tick;
    uint32_t          last_ms;
    uint32_t          index;
    uint32_t          carried = 0u;

    ut_section("every second snapshot lost: the blend carries across the gap with buffer three");

    mp_clock_init(&clock);
    for (tick = 300u; tick <= 320u; tick += 2u) {
        (void)mp_clock_snapshot(&clock, tick, cadence_ms(20000u, tick - 300u));
    }
    last_ms = cadence_ms(20000u, 20u);

    ut_check(mp_clock_interpolation(&clock, last_ms, &interp)
             && interp.from_tick == 316u && interp.to_tick == 318u,
             "the render moment falls on a lost tick and the bracket spans the gap");
    ut_near(interp.alpha, 0.5, 0.001, "halfway between the received neighbours, alpha is one half");

    ut_check(mp_clock_interpolation(&clock, last_ms + 15u, &interp)
             && interp.from_tick == 316u && interp.to_tick == 318u,
             "15 ms later the moment sits inside the gap's second half");
    ut_near(interp.alpha, 0.74, 0.001, "and alpha runs through the gap at half speed");

    for (index = 0u; index <= 93u; ++index) {
        if (mp_clock_interpolation(&clock, last_ms + index, &interp)) {
            ++carried;
        }
    }
    ut_check(carried == 94u,
             "the whole 94 ms window blends despite half the snapshots being gone");
}

static void check_reordering(void)
{
    mp_clock_t        clock;
    mp_clock_interp_t interp;
    uint32_t          tick = 0u;
    uint32_t          frac = 0u;

    ut_section("a late straggler fills its gap and never steps the estimate backwards");

    mp_clock_init(&clock);
    (void)mp_clock_set_buffer(&clock, 2u);
    (void)mp_clock_snapshot(&clock, 400u, 30000u);
    (void)mp_clock_snapshot(&clock, 402u, 30062u);

    ut_check(mp_clock_interpolation(&clock, 30077u, &interp)
             && interp.from_tick == 400u && interp.to_tick == 402u,
             "before the straggler the bracket spans the missing tick");
    ut_near(interp.alpha, 0.24, 0.001, "and alpha is measured against the two tick span");

    ut_check(mp_clock_snapshot(&clock, 401u, 30080u),
             "the straggler, older than the newest, is still taken");
    ut_check(mp_clock_estimate(&clock, 30080u, &tick, &frac) && tick == 402u && frac == 18000u,
             "but the estimate stays based on the newest tick, not on the late arrival");

    ut_check(mp_clock_interpolation(&clock, 30077u, &interp)
             && interp.from_tick == 400u && interp.to_tick == 401u,
             "the same moment now blends against the filled gap");
    ut_near(interp.alpha, 0.48, 0.001, "with alpha measured against the one tick span");

    ut_check(!mp_clock_snapshot(&clock, 402u, 30085u),
             "a duplicate tick changes nothing and says so");
}

static void check_wraparound(void)
{
    mp_clock_t        clock;
    mp_clock_interp_t interp;
    uint32_t          index;

    ut_section("the tick counter wraps mid blend");

    mp_clock_init(&clock);
    (void)mp_clock_set_buffer(&clock, 2u);
    (void)mp_clock_snapshot(&clock, 0xFFFFFFFEu, cadence_ms(40000u, 0u));
    (void)mp_clock_snapshot(&clock, 0xFFFFFFFFu, cadence_ms(40000u, 1u));
    (void)mp_clock_snapshot(&clock, 0u, cadence_ms(40000u, 2u));
    (void)mp_clock_snapshot(&clock, 1u, cadence_ms(40000u, 3u));

    ut_check(mp_clock_interpolation(&clock, cadence_ms(40000u, 3u), &interp)
             && interp.from_tick == 0xFFFFFFFFu && interp.to_tick == 0u,
             "the bracket crosses from the last representable tick to tick zero");
    ut_near(interp.alpha, 0.0, 0.0001, "landing on the from side, alpha is zero");

    ut_check(mp_clock_interpolation(&clock, cadence_ms(40000u, 3u) + 15u, &interp)
             && interp.from_tick == 0xFFFFFFFFu && interp.to_tick == 0u,
             "15 ms later the pair still straddles the wrap");
    ut_near(interp.alpha, 0.48, 0.001, "and alpha is ordinary arithmetic across it");

    ut_section("the caller's millisecond clock wraps mid session");

    mp_clock_init(&clock);
    (void)mp_clock_set_buffer(&clock, 2u);
    for (index = 0u; index <= 3u; ++index) {
        (void)mp_clock_snapshot(&clock, 700u + index, cadence_ms(0xFFFFFFF0u, index));
    }
    ut_check(mp_clock_interpolation(&clock, cadence_ms(0xFFFFFFF0u, 3u) + 15u, &interp)
             && interp.from_tick == 701u && interp.to_tick == 702u,
             "arrival stamps on both sides of the wrap still bracket correctly");
    ut_near(interp.alpha, 0.48, 0.001, "and the elapsed time across the wrap reads as 15 ms");
}

static void check_long_run_drift(void)
{
    mp_clock_t        clock;
    mp_clock_interp_t interp;
    uint32_t          index;
    uint32_t          tick = 0u;
    uint32_t          frac = 0u;
    uint32_t          exact = 0u;
    uint32_t          sampled = 0u;

    ut_section("the estimate does not drift over a long session");

    mp_clock_init(&clock);
    for (index = 0u; index <= 32000u; ++index) {
        (void)mp_clock_snapshot(&clock, 1000u + index, cadence_ms(100000u, index));
        if (index % 4000u == 0u && index > 0u) {
            ++sampled;
            if (mp_clock_estimate(&clock, cadence_ms(100000u, index) + 13u, &tick, &frac)
                && tick == 1000u + index && frac == 13000u) {
                ++exact;
            }
        }
    }
    ut_checkf(exact == sampled,
              "at %u probes across a quarter hour the estimate is exact to the microsecond, "
              "because it is recomputed from the newest snapshot instead of accumulated",
              (unsigned)sampled);

    ut_check(mp_clock_estimate(&clock, cadence_ms(100000u, 32000u), &tick, &frac)
             && tick == 33000u && frac == 0u,
             "after 32000 snapshots the estimate still lands exactly on the newest tick");
    ut_check(mp_clock_interpolation(&clock, cadence_ms(100000u, 32000u), &interp)
             && interp.from_tick == 32997u && interp.to_tick == 32998u,
             "and the blend target is still three whole ticks in the past");
}

static void check_underrun_refusal(void)
{
    mp_clock_t        clock;
    mp_clock_interp_t interp;
    uint32_t          index;
    uint32_t          last_ms;

    ut_section("when snapshots stop the clock refuses instead of extrapolating");

    mp_clock_init(&clock);
    for (index = 0u; index <= 15u; ++index) {
        (void)mp_clock_snapshot(&clock, 200u + index, cadence_ms(50000u, index));
    }
    last_ms = cadence_ms(50000u, 15u);

    ut_check(mp_clock_interpolation(&clock, last_ms + 93u, &interp),
             "93 ms after the last snapshot the buffer still covers the render moment");
    ut_check(!mp_clock_interpolation(&clock, last_ms + 94u, &interp),
             "at 94 ms the moment reaches the newest tick and the far side of the blend does not "
             "exist, so the answer is no rather than an invented position");
    ut_check(!mp_clock_interpolation(&clock, last_ms + 200u, &interp),
             "and it stays no for as long as nothing arrives");

    ut_check(mp_clock_snapshot(&clock, 216u, last_ms + 200u), "then a snapshot arrives");
    ut_check(mp_clock_interpolation(&clock, last_ms + 200u, &interp)
             && interp.from_tick == 213u && interp.to_tick == 214u,
             "and blending resumes at the rebased moment without any special recovery");
}

static void check_pace_hysteresis(void)
{
    mp_clock_pace_t pace = { 0u, 0u, 0u };
    uint32_t        report;
    int32_t         decision;
    uint32_t        nonzero = 0u;
    uint32_t        fired_at_4 = 0u;
    uint32_t        fired_at_37 = 0u;
    uint32_t        fired_at_70 = 0u;
    uint32_t        fired_elsewhere = 0u;

    ut_section("the pace decision under a fill that behaves");

    for (report = 1u; report <= 64u; ++report) {
        decision = mp_clock_pace_decide(&pace, (report % 2u == 0u) ? 2u : 1u);
        if (decision != 0) {
            ++nonzero;
        }
    }
    ut_check(nonzero == 0u, "a fill inside the one to two tick band never asks for a substep");

    ut_section("a starving fill: one insertion, then a cooldown, at one correction per second");

    pace.low_streak  = 0u;
    pace.high_streak = 0u;
    pace.cooldown    = 0u;
    for (report = 1u; report <= 80u; ++report) {
        decision = mp_clock_pace_decide(&pace, 0u);
        if (decision == 1) {
            if (report == 4u) {
                ++fired_at_4;
            } else if (report == 37u) {
                ++fired_at_37;
            } else if (report == 70u) {
                ++fired_at_70;
            } else {
                ++fired_elsewhere;
            }
        } else if (decision != 0) {
            ++fired_elsewhere;
        }
    }
    ut_check(fired_at_4 == 1u,
             "the first insertion comes only after a whole streak of starving reports");
    ut_check(fired_at_37 == 1u && fired_at_70 == 1u,
             "and repeats once per cooldown while the condition persists");
    ut_check(fired_elsewhere == 0u, "with nothing in between, which caps the correction rate");

    ut_section("an oscillating fill cannot flip the decision");

    pace.low_streak  = 0u;
    pace.high_streak = 0u;
    pace.cooldown    = 0u;
    nonzero = 0u;
    for (report = 1u; report <= 100u; ++report) {
        decision = mp_clock_pace_decide(&pace, (report % 2u == 0u) ? 3u : 0u);
        if (decision != 0) {
            ++nonzero;
        }
    }
    ut_check(nonzero == 0u,
             "a fill bouncing between starving and overfull is jitter, and every bounce resets "
             "the other side's streak, so no decision ever fires");

    ut_section("a brief dip is not a trend");

    pace.low_streak  = 0u;
    pace.high_streak = 0u;
    pace.cooldown    = 0u;
    nonzero = 0u;
    for (report = 1u; report <= 96u; ++report) {
        decision = mp_clock_pace_decide(&pace, (report % 4u == 0u) ? 1u : 0u);
        if (decision != 0) {
            ++nonzero;
        }
    }
    ut_check(nonzero == 0u,
             "three starving reports broken by one healthy one never reach the streak");

    ut_section("an overfull fill asks for a skip");

    pace.low_streak  = 0u;
    pace.high_streak = 0u;
    pace.cooldown    = 0u;
    decision = 0;
    for (report = 1u; report <= 4u; ++report) {
        decision = mp_clock_pace_decide(&pace, 5u);
    }
    ut_check(decision == -1,
             "a persistently overfull buffer earns a skipped substep on the fourth report");
}

static void check_arrival_stats(void)
{
    mp_clock_t clock;
    uint32_t   mean = 0u;
    uint32_t   jitter = 0u;
    uint32_t   index;
    uint32_t   at;

    ut_section("the arrival spacing estimator");

    mp_clock_init(&clock);
    ut_check(!mp_clock_arrival_stats(&clock, &mean, &jitter),
             "before any interval exists there is no estimate to report");
    (void)mp_clock_snapshot(&clock, 800u, 60000u);
    ut_check(!mp_clock_arrival_stats(&clock, &mean, &jitter),
             "one arrival is still no interval");

    at = 60000u;
    for (index = 1u; index <= 20u; ++index) {
        at += 31u;
        (void)mp_clock_snapshot(&clock, 800u + index, at);
    }
    ut_check(mp_clock_arrival_stats(&clock, &mean, &jitter)
             && mean == 31000u && jitter == 0u,
             "a perfectly even stream converges to its spacing with zero jitter");

    for (index = 21u; index <= 60u; ++index) {
        at += (index % 2u == 0u) ? 11u : 51u;
        (void)mp_clock_snapshot(&clock, 800u + index, at);
    }
    ut_check(mp_clock_arrival_stats(&clock, &mean, &jitter)
             && mean > 26000u && mean < 36000u,
             "an alternating stream keeps its mean near the true spacing");
    ut_check(jitter > 15000u,
             "while the jitter rises to the size of the swings, which is what a caller would "
             "read before choosing three intervals over two");

    ut_section("one stall is clamped instead of poisoning the estimator");

    at += 5000u;
    (void)mp_clock_snapshot(&clock, 900u, at);
    ut_check(mp_clock_arrival_stats(&clock, &mean, &jitter) && mean < 200000u,
             "a five second outage enters as at most one second, bounding the damage");
    for (index = 1u; index <= 30u; ++index) {
        at += 31u;
        (void)mp_clock_snapshot(&clock, 900u + index, at);
    }
    ut_check(mp_clock_arrival_stats(&clock, &mean, &jitter) && mean < 60000u,
             "and thirty ordinary arrivals later the mean is back near the truth");
}

int main(void)
{
    check_conversions();
    check_host_counting();
    check_refusal_before_snapshots();
    check_estimate();
    check_interpolation_and_alpha_bounds();
    check_buffer_setter();
    check_snapshot_loss();
    check_reordering();
    check_wraparound();
    check_long_run_drift();
    check_underrun_refusal();
    check_pace_hysteresis();
    check_arrival_stats();

    return ut_summary("multiplayer clock");
}
