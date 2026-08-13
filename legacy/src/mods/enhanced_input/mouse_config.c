/* mouse_config.c: the ini half of the mouse path.
 *
 * No engine, no device, no state that outlives a setting. What is here is what the player asked
 * for, what this build does when they asked for nothing, and the three keys that were renamed
 * because their meaning changed, which is worse under an unchanged name than a missing key.
 *
 * Lifted out of mouse_look.c along the seam that file's own size note had been naming.
 */
#include "mouse_config.h"

#include "mouse_look.h"

#include "common/ini.h"
#include "common/logging.h"

#include <stdbool.h>

#define INPUT_SECTION "enhanced_input"
#define MILLISECONDS_PER_SECOND 1000.0f

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

/* The spike bolt's slope, and it is a bolt rather than a speed limit. A 180-degree flick of the
 * wrist is 1200 to 1800 degrees per second and a fast player goes past 3000, so anything in that
 * band shapes ordinary aiming instead of catching a fault. 3000 is above the hand and far below a
 * device that has come apart. Nothing it holds back is lost; it goes into the bank and out on the
 * next drain, so the number decides how fast a spike is paid out, not whether it is. */
#define DEFAULT_MAX_TURN_RATE 3000.0f
#define MIN_MAX_TURN_RATE      360.0f
#define MAX_MAX_TURN_RATE    20000.0f

/* The smoothing is off by default: one mouse count in, one count of turn out, nothing averaged and
 * nothing delayed. At zero the delivery hands over everything banked since the previous take, which
 * is accumulate on events and drain once per fixed tick, the default path of every shooter that has
 * shipped since Quake. The survey behind that sentence, since it is the reason for the default:
 *
 *   Quake 1 and 3, `m_filter`, default off. `mouse_x = (mx + old_mouse_x) * 0.5`, a two tap box
 *     whose group delay is half a sample, which against a 32 Hz consumer is 15.6 ms.
 *   Doom 3, `m_smooth`, default 1, and a box of length 1 is the identity. A box of length N delays
 *     by (N-1)/2 samples.
 *   Source, no filter in the default path. Accumulate on events, drain once per fixed tick.
 *   Unreal, `SmoothMouse`, and it is the one that matches this engine. Epic's own comment is
 *     "smooth mouse movement, because mouse sampling doesn't match up with tick time", and the load
 *     bearing line divides by SampleCount, the number of device reports the frame contained rather
 *     than the frame's duration. That is a boundary correction, and it cannot be done from a sum.
 *
 * It was on by default once and that build was rejected in one sitting for input lag, having been
 * tuned against the spread of the delivered step and nothing else.
 *
 * What the setting is for is the case the arithmetic cannot reach: a device that reports far more
 * slowly than the simulation consumes, which is a streamed or injected pointer rather than a mouse.
 * A device reporting 62.5 times a second puts one or two reports into each substep, so the
 * delivered step swings 51 per cent peak to peak and beats at about 1.5 Hz, and only averaging
 * removes that. It is then a ceiling on an adaptive value, six of the device's own report
 * intervals.
 *
 * The maximum is a plausibility bound rather than a tuning range. The latency the filter adds is
 * one time constant, so a player who asks for the whole of this key has asked for four tenths of a
 * second of it and will find out immediately. */
#define DEFAULT_SMOOTH_CEILING_MS 0.0f
#define MAX_SMOOTHING_MS          400.0f

/* The default ceiling once the view is drawn per rendered frame rather than per simulation step.
 *
 * It cannot stay at zero there. The boundary error in the delivery is one device report either way,
 * and the size of that error is decided by whatever consumes the bank. Reports landing in one
 * consumed interval, and the share that one report either way represents:
 *
 *     device     per simulation step, 31.25 ms     per rendered frame at 90 fps, 11.1 ms
 *     1000 Hz    31.3 reports,  3.2 %              11.1 reports,  9.0 %
 *      500 Hz    15.6 reports,  6.4 %               5.6 reports, 17.9 %
 *      250 Hz     7.8 reports, 12.8 %               2.8 reports, 35.7 %
 *      125 Hz     3.9 reports, 25.6 %               1.4 reports, 71.9 %
 *
 * The per-step path is also followed by an interpolation that averages what is left across that
 * step's frames, and the per-frame path is not. So the same build shipped with the filter off is a
 * clear improvement on a fast device and a regression on a slow one, which is not something a
 * player can be asked to know about their own hardware. That is why this is raised by whatever arms
 * the per-frame path rather than left to the ini.
 *
 * The filter's length is not this number. It is six of the device's own report intervals, 6 ms at
 * 1000 Hz and 48 ms at 125, so a fast mouse pays almost nothing and a slow one is given what it
 * needs. At a 16 ms ceiling every device up to about 375 Hz gets the full length it asks for, and
 * a written value always wins over all of it. */
#define FRAME_CLOCK_SMOOTH_CEILING_MS 16.0f

/* The old key, kept only to be recognised. Its unit was the reader's, so one of it was 0.3 degrees
 * per count, a full turn in an inch and a half at 800 DPI, and that was before the collector
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

static mouse_config_t config;

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

const mouse_config_t *mouse_config(void)
{
    return &config;
}

void mouse_config_load(void)
{
    float legacy;
    float smoothing_ms;

    config.degrees_per_count =
        ini_read_float(INPUT_SECTION, "MouseDegreesPerCount", DEFAULT_DEGREES_PER_COUNT);
    config.degrees_per_count = clamp_float(config.degrees_per_count,
                                           MIN_DEGREES_PER_COUNT, MAX_DEGREES_PER_COUNT);

    /* The setting is in degrees per mouse count and everything upstream of it is in the reader's
     * own axis units, so the conversion happens once, here.
     *
     * control_applyMouseDefaults at 0x0046586C registers one binding on function 0 from mouse
     * source 6, flags 0x0C (INVERT | RELATIVE) and scale 0x3E99999A, which is 0.3f, so
     * input_axis(0) answers 0.3 * -lX. The axis itself is registered by
     * stdControl_registerAxis(6, -250, +250, 0.0f) at 0x0048DB64, with centre and dead zone both
     * provably 0. ENGINE_AXIS_SCALE holds that 0.3 and lives in mouse_look.h because the raw reader
     * has to answer in the same units to be a drop-in for the engine's. */
    config.degrees_per_axis_unit = config.degrees_per_count / ENGINE_AXIS_SCALE;

    config.max_turn_rate =
        ini_read_float(INPUT_SECTION, SPIKE_LIMIT_KEY, DEFAULT_MAX_TURN_RATE);
    config.max_turn_rate = clamp_float(config.max_turn_rate,
                                       MIN_MAX_TURN_RATE, MAX_MAX_TURN_RATE);

    config.accumulate_requested = ini_read_bool(INPUT_SECTION, "MouseAccumulate", true);
    config.raw_requested        = ini_read_bool(INPUT_SECTION, "MouseRawInput", true);
    config.menu_cursor_raw      = ini_read_bool(INPUT_SECTION, "MenuCursorRawInput", true);
    config.log_samples          = ini_read_bool(INPUT_SECTION, "MouseLog", false);

    /* Read with an impossible default, so that an absent key can be told from a written zero. A
     * written zero is a decision and is obeyed; an absent one leaves the choice to the control
     * mode, because the right answer differs between them by more than a player could guess. */
    smoothing_ms = ini_read_float(INPUT_SECTION, "MouseSmoothMaxMs", -1.0f);
    config.smoothing_is_automatic = !(smoothing_ms >= 0.0f);
    if (config.smoothing_is_automatic) {
        smoothing_ms = DEFAULT_SMOOTH_CEILING_MS;
    }
    smoothing_ms = clamp_float(smoothing_ms, 0.0f, MAX_SMOOTHING_MS);
    config.smooth_ceiling_seconds = smoothing_ms / MILLISECONDS_PER_SECOND;

    /* The migrations, said loudly and exactly once each. A negative default is impossible for all
     * three old keys, so each of these detects presence and not merely a value. */
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
                    (double)config.degrees_per_count);
    }

    legacy = ini_read_float(INPUT_SECTION, "MouseSmoothingMs", -1.0f);
    if (legacy >= 0.0f) {
        log_warning("MouseSmoothingMs=%.0f is no longer read. It set a FIXED time constant, and a "
                    "fixed one cannot be right at two different report rates: what is barely enough "
                    "for a mouse reporting a hundred times a second is a quarter of a second of "
                    "mush for one reporting a thousand times. The filter now measures the device's "
                    "own report interval and sizes itself from that, and the new key "
                    "MouseSmoothMaxMs is the CEILING on how much delay it may spend, default %.0f. "
                    "Delete the old key to silence this.",
                    (double)legacy, (double)DEFAULT_SMOOTH_CEILING_MS);
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
                    (double)config.max_turn_rate);
    }
}

bool mouse_config_set_degrees_per_count(float degrees_per_count)
{
    /* Refused rather than clamped: a value that is not a number cannot have been meant, and
     * turning it into the minimum would silently make the mouse the slowest it can be. */
    if (!(degrees_per_count == degrees_per_count)) {
        log_warning("a mouse sensitivity that is not a number was refused, the setting is "
                    "unchanged at %.3f degrees per count", (double)config.degrees_per_count);
        return false;
    }

    config.degrees_per_count     = clamp_float(degrees_per_count, MIN_DEGREES_PER_COUNT,
                                               MAX_DEGREES_PER_COUNT);
    config.degrees_per_axis_unit = config.degrees_per_count / ENGINE_AXIS_SCALE;

    /* The bank is left alone. It holds counts the player has already made at the old sensitivity,
     * and it is at most a few report intervals of them; rescaling would be inventing input, and
     * dropping would eat a flick that was in flight when the slider moved. */

    if (!ini_write_float(INPUT_SECTION, "MouseDegreesPerCount",
                         config.degrees_per_count, 3)) {
        log_warning("the mouse sensitivity is now %.3f degrees per count, but the setting could "
                    "not be written to the ini and will be back to its old value on the next "
                    "launch", (double)config.degrees_per_count);
        return false;
    }
    return true;
}

void mouse_config_use_frame_clock_smoothing(void)
{
    if (!config.smoothing_is_automatic) {
        /* A written value wins, and a written zero is a decision. But this tree's own shipped ini
         * used to write that zero, so most existing installations carry one that nobody made, and
         * there is no way to tell the two apart from inside. Saying what it costs is the only
         * honest thing left: silence here would hand those players the per-frame path with the
         * delivery it is least able to survive. */
        if (!(config.smooth_ceiling_seconds > 0.0f)) {
            log_warning("MouseSmoothMaxMs=0 is in your ini and the per-frame view path is on, so "
                        "each frame is handed whichever device reports happened to fall inside it. "
                        "That is about a tenth of the movement on a 1000 Hz mouse and most of it on "
                        "a 125 Hz one, and it is felt as a restless camera on fast turns. Delete "
                        "the line to let this build choose, which is up to %.0f ms sized from your "
                        "device's own report interval. Older versions of this file wrote the zero "
                        "themselves, so it may well not be a choice you made.",
                        (double)FRAME_CLOCK_SMOOTH_CEILING_MS);
        }
        return;
    }
    config.smooth_ceiling_seconds = FRAME_CLOCK_SMOOTH_CEILING_MS / MILLISECONDS_PER_SECOND;
    log_info("the delivery filter is on at up to %.0f ms, this build's default for the per-frame "
             "view path rather than a value from the ini. Its LENGTH is the device's own, six "
             "report intervals, so a 1000 Hz mouse pays about 6 ms and a slow one is given what the "
             "ceiling allows. Without it each frame is handed whichever reports fell inside it: a "
             "tenth of the movement on a fast device and most of it on a slow one. "
             "MouseSmoothMaxMs=0 is obeyed.", (double)FRAME_CLOCK_SMOOTH_CEILING_MS);
}
