/* input_config.c: the ini half of the input feature.
 *
 * No engine and no state that outlives a setting. Every number here is clamped on the way in, so a
 * finite pair of limits is what guarantees a finite setting, and a value that is not a number
 * becomes the minimum rather than poisoning whatever reads it.
 */
#include "input_config.h"

#include "free_look.h"
#include "mouse_look.h"
#include "view_lead.h"

#include "common/ini.h"
#include "common/logging.h"

#include <stdbool.h>

#define INPUT_SECTION "enhanced_input"

/* The damper's own two numbers. 250 ms to close 90 % of a gap sits between the median authored
 * NPC's first turn stage and its full stage, and clear of the player's own 120 deg/s turn clamp,
 * which would need three quarters of a second for a right angle. 240 deg/s is twice that clamp
 * and exists so that a large gap cannot spike on its first substep. */
#define DEFAULT_STRAFE_SETTLE_MS   250.0f
#define MAX_STRAFE_SETTLE_MS      1000.0f

/* The engine's own steady-state turn rate for a held key, in degrees per second. Matching it rather
 * than picking a taste value is what makes "both features off" the original control scheme rather
 * than an approximation of it. The one difference is that ours is instant where the original ramps
 * over about a fifth of a second.
 *
 * The number is the engine's clamp on the turn cell, written as two immediates rather than loaded
 * from anywhere, at the tail of Plr_Steer:
 *
 *     0044A24B  C780 A4020000 0000F0C2   turnWheel = -120.0
 *     0044A274  C782 A4020000 0000F042   turnWheel = +120.0
 *
 * Both input paths reach that clamp. The keyboard ramp's own ceiling of 40 deg/s bounds the
 * increment and not the cell, because `turnWheel += ramp * axis` accumulates: a held key climbs
 * 12, 26, 42, 60, 80, 102, 120 and saturates in seven substeps. An earlier reading took the 40 for
 * the cell's own ceiling and sized this whole feature at a third of the truth. */
#define DEFAULT_KEY_TURN_RATE 120.0f

#define MIN_KEY_TURN_RATE      15.0f
#define MAX_KEY_TURN_RATE     720.0f

#define DEFAULT_STRAFE_TURN_RATE   240.0f
#define MIN_STRAFE_TURN_RATE        30.0f
#define MAX_STRAFE_TURN_RATE      2000.0f
#define MILLISECONDS_PER_SECOND   1000.0f

/* The steer log writes one line per substep, so it is a burst of lines and not a running trace.
 * Nothing but the bound is meant by the maximum; it stops a mistyped key from filling a disk. */
#define MAX_STEER_LOG_SUBSTEPS 4000

static input_config_t config;

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

const input_config_t *input_config(void)
{
    return &config;
}

void input_config_set_strafe(bool enabled)
{
    config.strafe = enabled;
}

void input_config_load(void)
{
    float settle_ms;

    config.mouse_look        = ini_read_bool (INPUT_SECTION, "MouseLook", false);
    config.strafe            = ini_read_bool (INPUT_SECTION, "Strafe", false);
    config.strafe_invert     = ini_read_bool (INPUT_SECTION, "StrafeInvert", false);
    config.strafe_turns_body = ini_read_bool (INPUT_SECTION, "StrafeTurnsBody", true);

    /* Zero is a legitimate setting and means "no damping": the angle steps between 0, +-45 and
     * +-90 the way it used to, which is what makes the two comparable in one build. */
    settle_ms = ini_read_float(INPUT_SECTION, "StrafeSettleMs", DEFAULT_STRAFE_SETTLE_MS);
    settle_ms = clamp_float(settle_ms, 0.0f, MAX_STRAFE_SETTLE_MS);
    config.strafe_settle_seconds = settle_ms / MILLISECONDS_PER_SECOND;

    config.steer_lean = ini_read_bool(INPUT_SECTION, "SteerLean", true);
    config.steer_lean_from_hand =
        ini_read_bool(INPUT_SECTION, "SteerLeanFromHand", true);
    config.restore_turn_rate =
        ini_read_bool(INPUT_SECTION, "RestoreTurnRate", true);
    config.steer_lean_test_degrees =
        clamp_float(ini_read_float(INPUT_SECTION, "SteerLeanTestDegrees", 0.0f), -90.0f, 90.0f);
    config.steer_log = ini_read_int(INPUT_SECTION, "SteerLog", 0);
    if (config.steer_log < 0)                      { config.steer_log = 0; }
    if (config.steer_log > MAX_STEER_LOG_SUBSTEPS) { config.steer_log = MAX_STEER_LOG_SUBSTEPS; }

    config.strafe_turn_rate =
        ini_read_float(INPUT_SECTION, "StrafeTurnRate", DEFAULT_STRAFE_TURN_RATE);
    config.strafe_turn_rate = clamp_float(config.strafe_turn_rate,
                                          MIN_STRAFE_TURN_RATE, MAX_STRAFE_TURN_RATE);

    config.key_turn_rate =
        ini_read_float(INPUT_SECTION, "KeyTurnRate", DEFAULT_KEY_TURN_RATE);
    config.key_turn_rate = clamp_float(config.key_turn_rate,
                                       MIN_KEY_TURN_RATE, MAX_KEY_TURN_RATE);

    mouse_look_load_config();
    free_look_load_config();
    view_lead_load_config();

    /* StrafeSpeed used to set a sideways speed in units per second, because the sidestep was a
     * displacement this DLL pushed in itself. It is now the engine walking, so the speed is the
     * gait the player is in and there is nothing left for the key to set. Saying so beats leaving
     * a setting that quietly does nothing. */
    if (ini_read_float(INPUT_SECTION, "StrafeSpeed", -1.0f) >= 0.0f) {
        log_info("StrafeSpeed is no longer used and can be deleted. Sideways movement is now the "
                 "engine's own walk or run, so it matches the gait exactly.");
    }
}
