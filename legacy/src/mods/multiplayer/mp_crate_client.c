/* mp_crate_client.c: a client's side of the push blocks. See the header.
 *
 * SIZE NOTE: a little over 600 lines, two halves that share the block record and nothing else:
 * putting the host's blocks right at the start of a substep, and this player's own push with its
 * wishes. The second half, from the gate to the wish sent, is the seam when this file grows.
 */
#include "mp_crate_client.h"

#include "mp_crate_play.h"
#include "mp_crate_rule.h"
#include "mp_crate_wire.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static bool held_within(uint32_t since, uint32_t now, uint32_t ticks)
{
    return (int32_t)(now - since) < (int32_t)ticks;
}

static float length3(const float v[3])
{
    return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

static float distance3(const float a[3], const float b[3])
{
    float d[3];

    d[0] = b[0] - a[0];
    d[1] = b[1] - a[1];
    d[2] = b[2] - a[2];
    return length3(d);
}

/* The block record for `id`, made on first mention. Sixteen are more than any level holds, and
 * the host describes no more than that, so a full table only meets a message that is not one. */
static mp_crate_client_block_t *block_of(mp_crate_client_t *client, uint32_t id)
{
    size_t i;

    if (id > MP_CRATE_ID_MAX) {
        return NULL;   /* no mover index a message carries */
    }
    for (i = 0; i < MP_CRATE_MAX; ++i) {
        if (client->block[i].used && client->block[i].id == id) {
            return &client->block[i];
        }
    }
    for (i = 0; i < MP_CRATE_MAX; ++i) {
        if (!client->block[i].used) {
            memset(&client->block[i], 0, sizeof client->block[i]);
            client->block[i].used = true;
            client->block[i].id   = (uint8_t)id;
            return &client->block[i];
        }
    }
    return NULL;
}

void mp_crate_client_init(mp_crate_client_t *client)
{
    memset(client, 0, sizeof *client);
}

void mp_crate_client_level(mp_crate_client_t *client, uint16_t level)
{
    size_t i;

    if (client->level_known && client->level == level) {
        return;
    }
    if (client->level_known) {
        /* Another level: every block and every fall named a mover of the one that is gone. */
        memset(client->block, 0, sizeof client->block);
        client->falls       = 0u;
        client->pending     = false;
        client->host_speaks = false;
        client->whole_seen  = false;
    } else {
        /* The first level this side knows: what arrived before it counts only if it named it. */
        for (i = 0; i < MP_CRATE_MAX; ++i) {
            if (client->block[i].used && client->block[i].host_level != level) {
                client->block[i].known = false;
            }
        }
    }
    client->level_known = true;
    client->level       = level;
}

static bool generation_current(mp_crate_client_t *client, uint32_t generation)
{
    if (client->generation_known && (int32_t)(generation - client->newest_generation) < 0) {
        ++client->stats.stale_generation;
        return false;
    }
    client->generation_known  = true;
    client->newest_generation = generation;
    return true;
}

static void take_note(mp_crate_client_t *client, const mp_crate_note_t *note, uint32_t now)
{
    size_t i;

    if (note->whole) {
        ++client->stats.whole_taken;
        client->whole_seen = true;
    } else {
        ++client->stats.change_taken;
        client->stats.change_before_whole += client->whole_seen ? 0u : 1u;
    }
    client->host_speaks = true;
    for (i = 0; i < note->count; ++i) {
        mp_crate_client_block_t *block = block_of(client, note->entry[i].id);

        if (block == NULL) {
            continue;
        }
        /* Each entry stands for itself at the host's tick of its note, so a whole note the
         * channel held back cannot undo a newer change. */
        if (block->known && (int32_t)(note->tick - block->host_tick) < 0) {
            ++client->stats.older;
            continue;
        }
        block->known      = true;
        block->host       = note->entry[i];
        block->host_level = note->level;
        block->host_tick  = note->tick;
        block->host_at    = now;
        block->fresh      = true;
    }
}

bool mp_crate_client_take(mp_crate_client_t *client, const uint8_t *note, size_t bytes,
                          uint32_t now)
{
    mp_crate_note_t arrived;
    mp_crate_fall_t fall;

    if (mp_crate_is_note(note, bytes)) {
        if (!mp_crate_note_decode(note, bytes, &arrived)) {
            ++client->stats.torn;
        } else if (client->level_known && arrived.level != client->level) {
            ++client->stats.elsewhere;
        } else if (generation_current(client, arrived.generation)) {
            take_note(client, &arrived, now);
        }
        return true;
    }
    if (!mp_crate_is_fall(note, bytes)) {
        return false;
    }
    if (!mp_crate_fall_decode(note, bytes, &fall)) {
        ++client->stats.torn;
    } else if (client->level_known && fall.level != client->level) {
        ++client->stats.elsewhere;
    } else if (generation_current(client, fall.generation) &&
               client->falls < MP_CRATE_FALL_INBOX) {
        client->fall[client->falls++] = fall;
    }
    return true;
}

/* ==============================================================================================
 * Putting blocks where the host has them.
 * ============================================================================================ */

static void forget_wishes(mp_crate_client_block_t *block)
{
    block->target_waiting = false;
    block->first          = false;
    block->correcting     = false;
    block->mine           = false;
    memset(block->remembered_used, 0, sizeof block->remembered_used);
}

static const float *remembered(const mp_crate_client_block_t *block, uint16_t sequence)
{
    size_t at = sequence % MP_CRATE_REMEMBERED;

    if (!block->remembered_used[at] || block->remembered_sequence[at] != sequence) {
        return NULL;
    }
    return block->remembered_target[at];
}

static void chase(mp_crate_client_t *client, const mp_crate_engine_t *engine,
                  mp_crate_client_block_t *block, const mp_crate_body_t *body, bool fresh)
{
    float out[3];

    switch (mp_crate_rule_chase(body->position, block->host.position, out)) {
    case MP_CRATE_CHASE_STEP:
        (void)engine->place(engine->context, block->id, out, block->host.flags);
        ++client->stats.chase_steps;
        client->stats.chased += fresh ? 1u : 0u;
        break;
    case MP_CRATE_CHASE_JUMP:
    {
        float distance = distance3(body->position, block->host.position);

        (void)engine->place(engine->context, block->id, block->host.position, block->host.flags);
        ++client->stats.jumped;
        if (distance > client->stats.worst_jump) {
            client->stats.worst_jump = distance;
        }
        break;
    }
    case MP_CRATE_CHASE_AGREED:
    default:
        client->stats.agreed += fresh ? 1u : 0u;
        break;
    }
}

/* This player's own block: the host's answer on a wish, and a correction drawn out over the
 * substeps at the catch up pace, so the player's own push goes on moving it meanwhile. */
static void reconcile(mp_crate_client_t *client, const mp_crate_engine_t *engine,
                      mp_crate_client_block_t *block, const mp_crate_body_t *body, uint8_t my_slot,
                      bool fresh, uint32_t now)
{
    float correction[3];
    float size;

    if (fresh && block->host.pusher == my_slot) {
        const mp_crate_entry_t *host = &block->host;

        if (!block->taken_known || mp_crate_rule_sequence_newer(host->sequence, block->taken)) {
            block->taken       = host->sequence;
            block->taken_known = true;
        }
        block->mine_since = now;
        switch (mp_crate_rule_reconcile(host->verdict, host->position,
                                        remembered(block, host->sequence), correction)) {
        case MP_CRATE_RECONCILE_CORRECT:
            size = length3(correction);
            ++client->stats.corrections;
            if (size > client->stats.worst_correction) {
                client->stats.worst_correction = size;
            }
            memcpy(block->correction, correction, sizeof correction);
            block->correcting = true;
            break;
        case MP_CRATE_RECONCILE_TO_HOST:
            forget_wishes(block);
            block->locked       = true;
            block->locked_until = now + MP_CRATE_LOCKOUT_TICKS;
            return;
        case MP_CRATE_RECONCILE_NOTHING:
        default:
            break;
        }
        if (host->verdict != MP_CRATE_VERDICT_ON_THE_WAY && host->sequence == block->sequence &&
            !block->pushing && !block->target_waiting) {
            block->mine = false;   /* the last wish is answered and nothing more is coming */
        }
    }
    if (!block->correcting) {
        return;
    }
    size = length3(block->correction);
    {
        float out[3];
        float scale = size > MP_CRATE_JUMP || size <= MP_CRATE_STEP_MAX ? 1.0f
                                                                        : MP_CRATE_STEP_MAX / size;
        size_t axis;

        for (axis = 0; axis < 3u; ++axis) {
            float part = block->correction[axis] * scale;

            out[axis] = body->position[axis] + part;
            block->correction[axis] -= part;
        }
        (void)engine->place(engine->context, block->id, out, body->flags);
        block->correcting = scale < 1.0f;
    }
}

static void sink(mp_crate_client_t *client, const mp_crate_engine_t *engine,
                 mp_crate_client_block_t *block)
{
    (void)engine->place(engine->context, block->id, block->host.position, block->host.flags);
    if (engine->sink(engine->context, block->id)) {
        ++client->stats.sinks_performed;
    }
    block->sink_held    = false;
    block->sink_waiting = false;
    forget_wishes(block);
}

static void judge(mp_crate_client_t *client, const mp_crate_engine_t *engine,
                  mp_crate_client_block_t *block, uint8_t my_slot, uint32_t now)
{
    const mp_crate_entry_t *host = &block->host;
    mp_crate_body_t         body;
    mp_crate_view_t         view;
    bool                    fresh = block->fresh;

    block->fresh = false;
    if (!engine->read(engine->context, block->id, &body)) {
        return;
    }
    if (block->mine && !block->pushing && !held_within(block->mine_since, now,
                                                       MP_CRATE_ANSWER_TICKS)) {
        forget_wishes(block);   /* no word from the host on it for two seconds: given up */
    }
    memset(&view, 0, sizeof view);
    view.host_sunk    = host->kind != MP_CRATE_KIND_BLOCK;
    view.host_falling = (host->flags & MP_CRATE_FLAG_FALLING) != 0u;
    view.host_carried = (host->flags & MP_CRATE_FLAG_CARRIED) != 0u;
    view.here_sunk    = body.kind != (int32_t)MP_CRATE_KIND_BLOCK;
    view.here_falling = (body.flags & MP_CRATE_FLAG_FALLING) != 0u;
    view.same_carrier = (body.flags & MP_CRATE_FLAG_CARRIED) != 0u &&
                        body.carrier == host->carrier;
    view.sink_held    = block->sink_held;
    view.mine         = block->mine;
    view.other_pusher = host->pusher != MP_CRATE_NOBODY && host->pusher != my_slot;
    view.flags_agree  = mp_crate_rule_flags_agree(body.flags, host->flags);
    view.distance     = distance3(body.position, host->position);
    if (view.host_sunk && view.here_falling && !view.here_sunk) {
        if (!block->sink_waiting) {
            block->sink_waiting    = true;
            block->sink_wait_since = now;
        }
        view.sink_wait_over = !held_within(block->sink_wait_since, now, MP_CRATE_SINK_WAIT_TICKS);
    }

    switch (mp_crate_rule_decide(&view)) {
    case MP_CRATE_ACT_BOTH_SUNK:
    case MP_CRATE_ACT_HANG:
    case MP_CRATE_ACT_AGREED:
        client->stats.agreed += fresh ? 1u : 0u;
        break;
    case MP_CRATE_ACT_WAIT_TO_SINK:
    case MP_CRATE_ACT_WAIT_HOST_FALL:
    case MP_CRATE_ACT_WAIT_HERE_FALL:
        client->stats.held_falling += fresh ? 1u : 0u;
        break;
    case MP_CRATE_ACT_SINK:
        sink(client, engine, block);
        break;
    case MP_CRATE_ACT_UNREPAIRABLE:
        client->stats.unrepairable += fresh ? 1u : 0u;
        break;
    case MP_CRATE_ACT_SECOND:
        /* Somebody else holds it: this player's wishes will never be taken, so they go, and the
         * player's push stays off the block for a second, as after a refusal. */
        forget_wishes(block);
        block->locked       = true;
        block->locked_until = now + MP_CRATE_LOCKOUT_TICKS;
        chase(client, engine, block, &body, fresh);
        break;
    case MP_CRATE_ACT_RECONCILE:
        reconcile(client, engine, block, &body, my_slot, fresh, now);
        break;
    case MP_CRATE_ACT_CANCEL_SINK:
        (void)engine->place(engine->context, block->id, host->position, host->flags);
        block->sink_held = false;
        ++client->stats.sinks_cancelled;
        break;
    case MP_CRATE_ACT_JUMP:
        /* A carried block is put on its carrier once, when the host's word is new; the engine's
         * attach then carries it, and a carrier it cannot find here is not tried every substep. */
        if (!view.host_carried || fresh) {
            chase(client, engine, block, &body, fresh);
        }
        break;
    case MP_CRATE_ACT_CHASE:
    default:
        chase(client, engine, block, &body, fresh);
        break;
    }
}

static void run_falls(mp_crate_client_t *client, const mp_crate_engine_t *engine)
{
    size_t i;

    for (i = 0; i < client->falls; ++i) {
        const mp_crate_fall_t *fall = &client->fall[i];
        mp_crate_body_t        body;

        if (!engine->read(engine->context, fall->id, &body) ||
            body.kind != (int32_t)MP_CRATE_KIND_BLOCK) {
            continue;
        }
        if ((body.flags & MP_CRATE_FLAG_FALLING) != 0u) {
            ++client->stats.falls_already;   /* this player's own push had it over the edge */
            continue;
        }
        if (engine->fall(engine->context, fall->id, fall->position, fall->direction) == 1) {
            ++client->stats.falls_performed;
        } else {
            ++client->stats.falls_otherwise;
        }
    }
    client->falls = 0u;
}

void mp_crate_client_apply(mp_crate_client_t *client, const mp_crate_engine_t *engine,
                           uint8_t my_slot, uint32_t now)
{
    size_t i;

    if (!client->level_known) {
        return;
    }
    run_falls(client, engine);
    for (i = 0; i < MP_CRATE_MAX; ++i) {
        mp_crate_client_block_t *block = &client->block[i];

        if (block->used && block->known && block->host_level == client->level) {
            judge(client, engine, block, my_slot, now);
        }
    }
}

/* ==============================================================================================
 * This player's own push.
 * ============================================================================================ */

mp_crate_gate_t mp_crate_client_gate(mp_crate_client_t *client, const mp_crate_engine_t *engine,
                                     uint32_t id, uint8_t my_slot, uint32_t now)
{
    mp_crate_client_block_t *block = block_of(client, id);
    mp_crate_gate_view_t     view;
    mp_crate_body_t          body;
    mp_crate_gate_t          gate;

    ++client->stats.push_calls;
    if (block == NULL) {
        ++client->stats.let_through;
        return MP_CRATE_GATE_LET;
    }
    memset(&view, 0, sizeof view);
    view.host_speaks = client->host_speaks;
    view.ahead       = mp_crate_rule_sequence_ahead(block->sequence, block->taken);
    view.locked      = block->locked && (int32_t)(now - block->locked_until) < 0;
    view.falling_or_sunk = block->sink_held;
    if (engine->read(engine->context, id, &body)) {
        view.falling_or_sunk = view.falling_or_sunk ||
                               body.kind != (int32_t)MP_CRATE_KIND_BLOCK ||
                               (body.flags & MP_CRATE_FLAG_FALLING) != 0u;
    }
    if (block->known) {
        view.other_holds = block->host.pusher != MP_CRATE_NOBODY &&
                           block->host.pusher != my_slot &&
                           held_within(block->host_at, now, MP_CRATE_OWNER_TICKS);
        view.falling_or_sunk = view.falling_or_sunk ||
                               block->host.kind != MP_CRATE_KIND_BLOCK ||
                               (block->host.flags & MP_CRATE_FLAG_FALLING) != 0u;
    }
    gate = mp_crate_rule_gate(&view);
    switch (gate) {
    case MP_CRATE_GATE_OTHER_OWNER:
        ++client->stats.refused_owner;
        break;
    case MP_CRATE_GATE_AHEAD:
        ++client->stats.refused_ahead;
        break;
    case MP_CRATE_GATE_LOCKED:
        ++client->stats.refused_locked;
        break;
    case MP_CRATE_GATE_FALLING_OR_SUNK:
        ++client->stats.refused_falling;
        break;
    case MP_CRATE_GATE_LET:
    default:
        ++client->stats.let_through;
        break;
    }
    return gate;
}

void mp_crate_client_pushed(mp_crate_client_t *client, uint32_t id, int32_t answer,
                            const mp_crate_body_t *after, bool pull, uint32_t now)
{
    mp_crate_client_block_t *block = block_of(client, id);

    if (block == NULL) {
        return;
    }
    block->push_now   = true;
    block->mine       = true;
    block->mine_since = now;
    memcpy(block->target, after->position, sizeof block->target);
    if (answer != 0 || (after->flags & MP_CRATE_FLAG_FALLING) != 0u) {
        block->target_waiting = true;
        block->first          = block->first || !block->pushing;
        block->push_flags     = pull ? MP_CRATE_PUSH_PULL : MP_CRATE_PUSH_FORWARD;
        return;
    }
    /* Refused by this side's own engine: the player goes back to standing, and the host is told
     * where the block was left. */
    block->let_go_waiting = block->pushing || block->target_waiting;
}

void mp_crate_client_hold_sink(mp_crate_client_t *client, uint32_t id)
{
    mp_crate_client_block_t *block = block_of(client, id);

    if (block == NULL || block->sink_held) {
        return;
    }
    block->sink_held = true;
    ++client->stats.sinks_held;
}

void mp_crate_client_end_substep(mp_crate_client_t *client, uint32_t now)
{
    size_t i;

    (void)now;
    for (i = 0; i < MP_CRATE_MAX; ++i) {
        mp_crate_client_block_t *block = &client->block[i];

        if (!block->used) {
            continue;
        }
        if (block->pushing && !block->push_now) {
            block->let_go_waiting = true;
            /* A block the host took back is left where the host has it: a let-go must not send
             * the host pushing it toward a place it refused. */
            if (!block->mine && block->known) {
                memcpy(block->target, block->host.position, sizeof block->target);
            }
        }
        block->pushing  = block->push_now;
        block->push_now = false;
    }
}

size_t mp_crate_client_next_push(mp_crate_client_t *client, uint16_t level, uint32_t generation,
                                 uint32_t now, uint8_t *buffer, size_t capacity)
{
    size_t i;

    client->pending = false;
    for (i = 0; i < MP_CRATE_MAX; ++i) {
        mp_crate_client_block_t *block = &client->block[i];
        mp_crate_push_t          push;
        size_t                   bytes;

        if (!block->used || (!block->let_go_waiting && !block->target_waiting) ||
            !mp_crate_rule_push_due(block->first, block->let_go_waiting, now, block->last_sent)) {
            continue;
        }
        memset(&push, 0, sizeof push);
        push.tick       = now;
        push.level      = level;
        push.generation = generation;
        push.id         = block->id;
        push.sequence   = (uint16_t)(block->sequence + 1u);
        push.flags      = block->push_flags != 0u ? block->push_flags : MP_CRATE_PUSH_FORWARD;
        if (block->let_go_waiting) {
            push.flags = (uint8_t)(push.flags | MP_CRATE_PUSH_LET_GO);
        }
        memcpy(push.target, block->target, sizeof push.target);
        bytes = mp_crate_push_encode(&push, buffer, capacity);
        if (bytes == 0u) {
            block->target_waiting = false;   /* a target the wire cannot carry is not sent */
            block->let_go_waiting = false;
            continue;
        }
        client->pending          = true;
        client->pending_block    = i;
        client->pending_sequence = push.sequence;
        client->pending_let_go   = block->let_go_waiting;
        return bytes;
    }
    return 0u;
}

void mp_crate_client_push_sent(mp_crate_client_t *client, bool sent, uint32_t now)
{
    mp_crate_client_block_t *block;
    size_t                   at;

    if (!client->pending) {
        return;
    }
    client->pending = false;
    if (!sent) {
        ++client->stats.wish_refused;
        return;
    }
    block             = &client->block[client->pending_block];
    block->sequence   = client->pending_sequence;
    block->sent_any   = true;
    block->last_sent  = now;
    block->mine_since = now;
    at = block->sequence % MP_CRATE_REMEMBERED;
    block->remembered_used[at]     = true;
    block->remembered_sequence[at] = block->sequence;
    memcpy(block->remembered_target[at], block->target, sizeof block->target);
    block->target_waiting = false;
    block->first          = false;
    if (client->pending_let_go) {
        block->let_go_waiting = false;
        ++client->stats.let_go_sent;
    } else {
        block->mine = true;
        ++client->stats.wishes_sent;
    }
}
