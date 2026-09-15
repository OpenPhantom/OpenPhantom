/* free_look.c: the mouse turns the camera, the body turns toward where it travels.
 *
 * This file is the BODY half and the feature as a whole: its configuration, its live on/off
 * switch, the three detours on the attack path, and installation. The CAMERA half, the arming
 * gate, the release rules and the per-frame write into the two camera cells, is
 * free_look_camera.c, and the reasoning that belongs to the camera is at the top of that file.
 * The arithmetic is free_look_math.c and is tested without the game.
 *
 * ==============================================================================================
 * The body half
 *
 * Movement is made camera-relative rather than the body being turned toward its travel: with the
 * camera decoupled, "forward" has to mean "away from the camera" or holding one key never turns
 * the player at all and the result is tank controls with a swivelling camera.
 *
 *     wanted travel = cameraYaw + atan2(-strafe, forward)      positive is LEFT
 *     heading      -> damped toward it, using the sideways walk's own damper
 *     the walk bit and the drive are forced exactly as the sideways walk forces them
 *
 * Because the body now genuinely faces the way it travels, the model-root rotation the sideways
 * walk latches is not needed and is walked home on every substep. And because the drive is always
 * forward, a lone backward key is a half turn rather than a negative speed: the back-pedal clip is
 * retired while this is on. That is a real loss of authored content, taken deliberately, the
 * alternative, driving backward whenever the wanted travel is more than a right angle off the
 * current facing, flips the body through 180 degrees at the boundary and is worse.
 *
 * ==============================================================================================
 * While the trigger is held, this becomes the other control scheme
 *
 * Free look normally turns the body toward where it TRAVELS, so the aim sits at an angle to the
 * body that changes with every direction change. Three successive designs corrected the shot and
 * the chest for that angle on every substep, against a body that was itself still turning, and the
 * pose could never settle. No damper fixes that; the angle has to not exist.
 *
 * So while the trigger is held the body simply faces the CAMERA, exactly as it does with free look
 * switched off, and the feet strafe and back-pedal relative to that. Nothing is twisted, because
 * there is nothing left to twist: the weapon points where the player looks by construction, and
 * firing feels the same in both control schemes.
 *
 * SIZE NOTE. Over the 600 line mark, under the 900 hard limit. The code is a few dozen stores; the
 * rest is the reasoning, and every paragraph of it records a mistake that was made or nearly made,
 * writing from the frame hook, leashing the camera to the body, treating a cutscene and a floor
 * polygon as the same release, correcting a twist against a moving reference, and four rounds of
 * building on an addition that only one of the engine's two yaw arms performs. None of that is
 * visible in the stores themselves.
 *
 * The camera half was lifted out when this file reached 1139 lines, 239 past the hard limit. An
 * earlier note here recommended moving only the arming gate, build_gate() and release_for(), which
 * would have been about 120 lines and would NOT have been enough, the whole camera half was.
 * The aim went next, into free_look_aim.c, when this file came within twenty lines of the limit
 * again: the three attack detours and the offset they resolve, one responsibility, behind one
 * install call. The next seam, if one is ever needed, is the configuration block: it touches
 * nothing but its own struct, so it can move behind a single call that fills a free_look_config_t
 * and needs no shared state at all. Do not make room by deleting comments; that is not a way of
 * meeting the limit.
 */
#include "free_look.h"

#include "free_look_internal.h"

#include "camera_sites.h"
#include "camera_watch.h"
#include "free_look_aim.h"
#include "free_look_log.h"
#include "free_look_math.h"
#include "input_config.h"
#include "mouse_look.h"
#include "player_record.h"
#include "player_sites.h"
#include "strafe_walk.h"
#include "view_lead.h"

#include "common/detour.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/numeric.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define INPUT_SECTION "enhanced_input"
#define MILLISECONDS_PER_SECOND 1000.0f

/* The body turn. The engine's own clamp on the player is 120 degrees per second, which needs three
 * quarters of a second for a right angle, far too slow when the camera has just been flicked.
 * 150 ms to close 90 % of the gap turns a right angle in about a sixth of a second, and the rate
 * cap is deliberately well above the engine's own clamp so that a large gap cannot spike on its
 * first substep and cannot be mistaken for the engine turning by itself. */
#define DEFAULT_BODY_SETTLE_MS  150.0f
#define MAX_BODY_SETTLE_MS     1000.0f
#define DEFAULT_BODY_TURN_RATE  540.0f
#define MIN_BODY_TURN_RATE       60.0f
#define MAX_BODY_TURN_RATE     2000.0f

/* How far the upper body may be twisted away from the feet, and it is a real limit rather than a
 * taste value: the SAME number aims the shot and turns the chest, so whatever this allows is
 * exactly how far the weapon can point away from the direction of travel. That equality is the
 * whole point, a shot can never leave along a line the weapon is not on, at any setting.
 *
 * 90 lets a player strafing sideways shoot straight along the strafe. Beyond that the aim is
 * LIMITED rather than made inconsistent: running back-right while looking forward is 135 degrees,
 * and the shot then leaves at 90 with the weapon on that same line, instead of at 135 out of a
 * body facing elsewhere. */
/* How far a full sideways deflection swings the aim while the trigger is held, before the twist
 * limit below has its say. The mouse stays the aiming device: this rides ON TOP of the body already
 * being squared up to the camera, so it leads a target sideways without the camera having to move.
 *
 * Forty five rather than the ninety the limit allows, because these are two different quantities.
 * The limit is how far the weapon may ever point away from the body, a safety rail on the whole
 * sum; this is how much authority one input has inside it. At ninety a full sideways key would
 * spend the entire allowance on its own, leaving nothing for anything else to contribute; that is
 * not an adjustment but a second aiming device. Zero switches it off and leaves the mouse alone
 * with the job. */
#define DEFAULT_AIM_STRAFE_SWING 45.0f

#define DEFAULT_AIM_TWIST_MAX  90.0f
#define MAX_AIM_TWIST_MAX     180.0f

/* How far the engine's own recentre may have turned the camera during an authored region's hold
 * and still have that turn undone when the region lets go. Sized against the damper: the recentre
 * eats four per cent of the remaining gap per rendered frame, so a quarter of a second of release
 * moves a right angle by about twenty-five degrees. Below the limit the turn is an artefact of how
 * many frames the player happened to spend on those floor polygons; above it the engine has
 * genuinely re-aimed the shot and taking it back would be a jump rather than a repair. */
#define DEFAULT_REGION_RECOVER_DEG 25.0f
#define MAX_REGION_RECOVER_DEG    180.0f

/* The one instance. free_look_camera.c is handed a pointer to it at install time; nothing else
 * ever sees it. */
static free_look_state_t free_state;

static float read_field(const uint8_t *record, int offset)
{
    return *(const float *)(record + offset);
}

static void write_field(uint8_t *record, int offset, float value)
{
    *(float *)(record + offset) = value;
}

/* ==============================================================================================
 * Configuration. Every float is passed through clamp_float, which turns NaN into the minimum and
 * either infinity into a bound, so a finite pair of limits guarantees a finite setting.
 * ============================================================================================ */
/* Who is already aiming, if anyone. Either scheme points the body with the movement control, so
 * either one makes the aim snap take that control away at the moment the trigger goes down. */
static bool aim_pairing_owner(void)
{
    return input_config()->strafe || input_config()->camera_follow;
}

void free_look_load_config(void)
{
    float settle_ms;

    /* Off by default. This changes how the game plays, not how it looks. */
    free_state.config.enabled  = ini_read_bool(INPUT_SECTION, "FreeLook", false);
    /* These two default off when something else already aims, and the coupling is the point
     * rather than a convenience.
     *
     * The aim snap exists to make aiming under free look feel like aiming without it: it points the
     * body at the camera while the trigger is held, so the camera is the aiming device in both
     * schemes. That is a good answer when the body would otherwise be facing wherever it walks and
     * the player has no other way to aim.
     *
     * With the sideways walk on, the player has already chosen the other scheme. The body faces
     * where it travels and the movement keys point it, so squaring it up to the camera the moment
     * the trigger goes down takes the aiming away from the control they are already using and hands
     * it to the mouse.
     *
     * The follow camera counts for the same reason, and leaving it out was a defect rather than a
     * decision. It stands off while the body is held at the camera, so it appears to stop working
     * exactly while you shoot, and a player who switches it on has chosen to aim by pointing the
     * body. Keying only off the sideways walk meant they had to find these two keys and write them
     * by hand to get the scheme the switch had promised them.
     *
     * So the default follows the scheme rather than being one answer for both. An explicit key in
     * the file still wins either way, so this stays a default and not a rule.
     *
     * Read once, so switching the sideways walk on during a session does not move these underneath
     * the player: the fire detour is placed at install from the answer below and there is no way to
     * take a detour back out again. */
    {
        /* Read as an int against an impossible default, so a written 0 can be told from no key at
         * all. Only an absent key follows the pairing below; a stated one is the player's. */
        int32_t snap  = ini_read_int(INPUT_SECTION, "FreeLookAimSnap", -1);
        int32_t keeps = ini_read_int(INPUT_SECTION, "FreeLookAimKeepsMovement", -1);

        free_state.config.aim_snap_written  = (snap >= 0);
        free_state.config.aim_keeps_written = (keeps >= 0);
        free_state.config.aim_snap =
            free_state.config.aim_snap_written ? (snap != 0) : !aim_pairing_owner();
        free_state.config.aim_keeps_movement =
            free_state.config.aim_keeps_written ? (keeps != 0) : !aim_pairing_owner();
    }
    free_state.config.aim_twist_max =
        ini_read_float(INPUT_SECTION, "FreeLookAimTwistMax", DEFAULT_AIM_TWIST_MAX);
    if (!(free_state.config.aim_twist_max >= 0.0f)) {
        free_state.config.aim_twist_max = DEFAULT_AIM_TWIST_MAX;
    }
    if (free_state.config.aim_twist_max > MAX_AIM_TWIST_MAX) {
        free_state.config.aim_twist_max = MAX_AIM_TWIST_MAX;
    }
    free_state.config.aim_strafe_swing =
        ini_read_float(INPUT_SECTION, "FreeLookAimStrafeSwing", DEFAULT_AIM_STRAFE_SWING);
    free_state.config.aim_strafe_swing =
        numeric_clamp(free_state.config.aim_strafe_swing, 0.0f, MAX_AIM_TWIST_MAX);

    free_state.config.rigid_mouse_look_camera =
        ini_read_bool(INPUT_SECTION, "MouseLookRigidCamera", true);

    /* Reported by presence rather than ignored in silence: it USED to pull the camera back toward
     * the body, and a file carrying a value for it was tuned against a camera that is no longer
     * tethered. A negative default is impossible for the old key, so this detects the key itself
     * and not merely a value. */
    if (ini_read_float(INPUT_SECTION, "FreeLookMaxYawDeg", -1.0f) >= 0.0f) {
        log_warning("FreeLookMaxYawDeg is no longer read and can be deleted. It limited how far "
                    "the camera could be turned away from the body, which made the camera a "
                    "function of the body again, the one thing free look exists to undo. The "
                    "camera now turns without limit, and the body comes round on its own as soon "
                    "as you ask to move.");
    }

    settle_ms = ini_read_float(INPUT_SECTION, "FreeLookBodyTurnMs", DEFAULT_BODY_SETTLE_MS);
    settle_ms = numeric_clamp(settle_ms, 0.0f, MAX_BODY_SETTLE_MS);
    free_state.config.body_settle_seconds = settle_ms / MILLISECONDS_PER_SECOND;

    free_state.config.body_turn_rate =
        ini_read_float(INPUT_SECTION, "FreeLookBodyTurnMaxDegPerSec", DEFAULT_BODY_TURN_RATE);
    free_state.config.body_turn_rate =
        numeric_clamp(free_state.config.body_turn_rate, MIN_BODY_TURN_RATE, MAX_BODY_TURN_RATE);

    /* Zero is a legitimate setting and means "never take the wanted yaw back": the camera is then
     * always picked up wherever the engine's recentre had got to, as this feature did before the
     * recovery existed. */
    free_state.config.region_recover_degrees =
        ini_read_float(INPUT_SECTION, "FreeLookRegionRecoverDeg", DEFAULT_REGION_RECOVER_DEG);
    free_state.config.region_recover_degrees =
        numeric_clamp(free_state.config.region_recover_degrees, 0.0f, MAX_REGION_RECOVER_DEG);

    free_state.config.log_transitions = ini_read_bool(INPUT_SECTION, "FreeLookLog", false);

    /* Shared with the other camera follow rather than given keys of its own. The two are the
     * same feature answered two different ways and they can never both run: that one needs
     * free look OFF, this one needs it ON. One switch, one settle time, one rate cap. */
    free_state.config.passive_follow         = input_config()->camera_follow;
    free_state.config.passive_settle_seconds = input_config()->camera_follow_settle_seconds;
    free_state.config.passive_rate           = input_config()->camera_follow_rate;
    free_state.config.passive_hold_seconds   = input_config()->camera_follow_hold_seconds;
    free_state.config.air_control            = input_config()->air_control;
    free_state.config.air_settle_seconds     = input_config()->air_settle_seconds;
    free_state.config.air_turn_rate          = input_config()->air_turn_rate;
}

bool free_look_is_installed(void)
{
    return free_state.installed;
}

void free_look_set_air_control(bool enabled)
{
    free_state.config.air_control = enabled;
}

void free_look_set_passive_follow(bool enabled)
{
    free_state.config.passive_follow = enabled;
}

void free_look_refresh_aim_pairing(void)
{
    bool owned = aim_pairing_owner();

    if (!free_state.config.aim_snap_written) {
        free_state.config.aim_snap = !owned;
    }

    /* Both ways, because the fire detour is now placed whenever its site resolves and reads this
     * on every call. When the site did not resolve the pairing still moves; what it cannot do is
     * make the shot aim itself, and the branch that needs that already tests fire_shot_armed. */
    if (!free_state.config.aim_keeps_written) {
        free_state.config.aim_keeps_movement = !owned;
    }
}

bool free_look_is_enabled(void)
{
    return free_state.installed && free_state.config.enabled;
}

bool free_look_level_owns_camera(void)
{
    return free_state.installed && free_look_release_is_authored_region(free_state.world_gate);
}

bool free_look_set_enabled(bool enabled)
{
    if (!free_state.installed) {
        return false;
    }
    if (free_state.config.enabled == enabled) {
        return true;
    }

    free_state.config.enabled = enabled;
    if (enabled) {
        /* Nothing to prepare: the next camera update seeds the wanted yaw from the LIVE cell, the
         * same path every re-arming takes, so the camera is picked up where the engine has it. */
        return true;
    }

    /* Dropped in this instant rather than at the next camera update, so the phase thunks stop
     * taking substeps immediately and the mouse is back on the body on the very next one. The
     * model-root latch needs no reset: free look walks it home on every substep it takes, so by
     * the time this runs it is already at zero and the sideways walk owns it again. */
    free_look_camera_release();
    free_state.aim_hold_seconds = 0.0f;
    free_state.aim_stance       = false;
    return true;
}

/* What `armed` means, and it is not what these four callers want.
 *
 * `armed` says the CAMERA HOLD is taken: the recentre is frozen, the yaw arm is forced and the
 * authored regions are being watched. The camera follow takes that same hold, because building a
 * second copy of it would be the largest piece of duplication in this directory, so `armed` is now
 * true in a session where the player never switched free look on at all.
 *
 * Free look's STEERING is a separate thing: the mouse turning the camera instead of the body, the
 * body pointed at the camera under the trigger, the walk driven only in the aim stance. That
 * belongs to the player having asked for free look, and to nothing else. Running it because the
 * follow borrowed the camera took the substep away from the ordinary path, left the travel angle
 * at zero and so starved the very feature that borrowed it.
 *
 * So the camera side keeps asking `armed`, and the four steering sites ask this instead. */
bool free_look_is_steering(void)
{
    return free_state.installed && free_state.armed && free_state.config.enabled;
}

bool free_look_steer(uint8_t *record, float mouse_step_degrees, float strafe, float forward,
                     bool stand_mode, bool air_mode, bool melee_mode)
{
    float input_angle = 0.0f;

    if (!free_look_is_steering() || !free_state.camera_yaw_valid ||
        record == NULL) {
        return false;
    }

    /* Latched for the aim swing, which runs on the fire path and has no other way to see it. The
     * value is whatever this substep was handed, so the sideways-walk gate above this call has
     * already zeroed it when that feature is off. */
    free_state.aim_strafe = strafe;

    /* The mouse turns the camera here and nowhere else. */
    free_state.camera_yaw = free_look_wrap360(free_state.camera_yaw + mouse_step_degrees);

    /* Noted for the passive drift, which must never move the camera while the player is moving
     * it themselves. A camera that keeps sliding home under the hand is the exact fault the
     * first attempt at this feature had, and camera_sites.h warns of it in as many words.
     *
     * A LATCH rather than a per-substep answer, because the camera half runs on the render
     * clock and would otherwise sample this between the substeps that set it. */
    if (mouse_step_degrees != 0.0f) {
        free_state.look_seen = true;
    }

    /* The model-root latch is NOT walked home here, and it used to be. The caller takes exactly one
     * damper step per substep, either driving the walk or releasing it, and a release taken here as
     * well made two: the latch came home at about twice the configured settle time, and in the aim
     * stance one step toward zero was followed by one step toward the travel target from the
     * already-moved value. One substep, one damper step, and the caller owns it. */

    free_state.body_target_valid = false;

    /* The aim snap is deliberately NOT gated on Stand. It asks for one thing only, face the
     * camera, so it carries no travel direction and no drive sign, and an attack started in the
     * air has to point where the player is looking just as much as one started on the ground.
     * That is why this block sits above the Stand test rather than below it. */
    if (free_state.aim_hold_seconds > 0.0f && free_state.config.aim_snap) {
        free_state.body_target         = free_state.camera_yaw;
        free_state.body_target_valid   = true;
        free_state.target_settle_seconds = free_state.config.body_settle_seconds;
        free_state.target_turn_rate      = free_state.config.body_turn_rate;
        free_state.aim_stance          = free_state.config.aim_keeps_movement;
    } else {
        free_state.aim_stance = false;
    }

    /* The walk is driven in stand and nowhere else, and that gate is load-bearing well beyond the
     * animation: phase 2 also runs while shoving a crate, and that mode is only left when NEITHER
     * move bit is set, so a forced bit outside Stand would lock the player in it for good.
     *
     * The travel direction is Stand-gated with it, and deliberately so. Outside Stand the forced
     * forward drive is not in force, so a backward key is still a NEGATIVE SPEED along an
     * unchanged facing rather than a half turn; building the camera-relative angle there would
     * double-count the reversal and send the player the wrong way. Outside Stand the mouse
     * therefore moves the camera while the body holds its heading, unless an attack is live, a
     * swing is running, or air control is on and the body is genuinely in flight. */
    if (!stand_mode) {
        /* A SWING, aimed the way a shot is.
         *
         * A gun points the body at the camera the moment the trigger goes down, so the bolt leaves
         * along the direction the player is holding and the feet keep working underneath. The
         * sabre reaches none of that: Plr_HandleInput [0x0044AF93] sends weapon slot 1 to
         * Plr_SelectSabreAction and every other slot to Plr_StartFire, and the aim snap is armed
         * on the second of those, so a swing arms nothing.
         *
         * The mode was never the obstacle. Its descriptor [0x004B52C8] runs the steer at index 2
         * and pins at index 7, so Plr_Integrate turns the heading from the turn cell throughout a
         * swing exactly as it does standing; this file stopped it, at the Stand test below, and
         * the swing was then left pointing wherever the body faced when the button went down.
         *
         * So the direction the player asks for turns the body here as well, and the swept blade
         * follows it within the swing. Nothing else of the Stand branch comes with it: no forced
         * move bit, which would change which clip the combo chains into and lock a crate shove,
         * and no walk drive, so a swing still travels on its own authored lunge.
         *
         * The backward half is DROPPED. That is the second of the Stand gate's two reasons
         * arriving here rather than being escaped: nothing forces a forward walk outside Stand, so
         * a backward key keeps its backward move bit and its negative drive, and turning the body
         * a half circle to face the camera-relative angle would then send the player forwards
         * along it. Clamped at zero, a backward push keeps the engine's own back-pedal and asks
         * for no turn. Forward and sideways aim the swing.
         *
         * Not gated on air control. That setting buys a jump an ability the shipped game
         * withholds; this gives a swing back a turn the shipped game already has. */
        if (melee_mode && !free_state.body_target_valid &&
            free_look_input_angle(strafe, forward > 0.0f ? forward : 0.0f, &input_angle)) {
            free_state.body_target           = free_look_wrap360(free_state.camera_yaw +
                                                                 input_angle);
            free_state.body_target_valid     = true;
            free_state.target_settle_seconds = free_state.config.body_settle_seconds;
            free_state.target_turn_rate      = free_state.config.body_turn_rate;
            return true;
        }

        /* Air control, and it is the only OTHER thing that may happen outside Stand.
         *
         * The gate above it stays exactly as strict as it was, for the reasons alongside it: a
         * forced move bit outside Stand would lock the crate shove for good, and a backward key
         * outside Stand is still a negative speed along an unchanged facing rather than a half
         * turn. NOTHING is forced here and no move bit is written. This turns the heading and
         * stops, so the speed the player launched with is redirected rather than renewed, and a
         * jump cannot be flown further than it would have gone.
         *
         * Only the three modes that are genuinely a body in flight with the ordinary integrate
         * under it. A scripted jump follows an authored arc and its descriptor skips the steer
         * phase, so it never reaches here at all. */
        if (free_state.config.air_control && air_mode && !free_state.body_target_valid &&
            free_look_input_angle(strafe, forward, &input_angle)) {
            free_state.body_target           = free_look_wrap360(free_state.camera_yaw +
                                                                 input_angle);
            free_state.body_target_valid     = true;
            free_state.target_settle_seconds = free_state.config.air_settle_seconds;
            free_state.target_turn_rate      = free_state.config.air_turn_rate;
        }
        return true;
    }

    /* Tell the engine a forward walk is under way when the input does not already say so: a lone
     * sideways key leaves both move bits clear, and a backward key has to become a forward walk
     * in the new direction because the body is about to be turned to face it. Plain forward is
     * left completely alone, so walking forward is byte-for-byte what it always was.
     *
     * Not while aiming. Forcing a forward walk is how free look makes a body that faces its travel
     * play a walk clip; with the body facing the camera instead, a backward key really is a
     * back-pedal and a sideways key really is a sidestep, and the sideways walk drives both. */
    if (!free_state.aim_stance &&
        ((forward == 0.0f && strafe != 0.0f) || forward < 0.0f)) {
        strafe_walk_force_forward(record, forward < 0.0f);
    }

    /* The travel direction, for every substep the trigger is NOT held. While it is, the body is
     * already pointed at the camera above and the feet are handled by the sideways walk instead,
     * so this must not turn the body a second time. */
    if (!free_state.body_target_valid &&
        free_look_input_angle(strafe, forward, &input_angle)) {
        free_state.body_target           = free_look_wrap360(free_state.camera_yaw + input_angle);
        free_state.body_target_valid     = true;
        free_state.target_settle_seconds = free_state.config.body_settle_seconds;
        free_state.target_turn_rate      = free_state.config.body_turn_rate;
    }
    return true;
}

/* True while the trigger is held and the body is therefore pointed at the camera. The phase-2
 * thunk asks this to decide whether the feet belong to the sideways walk this substep. */
bool free_look_aim_stance(void)
{
    return free_look_is_steering() && free_state.aim_stance;
}

void free_look_integrate(uint8_t *record, float substep_seconds)
{
    float heading;
    float step;

    if (!free_state.installed || record == NULL) {
        return;
    }

    /* The attack is over when the engine clears the cell. It does that at the tail of the fire
     * handler, on the substep the bolt actually leaves, so a zero here is the release signal,
     * and dropping the lock with it stops the next attack from starting on this one's
     * residue. */
    if (free_state.aim_lock_valid && read_field(record, PLAYER_CHEST_CLAIM) == 0.0f) {
        free_state.aim_lock_valid = false;
    }

    if (free_state.aim_hold_seconds > 0.0f && substep_seconds > 0.0f) {
        free_state.aim_hold_seconds -= substep_seconds;
        if (free_state.aim_hold_seconds < 0.0f) {
            free_state.aim_hold_seconds = 0.0f;
        }
    }

    if (!free_state.body_target_valid) {
        return;
    }
    free_state.body_target_valid = false;

    heading = read_field(record, PLAYER_HEADING);
    step    = strafe_walk_damp_step(0.0f, free_look_wrap180(free_state.body_target - heading),
                                    substep_seconds, free_state.target_settle_seconds,
                                    free_state.target_turn_rate);
    if (!isfinite(step) || !isfinite(heading)) {
        return;
    }

    write_field(record, PLAYER_HEADING, free_look_wrap360(heading + step));
}

bool free_look_install(const player_sites_t *player, bool strafe_enabled)
{
    if (free_state.installed) {
        return true;
    }
    if (player == NULL || player->player_pointer == NULL) {
        log_warning("the player record did not resolve, so free look cannot be offered at all");
        return false;
    }

    if (!camera_sites_resolve(&free_state.camera, (const void *)player->player_pointer)) {
        log_warning("the follow camera in this build is not the one this feature knows, so free "
                    "look cannot be offered at all, not from the ini and not from the controls "
                    "screen. There is no honest half of it to run: writing the offset without "
                    "freezing the recentre gives a camera that slides home under the player's "
                    "hand. Mouse look still turns the body as it always did.");
        if (view_lead_is_enabled()) {
            log_warning("NewMouseInput=1 goes with it, for the same reason and not for one of its "
                        "own: the per-frame view lead is applied from a detour on this camera's "
                        "own update, so a camera that cannot be found is a camera whose drawn "
                        "angle cannot be reached.");
        }
        return false;
    }

    /* The local state is prepared BEFORE the hooks, because the moment the first detour stands the
     * hook can run and it reads both of these. `installed` is written last, so a hook that fires
     * between the two still finds the feature switched off and releases. */
    free_state.player          = player;
    free_state.drain_per_frame = mouse_look_is_accumulating();
    free_look_log_init(&free_state.camera, free_state.config.log_transitions);

    /* Nothing above this line has written a byte of the host, so a failure here leaves the process
     * exactly as it was and the feature simply never becomes active.
     *
     * The camera goes first and its failure abandons the whole feature, which is the only way to
     * keep that true. Detours here cannot be removed, so installing the attack pair after a failed
     * camera install would leave two live hooks in the host belonging to a feature that reports
     * itself as absent. */
    if (!free_look_camera_install(&free_state)) {
        return false;
    }
    free_look_aim_install(&free_state);

    free_state.installed = true;

    /* Armed whether free look is on or off, because the swing it watches for has been reported in
     * BOTH control modes. The detour it is sampled from stands either way. */
    camera_watch_install(&free_state.camera);

    /* The per-frame view lead belongs to MOUSE look, not to this feature, and it is armed from here
     * because this is where its two dependencies are both known: the mouse bank, which was decided
     * by mouse_look_install a moment ago, and the arm-select cell, which decides whether the drawn
     * yaw can be adjusted at all without feeding back into the next frame. It writes nothing here
     * and nothing later; it only answers a number of degrees that the camera detour above adds to
     * the yaw the engine has composed. */
    view_lead_install(free_state.drain_per_frame, free_state.config.rigid_mouse_look_camera &&
                                                  free_state.camera.last_interp != NULL);

    log_info("%s Body turn %.0f ms settle and %.0f deg/s cap, aim snap %s, sideways input %s.",
             free_state.config.enabled
                 ? "Free look is the live control mode. The mouse turns the camera and no longer "
                   "turns the body at all; the body turns toward the direction the player asks to "
                   "travel, measured from the camera. The camera turns without limit and is never "
                   "pulled back toward the body."
                 : "Free look is installed but switched off, so mouse look is the live control "
                   "mode. Both hooks are in place and both write nothing while it is off, the "
                   "two camera cells stay the engine's own, so it can be switched on in this "
                   "same session rather than at the next launch.",
             (double)(free_state.config.body_settle_seconds * MILLISECONDS_PER_SECOND),
             (double)free_state.config.body_turn_rate,
             free_state.config.aim_snap
                 ? (free_state.config.aim_keeps_movement && free_state.fire_shot_armed
                        ? "on, and it yields to your keys"
                        : "on, and it owns the walk while firing")
                 : "off",
             strafe_enabled ? "on" : "off (only forward and back are camera-relative)");

    /* The branch, named. A silent exit is a blind spot: without this line the two cases below look
     * identical in a log, and they are the difference between a camera that goes exactly where the
     * mouse asked and one that jumps up to a fifth of a turn whenever the body is also turning. */
    if (free_state.camera.last_interp != NULL) {
        log_info("the camera's yaw arm is FORCED at %08X: on armed frames the engine is told the "
                 "target heading did not move this frame, which makes it take the plain "
                 "heading-plus-offset arm instead of the eased one. The eased arm decides which "
                 "way to cross 0/360 from the sign of the body's own turn, which is meaningless "
                 "once the camera is decoupled from the body. The cost is that the engine's "
                 "one-frame easing of the camera YAW is gone while free look is armed, the yaw "
                 "becomes exactly what was asked for. The eye position keeps its own lag and the "
                 "pitch is untouched.", (unsigned)(uintptr_t)free_state.camera.last_interp);
    } else {
        log_warning("the cell that selects the camera's yaw arm did NOT resolve, so the engine "
                    "keeps choosing between its two arms itself. Free look works, but on any frame "
                    "the body is also turning the engine takes an EASED arm whose 0/360 handling "
                    "is derived from the body's turn direction, and with the camera decoupled "
                    "that is the wrong question. The visible symptom is the camera jumping a large "
                    "fraction of a turn when the wanted yaw is a few degrees across the seam. "
                    "This is the pre-existing behaviour, not a new fault.");
    }

    if (!free_state.drain_per_frame) {
        log_warning("the mouse bank is not live, so the camera yaw advances once per SUBSTEP "
                    "instead of once per rendered frame, the free look will feel stepped above "
                    "about 40 frames per second. Reading the axis a second time per frame is not "
                    "an option there: without the bank the read is live and unzeroed, so both "
                    "readers would receive the same sample and the turn would be doubled.");
    }
    log_warning("while free look is on the back-pedal clip is retired: the body faces the way it "
                "travels, so holding back is a half turn and a forward walk toward the camera. "
                "Walking into an authored fixed-camera region is noticed one frame late, because "
                "the engine chooses the region inside the camera update itself.");

    if (free_state.config.region_recover_degrees > 0.0f) {
        log_info("an authored camera region hands the camera back with up to %.0f degrees of the "
                 "engine's own recentre undone, so that walking across one does not quietly cost "
                 "the aim the player had. Past that the engine has genuinely re-aimed the shot and "
                 "its angle is kept. FreeLookRegionRecoverDeg=0 switches this off. A CUTSCENE "
                 "never recovers: it places the camera on purpose.",
                 (double)free_state.config.region_recover_degrees);
    } else {
        log_info("FreeLookRegionRecoverDeg=0, the camera is always picked up wherever the "
                 "engine's recentre had reached when an authored region let go, so crossing one "
                 "costs the aim the player had.");
    }

    if (free_state.config.log_transitions) {
        log_info("FreeLookLog=1, one line per CHANGE of the arming gate, never one per frame, and "
                 "at most %d of them. Each names the condition that fired, the camera region under "
                 "the player and its flags, the yaw on screen against the yaw free look wants, and "
                 "the camera PITCH and EYE HEIGHT. Free look writes neither of those two: when "
                 "they move, what moved them is the authored pitch and camera offset of the region "
                 "named on the same line.", free_look_log_line_budget());
    }
    return true;
}
