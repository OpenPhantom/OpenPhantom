#include "steer_lean.h"

#include "player_record.h"

#include "common/logging.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* --- 0x0044A26E..0x0044A2E2  the tail of Plr_Steer, and every number in this file -------------
 *
 *   0044A24B  C780 A4020000 0000F0C2   turnWheel = -120.0      (a mov imm32, not a load)
 *   0044A274  C782 A4020000 0000F042   turnWheel = +120.0
 *   0044A283  D980 A4020000            fld  turnWheel          <- degrees per SECOND
 *   0044A289  D835 E8864A00            fdiv [0x4A86E8] = 12.0
 *             ...                      setNodeYaw(hActor, [pPlr+0x44], that)
 *   0044A2B4  D982 A4020000            fld  turnWheel
 *   0044A2BA  D835 D0864A00            fdiv [0x4A86D0] = 10.0
 *             ...                      setNodeYaw(hActor, [pPlr+0x54], that)
 *
 * The clamp is the engine's KEYBOARD turn ceiling, and it is kept because it is what sizes the
 * pose: a wrist flick is 1200-1800 deg/s, and without the clamp the chest would twist by hundreds
 * of degrees. With it the twist saturates at the authored maximum, which is what the shipped game
 * shows when a player holds a turn key.                                                        */
#define LEAN_CLAMP_DEGREES_PER_SECOND 120.0f
#define LEAN_CHEST_DIVISOR             12.0f
#define LEAN_HEAD_DIVISOR              10.0f

/* There is no damper here, and its absence is still right. What was wrong was believing the same
 * argument covers a mouse.
 *
 * An earlier version added a 150 ms exponential damper because the twist looked like it would slam
 * between the extremes. It was solving a problem the engine had already solved: the turn cell is an
 * ACCUMULATOR, and a held key climbs 12, 26, 42, 60, 80, 102, 120 over seven substeps, so the
 * value handed to the divisors already eases in over about a fifth of a second and already falls
 * to zero on release. Damping it a second time only delayed it. Worse, the damper's own cap was
 * expressed in degrees per second while the value passed to it IS a rate, so it limited the change
 * of a rate to 3.75 units per substep; eleven substeps to reach a number the ramp reaches by
 * itself.
 *
 * Both halves of that are about a KEY. Neither is true of a mouse, which reaches the same cell
 * through a different arm with no ramp and no decay. The branch in steer_lean_apply carries the
 * disassembly. The answer there is not to damp the engine's number but to stop asking it, because
 * a smoothed number for the same substep already exists.                                        */

/* Long enough that a hitch cannot produce a rate no hand could have made, short enough that both
 * shipped substep lengths (1/32 and 1/64 s) pass. */
#define MAX_TRUSTED_SUBSTEP_SECONDS 0.100f

typedef struct steer_lean_state {
    set_node_yaw_fn_t set_node_yaw;
    bool              enabled;
    bool              prefer_hand_rate;  /* SteerLeanFromHand */
    float             test_degrees;      /* != 0: force this angle, ignore the turn entirely */
    bool              warned_about_rig;
} steer_lean_state_t;

static steer_lean_state_t lean_state;

/* Reset to "not-called" at the top of every apply, so a substep in which the function was never
 * reached is distinguishable from one in which it returned early. */
static steer_lean_report_t last_report = { "not-called", 0, 0, 0.0f, 0.0f, 0.0f, false, false };

void steer_lean_last_report(steer_lean_report_t *out)
{
    if (out != NULL) {
        *out = last_report;
    }
}

static bool report(const char *status)
{
    last_report.status = status;
    return false;
}

/* THE BISECTION KNOB, and it exists because a subtle effect cannot be told apart from no effect.
 *
 * The measurement so far says the ENGINE ITSELF writes this twist every substep the mouse moves,
 * and that it is still not visible. Either something overwrites the two nodes after Plr_Steer, or a
 * yaw on them does not reach the screen at all, and 10 degrees of chest rotation is small enough
 * that "I cannot see it" does not separate those. A forced 45 degrees does: it is impossible to
 * miss, so if the body does not visibly twist, the node path is the defect and everything built on
 * top of it is moot.
 *
 * It is a diagnostic, not a feature: it ignores the turn, the damper and the clamp. */
void steer_lean_set_test_degrees(float degrees)
{
    lean_state.test_degrees = degrees;
    if (degrees != 0.0f) {
        log_warning("SteerLeanTestDegrees=%.1f, chest and head are FORCED to a fixed angle every "
                    "substep, ignoring the turn. This is a measurement, not a setting: if the body "
                    "does not visibly twist at this angle, the node write never reaches the screen "
                    "and the steering lean cannot work either. Set it back to 0 when done.",
                    (double)degrees);
    }
}

void steer_lean_bind(set_node_yaw_fn_t set_node_yaw, bool enabled, bool prefer_hand_rate)
{
    lean_state.set_node_yaw    = set_node_yaw;
    lean_state.enabled         = enabled && (set_node_yaw != NULL);
    lean_state.prefer_hand_rate = prefer_hand_rate;

    if (enabled && set_node_yaw == NULL) {
        log_warning("the node setter did not resolve, the upper body will not lean into a turn. "
                    "Everything else about steering is unaffected.");
    }
}

bool steer_lean_is_active(void)
{
    return lean_state.enabled;
}

void steer_lean_release(void)
{
    last_report.status      = "released";
    last_report.raw_rate    = 0.0f;
    last_report.applied_rate = 0.0f;
    last_report.wrote_head  = false;
    last_report.wrote_chest = false;
}

static float clamp_rate(float rate)
{
    if (rate < -LEAN_CLAMP_DEGREES_PER_SECOND) {
        return -LEAN_CLAMP_DEGREES_PER_SECOND;
    }
    if (rate > LEAN_CLAMP_DEGREES_PER_SECOND) {
        return LEAN_CLAMP_DEGREES_PER_SECOND;
    }
    return rate;
}

bool steer_lean_aim(const uint8_t *record, float degrees)
{
    void    *body;
    uint32_t chest_node;

    if (lean_state.set_node_yaw == NULL || record == NULL || !isfinite(degrees)) {
        return false;
    }
    body       = *(void *const *)(record + PLAYER_ACTOR);
    chest_node = *(const uint32_t *)(record + PLAYER_CHEST_NODE);
    if (body == NULL || chest_node == 0u) {
        return false;               /* index 0 is the finder's failure answer, not a valid node */
    }

    lean_state.set_node_yaw(body, chest_node, degrees);
    last_report.status      = "aim";
    last_report.chest_node  = chest_node;
    last_report.raw_rate    = degrees;
    last_report.applied_rate = degrees;
    last_report.wrote_chest = true;
    last_report.wrote_head  = false;
    return true;
}

bool steer_lean_apply(const uint8_t *record, float engine_rate, float hand_rate,
                      bool keyboard_is_turning, float substep_seconds)
{
    void    *body;
    uint32_t chest_node;
    uint32_t head_node;
    float    rate;

    last_report.wrote_head  = false;
    last_report.wrote_chest = false;

    if (!lean_state.enabled) {
        return report("off");
    }
    if (record == NULL) {
        return report("no-record");
    }

    /* The division is the dangerous line. A zero or absurd substep would produce an infinity or a
     * NaN, and a NaN fed to an ordinary clamp comes out as the MINIMUM, a permanent hard-left
     * twist that no log line would explain. Refusing leaves the engine's own value from the
     * original standing, which is the correct fallback and needs no restore. */
    if (!(substep_seconds > 0.0f) || substep_seconds > MAX_TRUSTED_SUBSTEP_SECONDS) {
        return report("bad-dt");
    }
    if (!isfinite(engine_rate) || !isfinite(hand_rate)) {
        return report("bad-angle");
    }

    body = *(void *const *)(record + PLAYER_ACTOR);
    if (body == NULL) {
        return report("no-body");           /* between levels, or a player being reset */
    }

    chest_node = *(const uint32_t *)(record + PLAYER_CHEST_NODE);
    head_node  = *(const uint32_t *)(record + PLAYER_HEAD_NODE);
    last_report.chest_node  = chest_node;
    last_report.head_node   = head_node;
    last_report.chest_claim = *(const float *)(record + PLAYER_CHEST_CLAIM);

    /* Zero is the node finder's failure answer, not a valid target: node 0 is the model root, the
     * one the sideways walk latches. A rig missing either name is not ours to twist. */
    if (chest_node == 0u || head_node == 0u) {
        if (!lean_state.warned_about_rig) {
            lean_state.warned_about_rig = true;
            log_warning("this body has no chest (%u) or head (%u) node, so it is left alone, the "
                        "upper body will not lean into a turn while it is in play",
                        (unsigned)chest_node, (unsigned)head_node);
        }
        return report("no-nodes");
    }

    /* The forced angle short-circuits the whole computation, including the damper, so what lands in
     * the node is exactly the number in the ini and nothing else can be blamed for it. */
    if (lean_state.test_degrees != 0.0f) {
        last_report.status      = "FORCED";
        last_report.raw_rate    = lean_state.test_degrees;
        last_report.applied_rate = lean_state.test_degrees;
        lean_state.set_node_yaw(body, head_node,  lean_state.test_degrees);
        lean_state.set_node_yaw(body, chest_node, lean_state.test_degrees);
        last_report.wrote_head  = true;
        last_report.wrote_chest = true;
        return true;
    }

    /* THE ENGINE'S NUMBER, OR THE HAND'S. It used to be the engine's always, because that value is
     * authentic by construction. True of a key, false of a mouse, and the difference is one branch:
     *
     *   00449F9C  E8 ..                    call the relative axis reader   ; the mouse turn
     *   00449FA7  D8 1D A4864A00           fcomp 0.0
     *   0044A068  D8 0D DC864A00           fmul  6.0                       ; THE MOUSE ARM:
     *   0044A077  D8 81 A4020000           fadd  turnWheel                 ;   turnWheel += ax * 6
     *   0044A08E  6A 00 / E8 ..            call the absolute axis reader   ; then the keys
     *   0044A0A6  0F 85 42 01 00 00        jne  0x0044A1EE                 ; neither axis moved
     *   0044A223  C781 A4020000 00000000   turnWheel = 0                   ; an OUTRIGHT store
     *
     * The mouse arm has no decay term anywhere in the image, so that store is the only thing that
     * lowers the cell, and the axis it branches on is refilled once per RENDERED FRAME. A frame
     * that carried no report leaves the axis at zero and the next substep takes the store, so under
     * a mouse the cell is a sawtooth that visits zero rather than a rate. Ten degrees of chest
     * collapse to none and climb back, several times a second, more often the faster we draw.
     *
     * A held key has no such gap: while the digital axis is non-zero the store is unreachable, and
     * the climb 12, 26, 42, 60, 80, 102, 120 is itself the ease-in this twist exists to show. So a
     * substep that saw a key keeps the engine's number, and so does one our own reconstruction has
     * nothing to say about.
     *
     * `hand_rate` is the same movement read on the clock it was made on, already smoothed. Nothing
     * is damped here and no new constant is introduced. */
    if (lean_state.prefer_hand_rate && !keyboard_is_turning && hand_rate != 0.0f) {
        rate = clamp_rate(hand_rate);
        last_report.status = "hand";
    } else {
        rate = (engine_rate != 0.0f) ? engine_rate : clamp_rate(hand_rate);
        last_report.status = (engine_rate != 0.0f) ? "engine" : "filled";
    }
    last_report.raw_rate     = rate;
    last_report.applied_rate = rate;

    /* The head first, because it is the one write that is certainly ours: it is the only writer of
     * that node in the whole image. */
    lean_state.set_node_yaw(body, head_node, rate / LEAN_HEAD_DIVISOR);
    last_report.wrote_head = true;

    /* The chest is shared, and an earlier version of this comment was wrong about how.
     *
     * It claimed the claim cell hands the node to the auto-aim. It does not: Plr_Steer writes this
     * node UNCONDITIONALLY every substep, in phase 2, with no gate and no path that skips it, so
     * phase 2 is the last writer before the draw and nothing written in a later phase survives to
     * be seen. Skipping here protects nothing on its own.
     *
     * What the skip still buys is honesty about ownership: while the cell is non-zero an attack is
     * live and the shot is being aimed off that cell rather than off the body's facing. Writing a
     * turn-rate twist into the same node on top of that would be two features steering one bone
     * from two different quantities, and the visible result would be a chest that drifts while the
     * player holds an aim. */
    if (*(const float *)(record + PLAYER_CHEST_CLAIM) == 0.0f) {
        lean_state.set_node_yaw(body, chest_node, rate / LEAN_CHEST_DIVISOR);
        last_report.wrote_chest = true;
    }
    return true;
}
