/* mouse_look.c: the mouse axis, collected per rendered frame and drained by whoever consumes.
 *
 * ==============================================================================================
 * The order the engine runs things in, and the reason this file exists at all
 *
 *     sys_runSubsteps(0)          <- every substep of this frame
 *     module_send(0, 13)          <- the poll: one DirectInput sample per frame
 *     render_frameEnd             <- where the sample is collected
 *
 * The poll is stdControl_pollMouse at 0x0048E6C7. It calls GetDeviceState at 0x0048E6FD and stores
 * the three axes at 0x0048E714, 0x0048E720 and 0x0048E72C, so exactly one lX reaches the game per
 * rendered frame and that cell is overwritten by the next poll whether anybody read it or not.
 *
 * A substep can therefore only ever read the PREVIOUS frame's sample. Above 32 frames per second
 * most frames run no substep at all, so most of the hand's movement was polled and overwritten
 * unread. Collecting every frame and banking it fixes that outright rather than filtering its
 * output, and it costs no latency, because frame k's substeps already consumed frame k-1's input
 * before any of this.
 *
 * ==============================================================================================
 * The invariants a change here must not break
 *
 * One read per frame on the raw path. The raw reader CONSUMES what it answers and the engine's own
 * reader does not. The per-frame collector guarantees exactly one read per rendered frame,
 * so the raw source is only ever enabled while the collector is live. On the degraded path the
 * axis is read once per substep, and a consuming reader would hand each substep a different
 * fraction of the same movement.
 *
 * Both readers are consulted every frame. A raw input registration can succeed and then deliver
 * nothing, which would leave the player with a mouse that works in the menus and does not turn the
 * view. Asking the engine's reader as well costs nothing, since it is not destructive, and the two
 * disagreeing in one direction only is the signature of that failure alone.
 *
 * A step may never reach 180 degrees, because at that angle a turn is ambiguous and beyond it the
 * eye reads the short way round. One substep cannot get there on the banked path, since the step is
 * clamped below it and a take removes what it delivers. The route that exists is the REPEAT: a
 * frame long enough to owe two substeps runs them back to back with no poll in between, and on the
 * degraded path both read the same sample, so that path carries a much smaller cap.
 *
 * Nothing a limit holds back may be deleted. What is not delivered this drain stays banked and goes
 * out on the next one, so the total is conserved exactly. A guard that deletes evidence hides the
 * fault it guards against; two earlier versions of this file did, and both are described below.
 *
 * And the cut runs before the bank, not after it. Holding motion back rather than deleting
 * it is right for input and wrong for a FAULT: once clipped motion started being paid out instead
 * of dropped, a single impossible sample became a guaranteed full turn. bolt_implausible_sample()
 * cuts one frame's sample to the fastest a hand could have been BEFORE it is banked, so the bank
 * only ever holds motion a person really made. It cuts rather than drops, because at the top of the
 * sensitivity band a genuine flick can reach that rate.
 *
 * A drain may not invent and may not reverse. Two consumers share one bank at different cadences,
 * phase 2 once per substep and free look once per rendered frame, and between them they ask for
 * about two seconds of delivery per second of arrivals. The only thing that makes that safe is that
 * mouse_rate_take removes what it hands over and can reach neither past the bank nor against it.
 * An earlier version let a drain run one report interval ahead, granted per call rather than per
 * unit of time, and the ledger then paid the overdraft back as motion in the opposite direction.
 *
 * ==============================================================================================
 * What the reader actually answers, and why that turned out to be the question
 *
 * The engine's reader answers a SUM: everything accumulated since the previous call, with the
 * timing of the counts inside it destroyed. So a drain cannot tell three device reports from four,
 * and the simulation consumes at a fixed 32 Hz, which means a device reporting at 125 Hz delivers
 * three or four whole reports into each consumed step whatever the render rate does. A held
 * direction key has no report rate at all: its axis is a level.
 *
 * raw_mouse.c keeps that structure instead of summing it away and holds its own reasoning.
 *
 * ==============================================================================================
 * Two earlier answers to the same complaint, and both were wrong
 *
 * The complaint never changed: mouse movement shimmers, the direction keys do not. Both of the
 * answers below are what a reader reaches for first, so they are written down here rather than
 * left as deleted code.
 *
 * The first was the limiter, a speed limit measured against a noisy clock. It computed
 * its allowance as the rate times the drained interval's own duration, and it DISCARDED what it
 * clipped. Both halves are wrong and they compound. The interval is one rendered frame, and frame
 * times on real hardware swing by a factor of 4.6: measured on the reporting machine, one window of
 * 60 frames ran a minimum of 5.5 ms, an average of 11.3 and a maximum of 25.1. The counts arriving
 * in a frame do not scale with that frame's length, because a device reports on its own clock, so
 * identical hand movement was clipped on a short frame and passed on a long one and the difference
 * was deleted. The threshold was wrong as well: 720 deg/s had been justified against the engine's
 * own 120 deg/s ceiling, which governs a KEYBOARD turn, while a 180 degree flick of the wrist is
 * 1200 to 1800 deg/s. It sat in the middle of ordinary aiming. That was a real defect and it is
 * repaired, but it was not the reported one.
 *
 * The second was delivery cadence. The simulation step is pinned at 1/32 s while the render runs
 * far faster, so a drain carries however many rendered frames fell between two substeps, two or
 * three at 90 frames per second, while a key delivers a rate times the fixed step. All of that is
 * byte-true, a pacer was built for it, and it shipped. It was removed on four counts:
 *
 *   - the field test meant to confirm it, a 64 fps cap making the grouping exactly two frames per
 *     substep, came back unchanged. That run's own log reads dt in ms minimum 15.625, average
 *     15.627, maximum 15.775, jitter 1.0x, 30 substeps at 32.0 Hz, 2.00 frames per substep, so the
 *     input cadence was as regular as it can be made;
 *   - the pacer's closed form is step_k = e_k - r_prev * s_prev + r_k * s_k, and at a constant span
 *     the correction is constant, so at exactly two frames per substep it is a bit-exact identity.
 *     Maximum measured difference from the unpaced path 0.000000e+00 over 1500 substeps, so that
 *     same field run had also exercised the fix and could not have shown it working either way;
 *   - against a noisy reader it does not attenuate, it mildly amplifies: gain 1.012, 1.041, 1.071
 *     and 1.085 at 2, 4, 6 and 8 Hz, where the unpaced path was 1.002, 0.973 and 0.929;
 *   - and it bypassed the release path entirely, so it silently disabled the smoothing setting.
 *
 * The mechanism it removed is real and will come back below 32 frames per second, where a frame
 * that owes two substeps hands the second one an empty bank. If it is ever rebuilt, its test must
 * be driven by a NOISE model and must assert that the paced spread is no worse than the unpaced one
 * at 64, 91.5 and 144 frames per second. The shipped version fails that at 64.
 *
 * The test that let it ship reported a step spread of
 * 0.00 per cent and it was not lying: the model fed it a perfectly steady hand, so the only
 * variation present was the sampling, which is the one thing the pacer cancels in closed form. A
 * model that feeds a filter a clean signal measures what the filter does to the sampling and never
 * what it does to the signal, and it will pass a filter that makes the signal worse.
 *
 * ==============================================================================================
 * A claim about non-exclusive DirectInput that is not established
 *
 * An intermediate version of this file asserted that because the device is opened
 * DISCL_NONEXCLUSIVE, the number read is pointer travel carrying the acceleration curve, the
 * control panel speed multiplier, a quantisation to whole screen pixels and a stop at the edge of
 * the desktop. The `push 6` at 0x0048DAEA is byte-proven; those four consequences are not. The
 * documented behaviour is that control panel speed and acceleration do not affect DirectInput data,
 * and the image agrees: 0x00860BB8 has exactly one writer, the store from GetDeviceState at
 * 0x0048E714, while the pointer warp's product goes to 0x004B6C98 and 0x004B6C9C and is read only
 * inside the window procedure. The two paths share no storage. The claim was removed rather than
 * softened, and it is written down here so that it does not get reasoned back in.
 *
 * ==============================================================================================
 * The audit that removed four accumulators, and the one lesson worth keeping from it
 *
 * It found two defects in the delivery itself, and neither is visible in a reading of the code that
 * produced it. A frame carrying no device report was being handed to the filter as a zero count
 * over the frame's own duration, which taught the filter the frame interval where it wanted the
 * report interval. And two consumers shared one state, each allowed to run one report interval
 * ahead of what had arrived, so the ledger went negative and the negative came back as motion in
 * the opposite direction. The arithmetic of both is in mouse_rate.c.
 *
 * What belongs here is how the second one got in. The path it replaced had a guard against exactly
 * that case, and the guard's own comment named it. It was not argued with and it was not reasoned
 * away. It was ORPHANED: the function holding it went on compiling and went on passing its own unit
 * tests while nothing called it. A test that still passes is not evidence that the thing it tests
 * is still reachable.
 *
 * ==============================================================================================
 * SIZE NOTE: this file runs close to the nine hundred line hard limit. The excess is the invariants
 * and the two refuted answers above; each records a defect that has actually shipped, and none of
 * them is visible in the code that implements the repair. One seam has already been taken: the
 * CONFIGURATION half went into mouse_config.c with every key, every clamp and the three renamed
 * keys, and the census, the measurement that decides whether any of this worked, went into
 * mouse_census.c with its own account. Whatever is added here next has to come out of the file
 * along a seam of its own, not onto it.
 */
#include "mouse_look.h"

#include "input_gate.h"

#include "mouse_census.h"
#include "mouse_config.h"
#include "frame_clock.h"
#include "mouse_rate.h"
#include "menu_cursor.h"
#include "raw_mouse.h"

#include "common/frame_hook.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/numeric.h"

#include <windows.h>

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The backstop on the primary path. A single step is all one consumed interval can produce and
 * 90 is a factor of two below the ambiguity. It is deliberately far below the old 179:
 * 179 was already saturating inside ordinary aiming motions. */
#define MOUSE_MAX_STEP_DEGREES 90.0f

/* The backstop on the DEGRADED path, and this number is arithmetic rather than taste. Without the
 * bank the same sample is read by every substep of a frame. The substep driver clamps a frame to
 * 0.1 s before dividing it into substeps of 1/32 s, or 1/64 s when the engine's own sixty-frames
 * flag is set and nothing has pinned the rate, so at most seven substeps can run on one poll.
 * 7 x 25 = 175, which stays under the 180 at which a turn becomes ambiguous, in the worst case the
 * engine can build. At 32 Hz that is still 800 degrees per second of allowance. */
#define MOUSE_FALLBACK_MAX_STEP_DEGREES 25.0f
#define MAX_SUBSTEPS_PER_FRAME 7

/* Four substeps at 1/32 s. Long enough that ordinary play never trips it, at 144 frames per
 * second a substep runs about every 31 ms, and short enough that a pause or a cutscene parks the
 * collector almost at once.
 *
 * The guard is not a refinement. While the game is paused the engine skips the poll entirely, so
 * the axis reader keeps answering the SAME non-zero number it last saw; a bank that kept adding it
 * would integrate a frozen sample once per frame for the whole length of the pause and dump the
 * total into the first substep afterwards. Going dormant costs exactly one substep of input at the
 * resume, and it covers the cutscene case, polled and never consumed, with the same line. */
#define COLLECTOR_DORMANT_AFTER_SECONDS 0.125f

/* One frame's axis reading is 0.3 times minus lX and lX is a device count, so a million axis units
 * is over three million counts in a single frame and nothing that answers with more than that is a
 * mouse. The bound is deliberately far too loose to be the only one, and the door bolt below is
 * the other: at the shipped sensitivity a million axis units is a hundred thousand degrees, so a
 * single bad sample passed it comfortably. It never showed while the limiter deleted what it
 * clipped; once the bank started paying clipped motion out instead, the same sample became a
 * guaranteed full turn inside a tenth of a second. */
#define MAX_PLAUSIBLE_AXIS_SAMPLE 1.0e6f

#define MILLISECONDS_PER_SECOND 1000.0f


typedef struct mouse_look_state {
    input_axis_fn_t reader;

    bool  accumulating;               /* the collector is live; false = the degraded path */
    bool  raw_source;                 /* the samples come from raw input, not from the engine */
    float raw_silent_seconds;         /* how long raw input has answered nothing while the
                                       * engine's own reader was answering something */
    mouse_rate_t rate;
    raw_mouse_sample_t last_sample;   /* what the reader collected this frame */

    /* The dormancy guard. `idle_seconds` is cleared by every drain, so it measures how long nobody
     * has consumed. `dormant` exists so that going dormant resets the reconstruction ONCE rather
     * than on every frame of a menu, which previously erased the only diagnostic that could have
     * answered whether the reconstruction was running. */
    float idle_seconds;
    bool  dormant;

    mouse_census_t census;

    bool     warned_bolted_sample;
    uint32_t bolted_samples;
} mouse_look_state_t;

static mouse_look_state_t mouse_state;

/* ==============================================================================================
 * The pure half
 * ============================================================================================ */
float mouse_look_clamp_step(float degrees, float span_seconds,
                            float max_rate_deg_per_second, float hard_cap_degrees)
{
    float limit = hard_cap_degrees;

    if (span_seconds > 0.0f) {
        limit = max_rate_deg_per_second * span_seconds;
        if (limit > hard_cap_degrees) {
            limit = hard_cap_degrees;
        }
    }
    return numeric_clamp(degrees, -limit, limit);
}

/* ==============================================================================================
 * The engine half
 * ============================================================================================ */

/* ==============================================================================================
 * The door bolt: a sample no hand could have made is CUT before it is banked.
 *
 * This is the bolt the bank made necessary, and the distinction it rests on is the whole point. A
 * fast flick is legitimate input that happens to exceed what one drain may deliver, so it is
 * delayed and delivered whole. A fault is not input at all, and delaying it only means the view
 * turns later instead of now. The old limiter could not tell them apart because it was set at
 * 720 deg/s, inside the range of a hand, so it treated every flick as a fault and deleted it.
 * At 3000 deg/s the two separate cleanly: a hand does not reach it and a broken device does.
 *
 * It is applied to ONE FRAME'S sample rather than to the bank, because that is where the fault is:
 * the bank is the sum of frames that were each plausible.
 * ============================================================================================ */
static float bolt_implausible_sample(float sample, float seconds)
{
    float ceiling_units;
    float degrees;

    /* A sample that is not a number, or one past anything a device produces, is cut whatever the
     * clock says: neither needs a clock to be judged, and the frame with no clock was the one that
     * let them through to the bank. Both comparisons fail for NaN. */
    if (!(sample >= -MAX_PLAUSIBLE_AXIS_SAMPLE && sample <= MAX_PLAUSIBLE_AXIS_SAMPLE)) {
        return 0.0f;
    }
    /* With no clock and no scale there is nothing to measure a RATE against, so a plausible
     * sample is passed through rather than guessed at. */
    if (!(seconds > 0.0f) || !(mouse_config()->degrees_per_axis_unit > 0.0f)) {
        return sample;
    }

    ceiling_units = (mouse_config()->max_turn_rate * seconds)
                  / mouse_config()->degrees_per_axis_unit;
    if (sample >= -ceiling_units && sample <= ceiling_units) {
        return sample;
    }

    degrees = sample * mouse_config()->degrees_per_axis_unit;
    ++mouse_state.bolted_samples;
    if (!mouse_state.warned_bolted_sample) {
        mouse_state.warned_bolted_sample = true;
        log_warning("a single frame reported %.0f degrees of turn in %.2f ms, which is %.0f deg/s, "
                    "past the %.0f deg/s this build treats as the fastest a hand can be, so it "
                    "was cut to one frame at that speed. Banked whole it would have turned the "
                    "view roughly a full circle inside a tenth of a second. If you can produce "
                    "this by hand, raise MouseSpikeLimitDegPerSec; if it happens while you are not "
                    "touching the mouse, it is the device or its driver. MouseLog=1 reports every "
                    "one of them.",
                    (double)degrees, (double)(seconds * MILLISECONDS_PER_SECOND),
                    (double)(degrees / seconds), (double)mouse_config()->max_turn_rate);
    }
    if (mouse_config()->log_samples) {
        log_info("bolted sample %.0f degrees in %.2f ms (%.0f deg/s) cut to %.1f degrees, %u this "
                 "session", (double)degrees, (double)(seconds * MILLISECONDS_PER_SECOND),
                 (double)(degrees / seconds),
                 (double)(mouse_config()->max_turn_rate * seconds), mouse_state.bolted_samples);
    }

    /* Cut rather than dropped. A fault becomes one frame at the fastest plausible speed, which is a
     * bounded and barely visible nudge instead of a full turn. Dropping would have been wrong for
     * a different reason: at the top of the sensitivity band a genuine flick really can reach this
     * rate, and losing it whole is worse than shortening it. */
    return (sample > 0.0f) ? ceiling_units : -ceiling_units;
}

/* ==============================================================================================
 * The watchdog on the raw reader, and it guards the one failure that would be worse than jitter.
 *
 * A raw input registration can succeed and then deliver nothing: a policy, a filter driver, a
 * remote session or a virtual device that answers the registration and never sends a packet. The
 * player would then have a mouse that moves the pointer in menus and does not turn the view at all,
 * which is a broken game rather than a rough one.
 *
 * So both readers are consulted every frame. The engine's own is not destructive, so asking it
 * costs nothing, and the two disagreeing in one direction only, raw input silent while the engine
 * sees motion, is exactly the signature of that failure and of nothing else. A few seconds of it
 * and the engine's reader takes over. It cannot false-trigger the other way, because a still hand
 * produces nothing for either reader.
 * ============================================================================================ */
#define RAW_SILENCE_LIMIT_SECONDS 2.0f

/* `collector_dormant` is the caller's own dormancy decision, taken before this runs so the
 * watchdog below can be told about it. */
static float read_frame_sample(float seconds, bool collector_dormant)
{
    float engine_sample = 0.0f;
    float raw_sample;

    mouse_state.last_sample.axis = 0.0f;
    mouse_state.last_sample.packets = 0;
    mouse_state.last_sample.span_seconds = 0.0f;

    if (mouse_state.reader != NULL) {
        engine_sample = mouse_state.reader(0);
    }
    if (!mouse_state.raw_source) {
        return engine_sample;
    }

    raw_mouse_take(&mouse_state.last_sample);
    raw_sample = mouse_state.last_sample.axis;
    if (raw_sample != 0.0f) {
        mouse_state.raw_silent_seconds = 0.0f;
        return raw_sample;
    }
    if (engine_sample == 0.0f) {
        return 0.0f;                    /* nobody saw anything: the hand is still */
    }

    /* Only a frame somebody is consuming can say anything about whether raw input is delivering.
     * While the collector is dormant the engine skips its poll, so its reader keeps answering the
     * same non-zero number it last saw. That is the condition this watchdog tests for, so ageing it
     * there meant two seconds of pause, a cutscene or an alt-tab begun mid-turn demoted raw input
     * for the rest of the session and the log blamed the device. */
    if (collector_dormant) {
        return 0.0f;
    }

    mouse_state.raw_silent_seconds += (seconds > 0.0f) ? seconds : 0.0f;
    if (mouse_state.raw_silent_seconds <= RAW_SILENCE_LIMIT_SECONDS) {
        return 0.0f;
    }

    mouse_state.raw_source = false;
    log_warning("raw input registered but has answered nothing for %.0f seconds while the engine's "
                "own device kept reporting movement, so the mouse is switched back to the engine's "
                "reader for the rest of this session (%u raw packets arrived in total). The view "
                "turn is coarser that way, since that number has been through the pointer, but it "
                "works. MouseRawInput=0 makes this the startup choice and silences this.",
                (double)RAW_SILENCE_LIMIT_SECONDS, raw_mouse_packet_count());
    return engine_sample;
}

static void collect_frame_sample(void)
{
    float sample = 0.0f;
    float seconds = 0.0f;
    bool  dormant_now;

    if (frame_clock_seconds() > 0.0f) {
        seconds = frame_clock_seconds();
    }

    /* Aged before the read rather than after it, because the read has a watchdog in it that must
     * not run while nobody is consuming. Nothing else looks at this between the two. */
    mouse_state.idle_seconds += (seconds > 0.0f) ? seconds : 0.0f;
    dormant_now = mouse_state.idle_seconds > COLLECTOR_DORMANT_AFTER_SECONDS;

    /* Read first and unconditionally, whatever happens next. The raw reader CONSUMES, so skipping
     * the read would let its accumulator grow across a whole pause and arrive as one lump. */
    sample = read_frame_sample(seconds, dormant_now);
    sample = bolt_implausible_sample(sample, seconds);
    mouse_census_frame(&mouse_state.census, seconds);

    if (dormant_now) {
        /* Nobody has consumed for an eighth of a second: a pause, a cutscene, a loading screen or a
         * menu. The sample is dropped rather than banked, and the reconstruction is parked ONCE
         * rather than on every frame, because a rate measured before an interruption says nothing
         * about the hand after it and because a reset repeated every frame erases the only state a
         * diagnostic could report. */
        if (!mouse_state.dormant) {
            mouse_state.dormant = true;
            mouse_rate_reset(&mouse_state.rate);
        }
        return;
    }
    mouse_state.dormant = false;

    /* The packet count separates "the hand did not move" from "this frame happened to fall
     * between two reports", and only the reader knows which. Handing the second case in as a zero
     * count over the frame's own duration is the mistake mouse_rate.c exists to avoid: it pulls the
     * rate estimate to zero on every frame the device did not report in, which above the device's
     * own rate is most of them, and it teaches the filter the frame interval where it wants the
     * report interval. The engine's reader has no packets to count, so there the frame genuinely
     * does stand in for one report, and the boundary correction is given up. */
    if (mouse_state.raw_source) {
        mouse_rate_observe(&mouse_state.rate, sample, mouse_state.last_sample.packets,
                           mouse_state.last_sample.span_seconds, seconds,
                           mouse_config()->smooth_ceiling_seconds);
    } else {
        mouse_rate_observe(&mouse_state.rate, sample, (seconds > 0.0f) ? 1u : 0u, seconds, seconds,
                           mouse_config()->smooth_ceiling_seconds);
    }
}

void mouse_look_install(input_axis_fn_t reader)
{
    if (reader == NULL) {
        return;
    }
    mouse_state.reader = reader;
    mouse_rate_reset(&mouse_state.rate);

    if (!mouse_config()->accumulate_requested) {
        log_warning("MouseAccumulate=0, the mouse is read once per substep, as the engine does. "
                    "Motion between substeps is dropped and the effective sensitivity follows the "
                    "frame rate; the step is capped at %.0f degrees, which keeps a frame that "
                    "owes several substeps from reading one sample up to half a turn.",
                    (double)MOUSE_FALLBACK_MAX_STEP_DEGREES);
        return;
    }

    if (!frame_hook_add(collect_frame_sample)) {
        log_warning("the per-frame hook could not be installed, so the mouse is read once per "
                    "substep instead. Motion between substeps is dropped and the effective "
                    "sensitivity follows the frame rate; the step is capped at %.0f degrees so "
                    "that up to %d substeps on one sample still stay under half a turn.",
                    (double)MOUSE_FALLBACK_MAX_STEP_DEGREES, MAX_SUBSTEPS_PER_FRAME);
        return;
    }

    if (!frame_clock_install()) {
        /* Without a clock there is no span to measure the rate against AND no way to park the
         * collector during a pause, which is the one state where the axis reader keeps answering a
         * sample nobody refreshed. Both of those are load-bearing, so the bank is not used. */
        log_warning("the frame time could not be resolved, so the mouse is read once per substep "
                    "instead, a bank with no clock could neither size its own filter nor notice a "
                    "pause freezing the sample. The step is capped at %.0f degrees.",
                    (double)MOUSE_FALLBACK_MAX_STEP_DEGREES);
        return;
    }

    mouse_state.accumulating = true;

    /* AFTER the collector is live, and only then. Raw input consumes what it answers, so it is only
     * safe with exactly one reader per frame, and the collector gives it exactly that. On the
     * degraded path the sample is read once per substep and a consuming reader would hand each
     * substep a different fraction of the same movement. */
    if (mouse_config()->raw_requested) {
        mouse_state.raw_source = raw_mouse_install();
    }

    /* The menu pointer, and it has to come after the reader is up because it refuses to take the
     * engine's own motion away unless the device is actually delivering. It is a second consumer of
     * the same packets, on its own accumulator, so it takes nothing away from the view turn. */
    if (mouse_state.raw_source) {
        menu_cursor_install(mouse_config()->menu_cursor_raw);
    }

    /* The facts a log reader needs: which reader, and the numbers in force. The reasoning is in
     * the header of this file and of raw_mouse.c, where it can be read once. */
    if (mouse_state.raw_source) {
        log_info("the mouse is read as raw input, with the engine's own reader as the fallback "
                 "if it goes quiet");
    } else if (mouse_config()->raw_requested) {
        log_warning("raw input is not available, so the mouse is read through the engine's own "
                    "device; the report timing is then unknown, so the boundary correction is "
                    "off and only the smoothing is left");
    } else {
        log_info("MouseRawInput=0, the mouse is read through the engine's own DirectInput device");
    }

    log_info("%.3f degrees per count, smoothing up to %.0f ms (MouseSmoothMaxMs), spikes held "
             "back past %.0f deg/s and paid out later, never more than %.0f degrees in one step",
             (double)mouse_config()->degrees_per_count,
             (double)(mouse_config()->smooth_ceiling_seconds * MILLISECONDS_PER_SECOND),
             (double)mouse_config()->max_turn_rate, (double)MOUSE_MAX_STEP_DEGREES);
}

/* The degraded path, shared by both entry points: one live read, exactly as the engine itself does
 * it, with a cap small enough to survive being applied several times to one sample. There is no
 * bank here on purpose, the sample is not zeroed by reading it, so holding part of one back would
 * mean delivering that part twice. */
static float read_live_sample(void)
{
    float units = mouse_state.reader(0);

    return mouse_look_clamp_step(units * mouse_config()->degrees_per_axis_unit, 0.0f,
                                 mouse_config()->max_turn_rate, MOUSE_FALLBACK_MAX_STEP_DEGREES);
}

/* One consumer's share, over the interval that consumer covers. Phase 2 passes the simulation step
 * and free look passes the frame; nothing else differs between them. */
static float deliver(float dt_seconds)
{
    float degrees;

    if (mouse_state.reader == NULL) {
        return 0.0f;
    }
    if (!mouse_state.accumulating) {
        return read_live_sample();
    }

    /* Taking marks the collector as having a consumer, and it is done for a drain of no
     * length as well, because a substep with no clock is still a substep that ran. */
    mouse_state.idle_seconds = 0.0f;

    /* The units. Leaving this conversion out made the sensitivity setting stop working entirely.
     * Everything upstream of here is in the engine's own AXIS UNITS, the form both readers answer
     * in and the reconstruction therefore carries; degrees only exist once the setting has been
     * applied. Returning the reconstruction's answer directly meant one axis unit became one
     * degree, so the slider on the controls screen wrote a number nothing read and the mouse ran
     * at about four times the configured speed. */
    degrees = mouse_rate_take(&mouse_state.rate, dt_seconds, mouse_config()->smooth_ceiling_seconds)
            * mouse_config()->degrees_per_axis_unit;

    /* The last bolt, unchanged in meaning: whatever else happens, one step stays clear of the angle
     * at which a turn becomes ambiguous. */
    return numeric_clamp(degrees, -MOUSE_MAX_STEP_DEGREES, MOUSE_MAX_STEP_DEGREES);
}

/* Everything this file drains is gated on the engine running its own player phases, which is the
 * only state in which it reads input. input_gate.c has the mechanism; here it is one question and
 * one consequence: deliver nothing, and drop what was banked. mouse_rate_reset also clears the
 * measured report interval and the window behind it, so the filter re-primes when the gate
 * reopens. */
static bool drop_while_locked(void)
{
    if (input_gate_is_open()) {
        return false;
    }
    mouse_rate_reset(&mouse_state.rate);
    input_gate_note_closed();
    return true;
}

float mouse_look_take_substep_degrees(float substep_seconds)
{
    float degrees;

    if (drop_while_locked()) {
        return 0.0f;
    }
    degrees = deliver(substep_seconds);

    /* Censused here and not in deliver(), because the measurement is of the per-SUBSTEP increment.
     * That is the quantity the camera turns into a ramp; free look's per-frame drain covers a
     * different interval and mixing the two would measure the cadence rather than the hand. */
    mouse_census_step(&mouse_state.census, degrees, substep_seconds, mouse_state.raw_source,
                      mouse_rate_time_constant(&mouse_state.rate,
                                               mouse_config()->smooth_ceiling_seconds));
    return degrees;
}

float mouse_look_take_frame_degrees(void)
{
    float frame_seconds = frame_clock_seconds();

    if (drop_while_locked()) {
        return 0.0f;
    }
    return deliver(frame_seconds);
}

bool mouse_look_is_accumulating(void)
{
    return mouse_state.accumulating;
}

/* ==============================================================================================
 * The configuration entry points the rest of the DLL calls. Each one belongs to mouse_config.c and
 * is answered from here so that no caller has to know the module was split in two.
 * ============================================================================================ */
void mouse_look_load_config(void)
{
    mouse_config_load();
}

float mouse_look_degrees_per_count(void)
{
    return mouse_config()->degrees_per_count;
}

bool mouse_look_set_degrees_per_count(float degrees_per_count)
{
    return mouse_config_set_degrees_per_count(degrees_per_count);
}

/* Why the smoothing ceiling cannot have one default for both control paths, and the measurement is
 * here rather than at the setter because it is about what CONSUMES the bank.
 *
 * The boundary error in the delivery is one device report either way, and what it is measured
 * against is whatever consumes. That differs by a factor of three between the two paths, and the
 * consequence differs by much more, because the per-step path is followed by an interpolation that
 * averages the error across the frames of that step and the per-frame path is not.
 *
 * Reports landing in one consumed interval, and the share one report either way represents:
 *
 *     device     per simulation step, 31.25 ms      per rendered frame at 90 fps, 11.1 ms
 *     1000 Hz    31.3 reports,  3.2 per cent        11.1 reports,  9.0 per cent
 *      500 Hz    15.6 reports,  6.4 per cent         5.6 reports, 17.9 per cent
 *      250 Hz     7.8 reports, 12.8 per cent         2.8 reports, 35.7 per cent
 *      125 Hz     3.9 reports, 25.6 per cent         1.4 reports, 71.9 per cent
 *
 * So the same feature, shipped with the filter off, is a clear improvement on a fast device and a
 * regression on a slow one. That is not something a player can be asked to know about their own
 * hardware, and it is why the ceiling is raised by whatever arms the per-frame path rather than
 * left to the ini. The filter's LENGTH is not this number: it is six of the device's own report
 * intervals, 6 ms at 1000 Hz and 48 ms at 125, so a fast mouse pays almost nothing and a slow one
 * is given what it needs, and the ceiling only bounds how much delay a very slow device may be
 * handed. At 16 ms every device up to about 375 Hz gets the full length it asks for. An absent key
 * and a written zero are told apart by reading with an impossible default, and a written zero is a
 * decision and is obeyed. */
void mouse_look_use_frame_clock_smoothing(void)
{
    mouse_config_use_frame_clock_smoothing();
}
