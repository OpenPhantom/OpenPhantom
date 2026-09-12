/* input_config.c: the ini half of the input feature.
 *
 * No engine and no state that outlives a setting. Every number here is clamped on the way in, so a
 * finite pair of limits guarantees a finite setting, and a value that is not a number becomes the
 * minimum rather than poisoning whatever reads it.
 */
#include "input_config.h"

#include "free_look.h"
#include "mouse_look.h"
#include "view_lead.h"

#include "common/ini.h"
#include "common/logging.h"
#include "common/numeric.h"

#include <stdbool.h>

#define INPUT_SECTION "enhanced_input"

/* The damper's own two numbers. 250 ms to close 90 % of a gap sits between the median authored
 * NPC's first turn stage and its full stage, and clear of the player's own 120 deg/s turn clamp,
 * which would need three quarters of a second for a right angle. 240 deg/s is twice that clamp
 * and exists so that a large gap cannot spike on its first substep. */
#define DEFAULT_STRAFE_SETTLE_MS   250.0f
#define MAX_STRAFE_SETTLE_MS      1000.0f

/* The engine's own steady-state turn rate for a held key, in degrees per second. Matching it rather
 * than picking a taste value keeps "both features off" the original control scheme rather than an
 * approximation of it. The one difference is that ours is instant where the original ramps
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

/* The camera follow's own damping, deliberately slower than the body's 250 ms above. The point
 * of it is that the camera arrives noticeably after you do; matching the body would reproduce the
 * snap it exists to replace. 600 ms is a first guess and is meant to be tuned by feel, which is
 * why it is a key rather than a constant. The rate cap is lower than the body's for the same
 * reason: a right angle should swing, not whip. */
#define DEFAULT_CAMERA_FOLLOW_SETTLE_MS  600.0f
#define MAX_CAMERA_FOLLOW_SETTLE_MS     3000.0f
#define DEFAULT_CAMERA_FOLLOW_RATE       120.0f
#define DEFAULT_CAMERA_FOLLOW_STRENGTH     0.35f
#define DEFAULT_CAMERA_FOLLOW_MAX_DEG     25.0f
#define DEFAULT_CAMERA_FOLLOW_HOLD_MS    250.0f
#define MAX_CAMERA_FOLLOW_HOLD_MS       5000.0f
#define MAX_CAMERA_FOLLOW_MAX_DEG         90.0f
#define MIN_CAMERA_FOLLOW_RATE            15.0f

/* 0.24 is close to XInput's own left-thumb recommendation and is the value controller_input.dll
 * already uses for the right stick, so the two sticks feel the same at rest. It replaces the
 * engine's thirty per cent SQUARE cut rather than adding to it.
 *
 * The run threshold is a taste value and the reason it is a key. 0.70 of the stick means a
 * relaxed push walks and a deliberate one runs; the hysteresis is half the width of the band
 * either side of it, so a stick resting on the boundary cannot flicker the clip and the speed
 * cap on and off. */
/* Deliberately lazier than the body's own 150 ms and 540 deg/s on the ground. A jump that can
 * be pivoted in place is a different game; this is meant to be a lean, enough to make a gap
 * that was aimed slightly wrong reachable. */
#define DEFAULT_AIR_SETTLE_MS          400.0f
#define MAX_AIR_SETTLE_MS             3000.0f
#define DEFAULT_AIR_TURN_RATE          180.0f
#define MIN_AIR_TURN_RATE               15.0f
#define MAX_AIR_TURN_RATE             1000.0f

#define DEFAULT_PAD_DEADZONE           0.24f
#define DEFAULT_PAD_RUN_THRESHOLD      0.70f
#define DEFAULT_PAD_RUN_HYSTERESIS     0.05f
#define MAX_PAD_CONTROLLER_INDEX          3
#define MAX_CAMERA_FOLLOW_RATE          1000.0f
#define MILLISECONDS_PER_SECOND   1000.0f

/* The steer log writes one line per substep, so it is a burst of lines and not a running trace.
 * Nothing but the bound is meant by the maximum; it stops a mistyped key from filling a disk. */
#define MAX_STEER_LOG_SUBSTEPS 4000

static input_config_t config;

const input_config_t *input_config(void)
{
    return &config;
}

void input_config_set_air_control(bool enabled)
{
    config.air_control = enabled;
}

void input_config_set_camera_follow(bool enabled)
{
    config.camera_follow = enabled;
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
     * +-90 the way it used to, so the two are comparable in one build. */
    settle_ms = ini_read_float(INPUT_SECTION, "StrafeSettleMs", DEFAULT_STRAFE_SETTLE_MS);
    settle_ms = numeric_clamp(settle_ms, 0.0f, MAX_STRAFE_SETTLE_MS);
    config.strafe_settle_seconds = settle_ms / MILLISECONDS_PER_SECOND;

    config.steer_lean = ini_read_bool(INPUT_SECTION, "SteerLean", true);
    config.steer_lean_from_hand =
        ini_read_bool(INPUT_SECTION, "SteerLeanFromHand", true);
    config.restore_turn_rate =
        ini_read_bool(INPUT_SECTION, "RestoreTurnRate", true);
    config.steer_lean_test_degrees =
        numeric_clamp(ini_read_float(INPUT_SECTION, "SteerLeanTestDegrees", 0.0f), -90.0f, 90.0f);
    config.steer_log = ini_read_int(INPUT_SECTION, "SteerLog", 0);
    if (config.steer_log < 0)                      { config.steer_log = 0; }
    if (config.steer_log > MAX_STEER_LOG_SUBSTEPS) { config.steer_log = MAX_STEER_LOG_SUBSTEPS; }

    config.strafe_turn_rate =
        ini_read_float(INPUT_SECTION, "StrafeTurnRate", DEFAULT_STRAFE_TURN_RATE);
    config.strafe_turn_rate = numeric_clamp(config.strafe_turn_rate,
                                            MIN_STRAFE_TURN_RATE, MAX_STRAFE_TURN_RATE);

    /* Off by default: it changes how the game is played rather than repairing a fault, which
     * is the same reason free look ships off. */
    config.camera_follow = ini_read_bool(INPUT_SECTION, "CameraFollow", false);
    settle_ms = ini_read_float(INPUT_SECTION, "CameraFollowSettleMs",
                               DEFAULT_CAMERA_FOLLOW_SETTLE_MS);
    settle_ms = numeric_clamp(settle_ms, 0.0f, MAX_CAMERA_FOLLOW_SETTLE_MS);
    config.camera_follow_settle_seconds = settle_ms / MILLISECONDS_PER_SECOND;
    config.camera_follow_rate =
        numeric_clamp(ini_read_float(INPUT_SECTION, "CameraFollowRate",
                                     DEFAULT_CAMERA_FOLLOW_RATE),
                      MIN_CAMERA_FOLLOW_RATE, MAX_CAMERA_FOLLOW_RATE);
    config.camera_follow_strength =
        numeric_clamp(ini_read_float(INPUT_SECTION, "CameraFollowStrength",
                                     DEFAULT_CAMERA_FOLLOW_STRENGTH), 0.0f, 1.0f);
    config.camera_follow_max_degrees =
        numeric_clamp(ini_read_float(INPUT_SECTION, "CameraFollowMaxDeg",
                                     DEFAULT_CAMERA_FOLLOW_MAX_DEG),
                      0.0f, MAX_CAMERA_FOLLOW_MAX_DEG);

    settle_ms = ini_read_float(INPUT_SECTION, "CameraFollowHoldMs",
                               DEFAULT_CAMERA_FOLLOW_HOLD_MS);
    config.camera_follow_hold_seconds =
        numeric_clamp(settle_ms, 0.0f, MAX_CAMERA_FOLLOW_HOLD_MS) / MILLISECONDS_PER_SECOND;

    config.air_control = ini_read_bool(INPUT_SECTION, "AirControl", false);
    settle_ms = ini_read_float(INPUT_SECTION, "AirControlSettleMs", DEFAULT_AIR_SETTLE_MS);
    config.air_settle_seconds =
        numeric_clamp(settle_ms, 0.0f, MAX_AIR_SETTLE_MS) / MILLISECONDS_PER_SECOND;
    config.air_turn_rate =
        numeric_clamp(ini_read_float(INPUT_SECTION, "AirControlRate", DEFAULT_AIR_TURN_RATE),
                      MIN_AIR_TURN_RATE, MAX_AIR_TURN_RATE);

    /* On by default, unlike the two features above it, because this is a REPAIR rather than a
     * change of scheme: it gives the pad the direction and the magnitude the engine's own path
     * throws away, and a machine with no pad plugged in never reaches any of it. */
    config.pad_stick = ini_read_bool(INPUT_SECTION, "PadStick", true);
    config.pad_controller_index =
        (int)numeric_clamp((float)ini_read_int(INPUT_SECTION, "PadControllerIndex", 0),
                           0.0f, (float)MAX_PAD_CONTROLLER_INDEX);
    config.pad_deadzone =
        numeric_clamp(ini_read_float(INPUT_SECTION, "PadDeadzone", DEFAULT_PAD_DEADZONE),
                      0.0f, 0.9f);
    config.pad_run_threshold =
        numeric_clamp(ini_read_float(INPUT_SECTION, "PadRunThreshold",
                                     DEFAULT_PAD_RUN_THRESHOLD), 0.0f, 1.0f);
    config.pad_run_hysteresis =
        numeric_clamp(ini_read_float(INPUT_SECTION, "PadRunHysteresis",
                                     DEFAULT_PAD_RUN_HYSTERESIS), 0.0f, 0.25f);

    /* Off by default, which is a repair: the shipped binding on that axis walks the player
     * from the stick they are aiming with. 1 hands it back to somebody who bound it on
     * purpose in the game's own Controls screen. */
    config.pad_engine_right_stick = ini_read_bool(INPUT_SECTION, "PadEngineRightStick",
                                                  false);

    config.key_turn_rate =
        ini_read_float(INPUT_SECTION, "KeyTurnRate", DEFAULT_KEY_TURN_RATE);
    config.key_turn_rate = numeric_clamp(config.key_turn_rate,
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
