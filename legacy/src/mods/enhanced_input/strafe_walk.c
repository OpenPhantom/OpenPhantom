/* strafe_walk.c: walking sideways by driving the engine's own walk somewhere else.
 *
 * SIZE NOTE: past 750 lines, under the 900 hard limit, because this file holds both halves of one
 * quantity. The angle is damped on the simulation clock and drawn on the frame clock, and the two
 * are not separable without putting the pair of floats they interpolate between on either side of
 * a translation unit boundary. That is the same seam a previous split was rejected on elsewhere in
 * this directory, for the same reason.
 *
 * The seam, if this ever needs one, is the DRAWN half at the bottom: its two signatures, its cell
 * resolution and its detour share nothing with the simulation half except three fields of the state
 * struct and the setter. That is a shared internal header away, and the file would come back to
 * about four hundred lines. Do not cut anywhere else; the arithmetic above and the guard below are
 * one argument.
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
 * The angle is damped, and that damping decides how this feels
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
 * And a non-zero value has to be walked back down when the walk stops being driven at all, and
 * strafe_walk_release exists for that.
 */
#include "strafe_walk.h"

#include "player_record.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/numeric.h"
#include "common/signature.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

/* One argument, cdecl, and a result in EAX. Read out of the call site rather than assumed: the
 * caller pushes one dword and then does `add esp, 4`, which is the caller cleaning its own
 * argument, and it stores the returned value. */
typedef int32_t (__cdecl *object_draw_fn_t)(void *argument);

#define PI_F 3.14159265358979323846f

/* The engine's forward drive for a fully deflected axis: Plr_Steer computes dtScale30 * 0.6 * axis
 * and this is the axis == 1 case, so a forced walk is indistinguishable from a held key. */
#define FORWARD_DRIVE_SCALE  0.6f

/* Past a right angle the walk cycle's leg swing is visibly along the body axis rather than the
 * travel axis. The digital keys cannot ask for more than this, so the clamp only ever catches a
 * bad reading, so it is a limit and not a scale.
 *
 * It also keeps strafe_walk_restore_heading's single-step fold correct: the phase-7 thunk
 * adds this angle to heading and this function takes it off again, and one conditional +-360 step
 * only lands inside [0, 360) while every contribution stays well under a full turn. */
#define MAX_BODY_YAW_DEGREES 90.0f

/* The engine's own angular lerp returns the target outright once the two are within a thousandth
 * of a degree. Copied deliberately: it makes the value LAND rather than approach forever, and
 * landing on exactly zero is what lets the latch be given up. */
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

    /* The drawn half. The angle above is a simulation quantity, damped once per substep, and until
     * this existed it was also what got drawn: the pose compositor multiplies the node euler in
     * after the animation blend, so whatever stands in the node is exactly what appears. At 240
     * frames a second that is one new orientation every seven or eight frames, and the quarter
     * second ease into and out of a strafe arrives as a visible staircase.
     *
     * So the pair is kept and the drawn value is interpolated between them, on the engine's own
     * weight, once per rendered frame. */
    float             theta_previous;      /* what the angle was at the start of this substep */
    bool              damped_this_substep; /* whether our damper actually ran in it            */
    bool              owns_node;           /* whether the model root is ours to write at all   */
    uint32_t          previous_tick;       /* the substep counter as of the last drawn frame   */
    uint32_t          opened_tick;         /* the substep counter when the damper last ran     */
    uint8_t          *record;              /* the player record, captured where it is known    */
    uint8_t   *const *player_cell;         /* pPlayer: what the engine itself follows          */

    /* Resolved once at install. Plain pointers rather than reads through the memory helper: this
     * runs once per rendered frame and the cells are inside the host image, which is never
     * unmapped, so one check at install is the whole check needed. */
    bool                     installed;
    detour_t                 draw;
    object_draw_fn_t         draw_original;
    const volatile uint32_t *substep_counter;
    const volatile float    *alpha_global;
    const volatile uint32_t *alpha_gate;
    const volatile float    *alpha_latch;
    const volatile uint16_t *throttle_bytes;
} strafe_walk_state_t;

static strafe_walk_state_t strafe_state;

static void strafe_draw_install(void);
static void open_substep(uint8_t *record);

static float read_field(const uint8_t *record, int offset)
{
    return *(const float *)(record + offset);
}

static void write_field(uint8_t *record, int offset, float value)
{
    *(float *)(record + offset) = value;
}

/* Called at the top of every substep in which our damper is about to run. It records the angle the
 * substep starts from, which is one half of what the drawn frames interpolate between, and it says
 * that the damper really ran. The second part is not bookkeeping, it is the guard described at the
 * per-frame callback below. */
static void open_substep(uint8_t *record)
{
    strafe_state.theta_previous      = strafe_state.theta_degrees;
    strafe_state.damped_this_substep = true;
    strafe_state.owns_node           = true;
    strafe_state.record              = record;
    /* Guarded, unlike the read in the draw path: that one only runs from a detour the install
     * placed, and the counter is resolved in the same function. This runs from the damper, which
     * works whether or not the drawn half resolved anything. */
    strafe_state.opened_tick         = (strafe_state.substep_counter != NULL)
                                     ? *strafe_state.substep_counter : 0u;
}

void strafe_walk_bind(set_node_yaw_fn_t set_node_yaw, uint8_t *const *player_cell,
                      bool turns_body, float settle_seconds, float max_rate_deg_per_second)
{
    strafe_state.set_node_yaw            = set_node_yaw;
    strafe_state.player_cell             = player_cell;
    strafe_state.turns_body              = turns_body;
    strafe_state.settle_seconds          = settle_seconds;
    strafe_state.max_rate_deg_per_second = max_rate_deg_per_second;

    strafe_draw_install();
}

void strafe_walk_reset(void)
{
    strafe_state.theta_degrees       = 0.0f;
    strafe_state.theta_previous      = 0.0f;
    strafe_state.damped_this_substep = false;
    strafe_state.owns_node           = false;
    strafe_state.record              = NULL;
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
     * double when the substep halves, exactly the trap the exponent above avoids. */
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

void strafe_walk_apply_stick_move(uint8_t *record, float forward, float strafe)
{
    uint32_t move_input;

    if (record == NULL) {
        return;
    }

    /* The engine's own read of the pad has already run and is wrong, so this writes rather than
     * adds. Its axis 1 is cut by a thirty per cent square deadzone, so a light push
     * sets no move bit and no drive at all and the player simply does not move; and past half
     * the stick's travel it is saturated, so it cannot tell a walk from a run either. This
     * replaces both bits and the drive from the vector pad_stick.c read straight from the
     * device. It runs after the original steer, so there is nothing to undo.
     *
     * The drive is FULL whichever gait is chosen, and deliberately so. The clips play at a
     * fixed rate with nothing scaling them by speed, so a continuously variable pace would slide
     * the feet along the ground. Two gaits at their authored speeds keep the feet planted, and
     * the speed cap that separates them is the engine's own: 2.0 for a walk, 3.5 for a run. */
    move_input = *(const uint32_t *)(record + PLAYER_MOVE_INPUT) & ~3u;
    if (forward > 0.0f) {
        move_input |= 1u;
    } else if (forward < 0.0f) {
        move_input |= 2u;
    } else if (strafe != 0.0f) {
        /* A push with no forward component at all still has to tell the engine a walk is
         * under way, or neither bit is set, the drive decays and the player stands still
         * while the travel angle points politely sideways. The keyboard path forces the same
         * bit for the same reason; it is only reachable here on an exact zero, because any
         * real stick leaves a little forward component in a sideways push. */
        move_input |= 1u;
    }

    *(uint32_t *)(record + PLAYER_MOVE_INPUT) = move_input;

    /* No bit means no drive, and this has to be written rather than left alone. The engine's own
     * read of the pad has already put a drive there from its own axis, so leaving it standing is
     * not neutral. Without this the player accelerates with the standing animation playing and
     * slides across the floor: no clip is chosen, because no move bit is set, but the speed delta
     * is applied anyway. Reachable whenever the stick is live and neither component asks to move,
     * which is a sideways push with the sideways walk off, and the same push on a level that owns
     * its own camera. */
    if ((move_input & 3u) == 0u) {
        write_field(record, PLAYER_MOVE_DRIVE, 0.0f);
        return;
    }

    /* The drive carries the sign, and leaving it positive for a backward push is a whole broken
     * half of the stick. The engine writes dtScale30 * 0.6 * axis with a SIGNED axis, and that sign
     * takes curSpeed negative; the backward move bit only chooses the clip and the speed
     * cap. Written positive with the backward bit set, the player plays the back-pedal clip while
     * travelling forwards, and the travel angle is negated on top of it by the drive sign, so the
     * lower half of the stick went somewhere between wrong and nowhere.
     *
     * The MAGNITUDE stays full either way, which is the gait decision made in pad_run.h: the clips
     * play at a fixed rate, so two gaits at their authored speeds keep the feet planted. */
    write_field(record, PLAYER_MOVE_DRIVE,
                read_field(record, PLAYER_DT_SCALE30) * FORWARD_DRIVE_SCALE *
                ((forward < 0.0f) ? -1.0f : 1.0f));
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
     * angle coasting back to zero over the same interval carries the travel direction through
     * the stop. */
    if (strafe != 0.0f && forward == 0.0f) {
        strafe_walk_force_forward(record, false);
        drive_sign = 1.0f;
    }

    target = strafe_walk_travel_offset(strafe, forward, drive_sign);
    target = numeric_clamp(target, -MAX_BODY_YAW_DEGREES, MAX_BODY_YAW_DEGREES);

    open_substep(record);
    strafe_state.theta_degrees = strafe_walk_damp_step(
        strafe_state.theta_degrees, target, substep_seconds,
        strafe_state.settle_seconds, strafe_state.max_rate_deg_per_second);

    turn_body(record, strafe_state.theta_degrees);
    return strafe_state.theta_degrees;
}

float strafe_walk_drive_vector(uint8_t *record, float strafe, float forward,
                               float substep_seconds)
{
    float drive_sign = (forward < 0.0f) ? -1.0f : 1.0f;
    float target;

    /* The only difference from strafe_walk_drive is that `forward` is real, the whole
     * point of the pad path. The other one rebuilds it as exactly +1, -1 or 0 from the move
     * bits, because a keyboard has nothing finer to give. Handing atan2 a quantised 1 against an
     * analogue sideways value pulls every diagonal toward forward: a true forty five degree push
     * came out at thirty five degrees, and at half deflection at nineteen. With both components
     * honest the angle is the direction the stick is actually pointing. */
    if (record == NULL) {
        return strafe_state.theta_degrees;
    }

    target = strafe_walk_travel_offset(strafe, forward, drive_sign);
    target = numeric_clamp(target, -MAX_BODY_YAW_DEGREES, MAX_BODY_YAW_DEGREES);

    open_substep(record);
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

    open_substep(record);
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
     * Folded into [0, 360) by a whole number of turns rather than by one step. One step used to
     * be argued enough because the mouse step was "well under a quarter turn"; it is not. The
     * spike limit that bounds it is 3000 deg/s as shipped and 20000 at the key's ceiling, which is
     * 93.75 and 625 degrees in one 1/32 s substep, and one subtraction left the heading past 360
     * at the second. The other term is small: phase 2 leaves the turn rate in the cell, clamped
     * to the engine's own 120 deg/s, so it is at most 3.75 degrees a substep. */
    float turned = read_field(record, PLAYER_TURN_WHEEL) * read_field(record, PLAYER_FRAME_DELTA)
                 + heading_before;
    float radians;

    turned -= 360.0f * floorf(turned / 360.0f);
    if (!(turned >= 0.0f && turned < 360.0f)) {
        turned = 0.0f;                          /* not a number, or the fold's own rounding edge */
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

/* ==============================================================================================
 * The drawn angle
 *
 * Everything above runs on the simulation clock, once every 1/32 s, rightly: a damper is
 * a simulation quantity. What was wrong is that the damped value was also the DRAWN value. The pose
 * compositor multiplies the node euler in after the animation blend, so whatever stands in the node
 * is exactly what appears on screen, with nothing to smooth it. At 240 frames a second the body
 * holds one orientation for seven or eight frames and then jumps, and the quarter second ease into
 * and out of a strafe arrives as a staircase.
 *
 * The repair is the one the engine already applies to every object it draws: interpolate between
 * the previous and the current simulation value, on the engine's own weight, once per rendered
 * frame. The engine even hands us the weight, so nothing here has to invent a clock.
 *
 * Where. bapobj_drawAll, entered once per rendered frame before any pose is composed, so a write
 * here lands in the SAME frame. Writing at frame end instead would buy the smoothness and pay a
 * frame of latency for it.
 *
 * The evidence for the hook point. bapobj_drawAll is reached once per rendered frame, and the
 * argument shape is read out of its only call site rather than assumed:
 *
 *     00410908  mov eax, [ebp+0x10]
 *     0041090B  push eax
 *     0041090C  call 0x411028          bapobj_drawAll
 *     00410911  add esp, 4             the caller cleans its own argument, so cdecl, one argument
 *     00410914  mov [ebp-4], eax       and it returns a value
 *
 * The interpolation weight and the two cells that select between its live and frozen forms are
 * read out of the operands inside that function, at +0x3C, +0x48 and +0x51 of the pattern below.
 * The substep counter comes from a second site, inside the thing draw and deliberately not at its
 * entry, because another feature detours that entry and a pattern anchored there would stop
 * matching once it has. Both patterns resolve to exactly one match in each of the six shipped
 * executables, including the recompiled one, where the functions sit at the same addresses but
 * every cell has moved.
 *
 * The guard that is not optional. Our damper runs inside the player phases, and three of the
 * fourteen player modes skip both of those phases, and two more skip one of them, while the
 * engine's substep counter and its
 * interpolation weight keep running. Without the guard the pair stays frozen apart while the weight
 * sweeps zero to one every 1/32 s, and the body sweeps the same few degrees thirty-two times a
 * second, for as long as that mode lasts. That is worse than the staircase it replaces. Collapsing
 * the pair on any substep our damper did not run in prevents it, and it has to happen before any
 * early return, or a frame we decline to write still leaves the pair open.
 * ============================================================================================== */

/* bapobj_drawAll's own prologue and the first block of its frame set-up. The five absolute operands
 * are wildcarded; three of them are what this file needs and are read back out. */
static const uint8_t SIG_OBJECT_DRAW_ALL[] = {
    0x55, 0x8B, 0xEC, 0x81, 0xEC, 0xD8, 0x04, 0x00, 0x00,
    0xC7, 0x85, 0xF0, 0xFB, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xC7, 0x85, 0xE4, 0xFB, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xC7, 0x85, 0xDC, 0xFB, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xC7, 0x85, 0xA8, 0xFB, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xC7, 0x85, 0xEC, 0xFB, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xA1, 0x00, 0x00, 0x00, 0x00,
    0x89, 0x85, 0xE0, 0xFB, 0xFF, 0xFF,
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x74, 0x0C,
    0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00,
    0x89, 0x8D, 0xE0, 0xFB, 0xFF, 0xFF,
    0x8B, 0x95, 0xE0, 0xFB, 0xFF, 0xFF,
    0x89, 0x15, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t MSK_OBJECT_DRAW_ALL[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
};
_Static_assert(sizeof SIG_OBJECT_DRAW_ALL == sizeof MSK_OBJECT_DRAW_ALL,
               "the thing draw pattern and its mask are different lengths");
#define OBJECT_DRAW_ALL_PROLOGUE   9u
#define OFFSET_ALPHA_GLOBAL     0x3Cu    /* behind A1     mov eax,[abs32]   */
#define OFFSET_ALPHA_GATE       0x48u    /* behind 83 3D  cmp dword [abs32] */
#define OFFSET_ALPHA_LATCH      0x51u    /* behind 8B 0D  mov ecx,[abs32]   */

/* The substep counter, and the two bytes that say whether the pose is still throttled. Both come
 * from one site inside the thing draw, deliberately not from its entry: another feature detours
 * that entry and a pattern starting there would stop matching once it has. The two throttle bytes
 * are wildcarded so this resolves whether or not that patch is installed. */
static const uint8_t SIG_POSE_CLOCK[] = {
    0x8B, 0x3D, 0x00, 0x00, 0x00, 0x00,
    0x8D, 0x4C, 0xCA, 0x24,
    0x89, 0x0D, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x48, 0x1C,
    0x3B, 0xCF,
    0x00, 0x00,
    0x8D, 0x54, 0x24, 0x24,
    0x52, 0x50,
    0xE8, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t MSK_POSE_CLOCK[] = {
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00
};
_Static_assert(sizeof SIG_POSE_CLOCK == sizeof MSK_POSE_CLOCK,
               "the pose clock pattern and its mask are different lengths");
#define OFFSET_SUBSTEP_COUNTER  0x02u
#define OFFSET_THROTTLE_BYTES   0x15u

/* The two bytes stand as a conditional jump until the pose throttle is removed, and as two NOPs
 * afterwards. Only in the second case does the compositor rebuild on every rendered frame, and only
 * then is there anything for an interpolated angle to be drawn into. */
#define THROTTLE_REMOVED        0x9090u

static bool resolve_cell(uintptr_t site, unsigned offset, const void **out)
{
    uint32_t address = 0;

    if (!memory_read_u32(site + offset, &address) ||
        !memory_is_inside_image(address, sizeof(uint32_t))) {
        return false;
    }
    *out = (const void *)(uintptr_t)address;
    return true;
}

/* The engine's own choice, read exactly as it is about to make it a few instructions after we
 * return: a frozen weight when the gate is set, the live one otherwise. Reading only the live one
 * would be wrong on every frame the pause gate skipped the simulation while the draw kept
 * running. */
static float current_alpha(void)
{
    float alpha;

    if (*strafe_state.throttle_bytes != THROTTLE_REMOVED) {
        return 1.0f;                  /* the pose is still throttled: draw what we always drew */
    }

    alpha = (*strafe_state.alpha_gate != 0u) ? *strafe_state.alpha_latch
                                             : *strafe_state.alpha_global;

    /* The engine's own arithmetic keeps this inside (0, 1], so this is a bolt rather than a
     * correction. It stays because the product is written straight into the drawn pose, and a NaN
     * there is a character that silently disappears. The !(>) form catches NaN as well. */
    if (!(alpha > 0.0f)) {
        return 0.0f;
    }
    return (alpha > 1.0f) ? 1.0f : alpha;
}

/* At 32 substeps a second this is a quarter of a second, which is longer than the settle itself
 * and far longer than any gap the damper leaves while it is genuinely running. */
#define CLAIM_STALE_SUBSTEPS 8u

static void draw_interpolated_angle(void)
{
    uint8_t *record;
    void    *body;
    uint32_t tick;
    float    drawn;

    if (!strafe_state.owns_node) {
        return;
    }

    /* FIRST, before anything can return early. See the guard note above. */
    tick = *strafe_state.substep_counter;
    if (tick != strafe_state.previous_tick) {
        if (!strafe_state.damped_this_substep) {
            strafe_state.theta_previous = strafe_state.theta_degrees;
        }
        strafe_state.damped_this_substep = false;
        strafe_state.previous_tick       = tick;
    }

    /* The record belongs to the substep that handed it over, and the damper runs in every substep
     * it owns the node for, including the whole settle back to zero. So more than a handful of
     * substeps since the last one is not a slow settle, it is the damper having stopped: a level
     * opened, or the player left the ground into something that owns the root itself. The record
     * from before that is a pointer into memory the engine may have freed, and this runs on every
     * drawn frame, so the claim is dropped rather than followed. The difference is unsigned, so a
     * counter that restarted with the level reads as a very large gap.
     *
     * Nothing is lost by dropping it: the next substep that drives a strafe opens a new one. */
    if ((uint32_t)(tick - strafe_state.opened_tick) > CLAIM_STALE_SUBSTEPS) {
        strafe_state.owns_node = false;
        strafe_state.record    = NULL;
        return;
    }

    record = strafe_state.record;
    if (record == NULL || strafe_state.set_node_yaw == NULL || !strafe_state.turns_body) {
        return;
    }
    /* The substep count above only ages the claim while substeps run. Between a level being torn
     * down and its loading screen ticking the counter the simulation is stopped, frames are still
     * drawn, and the record is a pointer into memory the engine may have given back.
     *
     * Two tests, and the readable one is not enough on its own. Readable is not live: a record
     * the engine has released but whose page is still committed reads fine, and the body pointer
     * read out of it is then whatever the allocator left there. So the claim is first held
     * against the cell the engine itself loads the record from, pPlayer, read out of Plr_Steer's
     * own operand at install and inside the image, so a plain read is the whole check. A record
     * that is not what pPlayer holds now is not the player, whatever it reads as. Then the guarded
     * read, which costs no system call, for the case where the cell still names memory that has
     * since been unmapped. */
    if (strafe_state.player_cell != NULL && *strafe_state.player_cell != record) {
        strafe_state.owns_node = false;
        strafe_state.record    = NULL;
        return;
    }
    if (!memory_try_readable((uintptr_t)record, PLAYER_DROP_TIMER + sizeof(float))) {
        strafe_state.owns_node = false;
        strafe_state.record    = NULL;
        return;
    }
    if (read_field(record, PLAYER_DROP_TIMER) != 0.0f) {
        return;                        /* the flinch owns the root node while it runs */
    }
    body = *(void *const *)(record + PLAYER_ACTOR);
    if (body == NULL) {
        return;
    }

    drawn = strafe_state.theta_previous
          + (strafe_state.theta_degrees - strafe_state.theta_previous) * current_alpha();
    if (!isfinite(drawn)) {
        return;
    }

    strafe_state.set_node_yaw(body, 0, drawn);

    /* The claim is dropped only after an exact zero has been WRITTEN, which is one substep later
     * than the angle reaching zero. Dropping it as soon as the angle is zero would stop the writing
     * while the last frames were still interpolating toward it, and the body would stay a fraction
     * of a degree off for good. */
    if (strafe_state.theta_previous == 0.0f && strafe_state.theta_degrees == 0.0f) {
        strafe_state.owns_node = false;
    }
}

static int32_t __cdecl hook_object_draw(void *argument)
{
    draw_interpolated_angle();
    return strafe_state.draw_original(argument);
}

static void strafe_draw_install(void)
{
    uintptr_t draw_site;
    uintptr_t clock_site;
    uint32_t  counter = 0;

    if (strafe_state.installed) {
        return;
    }

    draw_site = signature_find_detour_target(SIG_OBJECT_DRAW_ALL, MSK_OBJECT_DRAW_ALL,
                                             sizeof SIG_OBJECT_DRAW_ALL, OBJECT_DRAW_ALL_PROLOGUE);
    if (draw_site == 0) {
        log_warning("the object draw did not resolve, so the strafe body angle keeps being drawn "
                    "straight off the simulation clock and steps once per simulation step. "
                    "Everything else about walking sideways is unaffected.");
        return;
    }

    clock_site = signature_find_unique(SIG_POSE_CLOCK, MSK_POSE_CLOCK, sizeof SIG_POSE_CLOCK);
    if (clock_site == 0) {
        log_warning("the substep counter did not resolve, so the strafe body angle is left on the "
                    "simulation clock. Interpolating without it would be worse than not "
                    "interpolating: the guard against a frozen pair is built on that counter.");
        return;
    }

    if (!memory_read_u32(clock_site + OFFSET_SUBSTEP_COUNTER, &counter) ||
        !memory_is_inside_image(counter, sizeof(uint32_t)) ||
        !resolve_cell(draw_site, OFFSET_ALPHA_GLOBAL, (const void **)&strafe_state.alpha_global) ||
        !resolve_cell(draw_site, OFFSET_ALPHA_GATE,   (const void **)&strafe_state.alpha_gate) ||
        !resolve_cell(draw_site, OFFSET_ALPHA_LATCH,  (const void **)&strafe_state.alpha_latch)) {
        log_warning("a cell behind the object draw did not resolve, so the strafe body angle is "
                    "left on the simulation clock");
        return;
    }
    strafe_state.substep_counter = (const volatile uint32_t *)(uintptr_t)counter;
    strafe_state.throttle_bytes  = (const volatile uint16_t *)(clock_site + OFFSET_THROTTLE_BYTES);

    if (!detour_install(&strafe_state.draw, draw_site, (const void *)hook_object_draw,
                        OBJECT_DRAW_ALL_PROLOGUE)) {
        log_warning("the detour on the object draw at %08X failed, so the strafe body angle is "
                    "left on the simulation clock", (unsigned)draw_site);
        return;
    }

    strafe_state.draw_original = (object_draw_fn_t)strafe_state.draw.original;
    strafe_state.installed     = true;

    log_info("the strafe body angle is now DRAWN interpolated, at %08X, once per rendered frame on "
             "the engine's own weight. It is still damped once per simulation step, which is where "
             "a damper belongs; what changed is that the frames between two steps no longer all "
             "show the same orientation. Above about 60 frames a second that was a visible "
             "staircase on the quarter second ease into and out of walking sideways, because the "
             "root node is multiplied in after the animation blend and nothing downstream smooths "
             "it. The pair is collapsed on any simulation step our damper did not run in, which is "
             "what keeps the three player modes that skip our phases from sweeping the same few "
             "degrees thirty-two times a second.", (unsigned)draw_site);
}
