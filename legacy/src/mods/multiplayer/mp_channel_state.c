/* mp_channel_state.c: a state note is the newest copy of its kind, not one message more.
 *
 * Layer 0, on the channel's own send ring. See the state section of mp_channel.h for the rules;
 * this file holds the one decision they come from, asked by the send and by the question whether
 * the send would take a note, so that the session asking before a broadcast and the channel
 * carrying it out cannot disagree.
 *
 * Why at most one shrunk copy of a kind may be unanswered: a peer that does not acknowledge would
 * otherwise gain a shrunk copy and a new message for every change, and a note that changes once a
 * second would fill the sixty four seats in half a minute on its own. With the bound, a kind
 * holds two seats however long the silence, and the newest copy waits outside them.
 */
#include "mp_channel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef enum state_plan {
    PLAN_REFUSE = 0,
    PLAN_UNCHANGED,
    PLAN_REPLACE_WAITING,
    PLAN_REPLACE_UNSENT,
    PLAN_WAIT,
    PLAN_SHRINK_AND_QUEUE,
    PLAN_QUEUE
} state_plan_t;

/* The copies of one kind in the send ring: where the youngest one that still carries its bytes
 * sits, and whether a shrunk one is still unanswered. Walked oldest first, so the last match is
 * the youngest. */
#define NO_COPY MP_CHANNEL_SEND_SLOTS

typedef struct state_copies {
    size_t newest;             /* its index in the ring, NO_COPY for none */
    bool   shrunk_unanswered;
} state_copies_t;

static state_copies_t find_copies(const mp_channel_t *channel, uint8_t kind)
{
    state_copies_t copies;
    uint16_t       step;

    copies.newest            = NO_COPY;
    copies.shrunk_unanswered = false;
    for (step = MP_CHANNEL_SEND_SLOTS; step > 0u; --step) {
        uint16_t                      id   = (uint16_t)(channel->next_message_id - step);
        size_t                        at   = id % MP_CHANNEL_SEND_SLOTS;
        const mp_channel_send_slot_t *slot = &channel->send_slots[at];

        if (!slot->used || slot->id != id || slot->kind != kind) {
            continue;
        }
        if (slot->shrunk) {
            copies.shrunk_unanswered = true;
        } else {
            copies.newest = at;
        }
    }
    return copies;
}

static const mp_channel_state_kind_t *row_of(const mp_channel_t *channel, uint8_t kind)
{
    size_t index;

    for (index = 0; index < MP_CHANNEL_STATE_KINDS; ++index) {
        if (channel->state_kinds[index].kind == kind) {
            return &channel->state_kinds[index];
        }
    }
    return NULL;
}

static bool a_row_is_free(const mp_channel_t *channel)
{
    return row_of(channel, 0u) != NULL;
}

/* The row of `kind`, taken when it has none yet; NULL when all are taken by other kinds. */
static mp_channel_state_kind_t *take_row(mp_channel_t *channel, uint8_t kind)
{
    size_t index;

    for (index = 0; index < MP_CHANNEL_STATE_KINDS; ++index) {
        if (channel->state_kinds[index].kind == kind) {
            return &channel->state_kinds[index];
        }
    }
    for (index = 0; index < MP_CHANNEL_STATE_KINDS; ++index) {
        if (channel->state_kinds[index].kind == 0u) {
            channel->state_kinds[index].kind = kind;
            return &channel->state_kinds[index];
        }
    }
    return NULL;
}

/* The one decision. */
static state_plan_t plan_state(const mp_channel_t *channel, uint8_t kind, uint32_t key,
                               size_t bytes, state_copies_t *copies)
{
    const mp_channel_state_kind_t *row = row_of(channel, kind);
    const mp_channel_send_slot_t  *newest;

    *copies = find_copies(channel, kind);
    newest  = copies->newest != NO_COPY ? &channel->send_slots[copies->newest] : NULL;
    if (kind == 0u || bytes > MP_CHANNEL_MESSAGE_BYTES) {
        return PLAN_REFUSE;
    }
    if (row != NULL && row->waiting) {
        return row->waiting_key == key ? PLAN_UNCHANGED : PLAN_REPLACE_WAITING;
    }
    if (newest != NULL && newest->key == key) {
        return PLAN_UNCHANGED;
    }
    if (newest != NULL && !newest->sent_ever) {
        return PLAN_REPLACE_UNSENT;
    }
    if (newest != NULL && copies->shrunk_unanswered) {
        return (row != NULL || a_row_is_free(channel)) ? PLAN_WAIT : PLAN_REFUSE;
    }
    if (!mp_channel_can_send(channel, bytes)) {
        return PLAN_REFUSE;
    }
    return newest != NULL ? PLAN_SHRINK_AND_QUEUE : PLAN_QUEUE;
}

/* A message of its own, marked with its kind and key. The send cannot fail: the plan asked. */
static void queue_copy(mp_channel_t *channel, uint8_t kind, uint32_t key, const void *data,
                       size_t bytes)
{
    mp_channel_send_slot_t *slot;

    if (!mp_channel_send(channel, data, bytes)) {
        return;
    }
    slot       = &channel->send_slots[(uint16_t)(channel->next_message_id - 1u) %
                                      MP_CHANNEL_SEND_SLOTS];
    slot->kind = kind;
    slot->key  = key;
}

static void overwrite(mp_channel_send_slot_t *slot, uint32_t key, const void *data, size_t bytes)
{
    slot->key   = key;
    slot->bytes = (uint16_t)bytes;
    if (bytes > 0u) {
        memcpy(slot->data, data, bytes);
    }
}

/* Keeps its id and its place in the order; its next copy is four bytes. */
static void shrink(mp_channel_send_slot_t *slot)
{
    slot->shrunk = true;
    slot->bytes  = 0u;
}

mp_channel_state_outcome_t mp_channel_send_state(mp_channel_t *channel, uint8_t kind, uint32_t key,
                                                 const void *data, size_t bytes)
{
    state_copies_t           copies;
    state_plan_t             plan;
    mp_channel_state_kind_t *row;

    if (channel == NULL || (data == NULL && bytes > 0u)) {
        return MP_CHANNEL_STATE_REFUSED;
    }
    plan = plan_state(channel, kind, key, bytes, &copies);
    if (plan == PLAN_REFUSE) {
        return MP_CHANNEL_STATE_REFUSED;
    }
    row = take_row(channel, kind);   /* NULL only with every row taken: counted nowhere then */
    switch (plan) {
    case PLAN_UNCHANGED:
        if (row != NULL) {
            ++row->unchanged;
        }
        return MP_CHANNEL_STATE_UNCHANGED;
    case PLAN_REPLACE_WAITING:
        row->waiting_key   = key;
        row->waiting_bytes = (uint16_t)bytes;
        if (bytes > 0u) {
            memcpy(row->waiting_data, data, bytes);
        }
        ++row->replaced;
        return MP_CHANNEL_STATE_REPLACED;
    case PLAN_REPLACE_UNSENT:
        overwrite(&channel->send_slots[copies.newest], key, data, bytes);
        if (row != NULL) {
            ++row->replaced;
        }
        return MP_CHANNEL_STATE_REPLACED;
    case PLAN_WAIT:
        row->waiting       = true;
        row->waiting_key   = key;
        row->waiting_bytes = (uint16_t)bytes;
        if (bytes > 0u) {
            memcpy(row->waiting_data, data, bytes);
        }
        ++row->waited;
        return MP_CHANNEL_STATE_WAITING;
    case PLAN_SHRINK_AND_QUEUE:
        shrink(&channel->send_slots[copies.newest]);
        if (row != NULL) {
            ++row->shrunk;
        }
        /* fall through: the younger copy is queued like any first one */
    case PLAN_QUEUE:
    default:
        queue_copy(channel, kind, key, data, bytes);
        if (row != NULL) {
            ++row->sent;
        }
        return MP_CHANNEL_STATE_QUEUED;
    }
}

bool mp_channel_state_would_take(const mp_channel_t *channel, uint8_t kind, uint32_t key,
                                 size_t bytes)
{
    state_copies_t copies;

    return channel != NULL && plan_state(channel, kind, key, bytes, &copies) != PLAN_REFUSE;
}

bool mp_channel_state_newest_key(const mp_channel_t *channel, uint8_t kind, uint32_t *key)
{
    const mp_channel_state_kind_t *row;
    state_copies_t                 copies;

    if (channel == NULL || key == NULL || kind == 0u) {
        return false;
    }
    row = row_of(channel, kind);
    if (row != NULL && row->waiting) {
        *key = row->waiting_key;
        return true;
    }
    copies = find_copies(channel, kind);
    if (copies.newest == NO_COPY) {
        return false;
    }
    *key = channel->send_slots[copies.newest].key;
    return true;
}

void mp_channel_state_promote(mp_channel_t *channel)
{
    size_t index;

    for (index = 0; index < MP_CHANNEL_STATE_KINDS; ++index) {
        mp_channel_state_kind_t *row = &channel->state_kinds[index];
        mp_channel_send_slot_t  *newest;
        state_copies_t           copies;

        if (row->kind == 0u || !row->waiting) {
            continue;
        }
        copies = find_copies(channel, row->kind);
        newest = copies.newest != NO_COPY ? &channel->send_slots[copies.newest] : NULL;
        if (newest != NULL && newest->sent_ever && copies.shrunk_unanswered) {
            continue;   /* still behind a shrunk copy nobody has answered */
        }
        if (newest != NULL && !newest->sent_ever) {
            overwrite(newest, row->waiting_key, row->waiting_data, row->waiting_bytes);
            row->waiting = false;
            continue;
        }
        if (!mp_channel_can_send(channel, row->waiting_bytes)) {
            continue;   /* no seat yet; the next acknowledgement or build tries again */
        }
        if (newest != NULL) {
            shrink(newest);
            ++row->shrunk;
        }
        queue_copy(channel, row->kind, row->waiting_key, row->waiting_data, row->waiting_bytes);
        ++row->sent;
        row->waiting = false;
    }
}

void mp_channel_state_count_unchanged(mp_channel_t *channel, uint8_t kind)
{
    mp_channel_state_kind_t *row = (channel != NULL && kind != 0u) ? take_row(channel, kind)
                                                                    : NULL;

    if (row != NULL) {
        ++row->unchanged;
    }
}

size_t mp_channel_state_waiting(const mp_channel_t *channel)
{
    size_t index;
    size_t waiting = 0u;

    for (index = 0; channel != NULL && index < MP_CHANNEL_STATE_KINDS; ++index) {
        waiting += channel->state_kinds[index].waiting ? 1u : 0u;
    }
    return waiting;
}
