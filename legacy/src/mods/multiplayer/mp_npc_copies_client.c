/* mp_npc_copies_client.c: the copies a client holds for its host. See the header. */
#include "mp_npc_copies_client.h"

#include "mp_npc_copy_wire.h"
#include "mp_wire.h"

#include "common/npc_spawn_note.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

void mp_npc_copies_client_init(mp_npc_copies_client_t *client, uint32_t orphan_blocks)
{
    if (client != NULL) {
        memset(client, 0, sizeof *client);
        client->orphan_blocks = orphan_blocks;
    }
}

void mp_npc_copies_client_clear(mp_npc_copies_client_t *client)
{
    size_t k;

    if (client == NULL) {
        return;
    }
    for (k = 0; k < MP_WIRE_COPY_MAX; ++k) {
        bool present = client->held[k].present;

        memset(&client->held[k], 0, sizeof client->held[k]);
        client->held[k].present = present;
    }
    memset(client->wish, 0, sizeof client->wish);
    client->refusals = 0u;
}

static void owe_refusal(mp_npc_copies_client_t *client, uint32_t note, uint8_t reason)
{
    if (note == 0u) {
        return;
    }
    if (client->refusals >= MP_NPC_COPIES_CLIENT_REFUSALS) {
        ++client->counters.refusals_overflow;
        return;
    }
    client->refusal[client->refusals].note   = note;
    client->refusal[client->refusals].reason = reason;
    ++client->refusals;
}

/* The oldest open spawn wish of this machine with these description bytes: the one a first build
 * owned by this machine answers. */
static mp_npc_open_wish_t *wish_built_by(mp_npc_copies_client_t *client, const uint8_t *desc)
{
    mp_npc_open_wish_t *found = NULL;
    size_t              i;

    for (i = 0; i < MP_NPC_COPIES_CLIENT_WISHES; ++i) {
        mp_npc_open_wish_t *w = &client->wish[i];

        if (w->used && w->kind == NPC_SPAWN_WISH_SPAWN &&
            memcmp(w->desc, desc, MP_NPC_COPY_DESC_BYTES) == 0 &&
            (found == NULL || npc_spawn_note_serial_after(found->note, w->note))) {
            found = w;
        }
    }
    return found;
}

/* A new life of k is handed to the overlay. What the census knows of the key stays. */
static void start(mp_npc_copies_client_t *client, mp_npc_held_t *h, uint8_t generation,
                  uint8_t owner, const uint8_t *desc, bool owner_is_me)
{
    mp_npc_open_wish_t *answered = owner_is_me ? wish_built_by(client, desc) : NULL;
    bool                present  = h->present;
    bool                seen     = h->seen;

    memset(h, 0, sizeof *h);
    h->present    = present;
    h->seen       = seen;
    h->state      = (uint8_t)MP_NPC_HELD_HANDED;
    h->generation = generation;
    h->owner      = owner;
    h->duties     = MP_NPC_HELD_DUTY_HAND;
    memcpy(h->desc, desc, MP_NPC_COPY_DESC_BYTES);
    ++client->counters.handed;
    if (answered != NULL) {
        h->wish        = answered->note;
        answered->used = false;
        ++client->counters.wishes_answered_built;
    }
}

/* A newer life waits until the old one is gone from this machine. The newest one wins. */
static void wait_for(mp_npc_copies_client_t *client, mp_npc_held_t *h,
                     const mp_npc_copy_entry_t *entry, const uint8_t *desc, bool owner_is_me)
{
    h->waiting         = true;
    h->next_generation = entry->generation;
    h->next_owner      = entry->owner;
    h->next_mine       = owner_is_me;
    memcpy(h->next_desc, desc, MP_NPC_COPY_DESC_BYTES);
    ++client->counters.waited;
}

/* The overlay is told to drop the life it holds. A life it was never told of is simply gone, and
 * the wish of this machine it was to answer is refused here. */
static void cancel(mp_npc_copies_client_t *client, mp_npc_held_t *h)
{
    if ((h->duties & MP_NPC_HELD_DUTY_HAND) != 0u) {
        h->duties = 0u;
        h->state  = (uint8_t)MP_NPC_HELD_GIVEN_UP;
        if (h->wish != 0u) {
            owe_refusal(client, h->wish, (uint8_t)NPC_SPAWN_REFUSED_NOT_BUILT);
            ++client->counters.refusals_said_here;
        }
        return;
    }
    h->state           = (uint8_t)MP_NPC_HELD_CANCELLING;
    h->duties          = MP_NPC_HELD_DUTY_CANCEL;
    h->cancel_serial   = 0u;
    h->cancel_answered = false;
    h->cancel_refused  = false;
    h->blocks          = 0u;
}

void mp_npc_copies_client_take_entry(mp_npc_copies_client_t *client,
                                     const mp_npc_copy_entry_t *entry, uint8_t own_slot)
{
    mp_npc_held_t *h;
    uint8_t        desc[MP_NPC_COPY_DESC_BYTES];
    bool           mine;

    if (client == NULL || entry == NULL || entry->kind != NPC_SPAWN_GRANT_BUILD ||
        entry->k >= MP_WIRE_COPY_MAX || entry->generation == 0u ||
        !mp_npc_copy_desc_put(&entry->desc, desc)) {
        return;
    }
    ++client->counters.entries;
    h    = &client->held[entry->k];
    mine = entry->owner == own_slot;
    if (h->state != MP_NPC_HELD_NONE && h->generation == entry->generation) {
        if ((h->state == MP_NPC_HELD_HANDED || h->state == MP_NPC_HELD_BUILT) &&
            h->owner != entry->owner) {
            h->owner = entry->owner;
            if ((h->duties & MP_NPC_HELD_DUTY_HAND) == 0u) {
                h->duties |= MP_NPC_HELD_DUTY_OWNER;
            }
        } else {
            ++client->counters.repeats;
        }
        return;
    }
    if (h->waiting && h->next_generation == entry->generation) {
        ++client->counters.repeats;   /* already waiting for this life */
        return;
    }
    if (h->state == MP_NPC_HELD_HANDED || h->state == MP_NPC_HELD_BUILT) {
        cancel(client, h);
    }
    /* Handed at once only into an empty row with no actor on the key; every other life waits for
     * a complete census to find the key free. */
    if (h->state == MP_NPC_HELD_NONE && !h->present) {
        start(client, h, entry->generation, entry->owner, desc, mine);
        return;
    }
    wait_for(client, h, entry, desc, mine);
}

bool mp_npc_copies_client_take_refusal(mp_npc_copies_client_t *client,
                                       const mp_npc_copy_entry_t *entry, uint8_t own_slot)
{
    size_t i;

    if (client == NULL || entry == NULL || entry->kind != NPC_SPAWN_GRANT_REFUSED) {
        return false;
    }
    if (entry->owner != own_slot) {
        ++client->counters.refusals_not_mine;
        return false;
    }
    for (i = 0; i < MP_NPC_COPIES_CLIENT_WISHES; ++i) {
        mp_npc_open_wish_t *w = &client->wish[i];

        if (w->used && w->wire == entry->serial) {
            w->used = false;
            owe_refusal(client, w->note, entry->reason);
            ++client->counters.refusals;
            return true;
        }
    }
    ++client->counters.refusals_unmatched;
    return false;
}

/* The answer to a build: built, or not. A build that the overlay answers while a cancel for it is
 * owed and not yet written needs no cancel if nothing was built. */
static void answer_hand(mp_npc_copies_client_t *client, mp_npc_held_t *h,
                        npc_spawn_answer_t answer)
{
    h->hand_answered = true;
    if (h->state == MP_NPC_HELD_HANDED) {
        if (answer == NPC_SPAWN_ANSWER_REFUSED) {
            h->state = (uint8_t)MP_NPC_HELD_REFUSED;
            ++client->counters.not_built;
        } else {
            h->state = (uint8_t)MP_NPC_HELD_BUILT;
            ++client->counters.built;
        }
    } else if (h->state == MP_NPC_HELD_CANCELLING && answer == NPC_SPAWN_ANSWER_REFUSED &&
               h->cancel_serial == 0u) {
        h->duties = (uint8_t)(h->duties & ~MP_NPC_HELD_DUTY_CANCEL);
        h->state  = (uint8_t)MP_NPC_HELD_GIVEN_UP;
    }
}

static void answer_cancel(mp_npc_held_t *h, npc_spawn_answer_t answer)
{
    h->cancel_answered = true;
    if (answer == NPC_SPAWN_ANSWER_DONE) {
        h->state = (uint8_t)MP_NPC_HELD_GIVEN_UP;
        h->actor = 0u;
    } else {
        h->cancel_refused = true;   /* ridden here; asked again after some blocks */
        h->blocks         = 0u;
    }
}

void mp_npc_copies_client_take_answer(mp_npc_copies_client_t *client, uint32_t serial,
                                      npc_spawn_answer_t answer)
{
    size_t k;

    if (client == NULL || serial == 0u) {
        return;
    }
    if (answer == NPC_SPAWN_ANSWER_OPEN) {
        ++client->counters.answers_open;
        return;
    }
    if (answer == NPC_SPAWN_ANSWER_LOST) {
        ++client->counters.answers_lost;
    }
    for (k = 0; k < MP_WIRE_COPY_MAX; ++k) {
        mp_npc_held_t *h = &client->held[k];

        if (h->hand_serial == serial && !h->hand_answered) {
            answer_hand(client, h, answer);
            return;
        }
        if (h->cancel_serial == serial && !h->cancel_answered) {
            answer_cancel(h, answer);
            return;
        }
        if (h->owner_serial == serial) {
            h->owner_serial = 0u;
            return;
        }
    }
    ++client->counters.answers_unmatched;
}

void mp_npc_copies_client_give_up(mp_npc_copies_client_t *client, uint32_t k,
                                  uint8_t generation, mp_npc_give_up_t why)
{
    mp_npc_held_t *h;

    if (client == NULL || k >= MP_WIRE_COPY_MAX || why >= MP_NPC_GIVE_UP_REASONS) {
        return;
    }
    h = &client->held[k];
    if (h->waiting && (generation == 0u || generation == h->next_generation)) {
        h->waiting = false;
        ++client->counters.waiting_dropped;
    }
    if (generation != 0u && generation != h->generation) {
        return;
    }
    if (h->state == MP_NPC_HELD_HANDED || h->state == MP_NPC_HELD_BUILT) {
        cancel(client, h);
        ++client->counters.given_up[why];
    } else if (h->state == MP_NPC_HELD_REFUSED) {
        h->state = (uint8_t)MP_NPC_HELD_GIVEN_UP;
        ++client->counters.given_up[why];
    }
}

void mp_npc_copies_client_release_all(mp_npc_copies_client_t *client)
{
    uint32_t k;

    for (k = 0; client != NULL && k < MP_WIRE_COPY_MAX; ++k) {
        mp_npc_copies_client_give_up(client, k, 0u, MP_NPC_GIVE_UP_RELEASE);
    }
}

void mp_npc_copies_client_census_begin(mp_npc_copies_client_t *client)
{
    size_t k;

    for (k = 0; client != NULL && k < MP_WIRE_COPY_MAX; ++k) {
        client->held[k].seen = false;
    }
}

void mp_npc_copies_client_census_saw(mp_npc_copies_client_t *client, uint32_t k)
{
    if (client != NULL && k < MP_WIRE_COPY_MAX) {
        client->held[k].seen = true;
    }
}

static void expire_wishes(mp_npc_copies_client_t *client)
{
    size_t i;

    for (i = 0; i < MP_NPC_COPIES_CLIENT_WISHES; ++i) {
        mp_npc_open_wish_t *w = &client->wish[i];

        if (w->used && (int32_t)(client->censuses - w->deadline) >= 0) {
            w->used = false;
            ++client->counters.wishes_expired;
            if (w->kind == NPC_SPAWN_WISH_SPAWN) {
                owe_refusal(client, w->note, (uint8_t)NPC_SPAWN_REFUSED_NOT_BUILT);
                ++client->counters.refusals_said_here;
            }
        }
    }
}

void mp_npc_copies_client_census_end(mp_npc_copies_client_t *client, bool complete)
{
    size_t k;

    if (client == NULL) {
        return;
    }
    ++client->censuses;
    for (k = 0; k < MP_WIRE_COPY_MAX; ++k) {
        mp_npc_held_t *h = &client->held[k];

        h->present = complete ? h->seen : (h->present || h->seen);
        if (!complete) {
            continue;
        }
        if (h->state == MP_NPC_HELD_BUILT && !h->present) {
            /* Lost here, and never handed again: a replica is parked and does not leave by
             * itself, so a built copy that is gone was taken by something on this machine. */
            h->state = (uint8_t)MP_NPC_HELD_GIVEN_UP;
            h->actor = 0u;
            ++client->counters.lost_here;
        }
        if (h->waiting && !h->present &&
            (h->state == MP_NPC_HELD_NONE || h->state == MP_NPC_HELD_REFUSED ||
             h->state == MP_NPC_HELD_GIVEN_UP)) {
            uint8_t desc[MP_NPC_COPY_DESC_BYTES];

            memcpy(desc, h->next_desc, sizeof desc);
            start(client, h, h->next_generation, h->next_owner, desc, h->next_mine);
        }
    }
    expire_wishes(client);
}

void mp_npc_copies_client_block(mp_npc_copies_client_t *client, const uint8_t *bitmap,
                                size_t bytes)
{
    uint32_t k;

    if (client == NULL) {
        return;
    }
    for (k = 0; k < MP_WIRE_COPY_MAX; ++k) {
        mp_npc_held_t *h     = &client->held[k];
        bool           named = bitmap != NULL && k / 8u < bytes &&
                     (bitmap[k / 8u] & (1u << (k % 8u))) != 0u;

        if (h->state == MP_NPC_HELD_HANDED || h->state == MP_NPC_HELD_BUILT) {
            h->blocks = named ? 0u : h->blocks + 1u;
            if (client->orphan_blocks != 0u && h->blocks >= client->orphan_blocks) {
                mp_npc_copies_client_give_up(client, k, 0u, MP_NPC_GIVE_UP_ORPHAN);
            }
        } else if (h->state == MP_NPC_HELD_CANCELLING && h->cancel_answered &&
                   h->cancel_refused && ++h->blocks >= MP_NPC_COPIES_CANCEL_RETRY_BLOCKS) {
            h->duties          = MP_NPC_HELD_DUTY_CANCEL;
            h->cancel_serial   = 0u;
            h->cancel_answered = false;
            h->cancel_refused  = false;
            h->blocks          = 0u;
            ++client->counters.cancel_retries;
        }
    }
}

bool mp_npc_copies_client_record(mp_npc_copies_client_t *client, uint32_t k, uint8_t generation)
{
    mp_npc_held_t *h;

    if (client == NULL || k >= MP_WIRE_COPY_MAX) {
        return false;
    }
    h = &client->held[k];
    if ((h->state == MP_NPC_HELD_HANDED || h->state == MP_NPC_HELD_BUILT) &&
        h->generation != generation) {
        mp_npc_copies_client_give_up(client, (uint32_t)k, h->generation,
                                     MP_NPC_GIVE_UP_GENERATION);
        return false;
    }
    return h->state == MP_NPC_HELD_BUILT && h->generation == generation;
}

void mp_npc_copies_client_parked(mp_npc_copies_client_t *client, uint32_t k, uintptr_t actor)
{
    if (client != NULL && k < MP_WIRE_COPY_MAX &&
        (client->held[k].state == MP_NPC_HELD_HANDED ||
         client->held[k].state == MP_NPC_HELD_BUILT)) {
        client->held[k].actor = actor;
        ++client->counters.parked;
    }
}

uintptr_t mp_npc_copies_client_replica(const mp_npc_copies_client_t *client, uint32_t k,
                                       uint8_t generation)
{
    if (client == NULL || k >= MP_WIRE_COPY_MAX ||
        client->held[k].state != MP_NPC_HELD_BUILT ||
        client->held[k].generation != generation) {
        return 0u;
    }
    return client->held[k].actor;
}

static const uint8_t HELD_DUTY_ORDER[] = {
    MP_NPC_HELD_DUTY_CANCEL, MP_NPC_HELD_DUTY_HAND, MP_NPC_HELD_DUTY_OWNER,
};

bool mp_npc_copies_client_next_duty(const mp_npc_copies_client_t *client, uint32_t *k,
                                    uint8_t *duty)
{
    uint32_t i;
    size_t   d;

    if (client == NULL || k == NULL || duty == NULL) {
        return false;
    }
    for (i = 0; i < MP_WIRE_COPY_MAX; ++i) {
        for (d = 0; d < sizeof HELD_DUTY_ORDER / sizeof HELD_DUTY_ORDER[0]; ++d) {
            if ((client->held[i].duties & HELD_DUTY_ORDER[d]) != 0u) {
                *k    = i;
                *duty = HELD_DUTY_ORDER[d];
                return true;
            }
        }
    }
    return false;
}

bool mp_npc_copies_client_note_for(const mp_npc_copies_client_t *client, uint32_t k,
                                   uint8_t duty, npc_spawn_grant_t *out)
{
    const mp_npc_held_t *h;

    if (client == NULL || out == NULL || k >= MP_WIRE_COPY_MAX ||
        (client->held[k].duties & duty) == 0u) {
        return false;
    }
    h = &client->held[k];
    memset(out, 0, sizeof *out);
    out->generation = h->generation;
    out->key        = (uint16_t)(NPC_SPAWN_KEY_FIRST + k);
    out->owner      = h->owner;
    switch (duty) {
    case MP_NPC_HELD_DUTY_HAND:
        out->kind = (uint8_t)NPC_SPAWN_GRANT_BUILD;
        out->wish = h->wish;
        return mp_npc_copy_desc_get(h->desc, &out->desc);
    case MP_NPC_HELD_DUTY_CANCEL:
        out->kind = (uint8_t)NPC_SPAWN_GRANT_CANCEL;
        return true;
    case MP_NPC_HELD_DUTY_OWNER:
        out->kind = (uint8_t)NPC_SPAWN_GRANT_OWNER;
        return true;
    default:
        return false;
    }
}

void mp_npc_copies_client_duty_done(mp_npc_copies_client_t *client, uint32_t k, uint8_t duty,
                                    uint32_t serial)
{
    mp_npc_held_t *h;

    if (client == NULL || k >= MP_WIRE_COPY_MAX) {
        return;
    }
    h = &client->held[k];
    if ((h->duties & duty) == 0u) {
        ++client->counters.duties_unowed;
        return;
    }
    h->duties = (uint8_t)(h->duties & ~duty);
    if (duty == MP_NPC_HELD_DUTY_HAND) {
        h->hand_serial   = serial;
        h->hand_answered = false;
    } else if (duty == MP_NPC_HELD_DUTY_CANCEL) {
        h->cancel_serial   = serial;
        h->cancel_answered = false;
        h->cancel_refused  = false;
    } else if (duty == MP_NPC_HELD_DUTY_OWNER) {
        h->owner_serial = serial;
    }
}

bool mp_npc_copies_client_next_refusal(mp_npc_copies_client_t *client,
                                       mp_npc_refusal_t *refusal)
{
    if (client == NULL || refusal == NULL || client->refusals == 0u) {
        return false;
    }
    *refusal = client->refusal[0];
    --client->refusals;
    memmove(&client->refusal[0], &client->refusal[1], client->refusals * sizeof client->refusal[0]);
    return true;
}

uint16_t mp_npc_copies_client_open_wish(mp_npc_copies_client_t *client, uint32_t note_serial,
                                        uint8_t kind, const uint8_t *desc)
{
    mp_npc_open_wish_t *slot = NULL;
    size_t              i;

    if (client == NULL) {
        return 0u;
    }
    for (i = 0; i < MP_NPC_COPIES_CLIENT_WISHES && slot == NULL; ++i) {
        if (!client->wish[i].used) {
            slot = &client->wish[i];
        }
    }
    if (slot == NULL) {
        /* Full: the one nearest its deadline gives way, and is answered here. */
        slot = &client->wish[0];
        for (i = 1; i < MP_NPC_COPIES_CLIENT_WISHES; ++i) {
            if ((int32_t)(client->wish[i].deadline - slot->deadline) < 0) {
                slot = &client->wish[i];
            }
        }
        ++client->counters.wishes_pushed_out;
        if (slot->kind == NPC_SPAWN_WISH_SPAWN) {
            owe_refusal(client, slot->note, (uint8_t)NPC_SPAWN_REFUSED_NOT_BUILT);
            ++client->counters.refusals_said_here;
        }
    }
    memset(slot, 0, sizeof *slot);
    slot->used     = true;
    slot->kind     = kind;
    slot->note     = note_serial;
    slot->wire     = (uint16_t)note_serial;
    slot->deadline = client->censuses + MP_NPC_COPIES_WISH_DEADLINE;
    if (kind == NPC_SPAWN_WISH_SPAWN && desc != NULL) {
        memcpy(slot->desc, desc, MP_NPC_COPY_DESC_BYTES);
    }
    ++client->counters.wishes_opened;
    return slot->wire;
}

uint8_t mp_npc_copies_client_state(const mp_npc_copies_client_t *client, uint32_t k)
{
    return (client != NULL && k < MP_WIRE_COPY_MAX) ? client->held[k].state
                                                    : (uint8_t)MP_NPC_HELD_NONE;
}
