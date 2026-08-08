/* free_look_math.c: the arithmetic of horizontal free look, with no engine in it. */
#include "free_look_math.h"

#include "player_record.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#define PI_F 3.14159265358979323846f

float free_look_wrap360(float degrees)
{
    float wrapped = fmodf(degrees, 360.0f);

    if (wrapped < 0.0f) {
        wrapped += 360.0f;
    }
    /* A value a hair below a full turn can round up to exactly 360 when the negative branch adds
     * one back. Landing on 360 rather than 0 is harmless for the arithmetic and confusing in a
     * log, so it is folded. Neither comparison holds for a value that is not a number, which is
     * what lets one pass through to the caller's finiteness gate. */
    if (wrapped >= 360.0f) {
        wrapped = 0.0f;
    }
    return wrapped;
}

float free_look_wrap180(float degrees)
{
    float wrapped = free_look_wrap360(degrees);

    if (wrapped > 180.0f) {
        wrapped -= 360.0f;
    }
    return wrapped;
}

float free_look_interpolated_heading(float previous, float current, float alpha)
{
    return previous + free_look_wrap180(current - previous) * alpha;
}

bool free_look_input_angle(float strafe, float forward, float *out_degrees)
{
    /* Subtracted from zero rather than negated, and that is not decoration. Negating a zero
     * strafe gives NEGATIVE zero, and atan2(-0, -1) is minus a half turn while atan2(+0, -1) is
     * plus one. Both name the same direction and the sum is wrapped either way, so nothing
     * downstream can tell, but an angle that carries a sign bit out of an input that had none is
     * the kind of thing that later gets compared against a bound and surprises somebody. */
    double across = 0.0 - (double)strafe;

    if (strafe == 0.0f && forward == 0.0f) {
        return false;
    }

    /* The sign, and why it is not the sideways walk's. That one divides the wanted travel by the
     * sign of the drive, because the integrator multiplies an unchanged facing by a SIGNED speed
     * and a backward key is a negative speed rather than a reversed facing. Here the body is
     * turned to face its travel and the drive is forced forward, so the reversal is expressed in
     * the ANGLE instead: a lone backward key is a half turn, and back-and-right is 135 degrees to
     * the right of the camera rather than 45 to the left.
     *
     * Positive is LEFT, because that is the direction increasing heading turns. */
    *out_degrees = (float)(atan2(across, (double)forward) * (180.0 / (double)PI_F));
    return true;
}

float free_look_offset(float camera_yaw, float interpolated_heading)
{
    return free_look_wrap360(camera_yaw - interpolated_heading);
}

free_look_release_t free_look_gate_refusal(const free_look_gate_t *gate)
{
    if (!gate->enabled) {
        return FREE_LOOK_RELEASE_SWITCHED_OFF;
    }
    if (!gate->record_valid) {
        return FREE_LOOK_RELEASE_NO_PLAYER_RECORD;
    }
    /* The phases are not running: no level, a cutscene that has parked the player, or the death
     * arm. All three are cases where the body cannot be turned and the camera is not ours. */
    if (gate->module_state != PLAYER_MODULE_PHASES_RUN) {
        return FREE_LOOK_RELEASE_PHASES_NOT_RUNNING;
    }
    /* An unknown mode means the record is not what we take it for. */
    if (gate->mode_index < 0 || gate->mode_index > PLAYER_MODE_MAX) {
        return FREE_LOOK_RELEASE_MODE_UNKNOWN;
    }
    /* A script owns the camera: dialogue, the cutscene director, the front end, a set piece, the
     * turret. Every one of them forces its region during the substeps, i.e. BEFORE the camera
     * update of the same frame, so this signal is exact rather than one frame late.
     *
     * This is tested before the camera object and before the region, and the order is what keeps
     * the two families apart. A forced region runs through exactly the same arms an authored one
     * does and leaves exactly the same camera state behind, so testing the state first would file
     * every cutscene under "the level author placed a camera here" and hand the wanted yaw back
     * afterwards, which is the one thing a cutscene must not get. */
    if (gate->camera_override != 0) {
        return FREE_LOOK_RELEASE_SCRIPT_FORCED_REGION;
    }
    /* A snap is in progress: level start, warp, a new view, or the frame after a load. This is
     * also what makes a save taken under free look restore with the camera behind the player,
     * the countdown is set by the restore, we release while it runs, and the engine's own snap
     * overwrites the offset the save carried before we write again. */
    if (gate->snap_countdown != 0) {
        return FREE_LOOK_RELEASE_SNAP_RUNNING;
    }
    if (!gate->view_valid) {
        return FREE_LOOK_RELEASE_NO_CAMERA_OBJECT;
    }
    if (gate->view_state != BAPVIEW_STATE_FOLLOW) {
        return FREE_LOOK_RELEASE_CAMERA_NOT_FOLLOW;
    }
    /* An authored fixed camera. The camera object's state already catches every one of them on
     * the shipped levels, because it is set from these same flags, reading the flags as well is
     * what keeps the rule from resting on a census of what shipped. It is the one signal that is
     * a frame late and cannot be made earlier without owning the region lookup, which the engine
     * performs inside the camera update itself. */
    if (gate->region_known && (gate->region_flags & CAMERA_REGION_FIXED_MASK) != 0) {
        return FREE_LOOK_RELEASE_CAMERA_REGION;
    }
    return FREE_LOOK_ARMED;
}

bool free_look_gate_is_armed(const free_look_gate_t *gate)
{
    return free_look_gate_refusal(gate) == FREE_LOOK_ARMED;
}

const char *free_look_release_name(free_look_release_t reason)
{
    switch (reason) {
    case FREE_LOOK_ARMED:
        return "armed";
    case FREE_LOOK_RELEASE_SWITCHED_OFF:
        return "free look is switched off";
    case FREE_LOOK_RELEASE_NO_PLAYER_RECORD:
        return "the player record is not readable";
    case FREE_LOOK_RELEASE_PHASES_NOT_RUNNING:
        return "the player phases are not running (no level, a parked player, or the death arm)";
    case FREE_LOOK_RELEASE_MODE_UNKNOWN:
        return "the player mode cannot be told apart";
    case FREE_LOOK_RELEASE_SCRIPT_FORCED_REGION:
        return "a script has forced a camera region";
    case FREE_LOOK_RELEASE_SNAP_RUNNING:
        return "the camera is snapping (level start, warp, new view, or a load)";
    case FREE_LOOK_RELEASE_NO_CAMERA_OBJECT:
        return "the camera object is not readable";
    case FREE_LOOK_RELEASE_CAMERA_NOT_FOLLOW:
        return "the camera object has left its follow state";
    case FREE_LOOK_RELEASE_CAMERA_REGION:
        return "an authored camera region under the player";
    case FREE_LOOK_RELEASE_NOT_A_NUMBER:
        return "a value that is not a number reached the store";
    }
    return "unnamed";
}

bool free_look_release_is_authored_region(free_look_release_t reason)
{
    return reason == FREE_LOOK_RELEASE_CAMERA_NOT_FOLLOW ||
           reason == FREE_LOOK_RELEASE_CAMERA_REGION;
}

bool free_look_recovers_wanted_yaw(float wanted, float engine_yaw, float limit_degrees)
{
    float gap;

    /* Negated so that a limit which is not a number switches the recovery off rather than
     * comparing against it. */
    if (!(limit_degrees > 0.0f)) {
        return false;
    }
    if (!isfinite(wanted) || !isfinite(engine_yaw)) {
        return false;
    }

    gap = free_look_wrap180(wanted - engine_yaw);
    if (gap < 0.0f) {
        gap = -gap;
    }
    return gap <= limit_degrees;
}
