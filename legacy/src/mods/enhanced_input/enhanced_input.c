/* enhanced_input.c: mouse look and strafing, as a pure data patch on the player phase table.
 *
 * ==============================================================================================
 * Why this is two pointers and not a single line of patched code
 *
 * Plr_RunPhases does not call the player phases through a switch. It calls them through a pointer
 * table in .data:
 *
 *     0x4482E0   mov  ecx, [ebp-8]                     ; the phase index
 *                call dword ptr [ecx*4 + g_plrPhases]  ; INDIRECT, THROUGH .data
 *
 * 13 entries plus a terminator of 1. Entry 2 is Plr_Steer, entry 7 is Plr_Integrate, the only
 * place a frame's displacement is built. We swap exactly those two pointers for our own thunks.
 * No byte in the camera path, no byte in the integrator, no byte in the collision code.
 *
 * And it costs no hand-written mode test: five of the fourteen player modes skip these phases
 * anyway because their descriptors say so (hanging, shimmy, auto-vault, death, turret). Swimming
 * has its OWN phase-2 pointer in its descriptor, so the steering there is the engine's own.
 *
 * The two thunks do NOT cover the same modes, and that asymmetry is used deliberately below:
 * phase 7 additionally runs while swimming, in a launched sidestep and in a fixed jump, which is
 * exactly where a body angle written in Stand would otherwise be stranded.
 *
 * ---- why the camera does not appear here -----------------------------------------------------
 * bapview_followYaw sets cam = interpHeading + yawOffset. In the follow state the camera
 * direction IS the player direction, so turning `heading` has already turned the camera, with
 * the authored damping and all 176 authored camera regions intact.
 *
 * ---- why aiming follows by itself ------------------------------------------------------------
 * Plr_AutoAim searches with Thing_FindNearestInCone(actor, 2, player->heading, 16.0f, ...), and
 * the melee sweep moves the contact node on the body, which carries actorYaw, which follows
 * heading. Both hang off the same number the mouse now sets.
 *
 * ---- and the OTHER control mode, which inverts both of those ---------------------------------
 * The two paragraphs above describe mouse look, and mouse look is only one of the two schemes
 * this DLL can be in. With FreeLook=1 the mouse turns the CAMERA's own yaw offset instead and the
 * body is turned toward the direction the player asks to travel, measured from the camera, so
 * the camera is no longer the player direction, and aiming no longer follows by itself and is
 * carried across Plr_AutoAim deliberately. The two schemes are mutually exclusive on any one
 * SUBSTEP, free look is asked first and, when it takes the substep, nothing here writes a view
 * yaw or a travel angle. Which of them owns the substep is a LIVE setting, not a launch-time
 * choice: free_look.c installs its machinery either way and gates it on a plain bool, so the check
 * box on the controls screen switches the scheme in the same session. free_look.c holds the whole
 * of that; this file only routes the mouse step to whichever of the two owns the substep.
 *
 * ---- the strafe channel: the engine's own legs, pointed somewhere else ------------------------
 * A sideways displacement pushed into the integrator's conveyor surcharge is the obvious way to
 * strafe, and it was how this file used to do it. It is wrong, and the reason is one line in the
 * clip selector: holding only a sideways key leaves moveInput at 0 and curSpeed at 0, so
 * Plr_StandClipSelect (0x0044CD61, index 4 of the Stand descriptor) picks an IDLE clip. The player
 * slid sideways while standing still.
 *
 * So this file no longer moves the player at all. It tells the engine the player is WALKING, in
 * the engine's own two fields and with the engine's own arithmetic,
 *
 *     moveInput |= 1                      the forward bit the clip selector branches on
 *     moveDrive  = dtScale30 * 0.6        exactly what Plr_Steer writes for a full forward axis
 *
 * and then turns the direction that walk comes out in. Everything else follows for free: the
 * walk and run clips, the footsteps, the speed caps and their ramp, the acceleration, the turn
 * penalty and the collision, because all of it is now the engine walking rather than us shoving.
 *
 * ---- turning the walk, without turning the view ----------------------------------------------
 * Plr_Integrate builds the frame's displacement from sincos_deg(heading) INSIDE its own body, so
 * a heading that is offset only across that one call rotates the displacement and nothing else.
 * Afterwards heading is put back to the value the engine itself would have written, recomputed
 * with the engine's own formula and its own inputs:
 *
 *     heading = wrap360(turnWheel * frameDt + heading_before_the_offset)
 *
 * The camera never sees the offset. It is fed in phase 11 (Plr_PublishGround) and phase 1, both
 * outside the window, it takes the heading as an ARGUMENT, and it never reads the player record,
 * so the two-deep heading history it keeps for its substep interpolation only ever contains
 * restored values. What deliberately stays rotated is the displacement, desiredPos and moveDir.
 *
 * Knockback and the conveyor surcharge are added AFTER the heading term, in world axes, so they
 * are untouched by the offset, which is exactly right and is the reason this is done with a
 * heading offset rather than by rotating the finished displacement.
 *
 * ---- and the body is turned to match ---------------------------------------------------------
 * bapobj_setNodeYaw(hActor, 0, theta) rotates the model root. It is additive on the animation
 * rather than a replacement, and it propagates down the whole node hierarchy, so the ordinary walk
 * cycle plays while pointing where the player actually travels. The engine does the same thing to
 * the same node for its hit reaction. That angle is DAMPED rather than stepped, and it is walked
 * back down whenever the walk stops being driven; both live in strafe_walk.c, with the reasons.
 *
 * ---- the mouse is banked per FRAME, not read per substep --------------------------------------
 * The engine polls the device once per rendered frame and does it after that frame's substeps
 * have already run, so most of the hand's movement was being overwritten unread. mouse_look.c
 * banks every frame's sample and hands the whole bank to one substep. This thunk only asks for the
 * step and applies it.
 *
 * ---- and the upper body leans into the turn again --------------------------------------------
 * Plr_Steer ends by twisting chest and head about the model's up axis, turnWheel/12 and /10, from
 * the cell this file has to clear. Overwriting the cell afterwards is too late for that twist,
 * but the twist itself is two ABSOLUTE stores through bapobj_setNodeYaw, so steer_lean.c simply
 * re-issues them after the original with the turn the player actually made, through the engine's
 * own divisors and its own +-120 deg/s clamp. Nothing about movement, speed, the turn penalty or
 * collision is touched. The chest is skipped while the auto-aim claims it, because the weapon and
 * the blade hang off that node; the head has no other writer in the image.
 *
 * SIZE NOTE (rule 9): 865 lines. The two phase thunks are short and their correctness rests
 * entirely on facts about the engine that no reader can see from them, which fields the clip
 * selector branches on, where sincos_deg is taken, which modes run which phase. Rule 8 requires
 * that evidence at the site, and rule 9 forbids deleting it to reach a limit.
 * NEXT SEAM, measured rather than guessed: the CONFIGURATION half, load_config, its defaults,
 * its clamps and the three cross-checks that switch features off when a dependency is missing,
 * is 150 lines that touch the engine at no point and share only the config struct with the rest.
 * The thunks are not the seam: they read eleven fields of input_state between them.
 */
#include "enhanced_input.h"

#include "free_look.h"
#include "input_menu.h"
#include "mouse_look.h"
#include "steer_lean.h"
#include "steer_log.h"
#include "player_record.h"
#include "player_sites.h"
#include "strafe_walk.h"

#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/patch.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define INPUT_SECTION "enhanced_input"

typedef void (__cdecl *phase_fn_t)(void);

/* The damper's own two numbers. 250 ms to close 90 % of a gap sits between the median authored
 * NPC's first turn stage and its full stage, and clear of the player's own 120 deg/s turn clamp,
 * which would need three quarters of a second for a right angle. 240 deg/s is twice that clamp
 * and exists so that a large gap cannot spike on its first substep. */
#define DEFAULT_STRAFE_SETTLE_MS   250.0f
#define MAX_STRAFE_SETTLE_MS      1000.0f
/* The engine's own steady-state turn rate for a held key: its accumulator climbs to the 120 deg/s
 * clamp in seven substeps and stays there. Matching it rather than picking a taste value is what
 * makes "both features off" the original control scheme rather than an approximation of it. The one
 * difference is that ours is instant where the original ramps over about a fifth of a second. */
#define DEFAULT_KEY_TURN_RATE 120.0f

/* The engine's clamp on the turn cell, and BOTH input paths reach it. The keyboard ramp's ceiling
 * of 40 deg/s bounds the INCREMENT, not the cell: turnWheel += ramp * axis accumulates, so a held
 * key climbs 12, 26, 42, 60, 80, 102, 120 and saturates in seven substeps. An earlier comment read
 * that ceiling as the cell's own and sized this whole feature at a third of the truth. */
#define ENGINE_TURN_CLAMP 120.0f
#define MIN_KEY_TURN_RATE      15.0f
#define MAX_KEY_TURN_RATE     720.0f

#define DEFAULT_STRAFE_TURN_RATE   240.0f
#define MIN_STRAFE_TURN_RATE        30.0f
#define MAX_STRAFE_TURN_RATE      2000.0f
#define MILLISECONDS_PER_SECOND   1000.0f

typedef struct enhanced_input_config {
    bool  mouse_look;
    bool  strafe;
    bool  strafe_invert;
    bool  strafe_turns_body;
    bool  steer_lean;
    bool  restore_turn_rate;
    int   steer_log;
    float steer_lean_test_degrees;   /* whether the model is turned to face the way it travels */
    float strafe_settle_seconds;
    float strafe_turn_rate;    /* degrees per second, the damper's hard rate cap */

    /* Degrees per second A and D turn the player while sideways walking is OFF. It exists because
     * mouse look has to clear the engine's own turn cell; that cell carries the mouse too, so
     * the keyboard's share has to be re-applied from here or the keys go dead. */
    float key_turn_rate;
} enhanced_input_config_t;

typedef struct enhanced_input_state {
    bool                installed;
    enhanced_input_config_t  config;

    player_sites_t      sites;
    phase_fn_t          original_steer;
    phase_fn_t          original_integrate;

    /* What phase 2 saw and phase 7 is to consume. One frame of lifetime, not state. */
    bool               turn_wheel_is_ours;
    bool  pending_valid;
    float pending_yaw_degrees;
    float pending_travel_degrees;   /* how far the walk is turned off the body's heading */

    /* Set by phase 2, read and cleared by phase 7. It is how phase 7 knows whether the body angle
     * has already been stepped this substep: both phases run in Stand, but only phase 7 runs while
     * swimming or in a launched sidestep, and the angle must be stepped exactly once either way. */
    bool  steer_ran_this_substep;

    bool  logged_steer;
    bool  logged_integrate;
    bool  logged_first_input;
    bool  warned_about_mode;
} enhanced_input_state_t;

static enhanced_input_state_t input_state;

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

static void load_config(void)
{
    float settle_ms;

    input_state.config.mouse_look        = ini_read_bool (INPUT_SECTION, "MouseLook", false);
    input_state.config.strafe            = ini_read_bool (INPUT_SECTION, "Strafe", false);
    input_state.config.strafe_invert     = ini_read_bool (INPUT_SECTION, "StrafeInvert", false);
    input_state.config.strafe_turns_body = ini_read_bool (INPUT_SECTION, "StrafeTurnsBody", true);

    /* Zero is a legitimate setting and means "no damping": the angle steps between 0, +-45 and
     * +-90 the way it used to, which is what makes the two comparable in one build. */
    settle_ms = ini_read_float(INPUT_SECTION, "StrafeSettleMs", DEFAULT_STRAFE_SETTLE_MS);
    settle_ms = clamp_float(settle_ms, 0.0f, MAX_STRAFE_SETTLE_MS);
    input_state.config.strafe_settle_seconds = settle_ms / MILLISECONDS_PER_SECOND;

    input_state.config.steer_lean = ini_read_bool(INPUT_SECTION, "SteerLean", true);
    input_state.config.restore_turn_rate =
        ini_read_bool(INPUT_SECTION, "RestoreTurnRate", true);
    input_state.config.steer_lean_test_degrees =
        clamp_float(ini_read_float(INPUT_SECTION, "SteerLeanTestDegrees", 0.0f), -90.0f, 90.0f);
    input_state.config.steer_log = ini_read_int(INPUT_SECTION, "SteerLog", 0);
    if (input_state.config.steer_log < 0)    { input_state.config.steer_log = 0; }
    if (input_state.config.steer_log > 4000) { input_state.config.steer_log = 4000; }

    input_state.config.strafe_turn_rate =
        ini_read_float(INPUT_SECTION, "StrafeTurnRate", DEFAULT_STRAFE_TURN_RATE);
    input_state.config.strafe_turn_rate = clamp_float(input_state.config.strafe_turn_rate,
                                                      MIN_STRAFE_TURN_RATE, MAX_STRAFE_TURN_RATE);

    input_state.config.key_turn_rate =
        ini_read_float(INPUT_SECTION, "KeyTurnRate", DEFAULT_KEY_TURN_RATE);
    input_state.config.key_turn_rate = clamp_float(input_state.config.key_turn_rate,
                                                   MIN_KEY_TURN_RATE, MAX_KEY_TURN_RATE);

    mouse_look_load_config();
    free_look_load_config();

    /* StrafeSpeed used to set a sideways speed in units per second, because the sidestep was a
     * displacement this DLL pushed in itself. It is now the engine walking, so the speed is the
     * gait the player is in and there is nothing left for the key to set. Saying so beats leaving
     * a setting that quietly does nothing. */
    if (ini_read_float(INPUT_SECTION, "StrafeSpeed", -1.0f) >= 0.0f) {
        log_info("StrafeSpeed is no longer used and can be deleted. Sideways movement is now the "
                 "engine's own walk or run, so it matches the gait exactly.");
    }
}

static float read_field(const uint8_t *record, int offset)
{
    return *(const float *)(record + offset);
}

static void write_field(uint8_t *record, int offset, float value)
{
    *(float *)(record + offset) = value;
}

/* ==============================================================================================
 * PHASE 2, steering
 *
 * The keyboard axis is read OURSELVES and before the original. Reading does NOT consume anything:
 * the query API never touches a device, and an earlier version of this comment claimed it did. The
 * original therefore sees the same value we did and builds its own turnWheel from it, which does
 * not matter, because we overwrite turnWheel afterwards. Reading first is simply the point at which
 * the value is still ours to interpret.
 *
 * The view step does not come from a read at all any more. mouse_look.c banks the axis once per
 * rendered frame and hands the whole bank over here, and taking it is what marks a substep as
 * having consumed, so it is done unconditionally, at the very top, before any gate can return.
 * ============================================================================================ */
static void __cdecl steer_thunk(void)
{
    uint8_t *record = player_sites_record(&input_state.sites);
    float    mouse_step;
    float    keyboard_axis = 0.0f;
    float    substep_seconds = 0.0f;
    float    axis;
    float    strafe;
    float    engine_rate = 0.0f;
    bool     phase_active = false;
    bool     stand_mode = false;
    int      mode_for_log = -1;

    /* The bank is consumed on EVERY run of this thunk, before any gate: a step left in it would be
     * applied later, in a state that did not earn it. The value is simply discarded below when the
     * gate is closed. */
    mouse_step = mouse_look_take_step_degrees();

    if (!input_state.logged_steer) {
        input_state.logged_steer = true;
        log_info("phase 2 ran for the first time (player record %08X)",
                 (unsigned)(uintptr_t)record);
    }

    if (record != NULL) {
        /* Plausibility bolt: the mode index is 0..13. Anything else means the record is not (yet)
         * what we take it for, then we touch nothing and let only the original run. Better no
         * mouse control than a shot into foreign memory. */
        int mode = player_sites_mode_index(&input_state.sites, record);

        if (mode < 0 || mode > PLAYER_MODE_MAX) {
            if (!input_state.warned_about_mode) {
                input_state.warned_about_mode = true;
                log_warning("mode index %d is outside 0..%d, mouse look and strafe stay out of "
                            "this frame", (int)mode, PLAYER_MODE_MAX);
            }
        } else {
            phase_active = (mode != PLAYER_MODE_SIDLE && mode != PLAYER_MODE_FIXED_JUMP);
            /* The walk is driven in stand and nowhere else, and this gate is load-bearing well
             * beyond the animation. Phase 2 also runs while shoving a crate, and Plr_UpdatePushBlock
             * leaves that mode only when NEITHER move bit is set, a forced forward bit would push
             * the crate on a sideways key and lock the player in the mode for good. It also keeps
             * the forced bit out of the air.
             *
             * What it does not do, because this used to claim the opposite: it does not keep the
             * forced bit away from the sabre action selector. That selector is Stand-ONLY; both
             * of its call sites inside the phase-6 tick compare the mode descriptor against Stand's
             * before calling it, so gating on Stand puts the forced bit in front of it, not
             * behind it. While a sideways key is held the selector therefore reads a set forward
             * bit and picks the moving swing over the standing one. The parry arm reads a different
             * bit and is untouched. That is a real change in which clip plays, and it is named
             * rather than hidden. */
            stand_mode = (mode == PLAYER_MODE_STAND);
            mode_for_log = mode;
        }
    }

    if (phase_active) {
        substep_seconds = read_field(record, PLAYER_FRAME_DELTA);
        /* Read whether or not sideways walking is on: with it OFF the same axis is what turns the
         * player, and this used to be the read that was skipped, which is why A and D went dead
         * rather than merely stopping strafing. Reading consumes nothing; the query API never
         * touches a device. */
        if (input_state.sites.read_absolute_axis != NULL) {
            keyboard_axis = input_state.sites.read_absolute_axis(0);
        }
    }

    input_state.original_steer();

    input_state.pending_valid          = false;
    input_state.pending_yaw_degrees    = 0.0f;
    input_state.pending_travel_degrees = 0.0f;
    if (!phase_active) {
        /* The flag is deliberately left alone here: this substep did NOT step the body angle, so
         * phase 7 has to. That is the case of a launched sidestep or a scripted jump, where this
         * phase runs and declines while phase 7 goes on running. */
        steer_log_substep(record, STEER_BRANCH_DECLINED, substep_seconds, 0.0f, 0.0f, mode_for_log);
        return;
    }

    /* NEGATED, and this is the whole of the "A and D are the wrong way round" bug. The digital
     * turn axis is POSITIVE FOR LEFT, the shipped defaults bind the left arrow and NUMPAD4
     * without the invert flag and the right arrow and NUMPAD6 with it, while this file counts
     * right as positive. Treating the axis as if positive meant right sidestepped the wrong way
     * for every key bound to it. */
    axis   = -clamp_float(keyboard_axis, -1.0f, 1.0f);
    strafe = input_state.config.strafe_invert ? -axis : axis;

    /* The mutual exclusion between free look and the mouse-TO-BODY PATH, and it is the reason free
     * look lives in this DLL rather than beside it. Free look turns the CAMERA with the mouse and
     * turns the body toward where it travels. If the branch below also added the same step to the
     * heading, the mouse would turn body and camera together, the decoupling would be exactly
     * zero, and every log line would still claim a working feature. So when free look takes the
     * substep, nothing else here writes a view yaw or a travel angle. */
    if (free_look_steer(record, mouse_step, strafe, stand_mode, substep_seconds)) {
        /* The cell is zeroed AFTER the lean has read it, not before: free look turns the CAMERA
         * with the mouse, and a turn rate left standing would make the engine turn the BODY's
         * heading on top of it in phase 7, the very coupling free look exists to break. */
        input_state.turn_wheel_is_ours = false;
        /* No lean under free look, and that is a decision rather than an omission. Here the mouse
         * turns the CAMERA and the body turns toward where it travels, so a mouse-driven twist
         * would be body language for a turn the body never made. The body's own turn already leans
         * through the sideways walk's root rotation, which chest and head hang off. */
        /* No twist under free look yet, and the reason is written down because the decision has
         * now been made twice on two different grounds.
         *
         * Reissuing the engine's own value here was tried and reverted before it shipped: on this
         * branch that cell carries OUR MOUSE, because the engine's analog arm reads the very same
         * device axis mouse_look.c does, and under free look the mouse is the CAMERA. Twisting
         * the chest with it is body language for a turn the body never made, which is exactly what
         * this comment used to claim it was avoiding while doing it.
         *
         * The right input here is the BODY's own turn: the damped root angle the sideways walk
         * drives toward the travel direction. That is separate work and is not faked meanwhile. */
        /* Nothing twists the chest here any more, and the absence is the design rather than an
         * omission. There used to be a correction re-issued on this line, because the original
         * writes that node unconditionally a few instructions earlier and anything written in
         * phase 1 or phase 6 is overwritten before it can be drawn. The correction was needed
         * while free look pointed the body at its travel: the aim then sat at an angle to the
         * body that changed on every direction change, and the pose never settled because it was
         * being recomputed each substep against a body that was itself still turning. Pointing
         * the body at the camera while the trigger is held removes the angle instead of damping
         * it, so there is nothing left to re-issue. */
        /* While the trigger is held the feet belong to the sideways walk, not to free look's own
         * travel turn. The body is already facing the camera, so a sideways key is a real sidestep
         * and a backward key a real back-pedal, the same thing this DLL does with free look
         * switched off, which is what makes firing feel identical in both schemes. */
        if (free_look_aim_stance() && stand_mode && input_state.config.strafe) {
            input_state.pending_travel_degrees =
                strafe_walk_drive(record, strafe, substep_seconds);
        } else {
            strafe_walk_release(record, substep_seconds);
        }
        steer_lean_release();
        write_field(record, PLAYER_TURN_WHEEL, 0.0f);
        input_state.steer_ran_this_substep = true;
        input_state.pending_valid          = true;
        steer_log_substep(record, STEER_BRANCH_FREE_LOOK, substep_seconds, mouse_step,
                          input_state.pending_travel_degrees, mode_for_log);
        return;
    }

    if (input_state.config.mouse_look) {
        input_state.pending_yaw_degrees = mouse_step;

        /* ---- And the keys still turn, which this used to destroy -------------------------------
         *
         * The turn rate has to be cleared: the engine's axis 0 carries the MOUSE as well as the
         * keys, so leaving it standing would integrate the mouse a second time, and the engine's
         * turn penalty (0.86..1.0) would then brake the player on every fast mouse movement.
         *
         * But clearing it also threw away the KEYBOARD's turn, and with sideways walking switched
         * off nothing else consumed that axis, so A and D did nothing at all. That is not a
         * trade-off, it is a hole: turning off both of this DLL's optional features is supposed to
         * leave the original control scheme plus mouse look, and the original turns with A and D.
         *
         * So the keyboard's share is folded back in here, on the same path as the mouse, and the
         * cell stays zero. One writer of the view direction, both sources routed through it, and
         * no double count, the mouse cannot arrive twice because the cell it would arrive in is
         * the one being zeroed.
         *
         * The rate is the engine's own ceiling for the player, and the sign is the axis's: the
         * digital turn axis is POSITIVE FOR LEFT, which is also the direction increasing heading
         * turns, so it is added unnegated. (The sideways walk negates it because THAT file counts
         * right as positive; this does not.) */
        if (!input_state.config.strafe && keyboard_axis != 0.0f) {
            input_state.pending_yaw_degrees +=
                clamp_float(keyboard_axis, -1.0f, 1.0f) *
                input_state.config.key_turn_rate * substep_seconds;
        }

        /* ---- The turn cell, and why it is no longer touched -----------------------------------
         *
         * turnWheel is not just the integrator's input. Nine readers take it as the TURN PENALTY on
         * speed, and Plr_PublishGround hands it to the camera, where its zero test decides whether
         * the follow camera eases or tracks rigidly. Writing 0 switched all of that off. */
        /* READ BEFORE OVERWRITING. This is the value the original just computed, the ramped
         * keyboard turn or the clamped mouse accumulation, and it is the authentic input for the
         * upper-body twist. Two lines further down the cell stops holding it. */
        engine_rate = read_field(record, PLAYER_TURN_WHEEL);

        if (input_state.config.restore_turn_rate) {
            /* NOTHING IS WRITTEN, and that is less than this used to do and more correct.
             *
             * Writing our own rate was the second mistake here. the cell is an accumulator: the
             * original does turnWheel += ramp * axis, so a held key climbs 12, 26, 42, 60, 80,
             * 102, 120 across seven substeps, and that climb IS the engine's ease-in, the thing
             * the upper-body twist is supposed to show. Writing a finished 120 into it poisons the
             * next substep and flattens the ease-in to a step.
             *
             * So the engine's own value stands. Every consumer gets what the original would have
             * given it, and the double integration that causes is subtracted in phase 7 from a
             * LIVE read of the same two fields. */
            input_state.turn_wheel_is_ours = true;
        } else {
            write_field(record, PLAYER_TURN_WHEEL, 0.0f);
        }

        /* And the upper body leans into it again. The original has already twisted chest and head
         * from its own turn cell, which in this mode is not the turn the player made. The setter
         * is an absolute store, so re-issuing both writes here replaces those values outright,
         * no fight over the cell above, and no effect on movement, speed or collision.
         *
         * It is driven from pending_yaw_degrees rather than from the mouse step, because the
         * keyboard's share has been folded into it three lines up: A and D turn in this mode, so
         * A and D must lean too. */
        steer_lean_apply(record, engine_rate,
                         (substep_seconds > 0.0f)
                             ? input_state.pending_yaw_degrees / substep_seconds : 0.0f,
                         substep_seconds);
    } else {
        /* Not our turn to drive it: the original's own twist is standing and is correct, so the
         * only thing to drop is the damper's memory. */
        steer_lean_release();
    }

    if (!input_state.config.strafe || !stand_mode) {
        /* Not driving the walk this substep, so the model root has to come home rather than keep
         * the last angle Stand wrote into it. */
        strafe_walk_release(record, substep_seconds);
    } else {
        input_state.pending_travel_degrees = strafe_walk_drive(record, strafe, substep_seconds);
    }

    input_state.steer_ran_this_substep = true;
    input_state.pending_valid          = true;

    steer_log_substep(record, input_state.config.mouse_look ? STEER_BRANCH_MOUSE_LOOK
                                                            : STEER_BRANCH_PASSIVE,
                      substep_seconds, input_state.pending_yaw_degrees,
                      input_state.pending_travel_degrees, mode_for_log);
}

/* ==============================================================================================
 * PHASE 7, integrating
 *
 * The view turn happens BEFORE the original, because the original consumes it: it wraps heading to
 * 0..360 right afterwards itself (0x44A6C5, call wrap360). So we do not wrap and do not clean up.
 *
 * The travel turn WRAPS the original, and that is the whole trick of this feature. The integrator
 * takes sincos_deg(heading) inside its own body, so a heading that is offset across exactly that
 * one call sends the displacement somewhere else while leaving every other consumer of heading,
 * the camera above all, looking at the value it always had.
 *
 * This phase also brings the body angle home, and that is a second reason it is thunked. Phase 2
 * does not run in every mode phase 7 runs in: swimming (whose descriptor replaces phase 2 with a
 * steer of its own), a launched sidestep and a fixed jump are the three. The model root is a
 * latch, so an angle written in Stand would otherwise stay on the model for the whole of whatever
 * came next. The release only ever touches the node while a non-zero angle of OURS is still on it,
 * so it unwinds our own latch and nothing else, in swimming, where this DLL never writes an
 * angle in the first place, it does nothing at all. That is the deliberate answer to a genuinely
 * new reach: the phase-7 thunk can see modes the phase-2 thunk never did.
 * ============================================================================================ */

static void __cdecl integrate_thunk(void)
{
    uint8_t *record = player_sites_record(&input_state.sites);
    float    travel = 0.0f;
    float    heading_before = 0.0f;
    float    frame_delta = 0.0f;
    bool     turning_travel = false;

    if (!input_state.logged_integrate) {
        input_state.logged_integrate = true;
        log_info("phase 7 ran for the first time (player record %08X)",
                 (unsigned)(uintptr_t)record);
    }

    if (record != NULL) {
        frame_delta = read_field(record, PLAYER_FRAME_DELTA);
    }

    if (record != NULL && input_state.pending_valid) {
        /* The view step, minus what the original is about to add by itself.
         *
         * Phase 2 now leaves a real turn rate in the cell instead of a zero, so the original's own
         * `heading += turnWheel * frameDt` is no longer a no-op and would turn the view a second
         * time. Both fields are read LIVE, here, rather than remembered from phase 2: Plr_UpdateFall
         * and Plr_EnterSidle can zero the cell in between, and a remembered value would then steer
         * the view BACKWARDS by a term the engine never applied.
         *
         * Gated on having written the cell ourselves. The free-look branch and MouseLook=0 both
         * leave a zero there deliberately, and correcting for a term that is not coming would be
         * the same bug with the sign flipped. */
        float applied = input_state.pending_yaw_degrees;

        if (input_state.turn_wheel_is_ours) {
            applied -= read_field(record, PLAYER_TURN_WHEEL) *
                       read_field(record, PLAYER_FRAME_DELTA);
        }
        if (applied != 0.0f) {
            write_field(record, PLAYER_HEADING, read_field(record, PLAYER_HEADING) + applied);
        }

        travel = input_state.pending_travel_degrees;
        if (travel != 0.0f && frame_delta > 0.0f && frame_delta < 0.5f) {
            turning_travel = true;
            heading_before = read_field(record, PLAYER_HEADING);
            write_field(record, PLAYER_HEADING, heading_before + travel);
        }

        if (!input_state.logged_first_input &&
            (input_state.pending_yaw_degrees != 0.0f || travel != 0.0f)) {
            input_state.logged_first_input = true;
            log_info("first input processed, view %+.2f deg, travel %+.1f deg off the body, "
                     "curSpeed %.2f, dt %.4f",
                     (double)input_state.pending_yaw_degrees, (double)travel,
                     (double)read_field(record, PLAYER_CURRENT_SPEED), (double)frame_delta);
        }
    }

    /* Cleared before the original rather than after it, so that a mode which runs phase 7 without
     * phase 2, a launched sidestep, a scripted jump, can never consume a value phase 2 left
     * behind in an earlier substep. */
    input_state.pending_valid = false;
    input_state.turn_wheel_is_ours = false;

    /* Exactly one damper step per substep: phase 2 takes it when it ran, this phase when it did
     * not. Read and cleared here so a substep that phase 2 skipped is never skipped twice. */
    if (!input_state.steer_ran_this_substep) {
        strafe_walk_release(record, frame_delta);
    }
    input_state.steer_ran_this_substep = false;

    /* Free look's body turn, and it goes in BEFORE the original for the same reason the view turn
     * does: the integrator takes sincos of the heading inside its own body. Unlike the sideways
     * walk's travel offset this write is NOT undone afterwards, the body has really turned, and
     * the camera anchor, the vault probe and the model's own world yaw all have to see it. */
    free_look_integrate(record, frame_delta);

    input_state.original_integrate();

    if (turning_travel) {
        strafe_walk_restore_heading(record, heading_before);
    }
}

/* ============================================================================================ */
static bool swap_phase_pointers(void)
{
    input_state.original_steer     = (phase_fn_t)input_state.sites.phase_table[PHASE_STEER];
    input_state.original_integrate = (phase_fn_t)input_state.sites.phase_table[PHASE_INTEGRATE];

    if (patch_write_pointer32((uintptr_t)&input_state.sites.phase_table[PHASE_STEER],
                              (const void *)steer_thunk) != PATCH_RESULT_OK) {
        log_error("phase entry %d is not writable, nothing changed", PHASE_STEER);
        return false;
    }
    if (patch_write_pointer32((uintptr_t)&input_state.sites.phase_table[PHASE_INTEGRATE],
                              (const void *)integrate_thunk) != PATCH_RESULT_OK) {
        patch_write_pointer32((uintptr_t)&input_state.sites.phase_table[PHASE_STEER],
                              (const void *)input_state.original_steer);
        log_error("phase entry %d is not writable, entry %d was rolled back",
                  PHASE_INTEGRATE, PHASE_STEER);
        return false;
    }

    return true;
}

void enhanced_input_install(void)
{
    log_init("enhanced_input", false);

    if (input_state.installed) {
        return;
    }
    if (!host_image_resolve()) {
        log_error("no 32-bit host image, mouse look and strafe stay OFF");
        return;
    }

    load_config();

    /* Strafe WITHOUT mouse look is impossible, and that is a byte finding rather than a taste:
     * turnWheel (+0x2A4) is the ONLY turn channel. Mouse and keyboard exclude each other in the
     * original, but both land there. Turning the keyboard axis into strafing means clearing
     * turnWheel, which clears the mouse with it. Without mouse look a character would be left
     * that cannot turn at all. */
    if (input_state.config.strafe && !input_state.config.mouse_look) {
        log_warning("Strafe=1 requires MouseLook=1 (turnWheel is the only turn channel) - "
                    "strafe is switched OFF");
        input_state.config.strafe = false;
    }
    if (!input_state.config.mouse_look) {
        /* MouseLook is the master switch, so nothing below this line runs, including the check
         * boxes on the controls screen. Both of them drive settings that need this DLL's two phase
         * thunks, which are not installed here, so the screen is left exactly as it shipped rather
         * than given switches that could not do anything. */
        log_info("MouseLook=0, the original tank controls are untouched, and the controls screen "
                 "gets no check boxes: sideways walking and free look both need the phase thunks, "
                 "which are not installed in this mode");
        return;
    }

    if (!player_sites_resolve(&input_state.sites)) {
        return;
    }
    if (input_state.config.strafe && input_state.sites.read_absolute_axis == NULL) {
        input_state.config.strafe = false;
    }
    if (!input_state.config.strafe) {
        log_warning("strafe is off, keyboard axis 0 (turn left/right in the original) therefore "
                    "does nothing at all now, because the mouse has taken over turning");
    }

    strafe_walk_bind(input_state.sites.set_node_yaw,
                     input_state.config.strafe_turns_body,
                     input_state.config.strafe_settle_seconds,
                     input_state.config.strafe_turn_rate);

    steer_lean_bind(input_state.sites.set_node_yaw, input_state.config.steer_lean);
    steer_lean_set_test_degrees(input_state.config.steer_lean_test_degrees);

    if (!swap_phase_pointers()) {
        return;
    }

    input_state.installed = true;

    /* AFTER the phase table, never before: both of these ask whether this DLL is really driving
     * anything, and until the two pointers are in place the honest answer is no. */
    mouse_look_install(input_state.sites.read_relative_axis);

    /* And free look after mouse look, which is an ordering constraint that lives in no type
     * system. Free look drains the per-frame mouse bank a second time, once per rendered frame, so
     * that the camera advances on every frame rather than only on the frames that run a substep,
     * and that is only safe while the bank is live, because the degraded path answers a live,
     * unzeroed sample that two readers would both receive. Whether the bank is live is not decided
     * until mouse_look_install has returned. Called the other way round the feature still installs
     * and still works one substep at a time, and says so, so a future reordering degrades rather
     * than doubling the turn. */
    (void)free_look_install(&input_state.sites, input_state.config.strafe);

    if (input_state.sites.read_absolute_axis != NULL) {
        input_menu_install();
    } else {
        log_warning("no check box on the controls screen, the keyboard axis reader did not "
                    "resolve, so there is no key sideways walking could be driven from");
    }

    log_info("the live control mode is %s. Mouse look on (%.3f deg per mouse count, banked per "
             "frame=%d), strafe %s (inverted=%d, turns the body=%d, %.0f ms settle, %.0f deg/s "
             "cap). Sideways movement is the engine's own walk or run, so it matches the gait. "
             "Driven in Stand only; Sidle and FixedJump are excluded, and hanging, shimmy, death, "
             "turret and swimming the engine excludes itself.",
             free_look_is_enabled() ? "FREE LOOK (the mouse turns the camera, the body turns "
                                      "toward its travel)"
                                    : "MOUSE LOOK (the mouse turns the body, the camera follows)",
             (double)mouse_look_degrees_per_count(), mouse_look_is_accumulating() ? 1 : 0,
             input_state.config.strafe ? "on" : "off",
             input_state.config.strafe_invert ? 1 : 0,
             input_state.config.strafe_turns_body ? 1 : 0,
             (double)(input_state.config.strafe_settle_seconds * MILLISECONDS_PER_SECOND),
             (double)input_state.config.strafe_turn_rate);
    if (steer_lean_is_active()) {
        log_info("the upper body leans into the turn, from the ENGINE'S OWN number: the turn cell "
                 "as the original just left it. That cell ACCUMULATES - a held key climbs 12, 26, "
                 "42, 60, 80, 102 to the 120 deg/s clamp over seven substeps, which is 1.0 up to "
                 "10.0 degrees of chest and is the engine's own ease-in. MOUSE LOOK ONLY: under "
                 "free look that cell carries the mouse, and there the mouse is the camera. The "
                 "chest is skipped while the auto-aim claims it.");
    } else {
        log_info("SteerLean=0, chest and head keep whatever twist the original computed from a "
                 "turn cell this mode has to clear, so the upper body stays centred");
    }
    steer_log_configure(input_state.config.steer_log);

    if (input_state.config.restore_turn_rate) {
        log_info("the engine's turn cell gets the real rate back (clamped to its own 120 deg/s) "
                 "instead of a zero, so the speed penalty on turning and the follow camera's own "
                 "eased arm are the engine's again. The view still turns as fast as the hand does; "
                 "the double integration is taken out in phase 7. RestoreTurnRate=0 reverts it.");
    } else {
        log_info("RestoreTurnRate=0, the turn cell is zeroed as before, so turning costs no speed "
                 "and the follow camera stays on its rigid arm");
    }
}

/* ==============================================================================================
 * What the two check boxes on the controls screen drive
 *
 * Both are settings of the mouse-look scheme rather than switches for the DLL itself, and both are
 * live because their machinery is installed unconditionally and gated by a plain bool: the phase
 * thunks for sideways walking, the two camera detours for free look. Neither setter patches a byte
 * of the host, and each refuses, and says why, when what it drives cannot run in this session,
 * so a switch can never claim more than the DLL can deliver.
 *
 * Neither box exists at all when MouseLook=0, because install returns before the menu is patched.
 * ============================================================================================ */
bool enhanced_input_is_active(void)
{
    return input_state.installed;
}

bool enhanced_input_strafe_enabled(void)
{
    return input_state.config.strafe;
}

void enhanced_input_set_strafe(bool enabled)
{
    if (input_state.config.strafe == enabled) {
        return;
    }
    if (enabled && !input_state.installed) {
        log_warning("sideways walking was switched on, but the player phases are not hooked - "
                    "the setting is not applied and not saved");
        return;
    }
    if (enabled && input_state.sites.read_absolute_axis == NULL) {
        log_warning("sideways walking was switched on, but the keyboard axis reader did not "
                    "resolve, so there is no key to read, the setting is not applied and not "
                    "saved");
        return;
    }

    input_state.config.strafe = enabled;

    /* The angle latched in the model root belongs to the feature that is being switched off, and
     * nothing would come back to walk it down, so it is dropped here and the next driven substep
     * starts from zero. The pending pair is one substep of lifetime and is cleared with it. */
    strafe_walk_reset();
    input_state.pending_valid          = false;
    input_state.pending_travel_degrees = 0.0f;

    if (!ini_write_int(INPUT_SECTION, "Strafe", enabled ? 1 : 0)) {
        log_warning("sideways walking is now %s, but the setting could not be written to the ini "
                    "and will be back to its old value on the next launch",
                    enabled ? "on" : "off");
        return;
    }
    log_info("sideways walking switched %s from the controls screen and saved",
             enabled ? "on" : "off");
}

bool enhanced_input_free_look_available(void)
{
    return input_state.installed && free_look_is_installed();
}

bool enhanced_input_free_look_enabled(void)
{
    return free_look_is_enabled();
}

void enhanced_input_set_free_look(bool enabled)
{
    if (free_look_is_enabled() == enabled) {
        return;
    }
    if (!input_state.installed) {
        log_warning("free look was switched on, but the player phases are not hooked, the "
                    "setting is not applied and not saved");
        return;
    }

    /* The one refusal a player could otherwise reach: the camera in this build was not recognised
     * at install, so there is nothing to turn. The controls screen leaves the box off the screen in
     * that case, which makes this the belt to that pair of braces. */
    if (!free_look_set_enabled(enabled)) {
        log_warning("free look was switched on, but the follow camera in this build is not the "
                    "one this feature knows, the setting is not applied and not saved");
        return;
    }

    /* No strafe_walk_reset here, unlike the strafe setter: both control schemes walk the model-root
     * latch home on every substep they do not drive it, so the mode entered next unwinds it. */

    if (!ini_write_int(INPUT_SECTION, "FreeLook", enabled ? 1 : 0)) {
        log_warning("free look is now %s, but the setting could not be written to the ini and "
                    "will be back to its old value on the next launch", enabled ? "on" : "off");
        return;
    }
    log_info("free look switched %s from the controls screen and saved, the mouse now turns the "
             "%s", enabled ? "on" : "off", enabled ? "camera" : "body");
}
