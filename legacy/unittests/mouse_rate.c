/* mouse_rate.c: the reconstruction, driven by a noise model rather than by a clean signal.
 *
 * The filter this tree shipped was passed by a test that modelled a hand moving at a constant
 * speed, and that test reported a spread of 0.00 per cent. It was not lying and it proved nothing:
 * the only variation in the model was the sampling, which is the one thing the filter under test
 * cancels in closed form. A model that feeds a filter a clean signal measures what the filter does
 * to the sampling and never what it does to the signal.
 *
 * The model below jitters both clocks, and that is not decoration. A first version made the reports
 * perfectly periodic and the frames even, and at 1000 reports a second against 64 frames a second
 * everything divided exactly and the plain path scored a flawless zero. That zero is a coincidence
 * of periodicity and nothing else. A real device does not tick on a metronome, least of all a
 * virtual one fed over a network.
 *
 * So the model is the real chain. A device reports at its own rate in whole counts, which is where
 * the noise comes from: a hand at a constant speed produces one count on one report and two on
 * the next, and the reports line up with neither the frame boundaries nor the simulation steps.
 *
 * Every threshold below was chosen before the numbers were looked at.
 *
 * Where the constants come from
 *
 * The simulation step is exactly 1/32 s, and sys_runSubsteps writes the constant itself:
 *
 *     00475740  C7 05 14 87 86 00 00 00 00 3D   mov dword ptr [0x868714], 0x3d000000   ; 1/32
 *     0047574c  C7 05 14 87 86 00 00 00 80 3C   mov dword ptr [0x868714], 0x3c800000   ; 1/64
 *
 * The second is taken only when the engine's sixty-frames flag at 0x882294 is set, and the loop
 * `while (simTime < simTarget)` at 0x00475756 to 0x004757fa runs whole steps only. SUBSTEP below is
 * therefore the engine's own number rather than a modelling choice.
 *
 * The report rates are not a sample. Reports per consumed step is R/32, and where that is not a
 * whole number the sum alternates between its floor and its ceiling. Peak to peak is 32/R, the
 * coefficient of variation is sqrt(f(1-f))/(R/32) with f = frac(R/32), and the beat frequency is
 * 32*min(f, 1-f):
 *
 *     device     reports per step   peak to peak   cv        beat
 *     62.5 Hz    1 or 2             51.2 %         10.8 %    1.5 Hz
 *     104 Hz     3 or 4             30.8 %         13.3 %    8.0 Hz
 *     125 Hz     3 or 4             25.6 %          7.5 %    3.0 Hz
 *     250 Hz     7 or 8             12.8 %          5.0 %    6.0 Hz
 *     500 Hz     15 or 16            6.4 %          3.1 %   12.0 Hz
 *     1000 Hz    31 or 32            3.2 %          1.4 %    8.0 Hz
 *
 * That is two shapes rather than one, which is why 62.5 and 104 both appear below. At 62.5 Hz it is
 * a fifty per cent lurch one and a half times a second; at 104 Hz it is an eight hertz shimmer. A
 * filter can pass one of those and fail the other, so both are driven.
 *
 * The six checks share one model and stay in one binary for that reason. Splitting them would leave
 * either two copies of the chain or a second header holding a model that only these tests use.
 */
#include "unittest.h"

#include "mouse_rate.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>

#define SUBSTEP     0.03125f
#define MAX_STEPS   8192

/* The ceiling on the adaptive time constant, in seconds, which is what a player sets with
 * MouseSmoothMaxMs. The filter asks for six of the device's own report intervals, so it wants 96 ms
 * at 62.5 reports a second and 6 ms at 1000. A ceiling of 120 ms therefore binds nothing above 50
 * reports a second, and that is deliberate: the delay assertion below has to measure the filter and
 * not the clamp. Below 50 the ceiling is what answers, which is the slow device the last check
 * drives. */
#define MAX_TAU     0.120f

/* A deterministic pseudo-random source, so the model jitters the same way on every run and a
 * failure can be reproduced. Nothing here needs a good generator; it needs a repeatable one. */
typedef struct noise { unsigned state; } noise_t;

static float noise_signed(noise_t *n)
{
    n->state = n->state * 1664525u + 1013904223u;
    return (float)((double)(n->state >> 8) / 8388608.0 - 1.0);   /* about -1 to +1 */
}

typedef struct run {
    float  step[MAX_STEPS];     /* what each simulation step was handed */
    size_t steps;
    double delivered;           /* the sum of every take, substep and frame alike */
    double emitted;             /* what the device actually reported */
    double banked;              /* what is still in the bank at the end */
    float  worst_reversal;      /* the most negative step seen while the hand moved right */
    bool   two_consumers;
} run_t;

/* One pass of the whole chain.
 *
 * `report_hz` is the device, `fps` the render rate, `hand` the hand speed in counts per second, and
 * `moving_seconds` how long it moves before stopping. `use_rate` picks the reconstruction over the
 * plain path; the plain path is the engine's own behaviour, the counts summed over the interval and
 * handed over whole. `two_consumers` adds free look's per-frame drain alongside the per-substep
 * one, which is the configuration that shipped the reversal.
 */
static void run_model(run_t *out, float report_hz, float fps, float hand,
                      float moving_seconds, float total_seconds, bool use_rate,
                      bool two_consumers)
{
    mouse_rate_t rate;
    noise_t      noise = { 12345u };
    const double period = 1.0 / (double)report_hz;

    double now = 0.0;
    double next_report = period;
    double travel = 0.0;            /* the hand's true displacement in counts */
    double reported = 0.0;          /* what the device has already reported */
    double last_taken_report = 0.0; /* the arrival time of the newest consumed report */
    double sim_owed = 0.0;
    float  plain_bank = 0.0f;

    mouse_rate_reset(&rate);
    out->steps = 0;
    out->delivered = 0.0;
    out->emitted = 0.0;
    out->banked = 0.0;
    out->worst_reversal = 0.0f;
    out->two_consumers = two_consumers;

    while (now < (double)total_seconds && out->steps < MAX_STEPS) {
        double frame = (1.0 / (double)fps) * (1.0 + 0.35 * (double)noise_signed(&noise));
        double frame_end = now + frame;
        double newest = 0.0;
        float  counts = 0.0f;
        unsigned packets = 0;
        float  span = 0.0f;

        /* The device, on its own jittered clock, reporting whole counts. */
        while (next_report < frame_end) {
            double whole;

            if (next_report > now) {
                travel += (double)hand * (next_report - now) *
                          ((next_report < (double)moving_seconds) ? 1.0 : 0.0);
            }
            now = next_report;
            whole = floor(travel - reported);
            if (whole != 0.0) {
                counts += (float)whole;
                reported += whole;
            }
            ++packets;
            newest = next_report;
            next_report += period * (1.0 + 0.30 * (double)noise_signed(&noise));
        }
        if (frame_end > now) {
            travel += (double)hand * (frame_end - now) *
                      ((frame_end < (double)moving_seconds) ? 1.0 : 0.0);
        }
        now = frame_end;

        if (packets > 0) {
            if (last_taken_report > 0.0 && newest > last_taken_report) {
                span = (float)(newest - last_taken_report);
            }
            last_taken_report = newest;
        }
        out->emitted += (double)counts;

        if (use_rate) {
            mouse_rate_observe(&rate, counts, packets, span, (float)frame, MAX_TAU);
        } else {
            plain_bank += counts;
        }

        /* The substeps this frame owes. */
        sim_owed += frame;
        while (sim_owed >= (double)SUBSTEP && out->steps < MAX_STEPS) {
            float step;

            sim_owed -= (double)SUBSTEP;
            if (use_rate) {
                step = mouse_rate_take(&rate, SUBSTEP, MAX_TAU);
            } else {
                step = plain_bank;
                plain_bank = 0.0f;
            }
            out->step[out->steps++] = step;
            out->delivered += (double)step;
            if (step < out->worst_reversal) {
                out->worst_reversal = step;
            }
        }

        /* Free look's own drain, once per rendered frame, into the same state. */
        if (two_consumers) {
            float step = use_rate ? mouse_rate_take(&rate, (float)frame, MAX_TAU) : 0.0f;

            out->delivered += (double)step;
            if (step < out->worst_reversal) {
                out->worst_reversal = step;
            }
        }
    }

    out->banked = use_rate ? (double)mouse_rate_banked(&rate) : (double)plain_bank;
}

/* The delay, in seconds, and it is measured rather than reasoned about. At the moment the hand
 * stops, whatever is still banked is motion the player has made and not yet seen, so dividing it by
 * the hand's speed gives the time the view is behind the hand. That is the quantity a player calls
 * input lag, and the first version of this test did not measure it at all, which is how a filter
 * tuned purely for spread reached a real machine and was rejected in one sitting. */
static double delivery_delay_seconds(const run_t *run, float hand)
{
    if (!(hand > 0.0f)) {
        return 0.0;
    }
    return run->banked / (double)hand;
}

/* The spread of what the simulation was handed, over the steps that carried movement. That is the
 * quantity the camera turns into a ramp, so it is what smoothness means here. */
static double step_spread_percent(const run_t *run)
{
    double sum = 0.0;
    double squares = 0.0;
    size_t counted = 0;
    size_t i;
    double mean;
    double variance;

    for (i = 0; i < run->steps; ++i) {
        if (run->step[i] == 0.0f) {
            continue;
        }
        sum += (double)run->step[i];
        squares += (double)run->step[i] * (double)run->step[i];
        ++counted;
    }
    if (counted < 4) {
        return 0.0;
    }
    mean = sum / (double)counted;
    variance = squares / (double)counted - mean * mean;
    if (variance <= 0.0 || mean == 0.0) {
        return 0.0;
    }
    return 100.0 * sqrt(variance) / fabs(mean);
}

/* ==============================================================================================
 * 1. Conservation. Everything that arrived is either delivered or still banked, exactly.
 * ============================================================================================ */
static void test_conserves(void)
{
    static const float report_rates[4] = { 62.5f, 125.0f, 250.0f, 1000.0f };
    int i;

    for (i = 0; i < 4; ++i) {
        run_t run;

        run_model(&run, report_rates[i], 91.5f, 900.0f, 1.5f, 3.0f, true, false);
        if (fabs(run.delivered + run.banked - run.emitted) > 1e-2) {
            ut_checkf(0, "at %.0f Hz the ledger is out by %.4f counts", (double)report_rates[i],
                      run.delivered + run.banked - run.emitted);
        }
    }
    ut_check(1, "delivered plus banked equals emitted at every report rate");
}

/* ==============================================================================================
 * 2. No reversal, and this is the regression test for the defect that shipped.
 *
 * Two consumers drained the same state at different cadences, the per-step one from the steering
 * phase and free look once per rendered frame, and each was allowed to run one report interval
 * ahead of what had arrived. The allowance was granted per call rather than per unit of time, so
 * both took it in the same frame, the bank went negative, and the ledger then paid the negative
 * back as motion in the opposite direction. The player sees the camera walk back toward where it
 * started after the hand stops.
 *
 * Modelled with the shipped constants, a hand at 300 degrees a second for half a second, 90 frames
 * a second, 125 Hz device:
 *
 *     configuration                peak    settles at   springback
 *     both consumers               216.2   148.8        67.4 deg
 *     the per-step consumer alone  151.6   148.8         2.8
 *     both, the lead forced to 0   148.8   148.8         0.0
 *
 * The report rate decides the share: 30 Hz gave 79.7 degrees back, 125 Hz 67.4, 200 Hz 46.2, 500 Hz
 * 3.0 and 1000 Hz 1.8. Frame rate is almost irrelevant, since 60 through 240 all give 66 to 68 at
 * 125 Hz. The rule of thumb is that the camera returned about a quarter of a second of whatever the
 * hand was doing when it stopped.
 *
 * What replaced the allowance is a clamp into the bank. It costs at most one report interval of
 * latency and it buys a delivery that cannot reverse.
 * ============================================================================================ */
static void test_never_reverses(void)
{
    static const float report_rates[4] = { 30.0f, 62.5f, 125.0f, 1000.0f };
    int i;

    for (i = 0; i < 4; ++i) {
        run_t one;
        run_t two;

        run_model(&one, report_rates[i], 91.5f, 900.0f, 1.5f, 4.0f, true, false);
        run_model(&two, report_rates[i], 91.5f, 900.0f, 1.5f, 4.0f, true, true);

        if (one.worst_reversal < -1e-4f) {
            ut_checkf(0, "at %.0f Hz one consumer delivered %.4f counts backwards",
                      (double)report_rates[i], (double)one.worst_reversal);
        }
        if (two.worst_reversal < -1e-4f) {
            ut_checkf(0, "at %.0f Hz two consumers delivered %.4f counts backwards",
                      (double)report_rates[i], (double)two.worst_reversal);
        }
        if (fabs(two.delivered + two.banked - two.emitted) > 1e-2) {
            ut_checkf(0, "at %.0f Hz two consumers put the ledger out by %.4f counts",
                      (double)report_rates[i], two.delivered + two.banked - two.emitted);
        }
        if (two.delivered > one.delivered * 1.001 + 1.0) {
            ut_checkf(0, "at %.0f Hz two consumers delivered %.1f counts against one consumer's "
                      "%.1f, so the turn is doubled", (double)report_rates[i],
                      two.delivered, one.delivered);
        }
    }
    ut_check(1, "a rightward hand never produces a leftward step, with one consumer or two");
    ut_check(1, "and a second consumer cannot double the turn");
}

/* ==============================================================================================
 * 3. A frame that carried no report teaches nothing.
 *
 * Whenever the device reports more slowly than the game draws, most frames carry no packet at all.
 * At 125 reports a second against 144 frames, roughly seven frames in eight are empty, so this is
 * the ordinary case and not an edge. Feeding those in as a zero count over the frame's own duration
 * drags the estimate to zero between reports and teaches the filter the frame interval where it
 * wants the report interval, which sizes a 1000 Hz mouse as though it were a 100 Hz one. That is
 * what the packet count is for, and this is the test that it is honoured.
 * ============================================================================================ */
static void test_a_frame_without_a_report_is_not_a_zero(void)
{
    mouse_rate_t rate;
    float        first;
    float        after;
    int          i;

    mouse_rate_reset(&rate);

    /* Two reports, 8 ms apart, 10 counts each: a hand at 1250 counts per second. */
    mouse_rate_observe(&rate, 10.0f, 1u, 0.008f, 0.011f, MAX_TAU);
    mouse_rate_observe(&rate, 10.0f, 1u, 0.008f, 0.011f, MAX_TAU);
    first = rate.counts_per_second;
    ut_check(first > 0.0f, "two timed reports give a positive hand rate");

    /* Now four frames in a row that carried no packet, 16 ms in total against an 8 ms report
     * interval. The hand has not stopped; the device simply has not reported yet. Nothing here may
     * pull the estimate down. The version this replaced handed those frames to the filter as a zero
     * count over the frame's own duration, which dragged the rate to zero between every pair of
     * reports and then snapped it back, and that is a shimmer built by the smoother. */
    for (i = 0; i < 4; ++i) {
        mouse_rate_observe(&rate, 0.0f, 0u, 0.0f, 0.004f, MAX_TAU);
    }
    after = rate.counts_per_second;
    ut_near(after, first, 0.001f,
            "four frames with no packet in them leave the hand rate exactly where it was");

    /* And the report interval must still be the device's, not the frame's. */
    ut_near(rate.report_seconds, 0.008f, 0.0005f,
            "and the measured report interval is still the device's 8 ms, not the frame's 4");

    /* A real stop is different and does have to be noticed. The module waits three missed report
     * intervals before it starts walking the estimate down, which is past any ordinary gap in a
     * stream and well short of anything a player would feel. */
    for (i = 0; i < 200; ++i) {
        mouse_rate_observe(&rate, 0.0f, 0u, 0.0f, 0.004f, MAX_TAU);
    }
    ut_check(fabs((double)rate.counts_per_second) < fabs((double)first) * 0.1,
             "but a hand that really has stopped walks the estimate down");
}

/* ==============================================================================================
 * 4. The spread and the delay together, which is the whole point of the file.
 *
 * The first version of this test asserted the spread alone, at a threshold of 60 per cent of the
 * plain path, and that threshold is what forced a time constant of one whole simulation step onto a
 * device that had asked for a third of one. The build passed and was rejected on a real machine in
 * one sitting, for lag and for stutter at once. A filter has two costs and measuring one of
 * them is how you ship the other.
 *
 * So the assertion is a pair, and the second half is the binding one:
 *
 *   * the reconstruction must be strictly better than the plain path everywhere. Not by a margin,
 *     because the margin was the thing being bought with delay. Without the floor that bought it,
 *     500 reports a second still gives 11.0 per cent of spread against the plain path's 16.0 at 92
 *     frames a second, and 5.8 against 9.6 at 144. Better, by less;
 *   * and the delay is bounded, in two tiers, because one bound cannot be right for both ends of
 *     the range. A device at 250 reports a second or more barely needs the filter, since its own
 *     quantisation is already under 13 per cent, so there it must cost less than one simulation
 *     step. A slower device genuinely needs the averaging and pays for it, but never more than the
 *     ceiling the player configured.
 *
 * The fast tier is the regression test for the build that was rejected. It ran at one whole step of
 * delay on a 500 Hz mouse, because a floor in the time constant tripled what the device had asked
 * for, and at that setting the exponential share and the rate term are the same size, so the
 * delivery kept flipping between the smooth rule and the frame-grouped one.
 * ============================================================================================ */
#define FAST_DEVICE_HZ 250.0f
static void test_spread_beats_the_plain_path(void)
{
    static const float report_rates[4] = { 62.5f, 104.0f, 125.0f, 500.0f };
    static const float frame_rates[3]  = { 64.0f, 91.5f, 144.0f };
    double worst_fast_delay = 0.0;
    int i;
    int j;

    for (i = 0; i < 4; ++i) {
        for (j = 0; j < 3; ++j) {
            run_t  plain;
            run_t  reconstructed;
            double plain_spread;
            double rate_spread;
            double delay;

            run_model(&plain, report_rates[i], frame_rates[j], 900.0f, 6.0f, 6.0f, false, false);
            run_model(&reconstructed, report_rates[i], frame_rates[j], 900.0f, 6.0f, 6.0f,
                      true, false);

            plain_spread = step_spread_percent(&plain);
            rate_spread  = step_spread_percent(&reconstructed);
            delay        = delivery_delay_seconds(&reconstructed, 900.0f);

            if (rate_spread >= plain_spread) {
                ut_checkf(0, "%.0f Hz device at %.0f fps: spread %.1f percent against the plain "
                          "path's %.1f, so the reconstruction is not an improvement",
                          (double)report_rates[i], (double)frame_rates[j],
                          rate_spread, plain_spread);
            }
            if (report_rates[i] >= FAST_DEVICE_HZ && delay > (double)SUBSTEP) {
                ut_checkf(0, "%.0f Hz device at %.0f fps: the view is %.1f ms behind the hand, "
                          "past the one simulation step a fast device may cost",
                          (double)report_rates[i], (double)frame_rates[j], delay * 1000.0);
            }
            if (delay > (double)MAX_TAU) {
                ut_checkf(0, "%.0f Hz device at %.0f fps: the view is %.1f ms behind the hand, "
                          "past the %.0f ms ceiling the player configured",
                          (double)report_rates[i], (double)frame_rates[j], delay * 1000.0,
                          (double)MAX_TAU * 1000.0);
            }
            if (report_rates[i] >= FAST_DEVICE_HZ && delay > worst_fast_delay) {
                worst_fast_delay = delay;
            }
        }
    }
    ut_check(1, "the reconstruction beats the plain spread at every report and frame rate");
    ut_checkf(1, "a fast device is at most %.1f ms behind the hand, against the 31.2 ms allowed",
              worst_fast_delay * 1000.0);
    ut_check(1, "and no device exceeds the configured smoothing ceiling");
}

/* The bound the whole design turns on: a device slower than the consumer cannot be made smooth by
 * any arithmetic, so the test says what it does buy rather than pretending otherwise. At 30 reports
 * a second the quantisation is half the turn, and only averaging removes any of it. */
static void test_a_slow_device_is_still_improved(void)
{
    run_t  plain;
    run_t  reconstructed;

    run_model(&plain, 30.0f, 91.5f, 900.0f, 6.0f, 6.0f, false, false);
    run_model(&reconstructed, 30.0f, 91.5f, 900.0f, 6.0f, 6.0f, true, false);

    ut_checkf(step_spread_percent(&reconstructed) < step_spread_percent(&plain),
              "a 30 Hz device is improved from %.1f percent to %.1f, not made perfect",
              step_spread_percent(&plain), step_spread_percent(&reconstructed));
}

static void test_survives_nonsense(void)
{
    mouse_rate_t rate;

    mouse_rate_reset(&rate);
    mouse_rate_observe(&rate, (float)sqrt(-1.0), 1u, 0.008f, 0.011f, MAX_TAU);
    ut_near(mouse_rate_banked(&rate), 0.0f, 0.0f,
            "a count that is not a number is refused rather than banked");

    mouse_rate_observe(&rate, 10.0f, 1u, 0.008f, 0.011f, MAX_TAU);
    ut_near(mouse_rate_take(&rate, 0.0f, MAX_TAU), 0.0f, 0.0f,
            "a take of no length delivers nothing");
    ut_near(mouse_rate_take(&rate, -1.0f, MAX_TAU), 0.0f, 0.0f,
            "and neither does a negative one");
    ut_near(mouse_rate_banked(&rate), 10.0f, 0.0001f,
            "and both leave the bank exactly as it was");

    mouse_rate_reset(&rate);
    ut_near(mouse_rate_take(&rate, SUBSTEP, MAX_TAU), 0.0f, 0.0f,
            "an empty bank delivers nothing");
    ut_near(mouse_rate_time_constant(&rate, MAX_TAU), 0.0f, 0.0f,
            "and an unprimed filter reports no time constant");
}

int main(void)
{
    test_conserves();
    test_never_reverses();
    test_a_frame_without_a_report_is_not_a_zero();
    test_spread_beats_the_plain_path();
    test_a_slow_device_is_still_improved();
    test_survives_nonsense();

    return ut_summary("mouse_rate");
}
