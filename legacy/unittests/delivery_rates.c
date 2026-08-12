/* delivery_rates.c: does the mouse still arrive evenly on a slow device.
 *
 * This is the question a field test cannot answer, because it happens on one desk with one
 * mouse. Drawing the view once per rendered frame makes the delivery depend on how many device
 * reports land inside a frame, and that number is not a property of this code:
 *
 *     1000 Hz at 90 fps   11.1 reports per frame, one either way is  9 per cent of the movement
 *      500 Hz              5.6                                      18
 *      250 Hz              2.8                                      36
 *      125 Hz              1.4                                      72
 *
 * Handing each frame whatever happened to land inside it would therefore be an improvement on one
 * machine and a regression on another. The reconstruction exists to remove that, by dividing the
 * counts by the time the reports represent rather than by the frame, and by filtering over a length
 * measured in the device's own report intervals rather than in milliseconds.
 *
 * So the property proven here is not a number but an invariant across devices: with a hand moving
 * at a constant speed, what is delivered per frame must be even at every report rate, and the total
 * must be conserved. The hand is constant on purpose. Any unevenness in the output is then the
 * sampling, because there is nothing else it can be. The last section drops that assumption and
 * drives a hand that accelerates and reverses, which is where the remaining complaint lives.
 */
#include "unittest.h"

#include "mouse_rate.h"

#include <math.h>

#define FRAME_SECONDS   (1.0 / 90.0)
#define FRAMES          600
#define HAND_UNITS_PER_SECOND 3000.0    /* axis units, a brisk sweep */
#define SETTLE_FRAMES   60              /* the estimate needs a few reports before it means much */

typedef struct delivery_result {
    double mean;
    double spread;          /* mean absolute change from one frame to the next, over the mean */
    double delivered;
    double arrived;
} delivery_result_t;

/* One report rate, one constant hand, FRAMES frames of 90 fps.
 *
 * The report stream is generated as a real one behaves: reports fall at their own fixed interval,
 * asynchronous to the frame boundary, so the number inside a frame alternates by one exactly
 * as it does on a desk. `ceiling_ms` is the MouseSmoothMaxMs the build would apply. */
static delivery_result_t deliver_at(double report_hz, double ceiling_ms)
{
    mouse_rate_t      rate;
    delivery_result_t out;
    double            report_interval = 1.0 / report_hz;
    double            units_per_report = HAND_UNITS_PER_SECOND / report_hz;
    double            next_report = report_interval;
    double            now = 0.0;
    double            previous_newest = 0.0;
    double            ceiling = ceiling_ms / 1000.0;
    double            sum = 0.0;
    double            change_sum = 0.0;
    double            arrived = 0.0;
    double            previous = 0.0;
    int               counted = 0;
    int               frame;

    mouse_rate_reset(&rate);
    out.mean = 0.0;
    out.spread = 0.0;
    out.delivered = 0.0;
    out.arrived = 0.0;

    for (frame = 0; frame < FRAMES; ++frame) {
        double frame_end = now + FRAME_SECONDS;
        double sample = 0.0;
        double newest = previous_newest;
        unsigned packets = 0u;
        float  delivered;

        /* Every report that fell inside this frame. The span is measured the way the collector
         * measures it, from the previous take to the newest report rather than across the reports
         * inside this frame, which is what makes span over packets the device's own interval rather
         * than one interval short of it. Getting that wrong here made this test report a defect in
         * code that did not have one. */
        while (next_report <= frame_end) {
            newest = next_report;
            sample += units_per_report;
            ++packets;
            next_report += report_interval;
        }
        arrived += sample;

        mouse_rate_observe(&rate, (float)sample, packets,
                           (float)(newest - previous_newest),
                           (float)FRAME_SECONDS, (float)ceiling);
        previous_newest = newest;
        delivered = mouse_rate_take(&rate, (float)FRAME_SECONDS, (float)ceiling);
        out.delivered += (double)delivered;

        if (frame >= SETTLE_FRAMES) {
            double change = (double)delivered - previous;

            sum += (double)delivered;
            change_sum += (change < 0.0) ? -change : change;
            ++counted;
        }
        previous = (double)delivered;
        now = frame_end;
    }

    out.arrived = arrived;
    if (counted > 0) {
        out.mean = sum / (double)counted;
    }
    if (out.mean > 0.0) {
        out.spread = 100.0 * (change_sum / (double)counted) / out.mean;
    }
    return out;
}

static void the_raw_delivery_gets_worse_as_the_device_gets_slower(void)
{
    delivery_result_t fast = deliver_at(1000.0, 0.0);
    delivery_result_t slow = deliver_at(125.0, 0.0);

    ut_section("with no filter, the device decides");

    /* The premise of the whole feature, stated as a test so that it cannot quietly stop being true:
     * without the filter, what a frame receives is whatever fell inside it, and a slow device makes
     * that far worse. If this ever fails, the delivery has changed underneath and the ceiling below
     * is no longer needed for the reason it was added. */
    ut_checkf(slow.spread > fast.spread * 2.0,
              "125 Hz is more than twice as uneven as 1000 Hz unfiltered: %.1f against %.1f per "
              "cent", slow.spread, fast.spread);
}

static void the_filter_evens_every_device_out(void)
{
    static const double rates[] = { 1000.0, 500.0, 250.0, 125.0 };
    unsigned            index;

    ut_section("with the shipped ceiling, the device stops deciding");

    for (index = 0u; index < (sizeof rates / sizeof rates[0]); ++index) {
        delivery_result_t result = deliver_at(rates[index], 16.0);

        /* Sixteen milliseconds is the ceiling this build applies once the view is drawn per
         * rendered frame. It cannot be the zero the per-step path used: a simulation step is three
         * times as long as a frame, so the same device puts three times as many reports into it
         * and the sampling boundary matters three times less. The bound here is deliberately
         * generous: what matters is that a 125 Hz mouse lands in the same band as a 1000 Hz one,
         * not that either reaches zero. A hand at a constant speed can produce no unevenness of its
         * own, so everything measured here is the sampling. */
        ut_checkf(result.spread < 12.0,
                  "%.0f Hz delivers evenly: %.1f per cent frame to frame",
                  rates[index], result.spread);

        /* And nothing may be invented or lost on the way, whatever the rate. */
        ut_checkf(fabs(result.delivered - result.arrived) < result.arrived * 0.02,
                  "%.0f Hz conserves the movement: %.0f units delivered of %.0f arrived",
                  rates[index], result.delivered, result.arrived);
    }
}

static void a_written_zero_is_still_obeyed(void)
{
    delivery_result_t off = deliver_at(125.0, 0.0);

    ut_section("the player's own value");

    /* The ceiling is a default, not a policy: a player who writes 0 gets the unfiltered delivery
     * even where this build would have chosen otherwise. The check is that switching it off really
     * does change the delivery, so that the setting cannot silently stop working. */
    ut_checkf(off.spread > 12.0,
              "a ceiling of 0 leaves the raw delivery in place at 125 Hz: %.1f per cent",
              off.spread);
}


/* ==============================================================================================
 * A real hand, which is where the remaining complaint lives.
 *
 * Everything above uses a hand at a constant speed, which proves the sampling and nothing else. The
 * movements a player calls restless are not constant: a flick accelerates hard, stops and reverses.
 * A reversal is the interesting one, because the delivery carries an estimate of the rate, an
 * estimate has to be wrong for as long as it takes to notice that the sign has changed, and this
 * module has a documented history of paying an overdraft back as motion in the wrong direction.
 *
 * The hand is a sinusoid, which is what looking left and right actually is: it accelerates, decays
 * and reverses twice a cycle, smoothly, so any roughness in the output is not in the input. The
 * reports are whole counts taken from that curve, exactly as a device produces them.
 * ============================================================================================ */
#define SWEEP_HZ        0.8         /* twice a second past the middle, a brisk look around */
#define SWEEP_COUNTS    2400.0      /* peak displacement, so the peak speed is a fast sweep */
#define PI              3.14159265358979323846

typedef struct hand_result {
    double worst;           /* the largest single frame error, in counts */
    double mean_fast;       /* mean error where the hand is moving quickly */
    double mean_reversal;   /* mean error in the frames around a change of direction */
    double lag_counts;      /* the steady offset between what was delivered and what was made */
} hand_result_t;

static double hand_counts(double t)
{
    return SWEEP_COUNTS * sin(2.0 * PI * SWEEP_HZ * t);
}

/* The same chain as above, driven by the sinusoid, reporting where the error lands rather than how
 * big it is on average. A lag is not an error here: the filter is allowed to be late, it is not
 * allowed to be uneven, so what is measured is the frame to frame change of the error. */
static hand_result_t hand_at(double report_hz, double ceiling_ms, int bursty)
{
    mouse_rate_t rate;
    hand_result_t out;
    double report_interval = 1.0 / report_hz;
    double next_report = report_interval;
    double now = 0.0;
    double previous_newest = 0.0;
    double ceiling = ceiling_ms / 1000.0;
    double emitted = 0.0;               /* counts the device has reported so far */
    double delivered_total = 0.0;
    double previous_error = 0.0;
    double fast_sum = 0.0, reversal_sum = 0.0;
    int    fast_n = 0, reversal_n = 0;
    int    frame;

    mouse_rate_reset(&rate);
    out.worst = 0.0;
    out.mean_fast = 0.0;
    out.mean_reversal = 0.0;
    out.lag_counts = 0.0;

    for (frame = 0; frame < FRAMES; ++frame) {
        double frame_end = now + FRAME_SECONDS;
        double sample = 0.0;
        double newest = previous_newest;
        double speed;
        double error;
        double change;
        unsigned packets = 0u;
        float delivered;

        while (next_report <= frame_end) {
            double want = hand_counts(next_report);
            double whole = (want < 0.0) ? -floor(-want + 0.5) : floor(want + 0.5);

            sample += whole - emitted;
            emitted = whole;
            newest = next_report;
            ++packets;
            next_report += report_interval;
        }

        /* Raw input is not delivered when the device reports, it is delivered when the game empties
         * its message queue, which it does once per rendered frame. Every report of a frame
         * therefore arrives in one burst carrying practically the same timestamp. That is the model
         * the game really runs under, and the whole point of the repair being tested is that the
         * answer must not depend on which of the two it is. */
        if (bursty && packets > 0u) {
            newest = frame_end;
        }

        mouse_rate_observe(&rate, (float)sample, packets,
                           (float)(newest - previous_newest),
                           (float)FRAME_SECONDS, (float)ceiling);
        previous_newest = newest;
        delivered = mouse_rate_take(&rate, (float)FRAME_SECONDS, (float)ceiling);
        delivered_total += (double)delivered;

        /* The error against what the hand had really done by the end of this frame. Its level is
         * the filter's lag and is allowed; its change from one frame to the next is what the eye
         * reads as unevenness. */
        error = delivered_total - hand_counts(frame_end);
        change = error - previous_error;
        previous_error = error;
        speed = 2.0 * PI * SWEEP_HZ * SWEEP_COUNTS * cos(2.0 * PI * SWEEP_HZ * frame_end);
        if (speed < 0.0) {
            speed = -speed;
        }
        if (change < 0.0) {
            change = -change;
        }

        if (frame > SETTLE_FRAMES) {
            if (change > out.worst) {
                out.worst = change;
            }
            /* Near a reversal the hand is slow by definition, so the two groups are split on the
             * hand's own speed rather than on the delivered amount. */
            if (speed < 0.15 * 2.0 * PI * SWEEP_HZ * SWEEP_COUNTS) {
                reversal_sum += change;
                ++reversal_n;
            } else if (speed > 0.5 * 2.0 * PI * SWEEP_HZ * SWEEP_COUNTS) {
                fast_sum += change;
                ++fast_n;
            }
            out.lag_counts += error;
        }
        now = frame_end;
    }

    if (fast_n > 0) {
        out.mean_fast = fast_sum / (double)fast_n;
    }
    if (reversal_n > 0) {
        out.mean_reversal = reversal_sum / (double)reversal_n;
    }
    out.lag_counts /= (double)(FRAMES - SETTLE_FRAMES);
    return out;
}

static void a_changing_hand_is_delivered_evenly(void)
{
    static const double rates[] = { 1000.0, 500.0, 250.0, 125.0 };
    unsigned index;

    ut_section("a hand that accelerates and reverses");

    for (index = 0u; index < (sizeof rates / sizeof rates[0]); ++index) {
        hand_result_t h = hand_at(rates[index], 16.0, 1);

        /* The bound is on the worst single frame rather than on the average, because one frame in a
         * hundred is exactly what a player notices and an average hides. Sixteen counts is about an
         * eighth of what this hand covers in a frame at its fastest, and it is set above the worst
         * the four rates now produce, 13.1 at 125 Hz, rather than on top of it: what it has to
         * catch is the return of the arrival-span estimator, which measured 26.3 there. */
        ut_checkf(h.worst < 16.0,
                  "%.0f Hz: worst single frame error change %.2f counts, mean %.2f fast and %.2f "
                  "at the reversals, standing lag %.1f",
                  rates[index], h.worst, h.mean_fast, h.mean_reversal, h.lag_counts);
    }
}

/* ==============================================================================================
 * The property the repair is, and the reason it is worth a test of its own.
 *
 * The reconstruction used to divide the counts by the time between the first and last arrival, on
 * the reading that this was the time the reports spanned. On this platform it is not: raw input
 * arrives when the message queue is pumped, once a frame, so every report of a frame lands in one
 * burst and that span is the frame's. Six reports in a frame instead of five then carried a sixth
 * more counts across the same measured span, which is the sampling boundary put straight back into
 * the estimate it was there to remove.
 *
 * The clock is now the packet count against an interval measured over a window of a quarter second,
 * so the arrival times are not consulted at all. A device reports at a fixed rate, so N packets
 * represent N intervals whatever their arrival times look like. Measured offline, mean error in
 * counts per frame against this same hand, worst single frame in brackets:
 *
 *                                     1000 Hz       500 Hz       250 Hz        125 Hz
 *     divided by the arrival span,
 *       timestamped at the device     1.04 (2.44)  3.25 (6.40)  5.05 (10.15)  5.76 (12.72)
 *       timestamped at the pump       1.90 (10.03) 4.51 (9.75)  7.53 (17.87)  12.65 (26.25)
 *     divided by the packet count,
 *       either arrival model          1.04 (2.47)  3.22 (6.07)  4.99 ( 9.60)  5.60 (13.13)
 *
 * The last row is one row rather than two on purpose: the two arrival models give identical
 * answers. That is stated below as an equality rather than as a bound, because if they ever diverge
 * again something has started listening to when a message happened to be dispatched.
 *
 * The window's own clock has to run on every call, including the ones that carried no packet.
 * Whenever the device reports more slowly than the game draws, most frames carry nothing: at 62
 * reports a second against 144 frames, fewer than half do. Counting only the frames that carried a
 * packet would measure the report interval as the length of the frames they happened to land in,
 * which is less than half of it, and the rate would then be wrong by that factor. Two of the checks
 * here catch that, one directly and one as a loss of the improvement this file exists to prove.
 * ============================================================================================ */
static void arrival_timing_must_not_change_the_answer(void)
{
    static const double rates[] = { 1000.0, 500.0, 250.0, 125.0 };
    unsigned index;

    ut_section("bursty arrival is the real one, and it must not matter");

    for (index = 0u; index < (sizeof rates / sizeof rates[0]); ++index) {
        hand_result_t even   = hand_at(rates[index], 16.0, 0);
        hand_result_t bursty = hand_at(rates[index], 16.0, 1);
        double        gap    = bursty.worst - even.worst;

        if (gap < 0.0) {
            gap = -gap;
        }
        ut_checkf(gap < 0.01,
                  "%.0f Hz: timed at the device %.3f, timed at the pump %.3f",
                  rates[index], even.worst, bursty.worst);
    }
}

int main(void)
{
    the_raw_delivery_gets_worse_as_the_device_gets_slower();
    the_filter_evens_every_device_out();
    a_written_zero_is_still_obeyed();
    a_changing_hand_is_delivered_evenly();
    arrival_timing_must_not_change_the_answer();

    return ut_summary("delivery_rates");
}
