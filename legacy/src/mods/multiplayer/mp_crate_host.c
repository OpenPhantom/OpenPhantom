/* mp_crate_host.c: the host's side of the push blocks. See the header. */
#include "mp_crate_host.h"

#include "mp_crate_play.h"
#include "mp_crate_rule.h"
#include "mp_crate_wire.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The host's own player pushes as world slot 0. */
#define HOST_SLOT 0u

/* A holder is named in the log at most once a second per block. */
#define SAY_TICKS 32u

static mp_crate_host_block_t *block_of(mp_crate_host_t *host, uint32_t id)
{
    size_t i;

    for (i = 0; i < host->count; ++i) {
        if (host->block[i].id == id) {
            return &host->block[i];
        }
    }
    return NULL;
}

static void reset_blocks(mp_crate_host_t *host)
{
    size_t i;

    memset(host->block, 0, sizeof host->block);
    for (i = 0; i < MP_CRATE_MAX; ++i) {
        mp_crate_owner_init(&host->block[i].owner);
    }
    host->count            = 0u;
    host->filed            = 0u;
    host->fall_head        = 0u;
    host->fall_count       = 0u;
    host->fall_waiting     = false;
    host->generation_known = 0u;
    host->pending_due      = MP_CRATE_DUE_NONE;
    mp_crate_cadence_init(&host->cadence);
}

void mp_crate_host_init(mp_crate_host_t *host)
{
    memset(host, 0, sizeof *host);
    reset_blocks(host);
}

void mp_crate_host_level(mp_crate_host_t *host, const mp_crate_engine_t *engine, uint16_t level,
                         uint32_t generation, uint32_t movers)
{
    uint32_t id;
    uint32_t found    = 0u;
    uint32_t sinkable = 0u;

    if (host->level_known && host->level == level && host->generation == generation &&
        host->movers == movers) {
        return;
    }
    reset_blocks(host);
    host->level_known = true;
    host->level       = level;
    host->generation  = generation;
    host->movers      = movers;

    /* Found by the rig, which does not change when a block sinks. A block past the cap is left
     * undescribed, and the count says it was there. */
    for (id = 0; id < movers && id <= MP_CRATE_ID_MAX; ++id) {
        mp_crate_body_t body;

        if (!engine->read(engine->context, id, &body) || !mp_crate_rule_is_block(body.rig_flags)) {
            continue;
        }
        ++found;
        sinkable += mp_crate_rule_can_sink(body.rig_flags) ? 1u : 0u;
        if (host->count < MP_CRATE_MAX) {
            host->block[host->count].id       = (uint8_t)id;
            host->block[host->count].can_sink = mp_crate_rule_can_sink(body.rig_flags);
            ++host->count;
        }
    }
    /* The level this side is in now, which is what the report's first words say. */
    host->stats.blocks   = found;
    host->stats.can_sink = sinkable;
}

void mp_crate_host_arrival(mp_crate_host_t *host)
{
    mp_crate_cadence_want_whole(&host->cadence);
}

bool mp_crate_host_take(mp_crate_host_t *host, uint8_t slot, const uint8_t *note, size_t bytes)
{
    mp_crate_push_t push;

    if (!mp_crate_is_push(note, bytes)) {
        return false;
    }
    if (!mp_crate_push_decode(note, bytes, &push)) {
        ++host->stats.torn;
        return true;
    }
    if (host->filed >= MP_CRATE_INBOX) {
        ++host->stats.stale;   /* a newer wish of the same client follows within four substeps */
        return true;
    }
    host->inbox[host->filed].slot = slot;
    host->inbox[host->filed].push = push;
    ++host->filed;
    return true;
}

/* A holder change, said once a second per block at most. */
static void say_holder(mp_crate_host_t *host, mp_crate_host_block_t *block, uint8_t slot,
                       uint32_t now)
{
    if (block->said_once && block->said_holder == slot) {
        return;
    }
    if (block->said_once && (int32_t)(now - block->said_at) < (int32_t)SAY_TICKS) {
        return;
    }
    block->said_once   = true;
    block->said_holder = slot;
    block->said_at     = now;
    if (host->say_held != NULL) {
        host->say_held(block->id, slot);
    }
}

static bool generation_current(mp_crate_host_t *host, uint8_t slot, uint32_t generation)
{
    uint16_t bit = (uint16_t)(1u << slot);

    if ((host->generation_known & bit) != 0u &&
        (int32_t)(generation - host->newest_generation[slot]) < 0) {
        return false;
    }
    host->generation_known          = (uint16_t)(host->generation_known | bit);
    host->newest_generation[slot]   = generation;
    return true;
}

/* One filed wish, judged in the order the refusals are counted: level, the index as the engine
 * would not check it, the sequence, the holder. */
static void judge(mp_crate_host_t *host, const mp_crate_engine_t *engine,
                  const mp_crate_host_filed_t *filed, uint32_t now)
{
    const mp_crate_push_t *push = &filed->push;
    mp_crate_host_block_t *block;
    mp_crate_body_t        body;
    bool                   record;
    uint16_t               bit;

    if (filed->slot >= MP_CRATE_SLOT_LIMIT || !host->level_known || push->level != host->level ||
        !generation_current(host, filed->slot, push->generation)) {
        ++host->stats.stale;
        return;
    }
    memset(&body, 0, sizeof body);
    record = engine->read(engine->context, push->id, &body);
    block  = block_of(host, push->id);
    if (mp_crate_rule_index((int32_t)push->id, host->movers, record, body.rig_flags, body.kind) !=
            MP_CRATE_INDEX_OK ||
        block == NULL) {
        ++host->stats.no_block;
        return;
    }
    bit = (uint16_t)(1u << filed->slot);
    if ((block->newest_known & bit) != 0u &&
        !mp_crate_rule_sequence_newer(push->sequence, block->newest[filed->slot])) {
        ++host->stats.stale;
        return;
    }
    block->newest[filed->slot] = push->sequence;
    block->newest_known        = (uint16_t)(block->newest_known | bit);
    if (!mp_crate_owner_claim(&block->owner, filed->slot, now)) {
        ++host->stats.other_owner;
        return;
    }
    ++host->stats.taken;
    say_holder(host, block, filed->slot, now);
    block->working       = true;
    block->wish_slot     = filed->slot;
    block->wish_flags    = push->flags;
    block->wish_sequence = push->sequence;
    block->verdict       = (uint8_t)MP_CRATE_VERDICT_ON_THE_WAY;
    memcpy(block->target, push->target, sizeof block->target);
    block->last_distance = mp_crate_rule_flat_distance(body.position, block->target);
}

/* The work on one block ends with `verdict`; a let-go ends the holding with it. */
static void finish(mp_crate_host_block_t *block, mp_crate_verdict_t verdict)
{
    block->working = false;
    block->verdict = (uint8_t)verdict;
    if ((block->wish_flags & MP_CRATE_PUSH_LET_GO) != 0u) {
        mp_crate_owner_release(&block->owner, block->wish_slot);
    }
}

/* One step of a client's push toward its target, through the engine's own push. */
static void work(mp_crate_host_t *host, const mp_crate_engine_t *engine,
                 mp_crate_host_block_t *block, uint32_t now)
{
    mp_crate_body_t before;
    mp_crate_body_t after;
    float           step[3];
    int32_t         answer;

    if (!engine->read(engine->context, block->id, &before) ||
        before.kind != (int32_t)MP_CRATE_KIND_BLOCK) {
        block->working = false;
        return;
    }
    if ((before.flags & MP_CRATE_FLAG_FALLING) != 0u) {
        return;   /* never pushed while it falls; the engine would refuse it anyway */
    }
    if (!mp_crate_rule_step_to(before.position, block->target, step)) {
        ++host->stats.reached;
        finish(block, MP_CRATE_VERDICT_REACHED);
        return;
    }
    answer = engine->push(engine->context, block->wish_slot, block->id, step);
    if (answer < 0) {
        ++host->stats.no_body;
        mp_crate_owner_release(&block->owner, block->wish_slot);
        block->working = false;
        return;
    }
    if (!engine->read(engine->context, block->id, &after)) {
        block->working = false;
        return;
    }
    switch (mp_crate_rule_after_push(answer, (after.flags & MP_CRATE_FLAG_FALLING) != 0u,
                                     mp_crate_rule_flat_distance(before.position, block->target),
                                     mp_crate_rule_flat_distance(after.position, block->target))) {
    case MP_CRATE_AFTER_FELL:
    case MP_CRATE_AFTER_REACHED:
        ++host->stats.reached;
        finish(block, MP_CRATE_VERDICT_REACHED);
        break;
    case MP_CRATE_AFTER_REFUSED:
        ++host->stats.engine_refused;
        finish(block, MP_CRATE_VERDICT_ENGINE);
        break;
    case MP_CRATE_AFTER_STALLED:
        ++host->stats.stalled;
        finish(block, MP_CRATE_VERDICT_ENGINE);
        break;
    case MP_CRATE_AFTER_MOVING:
    default:
        (void)mp_crate_owner_claim(&block->owner, block->wish_slot, now);
        break;
    }
}

void mp_crate_host_run(mp_crate_host_t *host, const mp_crate_engine_t *engine, uint32_t now)
{
    size_t i;

    for (i = 0; i < host->filed; ++i) {
        judge(host, engine, &host->inbox[i], now);
    }
    host->filed = 0u;
    for (i = 0; i < host->count; ++i) {
        if (host->block[i].working) {
            work(host, engine, &host->block[i], now);
        }
    }
}

bool mp_crate_host_own_push(mp_crate_host_t *host, uint32_t id, uint32_t now)
{
    mp_crate_host_block_t *block = block_of(host, id);

    if (block == NULL) {
        return true;   /* not a block this host describes: the engine decides alone */
    }
    if (!mp_crate_owner_claim(&block->owner, (uint8_t)HOST_SLOT, now)) {
        ++host->stats.own_refused;
        return false;
    }
    ++host->stats.own_pushes;
    block->own_push_now = true;
    say_holder(host, block, (uint8_t)HOST_SLOT, now);
    return true;
}

void mp_crate_host_end_substep(mp_crate_host_t *host, uint32_t now)
{
    size_t i;

    (void)now;
    for (i = 0; i < host->count; ++i) {
        mp_crate_host_block_t *block = &host->block[i];

        if (block->own_pushing && !block->own_push_now) {
            mp_crate_owner_release(&block->owner, (uint8_t)HOST_SLOT);
        }
        block->own_pushing  = block->own_push_now;
        block->own_push_now = false;
    }
}

void mp_crate_host_fell(mp_crate_host_t *host, uint32_t tick, uint32_t id, const float position[3],
                        const float delta[3])
{
    mp_crate_fall_t *fall;
    float            length = sqrtf(delta[0] * delta[0] + delta[1] * delta[1]);

    ++host->stats.falls;
    if (!host->level_known || id > MP_CRATE_ID_MAX || !(length > 0.0f) ||
        host->fall_count >= MP_CRATE_FALLS) {
        return;   /* the engine never drops a block on a push with no step along the floor */
    }
    fall = &host->fall[(host->fall_head + host->fall_count) % MP_CRATE_FALLS];
    memset(fall, 0, sizeof *fall);
    fall->tick         = tick;
    fall->level        = host->level;
    fall->generation   = host->generation;
    fall->id           = (uint8_t)id;
    memcpy(fall->position, position, sizeof fall->position);
    fall->direction[0] = delta[0] / length;
    fall->direction[1] = delta[1] / length;
    ++host->fall_count;
}

void mp_crate_host_sank(mp_crate_host_t *host)
{
    ++host->stats.sunk;
}

/* ==============================================================================================
 * The note.
 * ============================================================================================ */

static bool describe(const mp_crate_host_block_t *block, const mp_crate_body_t *body,
                     uint32_t now, mp_crate_entry_t *entry)
{
    uint8_t pusher = mp_crate_owner_holder(&block->owner, now);

    if (body->kind != (int32_t)MP_CRATE_KIND_BLOCK && body->kind != (int32_t)MP_CRATE_KIND_SUNK &&
        body->kind != (int32_t)MP_CRATE_KIND_SUNK_ALT) {
        return false;
    }
    memset(entry, 0, sizeof *entry);
    entry->id    = block->id;
    entry->kind  = (uint8_t)body->kind;
    entry->flags = (uint8_t)(body->flags & MP_CRATE_FLAGS_TRAVEL);
    memcpy(entry->position, body->position, sizeof entry->position);
    entry->pusher = pusher;
    if (pusher != MP_CRATE_NOBODY && pusher == block->wish_slot) {
        entry->sequence = block->wish_sequence;
        entry->verdict  = block->verdict;
    }
    if ((entry->flags & MP_CRATE_FLAG_CARRIED) != 0u) {
        entry->carrier      = body->carrier;
        entry->carrier_part = body->carrier_part;
        memcpy(entry->carry_offset, body->carry_offset, sizeof entry->carry_offset);
    }
    if ((entry->flags & MP_CRATE_FLAG_CARRYING) != 0u) {
        entry->carrying = body->carrying;
    }
    return true;
}

size_t mp_crate_host_next_note(mp_crate_host_t *host, const mp_crate_engine_t *engine,
                               uint32_t now, uint8_t *buffer, size_t capacity)
{
    mp_crate_note_t note;
    bool            changed = false;
    uint32_t        away    = 0u;
    size_t          i;

    host->pending_due = MP_CRATE_DUE_NONE;
    if (!host->level_known) {
        return 0u;
    }
    for (i = 0; i < host->count; ++i) {
        mp_crate_host_block_t *block = &host->block[i];
        mp_crate_body_t        body;

        host->pending_in[i] = engine->read(engine->context, block->id, &body) &&
                              describe(block, &body, now, &host->pending_entry[i]);
        if (!host->pending_in[i]) {
            continue;
        }
        away += mp_crate_rule_flat_distance(body.position, body.home) > MP_CRATE_AGREE ||
                        fabsf(body.position[2] - body.home[2]) > MP_CRATE_AGREE
                    ? 1u
                    : 0u;
        if (!block->sent_once ||
            mp_crate_rule_entry_differs(&block->sent, &host->pending_entry[i])) {
            changed = true;
        } else {
            host->pending_in[i] = false;   /* the same as sent: only a whole note repeats it */
        }
    }
    host->stats.away = away;

    host->pending_due = mp_crate_cadence_due(&host->cadence, now, changed);
    if (host->pending_due == MP_CRATE_DUE_NONE) {
        return 0u;
    }
    memset(&note, 0, sizeof note);
    note.tick       = now;
    note.level      = host->level;
    note.generation = host->generation;
    note.whole      = host->pending_due == MP_CRATE_DUE_WHOLE;
    for (i = 0; i < host->count; ++i) {
        mp_crate_body_t body;

        if (note.whole && !host->pending_in[i] &&
            engine->read(engine->context, host->block[i].id, &body) &&
            describe(&host->block[i], &body, now, &host->pending_entry[i])) {
            host->pending_in[i] = true;
        }
        if (host->pending_in[i]) {
            note.entry[note.count++] = host->pending_entry[i];
        }
    }
    host->pending_count = note.count;
    host->pending_bytes = mp_crate_note_encode(&note, buffer, capacity);
    if (host->pending_bytes == 0u) {
        host->pending_due = MP_CRATE_DUE_NONE;
    }
    return host->pending_bytes;
}

void mp_crate_host_note_sent(mp_crate_host_t *host, bool sent, uint32_t now)
{
    size_t i;

    if (host->pending_due == MP_CRATE_DUE_NONE) {
        return;
    }
    if (!sent) {
        ++host->stats.note_refused;
        host->pending_due = MP_CRATE_DUE_NONE;
        return;
    }
    for (i = 0; i < host->count; ++i) {
        if (host->pending_in[i]) {
            host->block[i].sent      = host->pending_entry[i];
            host->block[i].sent_once = true;
        }
    }
    if (host->pending_due == MP_CRATE_DUE_WHOLE) {
        ++host->stats.whole_sent;
    } else {
        ++host->stats.change_sent;
    }
    host->stats.entries_sent += host->pending_count;
    if (host->pending_bytes > host->stats.largest) {
        host->stats.largest = (uint32_t)host->pending_bytes;
    }
    mp_crate_cadence_sent(&host->cadence, host->pending_due, now);
    host->pending_due = MP_CRATE_DUE_NONE;
}

/* ==============================================================================================
 * The falls.
 * ============================================================================================ */

size_t mp_crate_host_next_fall(mp_crate_host_t *host, uint8_t *buffer, size_t capacity)
{
    while (host->fall_count != 0u) {
        size_t bytes = mp_crate_fall_encode(&host->fall[host->fall_head], buffer, capacity);

        if (bytes != 0u) {
            return bytes;
        }
        /* A fall the codec will not describe, a position past the wire's range: not sendable
         * now or later, so it goes rather than blocking every fall behind it. */
        host->fall_head = (host->fall_head + 1u) % MP_CRATE_FALLS;
        --host->fall_count;
    }
    return 0u;
}

void mp_crate_host_fall_sent(mp_crate_host_t *host, bool sent)
{
    if (host->fall_count == 0u) {
        return;
    }
    if (!sent) {
        if (!host->fall_waiting) {
            host->fall_waiting = true;
            ++host->stats.falls_waited;
        }
        return;
    }
    host->fall_head    = (host->fall_head + 1u) % MP_CRATE_FALLS;
    --host->fall_count;
    host->fall_waiting = false;
    ++host->stats.falls_sent;
}
