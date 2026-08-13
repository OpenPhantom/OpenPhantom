/* mouse_rate.c: the arithmetic of the reconstruction. No engine and no configuration in it, so the
 * test drives it directly with a noise model.
 *
 * The reasoning is in the header. What belongs at the site is the ordering and the three bounds,
 * because none of them is visible in the expressions themselves:
 *
 *   - a frame that carried no report teaches nothing and must not be fed to the filter as a zero.
 *     The interval it covers has not been reported on yet; the next report will cover it. Feeding
 *     zeroes there is what makes a slow device look like a stopping hand, and it is the mistake
 *     this file exists to avoid. The caller has to tell the difference, which is why observe takes
 *     a packet count rather than working it out from the counts;
 *   - a take may never deliver what has not arrived and may never run against the bank's own
 *     direction. Two consumers share this state at different cadences, and that rule is the whole
 *     of what keeps them from each taking a full interval's worth of the same motion;
 *   - and a take must nevertheless always deliver something while the bank is not empty, or the
 *     last of a movement would sit there until the hand moved again. That is the exponential
 *     share below, and it is the floor rather than the shape.
 */
#include "mouse_rate.h"

#include <math.h>
#include <stdbool.h>

/* The filter's length, in report intervals rather than in milliseconds, which is the whole point:
 * six intervals is 58 ms at 104 reports per second and 6 ms at 1000, so a fast device pays almost
 * nothing and a slow one is smoothed for as long as it has to be. */
#define SMOOTH_REPORTS 6.0f

/* How long the report interval is averaged over. Long compared with a frame, so that the whole
 * number of packets in it is large and its rounding is negligible; short enough that a device
 * switched from 125 to 1000 Hz mid-session is resized within a quarter of a second. */
#define REPORT_WINDOW_SECONDS 0.25f

/* Or this many packets, whichever comes first. The error in an interval measured over a window is
 * one packet in however many it holds, so two dozen is four per cent and a fast device reaches it
 * in a few frames. The time above is the backstop for a slow one. */
#define REPORT_WINDOW_PACKETS 24u

/* A report interval below this is not a device, it is a burst of queued messages counted as if
 * they had arrived apart. Two microseconds is five hundred thousand reports a second. */
#define MIN_REPORT_SECONDS 0.000002f

/* How much of each new window's answer is taken. A quarter averages the windows' own boundary
 * rounding away over about a second while still following a device that changes its rate. */
#define WINDOW_BLEND 0.25f

/* Below this there is nothing worth filtering and the arithmetic would only cost precision. */
#define TIME_CONSTANT_FLOOR 0.002f

/* How long without a report before the hand is taken to have stopped. Three intervals is past any
 * ordinary gap in a stream and well short of anything a player would notice. */
#define STOP_AFTER_REPORTS 3.0f

/* A report interval this long is not a device, it is a stall, and letting it into the estimate
 * would stretch the filter to a quarter of a second on one bad frame. */
#define MAX_REPORT_SECONDS 0.100f

static float clamp_float(float value, float minimum, float maximum)
{
    if (!(value >= minimum)) {          /* also catches NaN */
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

static float magnitude_of(float value)
{
    return (value < 0.0f) ? -value : value;
}

/* The share of a gap that one interval of length `span` closes, for a time constant of `tau`. */
static float weight(float span, float tau)
{
    if (!(tau > 0.0f) || !(span > 0.0f)) {
        return 1.0f;
    }
    return clamp_float(1.0f - (float)exp(-(double)span / (double)tau), 0.0f, 1.0f);
}

void mouse_rate_reset(mouse_rate_t *rate)
{
    rate->bank              = 0.0f;
    rate->counts_per_second = 0.0f;
    rate->report_seconds    = 0.0f;
    rate->idle_seconds      = 0.0f;
    rate->primed            = false;
    rate->window_seconds    = 0.0f;
    rate->window_packets    = 0u;
    rate->window_ready      = false;
}

float mouse_rate_banked(const mouse_rate_t *rate)
{
    return rate->bank;
}

float mouse_rate_time_constant(const mouse_rate_t *rate, float max_time_constant_seconds)
{
    float wanted;

    if (!rate->primed) {
        return 0.0f;
    }
    wanted = SMOOTH_REPORTS * rate->report_seconds;

    /* There is no floor at one consumed interval, and there used to be. The argument for it was
     * that sampling a fast-moving estimate slowly is aliasing, and that the reconstruction would
     * then be worse than handing the counts over untouched. The second half is measurably false: at
     * 500 reports per second the unfloored filter delivers 11.0 per cent of spread against the
     * plain path's 16.0 at 92 frames per second, and 5.8 against 9.6 at 144. It is still better,
     * just by a smaller margin.
     *
     * What the floor did buy was that margin, and what it cost was a time constant of one whole
     * simulation step for a device that asked for a third of it, which is delay the player feels
     * directly. It also put the two delivery rules within a hair of each other, since at tau equal
     * to the step the exponential share and the rate term are the same size, so the delivery kept
     * flipping between the smooth one and the frame-grouped one. Reported from a real machine as
     * lag and stutter at the same time, which is exactly what that pair produces. */
    return clamp_float(wanted, TIME_CONSTANT_FLOOR, max_time_constant_seconds);
}

void mouse_rate_observe(mouse_rate_t *rate, float counts, unsigned packets, float span_seconds,
                        float frame_seconds, float max_time_constant_seconds)
{
    float measured;
    float tau;

    if (!(counts == counts)) {          /* a count that is not a number would poison everything */
        return;
    }

    /* Banked first and unconditionally, because arriving and being measurable are different
     * things. The first report of a session has no predecessor to be timed against, so it teaches
     * the filter nothing, and its counts are still the player's. */
    rate->bank += counts;
    if (!(rate->bank == rate->bank)) {
        rate->bank = 0.0f;
    }

    /* The window's clock runs on every call, including the ones that carried nothing, and that is
     * not tidiness. Whenever the device reports more slowly than the game draws, most frames carry
     * no packet at all: at 62 reports a second against 144 frames, fewer than half of them do.
     * Counting only the frames that carried one would measure the interval between reports as the
     * length of the frames they happened to land in, which is less than half of it, and the rate is
     * then wrong by that factor. Two of this module's unit tests catch it, one directly and one as
     * a loss of the improvement the whole file exists to prove. */
    rate->window_seconds += (frame_seconds > 0.0f) ? frame_seconds : span_seconds;
    rate->window_packets += packets;

    if (packets == 0 || !(span_seconds > 0.0f)) {
        /* Nothing arrived, so nothing is known about this interval. Age the idle timer and, once it
         * is clear the hand really has stopped rather than merely fallen between two reports, walk
         * the estimate down instead of dropping it, so the last of the motion still comes out.
         *
         * This branch is the one that had to be added rather than tightened. The caller that drains
         * the reader used to choose between the two observe calls on "raw input is live and this
         * frame carried a packet", and the else arm, written for the engine's own reader, therefore
         * also caught the case where raw input was live and this frame simply carried nothing. It
         * then passed one packet over the frame's own duration with a count of zero. At 125 reports
         * a second against 144 frames, roughly seven frames in eight carry no packet, so the rate
         * estimate was dragged toward zero between every pair of reports and snapped back at each
         * one, and the report interval learned the frame interval instead, which sizes a 1000 Hz
         * mouse's filter as if it were a 100 Hz one.
         *
         * The stop detection below is what tells the two cases apart honestly: three missed report
         * intervals is past any ordinary gap in a stream, and only then does the estimate begin to
         * walk down. */
        rate->idle_seconds += (frame_seconds > 0.0f) ? frame_seconds : 0.0f;
        if (rate->primed && rate->idle_seconds > STOP_AFTER_REPORTS * rate->report_seconds) {
            tau = mouse_rate_time_constant(rate, max_time_constant_seconds);
            rate->counts_per_second -= rate->counts_per_second * weight(frame_seconds, tau);
        }
        return;
    }

    rate->idle_seconds = 0.0f;

    /* The report interval. Until a full window has closed there is nothing better than this call's
     * own arithmetic, and being roughly right at once beats being exactly right in a quarter of a
     * second; a device that has just been picked up must be sized before then. */
    measured = clamp_float(span_seconds / (float)packets, MIN_REPORT_SECONDS, MAX_REPORT_SECONDS);
    if (!rate->primed) {
        rate->report_seconds = measured;
        rate->primed         = true;
    } else if (!rate->window_ready) {
        if (measured < rate->report_seconds) {
            /* Downward at once, upward slowly. A report interval cannot be shorter than the reports
             * actually seen, so a shorter measurement is simply true and there is nothing to
             * average; primed too long, the estimate would otherwise take most of a second to come
             * down and the filter would be sized as if the device were ten times slower. */
            rate->report_seconds = measured;
        } else {
            rate->report_seconds += (measured - rate->report_seconds)
                                  * weight(span_seconds, SMOOTH_REPORTS * rate->report_seconds);
        }
    }

    /* And then the window owns it. Within a single frame the packet count is a small whole number,
     * five or six, and its rounding is the whole of the error being removed here; an interval
     * derived from it would carry that error into the very estimate meant to be free of it. Over a
     * window the count is in the dozens and the rounding is a few per cent, and successive windows
     * are blended so that what is left of it averages away rather than stepping. */
    if (rate->window_packets >= REPORT_WINDOW_PACKETS ||
        (rate->window_seconds >= REPORT_WINDOW_SECONDS && rate->window_packets > 0u)) {
        measured = clamp_float(rate->window_seconds / (float)rate->window_packets,
                               MIN_REPORT_SECONDS, MAX_REPORT_SECONDS);
        if (rate->window_ready) {
            rate->report_seconds += (measured - rate->report_seconds) * WINDOW_BLEND;
        } else {
            rate->report_seconds = measured;
            rate->window_ready   = true;
        }
        rate->window_seconds = 0.0f;
        rate->window_packets = 0u;
    }

    /* The boundary correction, and it is this one line. The divisor is what the reports represent,
     * their number times the device's own interval, and not how long they took to arrive. Dividing
     * by the measured span would put the sampling boundary straight back in, because six reports
     * instead of five carry a sixth more counts across a span that did not grow by a sixth.
     *
     * The reason is the arithmetic and nothing else. This comment used to justify it by claiming
     * that raw input arrives in one burst when the game pumps its queue once a frame, and that is
     * false: the packets are received on a thread of their own, whose window has its own blocking
     * pump, so they are drained as they arrive and the game's frame pacing never touches them. The
     * conclusion survived the correction, but the false reason is worth naming, because "the
     * delivery is bunched" is exactly the belief that makes a delivery cap look plausible and it
     * sent one investigation after a defect that was not there. */
    tau = mouse_rate_time_constant(rate, max_time_constant_seconds);
    measured = counts / ((float)packets * rate->report_seconds);
    rate->counts_per_second += (measured - rate->counts_per_second)
                             * weight((frame_seconds > 0.0f) ? frame_seconds : span_seconds, tau);

    if (!(rate->counts_per_second == rate->counts_per_second)) {
        rate->counts_per_second = 0.0f;
    }
}

float mouse_rate_take(mouse_rate_t *rate, float dt, float max_time_constant_seconds)
{
    float bank;
    float tau;
    float wanted;
    float share;

    if (!(dt > 0.0f)) {
        return 0.0f;
    }
    bank = rate->bank;
    if (!(bank == bank)) {
        rate->bank = 0.0f;
        return 0.0f;
    }
    if (bank == 0.0f) {
        return 0.0f;
    }

    tau    = mouse_rate_time_constant(rate, max_time_constant_seconds);
    wanted = rate->counts_per_second * dt;

    /* Clamped into the bank, in both directions. Delivering past the bank would be inventing
     * movement, and delivering against it would be reversing movement the player made; the second
     * is the one that shipped.
     *
     * What shipped was a lead allowance: a take was permitted to deliver up to one report interval
     * more than had arrived, on the argument that the interval between two reports is genuinely
     * unknown. The argument is sound and the implementation was not, because the allowance was
     * granted per call while two consumers drain this state in one frame, one of them once per
     * substep and the other once per rendered frame. Between them they asked for about two seconds
     * of delivery per second of arrivals, both took the allowance, the ledger went negative, and
     * the catch-up term then paid the negative back as motion in the opposite direction.
     *
     * Modelled with the constants that shipped, a hand at 300 degrees per second for half a second,
     * 90 frames per second, a 125 Hz device:
     *
     *     configuration              peak     settles at   springback
     *     both consumers             216.2      148.8      67.4 degrees
     *     the substep consumer only  151.6      148.8       2.8
     *     both, lead forced to zero  148.8      148.8       0.0
     *
     * The report rate decides the share: 30 Hz gives 79.7 degrees of springback, 125 Hz gives 67.4,
     * 200 Hz gives 46.2, 500 Hz gives 3.0 and 1000 Hz gives 1.8. The frame rate is almost
     * irrelevant, 60 through 240 all give 66 to 68. The rule of thumb is that the camera gave back
     * a quarter of a second of whatever the hand was doing when it stopped.
     *
     * The clamp costs the lead, which is at most one report interval of latency, and it buys a
     * delivery that cannot reverse, which is the one a player actually notices. */
    if (bank > 0.0f) {
        wanted = clamp_float(wanted, 0.0f, bank);
    } else {
        wanted = -clamp_float(-wanted, 0.0f, -bank);
    }

    /* The floor. Whatever the rate estimate says, an exponential share of the bank leaves on every
     * take, so a bank the estimate has stopped asking for still empties over one time constant
     * rather than standing there. This is also what pays out the tail of a movement after the hand
     * has come to rest.
     *
     * It is what bounds the bank as well, which is the part that is not obvious from the line.
     * Delivery is the larger of rate times dt and bank times w in magnitude, with w one minus
     * exp(-dt/tau), so the bank only shrinks while bank times w exceeds rate times dt and can
     * therefore never sit above rate times dt over w, which is about one time constant of the
     * hand's own motion. The buffer that builds during a ramp-up is roughly rate times tau and
     * stays there, so the latency this adds is one time constant and is bounded by the player's
     * own ceiling on tau. */
    share = bank * weight(dt, tau);
    if (magnitude_of(share) > magnitude_of(wanted)) {
        wanted = share;
    }

    rate->bank = bank - wanted;
    return wanted;
}
