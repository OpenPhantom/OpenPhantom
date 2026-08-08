/* strafe_walk.c: walking sideways by driving the engine's own walk somewhere else.
 *
 * ==============================================================================================
 * Why nothing here moves the player
 *
 * Pushing a sideways displacement into the integrator's conveyor surcharge is the obvious way to
 * strafe, and it is what this DLL used to do. It produced a player who slid sideways while
 * standing still, and the reason is one line in the clip selector: holding only a sideways key
 * leaves moveInput at 0 and curSpeed at 0, so Plr_StandClipSelect picks an IDLE clip. There is no
 * sideways clip to pick instead, the selector has six entries per weapon class (three idles,
 * walk forward, run forward, walk back) and branches on two bits.
 *
 * So this file does not move the player. It tells the engine the player is walking, in the
 * engine's own two fields and with the engine's own arithmetic:
 *
 *     moveInput |= 1                 the forward bit the clip selector branches on
 *     moveDrive  = dtScale30 * 0.6   exactly what Plr_Steer writes for a fully deflected axis
 *
 * and then turns the direction that walk comes out in. The walk and run clips, the footsteps, the
 * speed caps and their quarter-step ramp, the acceleration, the turn penalty and the collision all
 * follow for free, because all of it is now the engine walking rather than this DLL shoving.
 *
 * ==============================================================================================
 * The sign of the drive is part of the angle, and leaving it out was a real defect
 *
 * The integrator multiplies the facing by a SIGNED speed:
 *
 *     frameDelta = (-sin h, cos h) * frameDt * curSpeed * penalty
 *
 * so walking backward is a NEGATIVE speed along an unchanged facing, not a reversed facing. An
 * angle built as "where do I want to go, relative to forward" therefore double-counts the reversal
 * on every backward input: holding only the back key would have come out as a 180 degree offset,
 * which the negative speed then cancels, and the player would have walked FORWARD while the
 * backward clip played.
 *
 * The angle is built in the frame of the drive instead, the desired travel divided by the
 * drive's sign, and then backward alone is a true no-op and vanilla is untouched:
 *
 *     forward only          0        backward only         0
 *     sideways only       -+90       forward + sideways  -+45
 *     backward + right    +45        backward + left     -45
 *
 * ==============================================================================================
 * The angle is damped, and that is the whole of how this feels
 *
 * The raw angle above only ever takes five values: 0, +-45 and +-90. Stepping straight between
 * them moves the model 90 degrees in one substep, 2880 degrees per second, and no amount of
 * animation cross-fading can hide it, because the root yaw is applied AFTER the blend: the pose
 * compositor builds each joint matrix from the blended euler and only then multiplies our value in.
 * Whatever is written is exactly what is drawn.
 *
 * So the angle is damped toward its target instead, at the engine's own kind of exponential ease,
 * and holding it through the release is also the coast that used to be missing: the key goes up,
 * the forced walk bit stops being set, the engine's own decay brings the speed down, and the
 * travel direction swings back to the front over the same quarter second rather than snapping.
 *
 * The substep is an ARGUMENT and never a constant. The simulation runs at 1/32 s, or at 1/64 s
 * when the engine's own sixty-frames flag is set and nothing has pinned the rate; a damper built
 * on a hard-coded 1/32 would settle twice as fast and cap twice as high in that configuration,
 * silently.
 *
 * ==============================================================================================
 * Turning the body is a node rotation, not a clip
 *
 * bapobj_setNodeYaw on node 0, the model root. It is applied on top of the blended animation
 * rather than replacing it, and it propagates down the whole hierarchy, so the ordinary walk cycle
 * plays while pointing where the player actually travels. The engine rotates the same node the
 * same way for its hit reaction.
 *
 * The euler post-multiplies the joint matrix, joint := joint * euler, so it acts in the node's
 * OWN local frame and the node's translation is carried through unchanged. The rotation is
 * therefore about node 0's own origin, and in all four shipped hero rigs node 0 is a mesh-less
 * locator sitting within about a millimetre of the model's vertical centre line. The character
 * spins in place; he does not swing off a pivot.
 *
 * The node is a LATCH: whatever sits in it at pose-build time is used again on every rebuild until
 * someone writes something else. Two consequences, and both are load-bearing. The value is written
 * on EVERY driven substep, zero included, because a one-shot clear can be missed; it would be
 * missed exactly when a hit lands as the key is released, and the body would stay turned for good.
 * And a non-zero value has to be walked back down when the walk stops being driven at all, which
 * is what strafe_walk_release exists for.
 */
#include "strafe_walk.h"

#include "player_record.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#define PI_F 3.14159265358979323846f

/* The engine's forward drive for a fully deflected axis: Plr_Steer computes dtScale30 * 0.6 * axis
 * and this is the axis == 1 case, so a forced walk is indistinguishable from a held key. */
#define FORWARD_DRIVE_SCALE  0.6f

/* Past a right angle the walk cycle's leg swing is visibly along the body axis rather than the
 * travel axis. The digital keys cannot ask for more than this, so the clamp only ever catches a
 * bad reading, which is why it is a limit and not a scale.
 *
 * It is also what keeps strafe_walk_restore_heading's single-step fold correct: the phase-7 thunk
 * adds this angle to heading and this function takes it off again, and one conditional +-360 step
 * only lands inside [0, 360) while every contribution stays well under a full turn. */
#define MAX_BODY_YAW_DEGREES 90.0f

/* The engine's own angular lerp returns the target outright once the two are within a thousandth
 * of a degree. Copied deliberately: it is what makes the value LAND rather than approach forever,
 * and landing on exactly zero is what lets the latch be given up. */
#define DAMP_DEAD_BAND_DEGREES 0.001f

/* Ninety per cent of the gap per settle time. 0.1 is the "90 %" in that sentence. */
#define DAMP_REMAINDER 0.1f

/* A plausibility bolt on the substep, not a tuning value. The field can only ever hold 1/32 or
 * 1/64 while the phases run; anything larger means it is not what we take it for, and clamping it
 * bounds the resulting step instead of letting an implausible number produce a snap. */
#define MAX_TRUSTED_SUBSTEP_SECONDS 0.125f

typedef struct strafe_walk_state {
    set_node_yaw_fn_t set_node_yaw;
    bool              turns_body;
    float             settle_seconds;
    float             max_rate_deg_per_second;

    /* The damped angle, and the only state this file keeps between substeps. Non-zero means the
     * model root is ours and has to be brought home before we stop writing it. */
    float             theta_degrees;
} strafe_walk_state_t;

static strafe_walk_state_t strafe_state;

static float read_field(const uint8_t *record, int offset)
{
    return *(const float *)(record + offset);
}

static void write_field(uint8_t *record, int offset, float value)
{
    *(float *)(record + offset) = value;
}

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

void strafe_walk_bind(set_node_yaw_fn_t set_node_yaw, bool turns_body,
                      float settle_seconds, float max_rate_deg_per_second)
{
    strafe_state.set_node_yaw            = set_node_yaw;
    strafe_state.turns_body              = turns_body;
    strafe_state.settle_seconds          = settle_seconds;
    strafe_state.max_rate_deg_per_second = max_rate_deg_per_second;
}

void strafe_walk_reset(void)
{
    strafe_state.theta_degrees = 0.0f;
}

float strafe_walk_travel_offset(float strafe, float forward, float drive_sign)
{
    double along  = fabs((double)forward);
    double across = -(double)strafe * (double)drive_sign;

    if (across == 0.0 && along == 0.0) {
        return 0.0f;
    }

    /* Positive is LEFT, because that is the direction increasing heading turns: the shipped key
     * defaults bind the left arrow to a positive turn axis without an invert flag, and the aim
     * code adds this same kind of number to heading to point a shot. */
    return (float)(atan2(across, along) * (180.0 / PI_F));
}

float strafe_walk_damp_step(float current, float target, float substep_seconds,
                            float settle_seconds, float max_rate_deg_per_second)
{
    float keep;
    float next;
    float gap;
    float max_step;

    /* No time passed, or a substep that cannot be believed, so nothing moves. Holding is the
     * safe answer here; snapping would be a visible jump justified by a bad number. */
    if (!(substep_seconds > 0.0f)) {
        return current;
    }
    /* Damping switched off by configuration: the old instant step, kept so the two can be
     * compared against each other in the same build. */
    if (!(settle_seconds > 0.0f)) {
        return target;
    }
    if (substep_seconds > MAX_TRUSTED_SUBSTEP_SECONDS) {
        substep_seconds = MAX_TRUSTED_SUBSTEP_SECONDS;
    }

    gap = target - current;
    if (gap > -DAMP_DEAD_BAND_DEGREES && gap < DAMP_DEAD_BAND_DEGREES) {
        return target;
    }

    /* The remaining fraction of any gap after `settle_seconds` is DAMP_REMAINDER, regardless of
     * how the interval was cut into substeps; that is the whole point of taking the exponent
     * from the live substep. */
    keep = powf(DAMP_REMAINDER, substep_seconds / settle_seconds);

    /* The engine's own order of products in its angular lerp, so this reads the same way as the
     * code it was taken from: the factor weights the CURRENT value. */
    next = (1.0f - keep) * target + keep * current;

    /* The rate cap is a rate, so it is multiplied by the same live substep. Written flat it would
     * double when the substep halves, which is exactly the trap the exponent above avoids. */
    max_step = max_rate_deg_per_second * substep_seconds;
    gap      = next - current;
    if (gap > max_step) {
        next = current + max_step;
    } else if (gap < -max_step) {
        next = current - max_step;
    }
    return next;
}

/* While the hurt lock runs the hit reaction owns this node and writes its own twist into it; ours
 * would erase the flinch, so it gets the node for those few frames. */
static void turn_body(const uint8_t *record, float degrees)
{
    void *body;

    if (!strafe_state.turns_body || strafe_state.set_node_yaw == NULL || record == NULL) {
        return;
    }
    if (read_field(record, PLAYER_DROP_TIMER) != 0.0f) {
        return;
    }

    body = *(void *const *)(record + PLAYER_ACTOR);
    if (body != NULL) {
        strafe_state.set_node_yaw(body, MODEL_ROOT_NODE, degrees);
    }
}

void strafe_walk_force_forward(uint8_t *record, bool clear_backward)
{
    uint32_t move_input;

    if (record == NULL) {
        return;
    }

    move_input = *(const uint32_t *)(record + PLAYER_MOVE_INPUT) | 1u;

    /* Clearing the backward bit is NOT part of the sideways walk and must stay optional. There the
     * bit is the player's own input and the engine's signed speed still means it; only a scheme
     * that turns the body to face the way it travels may reinterpret a backward key as a forward
     * walk in the other direction, and it has to clear the bit or the clip selector would be told
     * two contradictory things at once. */
    if (clear_backward) {
        move_input &= ~2u;
    }

    *(uint32_t *)(record + PLAYER_MOVE_INPUT) = move_input;
    write_field(record, PLAYER_MOVE_DRIVE,
                read_field(record, PLAYER_DT_SCALE30) * FORWARD_DRIVE_SCALE);
}

float strafe_walk_drive(uint8_t *record, float strafe, float substep_seconds)
{
    uint32_t move_input = *(const uint32_t *)(record + PLAYER_MOVE_INPUT);
    float    forward    = (move_input & 1u) ? 1.0f : ((move_input & 2u) ? -1.0f : 0.0f);
    float    drive_sign = (forward < 0.0f) ? -1.0f : 1.0f;
    float    target;

    /* Neither forward nor backward is held, so tell the engine a walk is under way.
     *
     * Deliberately keyed on the RAW input and not on the damped angle: once the key is released
     * the bit stops being forced immediately, the engine's own decay takes the speed down, and the
     * angle coasting back to zero over the same interval is what carries the travel direction
     * through the stop. */
    if (strafe != 0.0f && forward == 0.0f) {
        strafe_walk_force_forward(record, false);
        drive_sign = 1.0f;
    }

    target = strafe_walk_travel_offset(strafe, forward, drive_sign);
    target = clamp_float(target, -MAX_BODY_YAW_DEGREES, MAX_BODY_YAW_DEGREES);

    strafe_state.theta_degrees = strafe_walk_damp_step(
        strafe_state.theta_degrees, target, substep_seconds,
        strafe_state.settle_seconds, strafe_state.max_rate_deg_per_second);

    turn_body(record, strafe_state.theta_degrees);
    return strafe_state.theta_degrees;
}

bool strafe_walk_release(uint8_t *record, float substep_seconds)
{
    if (strafe_state.theta_degrees == 0.0f) {
        return false;                       /* the node was never ours, or is ours no longer */
    }
    if (record == NULL) {
        strafe_state.theta_degrees = 0.0f;  /* no body to write to; drop the claim on it */
        return false;
    }

    strafe_state.theta_degrees = strafe_walk_damp_step(
        strafe_state.theta_degrees, 0.0f, substep_seconds,
        strafe_state.settle_seconds, strafe_state.max_rate_deg_per_second);

    /* The zero is WRITTEN before the claim is given up, in the same call that produces it. That is
     * the whole point: a latch cleared only in our own variable would leave the model turned. */
    turn_body(record, strafe_state.theta_degrees);
    return strafe_state.theta_degrees != 0.0f;
}

void strafe_walk_restore_heading(uint8_t *record, float heading_before)
{
    /* The engine's own formula, its own two fields, its own order of operations, so the result is
     * the number it would have written rather than an approximation of it.
     *
     * The fold is a comparison rather than a modulo because one step either way is provably
     * enough: it lands inside [0, 360) for any input in (-360, 720). Heading was inside [0, 360)
     * at the top of the substep, and two things are added to it before this point: the mouse step,
     * which the caller has clamped to well under a quarter turn, and the term below.
     *
     * That term is no longer zero. Phase 2 leaves the turn rate in the cell instead of a zero, so
     * the engine's own formula really does contribute here now. It stays inside the single-step
     * fold regardless: the rate is clamped to the engine's own 120 deg/s and the substep is at most
     * 1/32 s, so the term cannot exceed 3.75 degrees. */
    float turned = read_field(record, PLAYER_TURN_WHEEL) * read_field(record, PLAYER_FRAME_DELTA)
                 + heading_before;
    float radians;

    if (turned >= 360.0f) {
        turned -= 360.0f;
    } else if (turned < 0.0f) {
        turned += 360.0f;
    }
    write_field(record, PLAYER_HEADING, turned);

    /* The facing vector is rebuilt because the integrator has just written it from the OFFSET
     * heading, and it outlives the substep: the vault probe compares it against a wall normal, and
     * a hit reaction reads it to decide which way to twist. Both must see where the player looks,
     * not where the player is walking. */
    radians = turned * (PI_F / 180.0f);
    write_field(record, PLAYER_FACING_X, -(float)sin((double)radians));
    write_field(record, PLAYER_FACING_Y,  (float)cos((double)radians));
}
