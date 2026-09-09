/* input_steer.c: phase 2 of the player pipeline, where a substep's input is decided.
 *
 * Lifted out of enhanced_input.c, which keeps phase 7 and the install. See
 * enhanced_input_internal.h for why the seam is the phase rather than the state.
 */
#include "enhanced_input_internal.h"

#include "camera_follow.h"
#include "enhanced_input.h"
#include "free_look.h"
#include "input_config.h"
#include "input_gate.h"
#include "mouse_look.h"
#include "pad_stick.h"
#include "player_record.h"
#include "player_sites.h"
#include "steer_lean.h"
#include "steer_log.h"
#include "strafe_walk.h"
#include "view_lead.h"

#include "common/logging.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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
 * rendered frame and hands a share of the bank over here, and taking it is what marks a substep as
 * having consumed, so it is done unconditionally, at the very top, before any gate can return.
 * ============================================================================================ */
void __cdecl enhanced_input_steer_thunk(void)
{
    uint8_t *record = player_sites_record(&input_state.sites);
    float    mouse_step = 0.0f;
    float    keyboard_axis = 0.0f;
    float    substep_seconds = 0.0f;
    float    axis;
    float    pad_forward = 0.0f;
    float    pad_strafe  = 0.0f;
    float    steer_forward = 0.0f;
    bool     pad_driving = false;
    bool     air_mode    = false;
    bool     melee_mode  = false;
    bool     melee_turn  = false;
    float    strafe;
    float    engine_rate = 0.0f;
    float    pad_turn = 0.0f;
    bool     hand_back = false;
    bool     phase_active = false;
    bool     stand_mode = false;
    int      mode_for_log = -1;

    /* First, and before any gate of ours. The engine only reaches this phase when it is running
     * the player pipeline, which is the only state in which it reads input itself. A menu, a
     * dialogue and a cutscene stop the pipeline, and that is the whole of the original's input
     * lock. Everything this DLL turns on the render clock is gated on this stamp. */
    input_gate_note_phase_ran();

    /* The substep is read BEFORE the drain and outside every gate. The reconstruction underneath
     * answers a RATE, so it has to be told the interval this consumer covers, and the drain itself
     * has to stay unconditional, because taking it is what marks a substep as having consumed. A
     * record that is not there yet answers zero, which the drain understands.
     *
     * The bank is consumed on EVERY run of this thunk, before any gate: a step left in it would be
     * applied later, in a state that did not earn it. The value is simply discarded below when the
     * gate is closed. */
    if (record != NULL) {
        substep_seconds = enhanced_input_read_field(record, PLAYER_FRAME_DELTA);
    }
    /* ONE CONSUMER, ONE CADENCE, and which one it is depends on the control mode.
     *
     * With the per-frame path live the bank belongs to the camera update, and this phase takes only
     * what the rendered frames have already banked. Asking the bank for a substep's worth here as
     * well was measurably wrong once the delivery filter was switched on: the two consumers cover
     * different intervals, 31.25 ms against about 11, so the filter hands them different shares of
     * the same movement, and the frames that run a substep are then drawn by a different rule from
     * the frames that do not. In the field that showed up as the substep frames measuring half as
     * rough again as the others, which is the very asymmetry the per-frame path exists to remove.
     *
     * Without the per-frame path this is unchanged: the substep is the only consumer, and taking
     * is also what tells the collector somebody is consuming.
     *
     * The handover is called either way, because it is what marks a substep for the measurement,
     * and it answers zero while the feature is off. The view lead is PASSED the step that has just
     * been taken, and for two reasons of which only the first is obvious. The body owes the sum,
     * because the bank can be drained on both clocks and each drain removes what it hands over. And
     * the camera has to be TOLD the sum, because what it must stop drawing is the whole mouse turn
     * this substep applies, not only the part that came through the per-frame path; leaving the
     * engine's own per-step drain out of that total leaves its share to be drawn twice, which
     * is the
     * same double count the lead exists to remove, in miniature. */
    if (!view_lead_is_active()) {
        mouse_step = mouse_look_take_substep_degrees(substep_seconds);
    }
    mouse_step += view_lead_take_substep(mouse_step);

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
             * beyond the animation. Phase 2 also runs while shoving a crate, and
             * Plr_UpdatePushBlock
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
            air_mode   = (mode == PLAYER_MODE_JUMP || mode == PLAYER_MODE_JEDI_JUMP ||
                          mode == PLAYER_MODE_FALL);
            /* A swing, either hero's. Both descriptors run the steer and the integrate, so the
             * engine turns the body through one and this file's Stand gate is what stopped it. */
            melee_mode = (mode == PLAYER_MODE_SABRE_ATTACK || mode == PLAYER_MODE_PANAKA);
            /* And whether the swing needs anything folding back. Only with the sideways walk on:
             * with it off the turn axis was never taken away in the first place, and the arm
             * below that reads it is already open in every mode. */
            melee_turn = melee_mode && input_config()->strafe;
            if (melee_mode && !input_state.logged_melee_turn) {
                input_state.logged_melee_turn = true;
                log_info("a swing is running (mode %d) and the body may be turned through it",
                         mode);
            }
            mode_for_log = mode;
        }
    }

    if (phase_active) {
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
         * phase runs and declines while phase 7 goes on running. A substep that declines needs no
         * signal of its own to the camera: the bank is empty either way, and the last step's mouse
         * turn is paid back out by the engine's interpolation over the following step, so the
         * camera glides onto the body rather than snapping onto it. */
        steer_log_substep(record, STEER_BRANCH_DECLINED, substep_seconds, 0.0f, 0.0f, mode_for_log);
        return;
    }

    /* NEGATED, and this is the whole of the "A and D are the wrong way round" bug. The digital
     * turn axis is POSITIVE FOR LEFT, the shipped defaults bind the left arrow and NUMPAD4
     * without the invert flag and the right arrow and NUMPAD6 with it, while this file counts
     * right as positive. Treating the axis as if positive meant right sidestepped the wrong way
     * for every key bound to it. */
    axis   = -enhanced_input_clamp(keyboard_axis, -1.0f, 1.0f);
    strafe = input_config()->strafe_invert ? -axis : axis;

    /* ---- WHERE THE AUTHOR PLACED A CAMERA, THE GAME'S OWN SCHEME GETS ITS STICK BACK -------
     *
     * Free look already lets go of an authored camera, and for a long time it was the only one of
     * the three that did. That disagreement is what trapped a player on a balcony in the palace.
     * Our scheme spends the stick on a DIRECTION and leaves free look to turn the body to face it;
     * with free look gone what remained was a body lean clamped at ninety degrees and nothing that
     * rotates the player at all, so there was no way to turn round and jump back up.
     *
     * So the sideways walk and the pad stand down here too, and the stick goes back to meaning
     * what the shipped game means by it: sideways turns, forward walks. Mouse look stays, because
     * it only sharpens a turn the original already has and taking it away would coarsen the mouse
     * in every fixed-camera room in the game without stopping anybody being stuck. */
    hand_back = free_look_level_owns_camera();

    if (hand_back) {
        (void)pad_stick_take_handback(record, stand_mode, &pad_turn, &pad_forward);
        strafe = 0.0f;
    } else {
        /* The pad replaces both components of the input, or neither. pad_stick.h sets out why the
         * engine's own read of the same stick cannot be used for a direction. A substep it has
         * nothing to say about leaves the engine's own numbers exactly as they were, which is what
         * keeps the keyboard, and a pad the player has bound by hand, working unchanged. */
        pad_driving = pad_stick_take_substep(record, stand_mode, input_config()->strafe_invert,
                                             &pad_strafe, &pad_forward);
        if (pad_driving) {
            strafe = pad_strafe;
        }
    }

    /* The mutual exclusion between free look and the mouse-TO-BODY PATH, and it is the reason free
     * look lives in this DLL rather than beside it. Free look turns the CAMERA with the mouse and
     * turns the body toward where it travels. If the branch below also added the same step to the
     * heading, the mouse would turn body and camera together, the decoupling would be exactly
     * zero, and every log line would still claim a working feature. So when free look takes the
     * substep, nothing else here writes a view yaw or a travel angle. */
    /* The forward component is derived HERE rather than inside free look, because only this side
     * knows where the substep's input came from. From a stick it is a real magnitude and its sign
     * carries the whole lower half of the circle; from keys it can only ever be +1, -1 or 0. Free
     * look's own angle is a signed atan2 that reaches a full half turn either way, so given an
     * honest pair it turns the body anywhere the stick points, which is what makes a smooth 360
     * possible at all. */
    if (pad_driving) {
        steer_forward = pad_stick_y();      /* the RAW component, not the back-pedal deadbanded one:
                                             * facing your travel has no backward case to protect */
    } else if ((air_mode || melee_mode) && pad_stick_is_active()) {
        /* THE VECTOR IS WANTED IN THE AIR TOO, and only the move bits are not.
         *
         * pad_stick_take_substep is gated on Stand because that is where it WRITES, and a
         * forced bit outside Stand is what would lock the crate shove. Reading is a different
         * question and this used to conflate them, so a jump fell back to the engine's own
         * degraded read of the same stick. That is coarse everywhere, and on a machine where
         * the pad reaches XInput but never the engine's WinMM joystick path it is nothing at
         * all: walking worked and steering a jump did not, on the same stick.
         *
         * It polled above whatever it answered, so the vector is already here. Nothing is
         * written, exactly as before. */
        steer_forward = pad_stick_y();
        strafe        = input_config()->strafe_invert ? -pad_stick_x() : pad_stick_x();
        if (melee_mode) {
            /* The same deflection again and unmodified, for the turn fold below. That fold is
             * shared with the handback, which is handed the raw axis and negates it itself, so
             * inverting here as well would give the sideways walk's setting a say over which way
             * a swing turns. */
            pad_turn = pad_stick_x();
        }
    } else {
        uint32_t bits = *(const uint32_t *)(record + PLAYER_MOVE_INPUT);

        steer_forward = (bits & 1u) ? 1.0f : ((bits & 2u) ? -1.0f : 0.0f);
    }

    /* The sideways component is withheld when the sideways walk is off, and that gate was missing.
     * Two install lines have always claimed it: this DLL says the keyboard turn axis still turns
     * the player, and free look says only forward and back are camera-relative. The first was true
     * and the second was not. The same axis was turned into a sideways value a few lines above,
     * unconditionally, and handed to free look, which builds a travel angle out of it and turns the
     * body to face it. So with the sideways walk off the turn keys did both at once: they turned
     * the player, correctly, and they also walked them sideways relative to the camera, which is
     * the whole of the feature that was supposed to be switched off.
     *
     * Withheld here rather than at the assignment because this is the one consumer that was wrong:
     * the walk driver below is already gated on the same setting, and the turn fold further down
     * reads the raw axis rather than this value, so both keep working untouched. */
    if (free_look_steer(record, mouse_step, input_config()->strafe ? strafe : 0.0f,
                        steer_forward, stand_mode, air_mode, melee_mode)) {
        /* The cell is zeroed AFTER the lean has read it, not before: free look turns the CAMERA
         * with the mouse, and a turn rate left standing would make the engine turn the BODY's
         * heading on top of it in phase 7, the very coupling free look exists to break. */
        input_state.turn_wheel_is_ours = false;
        /* NOTHING IS WRITTEN INTO THE UPPER BODY HERE, and the empty line is the design rather than
         * an omission. It has carried three different corrections, each removed for a different
         * reason, and they are kept together because an empty line invites a fourth.
         *
         * The first re-issued the engine's own upper body twist from the turn cell. On this branch
         * that cell carries OUR MOUSE, because the engine's analog turn arm reads the same device
         * axis mouse_look.c does, and under free look the mouse is the camera. Twisting the chest
         * with it is body language for a turn the body never made.
         *
         * The second corrected the aim. The original writes the chest node unconditionally a few
         * instructions earlier, so anything written in phase 1 or phase 6 is overwritten before it
         * can be drawn and had to be re-issued here. It was needed only while free look pointed the
         * body at its travel, because the aim then sat at an angle to the body that changed with
         * every direction change, and the pose could never settle: it was recomputed each substep
         * against a body that was itself still turning. Pointing the body at the camera while the
         * trigger is held removes the angle instead of damping it, so there is nothing left to
         * re-issue.
         *
         * The third was a lean driven from the mouse step. The right input for a lean here would be
         * the BODY's own turn, the damped root angle the sideways walk drives toward the travel
         * direction, and the model root already carries chest and head with it, so that lean is
         * there already without a second write. */
        /* While the trigger is held the feet belong to the sideways walk, not to free look's own
         * travel turn. The body is already facing the camera, so a sideways key is a real sidestep
         * and a backward key a real back-pedal, the same thing this DLL does with free look
         * switched off, which is what makes firing feel identical in both schemes. */
        if (free_look_aim_stance() && stand_mode && input_config()->strafe) {
            input_state.pending_travel_degrees =
                strafe_walk_drive(record, strafe, substep_seconds);
        } else {
            strafe_walk_release(record, substep_seconds);
        }
        steer_lean_release();
        enhanced_input_write_field(record, PLAYER_TURN_WHEEL, 0.0f);
        input_state.steer_ran_this_substep = true;
        input_state.pending_valid          = true;
        steer_log_substep(record, STEER_BRANCH_FREE_LOOK, substep_seconds, mouse_step,
                          input_state.pending_travel_degrees, mode_for_log);
        return;
    }

    if (input_config()->mouse_look) {
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
        /* A swing joins the two cases that already fold, and for the same reason both of them
         * do: nothing else is spending the axis this substep. The sideways walk is Stand-only
         * and has just stood down, so with it switched on the turn keys had no consumer at all
         * during a swing, while the engine's own turn went on being subtracted in phase 7 and
         * never paid back. A sabre that only swung straight ahead was that and nothing more.
         *
         * The stick arm below EXCLUDES this one during a swing, and only during a swing. This
         * axis is the engine's own absolute axis 0, which a pad bound through the engine's
         * joystick path also lands on, so a stick that is already being folded there as XInput
         * would arrive twice and at two different scales. In the handback both arms still run
         * together, unchanged: a player holding a key and pushing a stick there wants the sum. */
        if ((hand_back || !input_config()->strafe || (melee_turn && pad_turn == 0.0f)) &&
            keyboard_axis != 0.0f) {
            input_state.pending_yaw_degrees +=
                enhanced_input_clamp(keyboard_axis, -1.0f, 1.0f) *
                input_config()->key_turn_rate * substep_seconds;
        }

        /* And the pad's sideways deflection, on the same path and at the same rate. It is negated
         * because this file counts right as positive while the axis above is positive for LEFT,
         * so passing -x is what puts the pad on the keyboard's own footing rather than adding a
         * second sign convention. Analog, so a small push is a slow turn. */
        if ((hand_back || melee_turn) && pad_turn != 0.0f) {
            input_state.pending_yaw_degrees +=
                enhanced_input_clamp(-pad_turn, -1.0f, 1.0f) *
                input_config()->key_turn_rate * substep_seconds;
        }

        /* ---- The turn cell, and why it is no longer touched -----------------------------------
         *
         * turnWheel is not just the integrator's input. Nine readers take it as the TURN PENALTY on
         * speed. It does NOT reach the follow camera's own zero test, though, and this used to say
         * it did: Plr_PublishGround hands the cell over, and updateCam overwrites it with its own
         * measurement of the interpolated heading before anything looks at it. Writing 0 still
         * switched off every one of the nine. */
        /* READ BEFORE OVERWRITING. This is the value the original just computed, the ramped
         * keyboard turn or the clamped mouse accumulation, and it is the authentic input for the
         * upper-body twist. Two lines further down the cell stops holding it. */
        engine_rate = enhanced_input_read_field(record, PLAYER_TURN_WHEEL);

        if (input_config()->restore_turn_rate) {
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
            enhanced_input_write_field(record, PLAYER_TURN_WHEEL, 0.0f);
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
                         keyboard_axis != 0.0f, substep_seconds);
    } else {
        /* Not our turn to drive it: the original's own twist is standing and is correct, so the
         * only thing to drop is the damper's memory. */
        steer_lean_release();
    }

    if (hand_back || !input_config()->strafe || !stand_mode) {
        /* Not driving the walk this substep, so the model root has to come home rather than keep
         * the last angle Stand wrote into it. */
        strafe_walk_release(record, substep_seconds);
    } else if (pad_driving) {
        input_state.pending_travel_degrees =
            strafe_walk_drive_vector(record, strafe, pad_forward, substep_seconds);
    } else {
        input_state.pending_travel_degrees = strafe_walk_drive(record, strafe, substep_seconds);
    }

    input_state.steer_ran_this_substep = true;
    input_state.pending_valid          = true;

    steer_log_substep(record, input_config()->mouse_look ? STEER_BRANCH_MOUSE_LOOK
                                                         : STEER_BRANCH_PASSIVE,
                      substep_seconds, input_state.pending_yaw_degrees,
                      input_state.pending_travel_degrees, mode_for_log);
}

