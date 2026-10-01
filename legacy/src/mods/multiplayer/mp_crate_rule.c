/* mp_crate_rule.c: the decisions about a push block. See the header. */
#include "mp_crate_rule.h"

#include "mp_crate_wire.h"
#include "mp_wire.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The three low bits of the authored rig make a mover a push block; the engine's own level reset
 * forces every such mover back to kind 7 by exactly that test. Bit 0 sinks it where it lands. */
#define RIG_BLOCK 0x7
#define RIG_SINKS 0x1

/* What the carried and sink bits say, which a client puts right by placing a block; the carrying
 * bit belongs to the block riding it and the falling bit to the drop. */
#define FLAGS_COMPARED (MP_CRATE_FLAG_CARRIED | MP_CRATE_FLAG_SINK)

static bool held_within(uint32_t last, uint32_t now, uint32_t ticks)
{
    return (int32_t)(now - last) < (int32_t)ticks;
}

/* ==============================================================================================
 * Who holds a block.
 * ============================================================================================ */

void mp_crate_owner_init(mp_crate_owner_t *owner)
{
    owner->slot = (uint8_t)MP_CRATE_NOBODY;
    owner->last = 0u;
}

uint8_t mp_crate_owner_holder(const mp_crate_owner_t *owner, uint32_t now)
{
    if (owner->slot == MP_CRATE_NOBODY || !held_within(owner->last, now, MP_CRATE_OWNER_TICKS)) {
        return (uint8_t)MP_CRATE_NOBODY;
    }
    return owner->slot;
}

bool mp_crate_owner_claim(mp_crate_owner_t *owner, uint8_t slot, uint32_t now)
{
    uint8_t holder = mp_crate_owner_holder(owner, now);

    if (holder != MP_CRATE_NOBODY && holder != slot) {
        return false;
    }
    owner->slot = slot;
    owner->last = now;
    return true;
}

void mp_crate_owner_release(mp_crate_owner_t *owner, uint8_t slot)
{
    if (owner->slot == slot) {
        owner->slot = (uint8_t)MP_CRATE_NOBODY;
    }
}

/* ==============================================================================================
 * What a mover is.
 * ============================================================================================ */

bool mp_crate_rule_is_block(int32_t rig_flags)
{
    return (rig_flags & RIG_BLOCK) != 0;
}

bool mp_crate_rule_can_sink(int32_t rig_flags)
{
    return (rig_flags & RIG_SINKS) != 0;
}

mp_crate_index_t mp_crate_rule_index(int32_t id, uint32_t movers, bool record, int32_t rig_flags,
                                     int32_t kind)
{
    if (id < 0 || (uint32_t)id >= movers) {
        return MP_CRATE_INDEX_OUT_OF_RANGE;
    }
    if (!record) {
        return MP_CRATE_INDEX_NO_RECORD;
    }
    if (!mp_crate_rule_is_block(rig_flags)) {
        return MP_CRATE_INDEX_NOT_A_BLOCK;
    }
    if (kind != (int32_t)MP_CRATE_KIND_BLOCK) {
        return MP_CRATE_INDEX_SUNK;
    }
    return MP_CRATE_INDEX_OK;
}

bool mp_crate_rule_opener_is_sink(int32_t rig_flags, int32_t kind)
{
    return mp_crate_rule_is_block(rig_flags) && kind != (int32_t)MP_CRATE_KIND_BLOCK;
}

uint8_t mp_crate_rule_merge_flags(uint8_t here, uint8_t host)
{
    return (uint8_t)((here & ~MP_CRATE_FLAG_SINK) | (host & MP_CRATE_FLAG_SINK));
}

/* ==============================================================================================
 * The host's step.
 * ============================================================================================ */

float mp_crate_rule_flat_distance(const float a[3], const float b[3])
{
    float dx = b[0] - a[0];
    float dy = b[1] - a[1];

    return sqrtf(dx * dx + dy * dy);
}

bool mp_crate_rule_step_to(const float at[3], const float target[3], float step[3])
{
    float distance = mp_crate_rule_flat_distance(at, target);
    float scale;

    step[0] = 0.0f;
    step[1] = 0.0f;
    step[2] = 0.0f;
    if (!(distance > MP_CRATE_AGREE)) {
        return false;   /* also a distance that is not a number: nothing to push toward */
    }
    scale   = distance > MP_CRATE_STEP_MAX ? MP_CRATE_STEP_MAX / distance : 1.0f;
    step[0] = (target[0] - at[0]) * scale;
    step[1] = (target[1] - at[1]) * scale;
    return true;
}

mp_crate_after_t mp_crate_rule_after_push(int32_t answer, bool falling, float before, float after)
{
    if (falling) {
        return MP_CRATE_AFTER_FELL;
    }
    if (answer == 0) {
        return MP_CRATE_AFTER_REFUSED;
    }
    if (after <= MP_CRATE_AGREE) {
        return MP_CRATE_AFTER_REACHED;
    }
    if (!(after < before - MP_CRATE_PROGRESS_MIN)) {
        return MP_CRATE_AFTER_STALLED;
    }
    return MP_CRATE_AFTER_MOVING;
}

/* ==============================================================================================
 * A client follows the host.
 * ============================================================================================ */

static float distance3(const float a[3], const float b[3])
{
    float dx = b[0] - a[0];
    float dy = b[1] - a[1];
    float dz = b[2] - a[2];

    return sqrtf(dx * dx + dy * dy + dz * dz);
}

mp_crate_chase_t mp_crate_rule_chase(const float here[3], const float goal[3], float out[3])
{
    float distance = distance3(here, goal);
    float scale;
    size_t axis;

    if (distance <= MP_CRATE_AGREE) {
        memcpy(out, here, 3u * sizeof(float));
        return MP_CRATE_CHASE_AGREED;
    }
    if (!(distance <= MP_CRATE_JUMP)) {
        memcpy(out, goal, 3u * sizeof(float));
        return MP_CRATE_CHASE_JUMP;
    }
    scale = distance > MP_CRATE_STEP_MAX ? MP_CRATE_STEP_MAX / distance : 1.0f;
    for (axis = 0; axis < 3u; ++axis) {
        out[axis] = scale >= 1.0f ? goal[axis] : here[axis] + (goal[axis] - here[axis]) * scale;
    }
    return MP_CRATE_CHASE_STEP;
}

mp_crate_act_t mp_crate_rule_decide(const mp_crate_view_t *view)
{
    if (view->host_sunk && view->here_sunk) {
        return MP_CRATE_ACT_BOTH_SUNK;
    }
    if (view->host_sunk) {
        return view->here_falling && !view->sink_wait_over ? MP_CRATE_ACT_WAIT_TO_SINK
                                                           : MP_CRATE_ACT_SINK;
    }
    if (view->here_sunk) {
        return MP_CRATE_ACT_UNREPAIRABLE;
    }
    if (view->host_falling) {
        return MP_CRATE_ACT_WAIT_HOST_FALL;
    }
    if (view->here_falling) {
        return MP_CRATE_ACT_WAIT_HERE_FALL;
    }
    if (view->mine) {
        return view->other_pusher ? MP_CRATE_ACT_SECOND : MP_CRATE_ACT_RECONCILE;
    }
    if (view->sink_held) {
        return MP_CRATE_ACT_CANCEL_SINK;
    }
    if (view->host_carried) {
        return view->same_carrier ? MP_CRATE_ACT_HANG : MP_CRATE_ACT_JUMP;
    }
    if (view->distance <= MP_CRATE_AGREE && view->flags_agree) {
        return MP_CRATE_ACT_AGREED;
    }
    return view->distance <= MP_CRATE_JUMP ? MP_CRATE_ACT_CHASE : MP_CRATE_ACT_JUMP;
}

mp_crate_gate_t mp_crate_rule_gate(const mp_crate_gate_view_t *view)
{
    if (view->other_holds) {
        return MP_CRATE_GATE_OTHER_OWNER;
    }
    if (view->host_speaks && view->ahead >= MP_CRATE_WINDOW) {
        return MP_CRATE_GATE_AHEAD;
    }
    if (view->locked) {
        return MP_CRATE_GATE_LOCKED;
    }
    if (view->falling_or_sunk) {
        return MP_CRATE_GATE_FALLING_OR_SUNK;
    }
    return MP_CRATE_GATE_LET;
}

mp_crate_reconcile_t mp_crate_rule_reconcile(uint8_t verdict, const float host[3],
                                             const float *target, float correction[3])
{
    size_t axis;

    correction[0] = 0.0f;
    correction[1] = 0.0f;
    correction[2] = 0.0f;
    switch ((mp_crate_verdict_t)verdict) {
    case MP_CRATE_VERDICT_ENGINE:
    case MP_CRATE_VERDICT_OTHER_OWNER:
        return MP_CRATE_RECONCILE_TO_HOST;
    case MP_CRATE_VERDICT_REACHED:
        break;
    case MP_CRATE_VERDICT_ON_THE_WAY:   /* the host has the wish and is pushing toward it */
    case MP_CRATE_VERDICT_STALE:        /* a newer wish of this player is on its way */
    case MP_CRATE_VERDICT_COUNT:
    default:
        return MP_CRATE_RECONCILE_NOTHING;
    }
    if (target == NULL || distance3(host, target) <= MP_CRATE_AGREE) {
        return MP_CRATE_RECONCILE_NOTHING;
    }
    for (axis = 0; axis < 3u; ++axis) {
        correction[axis] = host[axis] - target[axis];
    }
    return MP_CRATE_RECONCILE_CORRECT;
}

bool mp_crate_rule_sequence_newer(uint16_t candidate, uint16_t held)
{
    return (int16_t)(uint16_t)(candidate - held) > 0;
}

uint16_t mp_crate_rule_sequence_ahead(uint16_t sent, uint16_t taken)
{
    int16_t ahead = (int16_t)(uint16_t)(sent - taken);

    return ahead > 0 ? (uint16_t)ahead : 0u;
}

bool mp_crate_rule_push_due(bool first, bool let_go, uint32_t now, uint32_t last_sent)
{
    return first || let_go || (int32_t)(now - last_sent) >= (int32_t)MP_CRATE_PUSH_TICKS;
}

bool mp_crate_rule_is_pull(const float block[3], const float player[3], const float delta[3])
{
    float away_x = block[0] - player[0];
    float away_y = block[1] - player[1];

    return away_x * delta[0] + away_y * delta[1] < 0.0f;
}

bool mp_crate_rule_in_cylinder(const float centre[3], float radius, float height,
                               const float body[3], float body_radius, float body_height)
{
    float dx = body[0] - centre[0];
    float dy = body[1] - centre[1];
    float reach = radius + body_radius;

    if (body[2] + body_height < centre[2] || centre[2] + height < body[2]) {
        return false;
    }
    return dx * dx + dy * dy < reach * reach;
}

/* ==============================================================================================
 * The note's cadence.
 * ============================================================================================ */

void mp_crate_cadence_init(mp_crate_cadence_t *cadence)
{
    memset(cadence, 0, sizeof *cadence);
}

mp_crate_due_t mp_crate_cadence_due(const mp_crate_cadence_t *cadence, uint32_t now, bool changed)
{
    if (!cadence->started || cadence->whole_wanted ||
        !held_within(cadence->last_whole, now, MP_CRATE_WHOLE_TICKS)) {
        return MP_CRATE_DUE_WHOLE;
    }
    if (changed && !held_within(cadence->last_change, now, MP_CRATE_CHANGE_TICKS)) {
        return MP_CRATE_DUE_CHANGE;
    }
    return MP_CRATE_DUE_NONE;
}

void mp_crate_cadence_sent(mp_crate_cadence_t *cadence, mp_crate_due_t due, uint32_t now)
{
    if (due == MP_CRATE_DUE_WHOLE) {
        cadence->started      = true;
        cadence->whole_wanted = false;
        cadence->last_whole   = now;
        cadence->last_change  = now;
    } else if (due == MP_CRATE_DUE_CHANGE) {
        cadence->last_change = now;
    }
}

void mp_crate_cadence_want_whole(mp_crate_cadence_t *cadence)
{
    cadence->whole_wanted = true;
}

/* A position in the wire's own steps, so two values the wire would send alike compare alike. */
static int32_t on_the_wire(float value)
{
    return (int32_t)floorf(value * MP_WIRE_POSITION_SCALE + 0.5f);
}

static bool same_place(const float a[3], const float b[3])
{
    return on_the_wire(a[0]) == on_the_wire(b[0]) && on_the_wire(a[1]) == on_the_wire(b[1]) &&
           on_the_wire(a[2]) == on_the_wire(b[2]);
}

bool mp_crate_rule_entry_differs(const mp_crate_entry_t *sent, const mp_crate_entry_t *now)
{
    bool falling = (now->flags & MP_CRATE_FLAG_FALLING) != 0u &&
                   (sent->flags & MP_CRATE_FLAG_FALLING) != 0u;

    if (sent->id != now->id || sent->kind != now->kind || sent->flags != now->flags ||
        sent->pusher != now->pusher || sent->sequence != now->sequence ||
        sent->verdict != now->verdict) {
        return true;
    }
    if (!falling && !same_place(sent->position, now->position)) {
        return true;
    }
    if ((now->flags & MP_CRATE_FLAG_CARRIED) != 0u &&
        (sent->carrier != now->carrier || sent->carrier_part != now->carrier_part ||
         !same_place(sent->carry_offset, now->carry_offset))) {
        return true;
    }
    return (now->flags & MP_CRATE_FLAG_CARRYING) != 0u && sent->carrying != now->carrying;
}

bool mp_crate_rule_flags_agree(uint8_t here, uint8_t host)
{
    return ((here ^ host) & FLAGS_COMPARED) == 0u;
}
