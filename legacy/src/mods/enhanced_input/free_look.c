/* free_look.c: the mouse turns the camera, the body turns toward where it travels.
 *
 * This file is the BODY half and the feature as a whole: its configuration, its live on/off
 * switch, the two detours on the attack path, and installation. The CAMERA half, the arming
 * gate, the release rules and the per-frame write into the two camera cells, is
 * free_look_camera.c, and the reasoning that belongs to the camera is at the top of that file.
 * The arithmetic is free_look_math.c and is tested without the game.
 *
 * ==============================================================================================
 * THE BODY HALF
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
 * SIZE NOTE (rule 9): 625 lines. The code is a few dozen stores; the rest is the reasoning, and
 * every paragraph of it records a mistake that was made or nearly made, writing from the frame
 * hook, leashing the camera to the body, treating a cutscene and a floor polygon as the same
 * release, correcting a twist against a moving reference, and four rounds of building on an
 * addition that only one of the engine's two yaw arms performs. None of that is visible in the
 * stores themselves.
 *
 * The camera half was lifted out when this file reached 1139 lines, 239 past the hard limit. An
 * earlier note here recommended moving only the arming gate, build_gate() and release_for(), which
 * would have been about 120 lines and would NOT have been enough, the whole camera half was.
 * The next seam, if one is ever needed, is the configuration block: it touches nothing but its own
 * struct, so it can move behind a single call that fills a free_look_config_t and needs no shared
 * state at all. Do not make room by deleting comments; rule 9 forbids it.
 */
#include "free_look.h"

#include "free_look_internal.h"

#include "camera_sites.h"
#include "camera_watch.h"
#include "free_look_log.h"
#include "free_look_math.h"
#include "mouse_look.h"
#include "player_record.h"
#include "player_sites.h"
#include "strafe_walk.h"

#include "common/detour.h"
#include "common/ini.h"
#include "common/logging.h"

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

/* How long the body keeps being driven to the camera after an attack starts. Long enough that a
 * held trigger keeps the body on the camera, short enough that it does not fight the next movement
 * input. Counted down in real seconds off the live substep, never in frames. */
#define AIM_SNAP_TAIL_SECONDS 0.35f

/* How far the upper body may be twisted away from the feet, and it is a real limit rather than a
 * taste value: the SAME number aims the shot and turns the chest, so whatever this allows is
 * exactly how far the weapon can point away from the direction of travel. That equality is the
 * whole point, a shot can never leave along a line the weapon is not on, at any setting.
 *
 * 90 lets a player strafing sideways shoot straight along the strafe. Beyond that the aim is
 * LIMITED rather than made inconsistent: running back-right while looking forward is 135 degrees,
 * and the shot then leaves at 90 with the weapon on that same line, instead of at 135 out of a
 * body facing elsewhere. */
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

typedef int32_t (__cdecl *auto_aim_fn_t)(int32_t kind);

/* The fire handler takes NOTHING: its prologue reads no argument slot, and its only caller reaches
 * it through a stored pointer with no push. A thunk declared with a parameter would leave the
 * stack believing one was there. */
typedef void (__cdecl *fire_shot_fn_t)(void);

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

/* Also catches NaN through the negated comparison, and turns either infinity into a bound, so a
 * finite pair of limits guarantees a finite result. */
static float clamp_float(float value, float minimum, float maximum)
{
    if (!(value >= minimum)) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

/* ==============================================================================================
 * Configuration. Every float is passed through clamp_float, which turns NaN into the minimum and
 * either infinity into a bound, so a finite pair of limits is what guarantees a finite setting.
 * ============================================================================================ */
void free_look_load_config(void)
{
    float settle_ms;

    /* OFF BY DEFAULT. This changes how the game plays, not how it looks. */
    free_state.config.enabled  = ini_read_bool(INPUT_SECTION, "FreeLook", false);
    free_state.config.aim_snap = ini_read_bool(INPUT_SECTION, "FreeLookAimSnap", true);
    free_state.config.aim_keeps_movement =
        ini_read_bool(INPUT_SECTION, "FreeLookAimKeepsMovement", true);
    free_state.config.aim_twist_max =
        ini_read_float(INPUT_SECTION, "FreeLookAimTwistMax", DEFAULT_AIM_TWIST_MAX);
    if (!(free_state.config.aim_twist_max >= 0.0f)) {
        free_state.config.aim_twist_max = DEFAULT_AIM_TWIST_MAX;
    }
    if (free_state.config.aim_twist_max > MAX_AIM_TWIST_MAX) {
        free_state.config.aim_twist_max = MAX_AIM_TWIST_MAX;
    }
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
    settle_ms = clamp_float(settle_ms, 0.0f, MAX_BODY_SETTLE_MS);
    free_state.config.body_settle_seconds = settle_ms / MILLISECONDS_PER_SECOND;

    free_state.config.body_turn_rate =
        ini_read_float(INPUT_SECTION, "FreeLookBodyTurnMaxDegPerSec", DEFAULT_BODY_TURN_RATE);
    free_state.config.body_turn_rate =
        clamp_float(free_state.config.body_turn_rate, MIN_BODY_TURN_RATE, MAX_BODY_TURN_RATE);

    /* Zero is a legitimate setting and means "never take the wanted yaw back": the camera is then
     * always picked up wherever the engine's recentre had got to, which is what this feature did
     * before the recovery existed. */
    free_state.config.region_recover_degrees =
        ini_read_float(INPUT_SECTION, "FreeLookRegionRecoverDeg", DEFAULT_REGION_RECOVER_DEG);
    free_state.config.region_recover_degrees =
        clamp_float(free_state.config.region_recover_degrees, 0.0f, MAX_REGION_RECOVER_DEG);

    free_state.config.log_transitions = ini_read_bool(INPUT_SECTION, "FreeLookLog", false);
}

bool free_look_is_installed(void)
{
    return free_state.installed;
}

bool free_look_is_enabled(void)
{
    return free_state.installed && free_state.config.enabled;
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

/* THE ONE NUMBER. Both the shot and the chest are driven from this and from nothing else, which is
 * what guarantees the weapon is always on the line the bolt leaves along.
 *
 * The bolt's direction is built inside the fire handler as `heading + [pPlayer+0x178]`, from a
 * heading read LIVE at that instant, several substeps after Plr_AutoAim ran. So the heading
 * swapped across Plr_AutoAim aims its search cone and nothing else; what decides where the shot
 * goes is the body's facing right now, plus this cell.
 *
 * `lock` is what Plr_AutoAim put in the cell ONCE, captured when it ran, a target bearing relative
 * to the CAMERA, because the camera yaw is the heading it was shown. Adding the camera-to-body
 * angle resolves both terms into the body's frame, which is what the bolt is built in.
 *
 * It is assigned, never accumulated, and that is not style. The handler this feeds is polled from
 * the tail of phase 1 on EVERY substep while an attack is armed; it returns early until the
 * animation marker fires, so a version that read the cell and added to it compounded once per
 * substep and saturated the clamp at every angle within a few frames. A player walking
 * forward-right fired at ninety degrees. Reading only the captured lock makes the result depend on
 * nothing that has already been written.
 *
 * Nothing is restored afterwards: the engine clears the cell itself before the fire handler
 * returns, which is also what phase 7 below uses as the signal that the attack is over. */
static float aim_offset_for(const uint8_t *record, float lock)
{
    float heading = read_field(record, PLAYER_HEADING);
    float total;

    if (!isfinite(heading) || !isfinite(lock)) {
        return lock;
    }
    total = free_look_wrap180(lock + free_look_wrap180(free_state.camera_yaw - heading));

    if (total >  free_state.config.aim_twist_max) { total =  free_state.config.aim_twist_max; }
    if (total < -free_state.config.aim_twist_max) { total = -free_state.config.aim_twist_max; }
    return total;
}

static void __cdecl hook_fire_shot(void)
{
    fire_shot_fn_t original = (fire_shot_fn_t)free_state.fire_shot_detour.original;
    uint8_t       *record;
    float          total;

    if (free_state.armed && free_state.camera_yaw_valid &&
        free_state.config.aim_keeps_movement) {
        record = player_sites_record(free_state.player);
        if (record != NULL) {
            total = aim_offset_for(record, free_state.aim_lock_valid ? free_state.aim_lock : 0.0f);
            if (isfinite(total)) {
                /* Rebuilt against the heading the body has RIGHT NOW: this handler is polled every
                 * substep and the body keeps turning toward its travel in between, while the bolt
                 * is built from a heading read at this very instant.
                 *
                 * No chest write here. This is phase 1, and phase 2's Plr_Steer writes that node
                 * unconditionally a moment later in the same substep, a twist written here could
                 * never be drawn. */
                write_field(record, PLAYER_CHEST_CLAIM, total);
            }
        }
    }
    original();
}

/* The auto-aim searches a sixteen-degree cone about the player's heading. Under free look the body
 * faces where the feet go, so without this a player running past an enemy while looking at it
 * would lock onto whatever is in front of the feet. The swap is exact: the heading is read twice
 * inside the original and both reads are covered by an offset across the one call.
 *
 * It is also the only byte-proven signal this DLL has that an attack has begun, so it is what
 * starts the aim snap. */
static int32_t __cdecl hook_auto_aim(int32_t kind)
{
    auto_aim_fn_t original = (auto_aim_fn_t)free_state.auto_aim_detour.original;
    uint8_t      *record;
    float         saved;
    int32_t       result;

    free_state.aim_hold_seconds = free_state.config.aim_snap ? AIM_SNAP_TAIL_SECONDS : 0.0f;

    record = player_sites_record(free_state.player);
    if (!free_state.armed || !free_state.camera_yaw_valid || record == NULL) {
        return original(kind);
    }

    saved = read_field(record, PLAYER_HEADING);
    write_field(record, PLAYER_HEADING, free_state.camera_yaw);
    result = original(kind);
    write_field(record, PLAYER_HEADING, saved);

    /* The lock is captured here and nowhere else. This is the one moment the engine's own target
     * bearing exists in the cell; from here on the cell is ours and is assigned, never read back.
     * Capturing rather than re-reading is what stops the per-substep poll from compounding.
     *
     * No chest write here either: phase 6 is followed by phase 2 of the next substep, whose
     * Plr_Steer writes that node unconditionally, so a twist written here lasts at most one
     * substep and usually not even that. */
    if (free_state.config.aim_keeps_movement && free_state.fire_shot_armed) {
        float lock = read_field(record, PLAYER_CHEST_CLAIM);
        float aimed;

        free_state.aim_lock       = isfinite(lock) ? lock : 0.0f;
        free_state.aim_lock_valid = true;

        aimed = aim_offset_for(record, free_state.aim_lock);
        if (isfinite(aimed)) {
            write_field(record, PLAYER_CHEST_CLAIM, aimed);
        }
    }
    return result;
}

bool free_look_steer(uint8_t *record, float mouse_step_degrees, float strafe, bool stand_mode,
                     float substep_seconds)
{
    uint32_t move_input;
    float    forward;
    float    input_angle = 0.0f;

    if (!free_state.installed || !free_state.armed || !free_state.camera_yaw_valid ||
        record == NULL) {
        return false;
    }

    /* The mouse turns the camera here and nowhere else. */
    free_state.camera_yaw = free_look_wrap360(free_state.camera_yaw + mouse_step_degrees);

    /* The model-root angle the sideways walk latches belongs to a body that faces the way it
     * looks. This body faces the way it travels, so the latch has to come home and stay there,
     * and it is a latch, so it has to be written down rather than merely forgotten. */
    (void)strafe_walk_release(record, substep_seconds);

    free_state.body_target_valid = false;

    /* The aim snap is deliberately NOT gated on Stand. It asks for one thing only, face the
     * camera, so it carries no travel direction and no drive sign, and an attack started in the
     * air has to point where the player is looking just as much as one started on the ground.
     * That is why this block sits above the Stand test rather than below it. */
    if (free_state.aim_hold_seconds > 0.0f && free_state.config.aim_snap) {
        free_state.body_target       = free_state.camera_yaw;
        free_state.body_target_valid = true;
        free_state.aim_stance        = free_state.config.aim_keeps_movement;
    } else {
        free_state.aim_stance = false;
    }

    /* The walk is driven in stand and nowhere else, and that gate is load-bearing well beyond the
     * animation: phase 2 also runs while shoving a crate, and that mode is only left when NEITHER
     * move bit is set, so a forced bit outside Stand would lock the player in it for good.
     *
     * The travel direction is Stand-gated with it, and deliberately so. Outside Stand the forced
     * forward drive is not in force, so a backward key is still a NEGATIVE SPEED along an
     * unchanged facing rather than a half turn, building the camera-relative angle there would
     * double-count the reversal and send the player the wrong way. Outside Stand the mouse
     * therefore moves the camera while the body holds its heading, unless an attack is live. */
    if (!stand_mode) {
        return true;
    }

    move_input = *(const uint32_t *)(record + PLAYER_MOVE_INPUT);
    forward    = (move_input & 1u) ? 1.0f : ((move_input & 2u) ? -1.0f : 0.0f);

    /* Tell the engine a forward walk is under way when the input does not already say so: a lone
     * sideways key leaves both move bits clear, and a backward key has to become a forward walk
     * in the new direction because the body is about to be turned to face it. Plain forward is
     * left completely alone, so walking forward is byte-for-byte what it always was.
     *
     * NOT WHILE AIMING. Forcing a forward walk is how free look makes a body that faces its travel
     * play a walk clip; with the body facing the camera instead, a backward key really is a
     * back-pedal and a sideways key really is a sidestep, and the sideways walk drives both. */
    if (!free_state.aim_stance &&
        ((forward == 0.0f && strafe != 0.0f) || forward < 0.0f)) {
        strafe_walk_force_forward(record, forward < 0.0f);
    }

    /* THE TRAVEL DIRECTION, for every substep the trigger is NOT held. While it is, the body is
     * already pointed at the camera above and the feet are handled by the sideways walk instead,
     * so this must not turn the body a second time. */
    if (!free_state.body_target_valid &&
        free_look_input_angle(strafe, forward, &input_angle)) {
        free_state.body_target       = free_look_wrap360(free_state.camera_yaw + input_angle);
        free_state.body_target_valid = true;
    }
    return true;
}

/* True while the trigger is held and the body is therefore pointed at the camera. The phase-2
 * thunk asks this to decide whether the feet belong to the sideways walk this substep. */
bool free_look_aim_stance(void)
{
    return free_state.installed && free_state.armed && free_state.aim_stance;
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
     * and dropping the lock with it is what stops the next attack from starting on this one's
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
                                    substep_seconds, free_state.config.body_settle_seconds,
                                    free_state.config.body_turn_rate);
    if (!isfinite(step) || !isfinite(heading)) {
        return;
    }

    write_field(record, PLAYER_HEADING, free_look_wrap360(heading + step));
}

/* ==============================================================================================
 * The two detours on the attack path. Both are OPTIONAL: each failure is a named degraded mode
 * rather than a refusal, because the camera half is already standing by the time these run and
 * there is no way to take a detour back out again.
 *
 * fire_shot BEFORE auto_aim, and that order is load-bearing: hook_auto_aim only captures the
 * engine's target lock when fire_shot_armed is already true, so the reverse order would silently
 * drop the lock for whichever attack happened to land in between.
 * ============================================================================================ */
static void install_attack_detours(void)
{
    /* This is what lets the aim snap stop stealing the walk. Without it the shot still leaves
     * along the BODY, so the snap has to keep turning the body to aim, and holding the trigger
     * keeps walking the player forward whatever key is pressed. */
    if (free_state.camera.fire_shot != 0 && free_state.config.aim_keeps_movement) {
        if (detour_install(&free_state.fire_shot_detour, free_state.camera.fire_shot,
                           (const void *)hook_fire_shot, PLAYER_FIRE_SHOT_PROLOGUE_SIZE)) {
            free_state.fire_shot_armed = true;
        } else {
            log_warning("the fire handler at %08X could not be detoured, the shot keeps following "
                        "the body, so the aim snap has to keep turning it and movement stays "
                        "locked while firing", (unsigned)free_state.camera.fire_shot);
        }
    }

    if (free_state.camera.auto_aim != 0 &&
        !detour_install(&free_state.auto_aim_detour, free_state.camera.auto_aim,
                        (const void *)hook_auto_aim, PLAYER_AUTO_AIM_PROLOGUE_SIZE)) {
        log_warning("Plr_AutoAim at %08X could not be detoured, the auto-aim cone stays centred "
                    "on the body and only the aim snap brings it round",
                    (unsigned)free_state.camera.auto_aim);
    }
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
                    "hand. Mouse look is untouched.");
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
    install_attack_detours();

    free_state.installed = true;

    /* Armed whether free look is on or off, because the swing it watches for has been reported in
     * BOTH control modes. The detour it is sampled from stands either way. */
    camera_watch_install(&free_state.camera);

    log_info("%s Body turn %.0f ms settle and %.0f deg/s cap, aim snap %s, sideways input %s.",
             free_state.config.enabled
                 ? "Free look is the live control mode. The mouse turns the camera and no longer "
                   "turns the body at all; the body turns toward the direction the player asks to "
                   "travel, measured from the camera. The camera turns without limit and is never "
                   "pulled back toward the body."
                 : "Free look is installed but switched off, so mouse look is the live control "
                   "mode. Both hooks are in place and both write nothing while it is off, the "
                   "two camera cells stay the engine's own, which is what lets the check box on "
                   "the controls screen switch the mode in this same session.",
             (double)(free_state.config.body_settle_seconds * MILLISECONDS_PER_SECOND),
             (double)free_state.config.body_turn_rate,
             free_state.config.aim_snap
                 ? (free_state.fire_shot_armed ? "on, and it yields to your keys"
                                               : "on, and it owns the walk while firing")
                 : "off",
             strafe_enabled ? "on" : "off (only forward and back are camera-relative)");

    /* THE BRANCH, NAMED. A silent exit is a blind spot: without this line the two cases below look
     * identical in a log, and they are the difference between a camera that goes exactly where the
     * mouse asked and one that jumps up to a fifth of a turn whenever the body is also turning. */
    if (free_state.camera.last_interp != NULL) {
        log_info("the camera's yaw ARM IS FORCED at %08X: on armed frames the engine is told the "
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
        log_info("FreeLookLog=1 - one line per CHANGE of the arming gate, never one per frame, and "
                 "at most %d of them. Each names the condition that fired, the camera region under "
                 "the player and its flags, the yaw on screen against the yaw free look wants, and "
                 "the camera PITCH and EYE HEIGHT. Free look writes neither of those two: when "
                 "they move, what moved them is the authored pitch and camera offset of the region "
                 "named on the same line.", free_look_log_line_budget());
    }
    return true;
}
