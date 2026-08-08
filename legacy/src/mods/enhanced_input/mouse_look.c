/* mouse_look.c: the mouse axis, banked per rendered frame and consumed per substep.
 *
 * ==============================================================================================
 * What was actually wrong
 *
 * game_frame runs the substeps FIRST and polls the devices afterwards:
 *
 *     sys_runSubsteps(0)          <- every substep of this frame
 *     module_send(0, 13)          <- the poll: DirectInput GetDeviceState, one lX per frame
 *     ... world, render ...
 *     render_frameEnd
 *
 * So a substep can only ever read the PREVIOUS frame's sample, and the sample cell is overwritten
 * by the next poll whether anybody read it or not. The substep driver is a fixed-step accumulator,
 * so at 144 frames per second only about one frame in four and a half runs a substep at all: the
 * other frames' motion is polled, stored, and overwritten unread. Roughly four fifths of the hand
 * movement is discarded, the surviving fifth is modulated by where the frame boundaries fell, and
 * the loss forces the sensitivity up until the remaining defect starts to bite.
 *
 * The banking below fixes the arithmetic outright rather than filtering its output: the total turn
 * over any interval becomes "degrees per count times the counts the hand actually produced",
 * independent of frame rate, of frame-time jitter, and of how many substeps fell where.
 *
 * It costs no latency. Frame k's substeps already run before frame k's poll, so they already
 * consume frame k-1's input; afterwards they consume everything banked up to the end of frame
 * k-1. The same one-frame delay, with nothing thrown away.
 *
 * ==============================================================================================
 * The one arithmetic hazard, stated correctly
 *
 * A heading step of exactly 180 degrees is ambiguous, and anything past it turns the short way
 * round, i.e. backwards. There is exactly ONE route to it, and it is not the one that looks
 * obvious: a single substep cannot reverse, because the step is clamped before it is applied and
 * the clamp is below 180. The route that exists is the repeat. A frame long enough to owe two
 * substeps runs them back to back with no poll in between, both read the same sample, and two
 * capped steps add up. That is why the primary path consumes and ZEROES: at most one substep per
 * frame receives a non-zero step, so the cap alone bounds it. And it is why the degraded path
 * below carries a much smaller cap; there the repeat is unavoidable, so the cap has to survive
 * being multiplied by the worst-case substep count.
 *
 * (What the clamp does at its own limit is saturation, not reversal: a 179-degree step is a
 * 179-degree turn the correct way. It is a gross overshoot and it reads as a defect, but it does
 * not change sign.)
 *
 * ==============================================================================================
 * The second defect: a spike bolt that was really a speed limit, measured against a noisy clock
 *
 * The banking above fixed how much of the hand's movement survives. It did not fix how EVENLY what
 * survives is delivered, and the limiter underneath was making that actively worse.
 *
 * The limiter computed its allowance as `rate * the drained interval's own duration` and DISCARDED
 * whatever it clipped. Both halves of that are wrong, and they compound:
 *
 *   - The interval is one rendered frame. Frame times on real hardware are not constant: a
 *     measured window on the reporting machine ran 60 frames with dt min 5.5 ms, avg 11.3 ms and
 *     max 25.1 ms, a factor of 4.6 between the shortest and the longest frame. The allowance
 *     therefore swung between about 4 and 18 degrees at the old 720 deg/s.
 *   - The mouse counts arriving in a frame are NOT proportional to that frame's duration. A device
 *     reports on its own clock, so a short frame can carry as many counts as a long one.
 *
 *   ==> identical hand movement was clipped on a short frame and passed on a long one, and the
 *       difference was DELETED. That converts frame-time jitter directly into aim jitter, and it
 *       is worst in exactly the mode where the drain is most frequent, free look, which drains
 *       once per rendered frame rather than once per substep.
 *
 * And 720 deg/s was never a spike threshold to begin with. It was justified against the engine's
 * own 120 deg/s turn ceiling, but that ceiling governs a KEYBOARD turn. A 180-degree flick of the
 * wrist takes 100 to 150 ms, i.e. 1200 to 1800 deg/s, and a fast player goes past 3000. So the old
 * default sat in the middle of ordinary aiming and cut the top off every flick.
 *
 * The repair is three parts, and each is separately visible:
 *
 *   1. NOTHING IS DISCARDED. What the bolt holds back stays in a reservoir and is delivered on the
 *      next drain, so a clip is a delay of one frame rather than a permanent loss. Over any
 *      interval the total turn is exactly "degrees per count times the counts the hand produced".
 *   2. the bolt is a bolt. The default is 3000 deg/s, which a hand cannot reach and a broken
 *      device or a driver hiccup can; the reservoir is bounded so that such a sample cannot be
 *      played back for the rest of the session either.
 *
 *      And the bolt has to be at the door as well as at the till, which the first version of
 *      this repair got wrong and shipped. Holding motion back instead of deleting it is right for
 *      INPUT and wrong for a FAULT: the old limiter had been deleting bad samples along with good
 *      ones, so making it delay instead turned a single impossible sample into a guaranteed full
 *      turn, paid out over about a tenth of a second. The only bound on a sample was
 *      MAX_PLAUSIBLE_AXIS_SAMPLE, which at the shipped sensitivity permits a HUNDRED THOUSAND
 *      degrees in one frame. bolt_implausible_sample() now cuts a single frame's sample to the
 *      fastest a hand could have been, before it is banked, so the reservoir only ever holds
 *      motion that a person really made. It cuts rather than drops, because at the top of the
 *      sensitivity band a genuine flick can reach that rate and losing it whole is worse.
 *   3. the delivery is smoothed in real time, not in frames: a share `1, exp(-dt/tau)` of the
 *      reservoir goes out per drain. That is loss-free; it redistributes, it does not remove,
 *      it costs one time constant of latency, and it is the same at any frame rate. The default is
 *      deliberately small enough to stay direct.
 *
 * SIZE NOTE (rule 9): 717 lines, of which 376 are code. The excess is the two accounts above. Both
 * describe a defect whose symptom was "the aim feels wrong sometimes" and whose cause is arithmetic
 * that looks completely reasonable at the site, a limit measured against the frame it is applied
 * in reads as careful, and it was the bug. A reader who cannot see why the allowance may not track
 * the frame clock will reintroduce it, and that reasoning exists in no line of the code.
 */
#include "mouse_look.h"

#include "common/frame_hook.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/memory.h"

#include <windows.h>

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define INPUT_SECTION "enhanced_input"

/* control_applyMouseDefaults binds turn axis 0 to mouse source 6 with scale 0.3 and the INVERT
 * flag, and input_axis returns `scale * -lX`. So the reader's unit is 0.3 device counts, and a
 * setting expressed in degrees per COUNT, the unit a mouse is actually specified in, has to be
 * divided by it once, here, rather than folded silently into the number the user types. */
#define ENGINE_AXIS_SCALE 0.3f

/* Degrees of view turn per mouse count.
 *
 * 0.030 is 12000 counts for a full turn, i.e. 15 inches of desk at 800 DPI. The band a shooter is
 * usually tuned in, 10 to 20 inches, is 0.045 down to 0.0225.
 *
 * The floor and the ceiling are plausibility bolts, not taste: 0.001 is 450 inches per turn and
 * 1.0 is less than half an inch. */
#define DEFAULT_DEGREES_PER_COUNT 0.030f
#define MIN_DEGREES_PER_COUNT     0.001f
#define MAX_DEGREES_PER_COUNT     1.0f

/* The band the slider on the controls screen spans: 1 to 100 as the caption shows it, which is
 * 0.001 to 0.100 degrees per count in steps of exactly 0.001.
 *
 * The first version spanned 0.010 to 0.060, the band a shooter is usually TUNED in, on the
 * reasoning that a slider has to be usable along its whole length. That is a real argument and it
 * was still the wrong call: it silently decided for the player that the ends were not worth having,
 * and the two things it excluded are exactly the two a person is most likely to want. The bottom is
 * for a high-DPI mouse, where 0.010 is still fast; the top is for someone who wants to spin on a
 * mousepad the size of a beer mat. Neither is a misconfiguration.
 *
 * So the slider now spans the ini's own minimum to a hundred steps above it, the step is a round
 * number in the unit the caption displays, and the default 0.030 lands exactly on notch 29. The ini
 * still accepts up to 1.0 for anyone who wants past the right-hand end. */
#define SLIDER_MIN_DEGREES_PER_COUNT 0.001f
#define SLIDER_MAX_DEGREES_PER_COUNT 0.100f

/* The spike bolt's slope, and it is a bolt rather than a speed limit. A 180-degree flick of the
 * wrist is 1200 to 1800 degrees per second and a fast player goes past 3000, so anything in that
 * band shapes ordinary aiming instead of catching a fault. 3000 is above the hand and far below a
 * device that has come apart. Nothing it holds back is lost; it goes into the reservoir and out
 * on the next drain, so the number decides how fast a spike is paid out, not whether it is. */
#define DEFAULT_MAX_TURN_RATE 3000.0f
#define MIN_MAX_TURN_RATE      360.0f
#define MAX_MAX_TURN_RATE    20000.0f

/* How much turn the reservoir may hold, expressed as seconds of the bolt's own rate. It has to be
 * large enough that a real flick survives whole, 0.1 s at 3000 deg/s is 300 degrees, and a flick
 * is at most half of that, and small enough that a broken sample cannot keep turning the view
 * after the hand has stopped. Excess beyond this IS dropped, and that is the one place in this file
 * where input is deliberately thrown away; it is reported once. */
#define RESERVOIR_MAX_SECONDS 0.1f

/* The delivery smoothing, as a time constant in milliseconds. This does not remove motion, it
 * spreads it: a share `1, exp(-dt/tau)` of the reservoir leaves per drain and the remainder stays,
 * so the total is conserved exactly and the cost is one time constant of latency.
 *
 * 10 ms is about one frame at a hundred frames per second. It is enough to even out the per-frame
 * quantisation of a device that reports slower than the game draws, a 125 Hz mouse at 100 fps
 * delivers counts to roughly every other frame, which is visible as shimmer, and short enough
 * that the aim still lands where the hand puts it. 0 switches it off entirely. */
#define DEFAULT_SMOOTHING_MS 10.0f
#define MAX_SMOOTHING_MS     80.0f

/* The backstop on the primary path. With consume-and-zero at most one substep per frame gets a
 * non-zero step, so a single step is the whole of what one frame can produce and 90 is a factor of
 * two below the ambiguity. It is deliberately far below the old 179: 179 was already saturating
 * inside ordinary aiming motions. */
#define MOUSE_MAX_STEP_DEGREES 90.0f

/* The backstop on the DEGRADED path, and this number is arithmetic rather than taste. Without the
 * bank the same sample is read by every substep of a frame. The substep driver clamps a frame to
 * 0.1 s before dividing it into substeps of 1/32 s, or 1/64 s when the engine's own
 * sixty-frames flag is set and nothing has pinned the rate, so at most seven substeps can ever
 * run on one poll. 7 * 25 = 175, which stays under 180 in the worst case the engine can build.
 * At 32 Hz that is still 800 degrees per second of allowance, well past any real aiming motion. */
#define MOUSE_FALLBACK_MAX_STEP_DEGREES 25.0f
#define MAX_SUBSTEPS_PER_FRAME 7

/* Four substeps at 1/32 s. Long enough that ordinary play never trips it, at 144 frames per
 * second a substep runs about every 31 ms, and short enough that a pause or a cutscene parks the
 * bank almost at once. */
#define BANK_DORMANT_AFTER_SECONDS 0.125f

/* One frame's axis reading is 0.3 * -lX, and lX is a device count. A million axis units is over
 * three million counts in a single frame; nothing that answers with more than that is a mouse.
 *
 * This bound is far too loose to be the only one, and that is what the door bolt below is for. At
 * the shipped sensitivity a million axis units is a HUNDRED THOUSAND degrees, so a single bad
 * sample passed this test comfortably. It never showed while the limiter deleted what it clipped;
 * once the reservoir started paying clipped motion out instead, the same bad sample became a
 * guaranteed full turn delivered in about a tenth of a second. */
#define MAX_PLAUSIBLE_AXIS_SAMPLE 1.0e6f

/* The old key, kept only to be recognised. Its unit was the reader's, so one of it was 0.3 degrees
 * per count, a full turn in an inch and a half at 800 DPI, and that was BEFORE the banking
 * stopped discarding four fifths of the motion. Silently reusing the number would have made every
 * tuned installation about four and a half times hotter at 144 frames per second, which is why the
 * key is renamed rather than reinterpreted. */
#define LEGACY_SENSITIVITY_KEY "MouseSensitivity"

/* The bolt's key is renamed for the same reason, and the reason is not tidiness. An installation
 * carrying the old MouseMaxTurnRate=720 would go on being clipped inside ordinary aiming, and the
 * whole repair would be invisible to exactly the people who had tuned around the defect. The number
 * also no longer means what it did: what it holds back is now delivered on the next drain instead
 * of being deleted, so it sets how fast a spike is paid out rather than how much of a flick
 * survives. A changed meaning under an unchanged name is the thing this tree renames for. */
#define LEGACY_TURN_RATE_KEY "MouseMaxTurnRate"
#define SPIKE_LIMIT_KEY      "MouseSpikeLimitDegPerSec"

#define MILLISECONDS_PER_SECOND 1000.0f

typedef struct mouse_look_state {
    input_axis_fn_t reader;
    const float    *frame_delta;      /* the engine's own g_frameDelta, read at frame end */

    bool  accumulate_requested;
    bool  accumulating;               /* the bank is live; false = the degraded path */
    float degrees_per_count;
    float degrees_per_axis_unit;      /* the above, divided by ENGINE_AXIS_SCALE once */
    float max_turn_rate;
    float smoothing_seconds;

    /* Turn that has been banked but not yet handed to the engine: what the bolt held back plus
     * what the smoothing has not released. It is emptied by delivery alone, never by a reset of
     * the bank, a reset means "the samples in there are stale", which says nothing about turn
     * the player has already earned. */
    float    pending_degrees;
    bool     warned_reservoir_full;
    bool     warned_bolted_sample;
    bool     log_samples;
    uint32_t bolted_samples;

    mouse_bank_t bank;
} mouse_look_state_t;

static mouse_look_state_t mouse_state;

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

/* ==============================================================================================
 * The pure half
 * ============================================================================================ */
void mouse_bank_reset(mouse_bank_t *bank)
{
    bank->axis_sum     = 0.0f;
    bank->time_sum     = 0.0f;
    bank->idle_seconds = 0.0f;
    bank->armed        = false;
}

void mouse_bank_frame(mouse_bank_t *bank, float axis_sample, float frame_seconds,
                      float dormant_after_seconds)
{
    if (!(frame_seconds >= 0.0f)) {     /* also catches NaN */
        frame_seconds = 0.0f;
    }
    bank->idle_seconds += frame_seconds;

    if (bank->idle_seconds > dormant_after_seconds) {
        /* Nobody has consumed for too long: park the bank rather than let it fill. The next
         * consume arms it again, so the feature comes back by itself. */
        bank->axis_sum = 0.0f;
        bank->time_sum = 0.0f;
        bank->armed    = false;
        return;
    }
    if (!bank->armed) {
        return;
    }
    /* A sample that is not a number would poison the bank for good, and the rate limit downstream
     * cannot undo it, clamping only bounds magnitudes. Both comparisons fail for NaN, which is
     * exactly the wanted behaviour, and the bound itself is far past anything a device can
     * produce in one frame. */
    if (axis_sample >= -MAX_PLAUSIBLE_AXIS_SAMPLE && axis_sample <= MAX_PLAUSIBLE_AXIS_SAMPLE) {
        bank->axis_sum += axis_sample;
    }
    bank->time_sum += frame_seconds;
}

float mouse_bank_take(mouse_bank_t *bank, float *out_span_seconds)
{
    float units = bank->axis_sum;

    *out_span_seconds  = bank->time_sum;
    bank->axis_sum     = 0.0f;
    bank->time_sum     = 0.0f;
    bank->idle_seconds = 0.0f;
    bank->armed        = true;
    return units;
}

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
    return clamp_float(degrees, -limit, limit);
}

float mouse_look_smoothing_weight(float span_seconds, float time_constant_seconds)
{
    float weight;

    /* Both of these mean "no smoothing was asked for", and the negated comparisons catch NaN in
     * either argument along with them. */
    if (!(time_constant_seconds > 0.0f) || !(span_seconds > 0.0f)) {
        return 1.0f;
    }

    weight = 1.0f - (float)exp(-(double)span_seconds / (double)time_constant_seconds);
    return clamp_float(weight, 0.0f, 1.0f);
}

float mouse_look_release(float *pending, float span_seconds, float time_constant_seconds,
                         float max_rate_deg_per_second, float hard_cap_degrees)
{
    float wanted;
    float limit;

    /* A drain that collected no time is not this reservoir's turn. Two consumers share it, phase
     * 2 once per substep and free look once per rendered frame, and within one frame the second
     * of them finds the bank already emptied by the first. Releasing on that call would hand out a
     * second share against no elapsed time at all, which is both a double delivery and a hole in
     * the smoothing. The reservoir is left exactly as it was. */
    if (!(span_seconds > 0.0f)) {
        return 0.0f;
    }
    /* A reservoir that is not a number can never be spent down again, so it is dropped whole rather
     * than allowed to poison every later step. */
    if (!(*pending == *pending)) {
        *pending = 0.0f;
        return 0.0f;
    }

    wanted = *pending * mouse_look_smoothing_weight(span_seconds, time_constant_seconds);

    limit = max_rate_deg_per_second * span_seconds;
    if (limit > hard_cap_degrees) {
        limit = hard_cap_degrees;
    }
    wanted = clamp_float(wanted, -limit, limit);

    *pending -= wanted;
    return wanted;
}

/* ==============================================================================================
 * The engine half
 * ============================================================================================ */
/* The reservoir's ceiling, in degrees. Derived from the bolt rather than configured separately:
 * whatever rate the bolt runs at, the reservoir holds a tenth of a second of it. */
static float reservoir_ceiling(void)
{
    return mouse_state.max_turn_rate * RESERVOIR_MAX_SECONDS;
}

void mouse_look_load_config(void)
{
    float legacy;
    float smoothing_ms;

    mouse_state.degrees_per_count =
        ini_read_float(INPUT_SECTION, "MouseDegreesPerCount", DEFAULT_DEGREES_PER_COUNT);
    mouse_state.degrees_per_count = clamp_float(mouse_state.degrees_per_count,
                                                MIN_DEGREES_PER_COUNT, MAX_DEGREES_PER_COUNT);
    mouse_state.degrees_per_axis_unit = mouse_state.degrees_per_count / ENGINE_AXIS_SCALE;

    mouse_state.max_turn_rate =
        ini_read_float(INPUT_SECTION, SPIKE_LIMIT_KEY, DEFAULT_MAX_TURN_RATE);
    mouse_state.max_turn_rate = clamp_float(mouse_state.max_turn_rate,
                                            MIN_MAX_TURN_RATE, MAX_MAX_TURN_RATE);

    smoothing_ms = ini_read_float(INPUT_SECTION, "MouseSmoothingMs", DEFAULT_SMOOTHING_MS);
    smoothing_ms = clamp_float(smoothing_ms, 0.0f, MAX_SMOOTHING_MS);
    mouse_state.smoothing_seconds = smoothing_ms / MILLISECONDS_PER_SECOND;

    mouse_state.accumulate_requested = ini_read_bool(INPUT_SECTION, "MouseAccumulate", true);
    mouse_state.log_samples          = ini_read_bool(INPUT_SECTION, "MouseLog", false);

    /* The migration, said loudly and exactly once. A negative default is impossible for the old
     * key, so this detects PRESENCE and not merely a value. */
    legacy = ini_read_float(INPUT_SECTION, LEGACY_SENSITIVITY_KEY, -1.0f);
    if (legacy >= 0.0f) {
        log_warning("%s=%.2f is no longer read. It is replaced by MouseDegreesPerCount, which is "
                    "measured in degrees of view turn per MOUSE COUNT rather than per axis unit, "
                    "and the mouse is no longer sampled once per substep, nothing is discarded "
                    "any more, so the same feel needs a much smaller number. Your old value is "
                    "%.3f in the new key; the default is %.3f and the usual band is 0.023 to "
                    "0.045. MouseDegreesPerCount=%.3f is in force. Delete the old key to silence "
                    "this.",
                    LEGACY_SENSITIVITY_KEY, (double)legacy,
                    (double)(legacy * ENGINE_AXIS_SCALE), (double)DEFAULT_DEGREES_PER_COUNT,
                    (double)mouse_state.degrees_per_count);
    }

    legacy = ini_read_float(INPUT_SECTION, LEGACY_TURN_RATE_KEY, -1.0f);
    if (legacy >= 0.0f) {
        log_warning("%s=%.0f is no longer read; the key is now %s and the default is %.0f. The old "
                    "value was a SPEED LIMIT: it was measured against a single frame's own "
                    "duration and it deleted what it cut, so a flick faster than it was truncated "
                    "and the frame clock's own jitter was cut straight into the aim. What the new "
                    "limit holds back is delivered on the next drain instead, so it bounds a "
                    "broken device rather than your hand. %s=%.0f is in force. Delete the old key "
                    "to silence this.",
                    LEGACY_TURN_RATE_KEY, (double)legacy, SPIKE_LIMIT_KEY,
                    (double)DEFAULT_MAX_TURN_RATE, SPIKE_LIMIT_KEY,
                    (double)mouse_state.max_turn_rate);
    }
}

/* Runs once per rendered frame, right where the device sample is freshest, after the poll and
 * before the next frame's substeps. */
/* ==============================================================================================
 * THE DOOR BOLT: a sample no hand could have made is DROPPED, not banked.
 *
 * This is the bolt the reservoir made necessary, and the distinction it rests on is the whole
 * point. A fast flick is legitimate input that happens to exceed what one frame may deliver, so it
 * is delayed and delivered whole. A fault is not input at all, and delaying it only means the view
 * turns later instead of now. The old limiter could not tell them apart because it was set at
 * 720 deg/s, inside the range of a hand, so it treated every flick as a fault and deleted it.
 * At 3000 deg/s the two separate cleanly: a hand does not reach it and a broken device does.
 *
 * It is applied to ONE FRAME'S sample rather than to the reservoir, because that is where the
 * fault is: the reservoir is the sum of frames that were each plausible. And it applies to BOTH
 * control modes, since both fill this one bank, free look merely shows it first, because it
 * drains every rendered frame while the mouse-to-body path drains once per substep.
 * ============================================================================================ */
static float bolt_implausible_sample(float sample, float seconds)
{
    float ceiling_units;
    float degrees;

    /* With no clock and no scale there is nothing to measure plausibility against, so the sample
     * is passed through to the existing bound rather than guessed at. */
    if (!(seconds > 0.0f) || !(mouse_state.degrees_per_axis_unit > 0.0f)) {
        return sample;
    }

    ceiling_units = (mouse_state.max_turn_rate * seconds) / mouse_state.degrees_per_axis_unit;
    if (sample >= -ceiling_units && sample <= ceiling_units) {
        return sample;
    }

    degrees = sample * mouse_state.degrees_per_axis_unit;
    ++mouse_state.bolted_samples;
    if (!mouse_state.warned_bolted_sample) {
        mouse_state.warned_bolted_sample = true;
        log_warning("a single frame reported %.0f degrees of turn in %.2f ms, which is %.0f deg/s "
                    "- past the %.0f deg/s this build treats as the fastest a hand can be, so it "
                    "was cut to one frame at that speed. Banked whole it would have turned the "
                    "view roughly a full circle inside a tenth of a second. If you can produce "
                    "this by hand, raise MouseSpikeLimitDegPerSec; if it happens while you are not "
                    "touching the mouse, it is the device or its driver. MouseLog=1 reports every "
                    "one of them.",
                    (double)degrees, (double)(seconds * MILLISECONDS_PER_SECOND),
                    (double)(degrees / seconds), (double)mouse_state.max_turn_rate);
    }
    if (mouse_state.log_samples) {
        log_info("bolted sample %.0f degrees in %.2f ms (%.0f deg/s) cut to %.1f degrees, %u this "
                 "session", (double)degrees, (double)(seconds * MILLISECONDS_PER_SECOND),
                 (double)(degrees / seconds),
                 (double)(mouse_state.max_turn_rate * seconds), mouse_state.bolted_samples);
    }

    /* CUT, not dropped. A fault becomes one frame at the fastest plausible speed, which is a
     * bounded and barely visible nudge instead of a full turn. Dropping would have been wrong for
     * a different reason: at the top of the sensitivity band a genuine flick really can reach this
     * rate, and losing it whole is worse than shortening it. */
    return (sample > 0.0f) ? ceiling_units : -ceiling_units;
}

static void bank_frame_sample(void)
{
    float sample = 0.0f;
    float seconds = 0.0f;

    if (mouse_state.reader != NULL) {
        sample = mouse_state.reader(0);
    }
    if (mouse_state.frame_delta != NULL) {
        seconds = *mouse_state.frame_delta;
    }
    sample = bolt_implausible_sample(sample, seconds);
    mouse_bank_frame(&mouse_state.bank, sample, seconds, BANK_DORMANT_AFTER_SECONDS);

    /* The reservoir goes dormant with the bank, and this is not symmetry for its own sake. Turn
     * held back by the bolt is turn the player really earned, so it is normally right to deliver it
     * a frame late, but a bank that has gone dormant means nobody has consumed for an eighth of a
     * second, i.e. a pause, a cutscene or a loading screen. Delivering a held-back spike after that
     * would turn the view on the first frame the player gets back, from a hand movement made before
     * the interruption. The bank's own guard exists for exactly that case and the reservoir has to
     * share it. */
    if (!mouse_state.bank.armed) {
        mouse_state.pending_degrees = 0.0f;
    }
}

/* g_frameDelta, out of the operand of render_frameEnd's own first instruction. Read rather than
 * hard-coded, and it is the frame time here: the substep driver overwrites that cell with the
 * substep and restores the frame's own value before the frame ends. */
static const float *resolve_frame_delta(void)
{
    uintptr_t site = frame_hook_site();
    uint32_t  address = 0;

    if (site == 0) {
        return NULL;
    }
    if (!memory_read_u32(site + FRAME_HOOK_FRAME_DELTA_OPERAND_OFFSET, &address) ||
        !memory_is_inside_image(address, sizeof(float))) {
        log_warning("g_frameDelta would be at %08X, outside the image", (unsigned)address);
        return NULL;
    }
    return (const float *)(uintptr_t)address;
}

void mouse_look_install(input_axis_fn_t reader)
{
    if (reader == NULL) {
        return;
    }
    mouse_state.reader = reader;
    mouse_bank_reset(&mouse_state.bank);

    if (!mouse_state.accumulate_requested) {
        log_warning("MouseAccumulate=0, the mouse is read once per substep, as the engine does. "
                    "Motion between substeps is dropped and the effective sensitivity follows the "
                    "frame rate; the step is capped at %.0f degrees, which is what keeps a frame "
                    "that owes several substeps from reading one sample up to half a turn.",
                    (double)MOUSE_FALLBACK_MAX_STEP_DEGREES);
        return;
    }

    if (!frame_hook_add(bank_frame_sample)) {
        log_warning("the per-frame hook could not be installed, so the mouse is read once per "
                    "substep instead. Motion between substeps is dropped and the effective "
                    "sensitivity follows the frame rate; the step is capped at %.0f degrees so "
                    "that up to %d substeps on one sample still stay under half a turn.",
                    (double)MOUSE_FALLBACK_MAX_STEP_DEGREES, MAX_SUBSTEPS_PER_FRAME);
        return;
    }

    mouse_state.frame_delta = resolve_frame_delta();
    if (mouse_state.frame_delta == NULL) {
        /* Without a clock there is no span to measure the rate limit against AND no way to park
         * the bank during a pause, which is the one state where the axis reader keeps answering a
         * sample nobody refreshed. Both of those are load-bearing, so the bank is not used. */
        log_warning("the frame time could not be resolved, so the mouse is read once per substep "
                    "instead, a bank with no clock could neither measure its own rate limit nor "
                    "notice a pause freezing the sample. The step is capped at %.0f degrees.",
                    (double)MOUSE_FALLBACK_MAX_STEP_DEGREES);
        return;
    }

    mouse_state.accumulating = true;
    log_info("the mouse is banked once per rendered frame and consumed whole, so nothing is "
             "discarded: %.3f degrees per count, delivery smoothed over %.0f ms of REAL time "
             "(%s), spikes held back past %.0f deg/s and paid out afterwards rather than deleted, "
             "reservoir %.0f degrees, and never more than %.0f degrees in one step.",
             (double)mouse_state.degrees_per_count,
             (double)(mouse_state.smoothing_seconds * MILLISECONDS_PER_SECOND),
             (mouse_state.smoothing_seconds > 0.0f) ? "MouseSmoothingMs, 0 switches it off"
                                                    : "off",
             (double)mouse_state.max_turn_rate, (double)reservoir_ceiling(),
             (double)MOUSE_MAX_STEP_DEGREES);
}

float mouse_look_take_step_degrees(void)
{
    float units;
    float span = 0.0f;
    float ceiling;

    if (mouse_state.reader == NULL) {
        return 0.0f;
    }

    if (!mouse_state.accumulating) {
        /* The degraded path: one live read per substep, exactly as the engine itself does it, with
         * a cap small enough to survive being applied several times to one sample. There is no
         * reservoir here on purpose, the sample is not zeroed by reading it, so holding part of
         * one back would mean delivering that part twice. */
        units = mouse_state.reader(0);
        return mouse_look_clamp_step(units * mouse_state.degrees_per_axis_unit, 0.0f,
                                     mouse_state.max_turn_rate,
                                     MOUSE_FALLBACK_MAX_STEP_DEGREES);
    }

    units = mouse_bank_take(&mouse_state.bank, &span);
    mouse_state.pending_degrees += units * mouse_state.degrees_per_axis_unit;

    /* The one place input is deliberately dropped. Past this the sample was not a hand, and a
     * reservoir allowed to grow without bound would go on turning the view long after the device
     * had recovered. */
    ceiling = reservoir_ceiling();
    if (mouse_state.pending_degrees > ceiling || mouse_state.pending_degrees < -ceiling) {
        mouse_state.pending_degrees = clamp_float(mouse_state.pending_degrees, -ceiling, ceiling);
        if (!mouse_state.warned_reservoir_full) {
            mouse_state.warned_reservoir_full = true;
            log_warning("more than %.0f degrees of turn were banked at once and the excess was "
                        "dropped. That is %.0f deg/s held for a tenth of a second, far past a "
                        "hand, so the sample came from the device or its driver rather than from "
                        "you. Reported once.",
                        (double)ceiling, (double)mouse_state.max_turn_rate);
        }
    }

    return mouse_look_release(&mouse_state.pending_degrees, span, mouse_state.smoothing_seconds,
                              mouse_state.max_turn_rate, MOUSE_MAX_STEP_DEGREES);
}

bool mouse_look_is_accumulating(void)
{
    return mouse_state.accumulating;
}

float mouse_look_degrees_per_count(void)
{
    return mouse_state.degrees_per_count;
}

bool mouse_look_set_degrees_per_count(float degrees_per_count)
{
    /* Refused rather than clamped: a value that is not a number cannot have been meant, and
     * turning it into the minimum would silently make the mouse the slowest it can be. */
    if (!(degrees_per_count == degrees_per_count)) {
        log_warning("a mouse sensitivity that is not a number was refused, the setting is "
                    "unchanged at %.3f degrees per count", (double)mouse_state.degrees_per_count);
        return false;
    }

    mouse_state.degrees_per_count     = clamp_float(degrees_per_count, MIN_DEGREES_PER_COUNT,
                                                    MAX_DEGREES_PER_COUNT);
    mouse_state.degrees_per_axis_unit = mouse_state.degrees_per_count / ENGINE_AXIS_SCALE;

    /* The reservoir is left alone. It holds turn the player has already earned at the OLD
     * sensitivity, and it is at most a tenth of a second of it; rescaling it would be inventing
     * input, and dropping it would eat a flick that was in flight when the slider moved. */

    if (!ini_write_float(INPUT_SECTION, "MouseDegreesPerCount",
                         mouse_state.degrees_per_count, 3)) {
        log_warning("the mouse sensitivity is now %.3f degrees per count, but the setting could "
                    "not be written to the ini and will be back to its old value on the next "
                    "launch", (double)mouse_state.degrees_per_count);
        return false;
    }
    return true;
}

/* ==============================================================================================
 * The slider's arithmetic. Both directions are pure, so both are unit-tested: a notch that does not
 * survive the round trip is a slider that jumps under the hand the first time it is opened.
 * ============================================================================================ */
float mouse_look_degrees_per_count_from_notch(int notch)
{
    const float step = (SLIDER_MAX_DEGREES_PER_COUNT - SLIDER_MIN_DEGREES_PER_COUNT)
                     / (float)(MOUSE_SLIDER_NOTCH_COUNT - 1);

    if (notch < 0) {
        notch = 0;
    }
    if (notch > MOUSE_SLIDER_NOTCH_COUNT - 1) {
        notch = MOUSE_SLIDER_NOTCH_COUNT - 1;
    }
    return SLIDER_MIN_DEGREES_PER_COUNT + (float)notch * step;
}

int mouse_look_notch_from_degrees_per_count(float degrees_per_count)
{
    const float step = (SLIDER_MAX_DEGREES_PER_COUNT - SLIDER_MIN_DEGREES_PER_COUNT)
                     / (float)(MOUSE_SLIDER_NOTCH_COUNT - 1);
    float       notch;

    /* NaN lands on the nearest notch to the default rather than at an end of the slider, because
     * the seed runs before anything has validated what came out of the ini. */
    if (!(degrees_per_count == degrees_per_count)) {
        degrees_per_count = DEFAULT_DEGREES_PER_COUNT;
    }

    notch = (degrees_per_count - SLIDER_MIN_DEGREES_PER_COUNT) / step;
    /* Rounded to the NEAREST notch, so a setting typed into the ini between two notches is shown
     * as the closer one instead of always the lower one. */
    notch = (notch >= 0.0f) ? (notch + 0.5f) : 0.0f;

    if ((int)notch > MOUSE_SLIDER_NOTCH_COUNT - 1) {
        return MOUSE_SLIDER_NOTCH_COUNT - 1;
    }
    return (int)notch;
}
