/* mp_npc_copies_run.c: the NPC copies' protocol, the part both roles share. See the header; the
 * host's part is mp_npc_copies_run_host.c, a client's mp_npc_copies_run_client.c.
 */
#include "mp_npc_copies_run.h"

#include "mp_npc_copies.h"
#include "mp_npc_copies_client.h"
#include "mp_npc_copies_run_internal.h"
#include "mp_npc_copy_wire.h"
#include "mp_wire.h"

#include "common/npc_spawn_note.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

void mp_npc_copies_run_init(mp_npc_copies_run_t *run)
{
    if (run == NULL) {
        return;
    }
    memset(run, 0, sizeof *run);
    mp_npc_copies_init(&run->host, 0u);
    mp_npc_copies_client_init(&run->client, MP_NPC_RUN_ORPHAN_BLOCKS);
    run->record.first = 1u;   /* serials start at 1: 0 names no entry */
    run->ready        = true;
}

/* A new epoch, in one call with the ring emptied and every queue behind it, and published at once:
 * a record written a substep later would let the overlay read the old epoch in the new world. */
static void raise_epoch(mp_npc_copies_run_t *run)
{
    ++run->record.epoch;
    npc_spawn_note_grants_restart(&run->record);
    run->in_level      = false;
    run->own_refusals  = 0u;
    run->wire_refusals = 0u;
    run->repeat_known  = false;
    memset(run->granted, 0, sizeof run->granted);
    run->dirty = true;
}

void mp_npc_copies_run_arm(mp_npc_copies_run_t *run, mp_npc_run_role_t role, uint16_t cap,
                           uint32_t corpse_censuses, uint32_t orphan_blocks)
{
    if (run == NULL) {
        return;
    }
    if (!run->ready) {
        mp_npc_copies_run_init(run);
    }
    mp_npc_copies_clear(&run->host);
    mp_npc_copies_client_clear(&run->client);
    run->host.corpse_censuses  = corpse_censuses;
    run->client.orphan_blocks  = orphan_blocks;
    run->role                  = (uint8_t)role;
    run->cap                   = role == MP_NPC_RUN_HOST ? cap : 0u;
    run->builder               = false;
    run->own_slot              = 0u;
    run->can_park              = false;
    run->record.cap            = run->cap;
    run->record.own_slot       = 0u;
    raise_epoch(run);
}

void mp_npc_copies_run_disarm(mp_npc_copies_run_t *run)
{
    if (run == NULL || run->role == MP_NPC_RUN_OFF) {
        return;
    }
    mp_npc_copies_clear(&run->host);
    mp_npc_copies_client_clear(&run->client);
    run->role            = (uint8_t)MP_NPC_RUN_OFF;
    run->cap             = 0u;
    run->record.cap      = 0u;
    run->record.own_slot = 0u;
    raise_epoch(run);
}

void mp_npc_copies_run_new_world(mp_npc_copies_run_t *run)
{
    if (run == NULL || run->role == MP_NPC_RUN_OFF) {
        return;
    }
    mp_npc_copies_clear(&run->host);
    mp_npc_copies_client_clear(&run->client);
    raise_epoch(run);
}

void mp_npc_copies_run_census_begin(mp_npc_copies_run_t *run)
{
    if (run == NULL) {
        return;
    }
    if (run->role == MP_NPC_RUN_HOST) {
        mp_npc_copies_census_begin(&run->host);
    } else if (run->role == MP_NPC_RUN_CLIENT) {
        mp_npc_copies_client_census_begin(&run->client);
    }
}

void mp_npc_copies_run_census_saw(mp_npc_copies_run_t *run, uint32_t k, bool live)
{
    if (run == NULL) {
        return;
    }
    if (run->role == MP_NPC_RUN_HOST) {
        mp_npc_copies_census_saw(&run->host, k, live);
    } else if (run->role == MP_NPC_RUN_CLIENT) {
        mp_npc_copies_client_census_saw(&run->client, k);
    }
}

void mp_npc_copies_run_census_end(mp_npc_copies_run_t *run, bool complete,
                                  uint32_t chain_length, uint32_t capacity)
{
    if (run == NULL) {
        return;
    }
    if (run->role == MP_NPC_RUN_HOST) {
        mp_npc_copies_census_end(&run->host, complete, chain_length, capacity);
    } else if (run->role == MP_NPC_RUN_CLIENT) {
        mp_npc_copies_client_census_end(&run->client, complete);
    }
}

bool mp_npc_copies_run_append(mp_npc_copies_run_t *run, const npc_spawn_grant_t *grant,
                              uint32_t *serial)
{
    uint32_t next = run->record.first + run->record.count;

    /* A counter that would give out 0 next stops instead (the contract's top). */
    if (next == 0u) {
        ++run->counters.serials_spent;
        return false;
    }
    if (!npc_spawn_note_grants_append(&run->record, next, grant)) {
        return false;
    }
    *serial    = next;
    run->dirty = true;
    return true;
}

void mp_npc_copies_run_refuse_here(mp_npc_copies_run_t *run, uint32_t wish, uint8_t reason)
{
    npc_spawn_grant_t *grant;

    if (wish == 0u || reason == 0u) {
        return;
    }
    ++run->counters.refused_here[reason < MP_NPC_COPIES_REASONS ? reason : 0u];
    if (run->own_refusals >= MP_NPC_RUN_REFUSALS) {
        ++run->counters.own_overflow;
        return;
    }
    grant = &run->own_refusal[run->own_refusals++];
    memset(grant, 0, sizeof *grant);
    grant->kind   = (uint8_t)NPC_SPAWN_GRANT_REFUSED;
    grant->wish   = wish;
    grant->reason = reason;
}

void mp_npc_copies_run_write_refusals(mp_npc_copies_run_t *run)
{
    while (run->own_refusals > 0u && run->record.count < NPC_SPAWN_GRANT_SLOTS) {
        uint32_t serial = 0;

        if (!mp_npc_copies_run_append(run, &run->own_refusal[0], &serial) &&
            run->record.first + run->record.count == 0u) {
            return;   /* the serials are spent; anything else it could not take is dropped */
        }
        --run->own_refusals;
        memmove(&run->own_refusal[0], &run->own_refusal[1],
                run->own_refusals * sizeof run->own_refusal[0]);
    }
}

/* The overlay's answers, in the read that drops what they answer (the contract's top). An answer
 * to a refusal entry is only counted: nothing in the tables waits for it. */
static void read_answers(mp_npc_copies_run_t *run, const npc_spawn_wish_record_t *wishes)
{
    uint32_t i;

    for (i = 0; i < run->record.count; ++i) {
        uint32_t                 serial = run->record.first + i;
        const npc_spawn_grant_t *grant  = &run->record.entry[i];
        npc_spawn_answer_t       answer;

        if (npc_spawn_note_serial_after(serial, wishes->grants_done)) {
            break;
        }
        answer = npc_spawn_note_answer(wishes, serial);
        if (grant->kind == NPC_SPAWN_GRANT_REFUSED) {
            ++run->counters.answers_to_refusals;
        } else if (run->role == MP_NPC_RUN_HOST) {
            mp_npc_copies_take_answer(&run->host, serial, answer);
        } else {
            mp_npc_copies_client_take_answer(&run->client, serial, answer);
        }
    }
    if (npc_spawn_note_grants_drop(&run->record, wishes->grants_done) != 0u) {
        run->dirty = true;
    }
}

/* The overlay's new wishes, in serial order. A client whose wish could not go out stops there and
 * takes it again next substep, and so does either side while the refusals it owes its overlay
 * fill their queue: a wish left in the overlay's record is answered later, one dropped never.
 *
 * The host leaves its own overlay's wishes there until a level is entered. The ones that come
 * before it are a load's restores, published between the load and the level's beginning; refused
 * for want of a level, every copy of the save would be lost, and waiting costs nothing: a wish of
 * a world that never began carries an epoch the next one ends. */
static void read_wishes(mp_npc_copies_run_t *run, const mp_npc_copies_run_in_t *in,
                        const npc_spawn_wish_record_t *wishes)
{
    uint32_t i;

    for (i = 0; i < wishes->count; ++i) {
        uint32_t serial = wishes->first + i;

        if (!npc_spawn_note_serial_after(serial, run->record.wishes_taken)) {
            continue;
        }
        if (run->own_refusals >= MP_NPC_RUN_REFUSALS ||
            (run->role == MP_NPC_RUN_HOST && !run->in_level)) {
            return;
        }
        if (run->role == MP_NPC_RUN_HOST) {
            mp_npc_copies_run_host_wish(run, &wishes->entry[i], serial);
        } else if (!mp_npc_copies_run_client_wish(run, in, &wishes->entry[i], serial)) {
            return;
        }
        run->record.wishes_taken = serial;
        run->dirty               = true;
        ++run->counters.wishes_read;
    }
}

bool mp_npc_copies_run_substep(mp_npc_copies_run_t *run, const mp_npc_copies_run_in_t *in)
{
    uint8_t own_slot;

    if (run == NULL || in == NULL || run->role == MP_NPC_RUN_OFF) {
        return false;
    }
    run->in_level = in->level_known;
    run->level    = in->level_known ? in->level : 0u;
    run->world    = in->world;
    run->can_park = in->can_park;
    own_slot      = (run->role == MP_NPC_RUN_CLIENT && in->own_slot < NPC_SPAWN_WORLD_SLOTS)
                        ? in->own_slot
                        : 0u;
    run->own_slot = own_slot;
    if (run->record.own_slot != own_slot) {
        run->record.own_slot = own_slot;
        run->dirty           = true;
    }
    if (in->wishes != NULL) {
        run->builder = run->builder || (in->wishes->panel & NPC_SPAWN_PANEL_BUILDER) != 0u;
        read_answers(run, in->wishes);
        read_wishes(run, in, in->wishes);
    }
    if (run->role == MP_NPC_RUN_HOST) {
        mp_npc_copies_run_host_duties(run, in);
    } else {
        mp_npc_copies_run_client_duties(run);
    }
    return run->dirty;
}

bool mp_npc_copies_run_take(mp_npc_copies_run_t *run, uint8_t slot, uint64_t connection,
                            const uint8_t *note, size_t bytes, uint32_t now_ms)
{
    bool wish;

    if (run == NULL) {
        return false;
    }
    wish = mp_npc_copy_wish_is(note, bytes);
    if (!wish && !mp_npc_copy_entry_is(note, bytes)) {
        return false;
    }
    if (wish && run->role == MP_NPC_RUN_HOST) {
        mp_npc_copies_run_host_take(run, slot, connection, note, bytes, now_ms);
    } else if (!wish && run->role == MP_NPC_RUN_CLIENT) {
        mp_npc_copies_run_client_take(run, note, bytes);
    } else {
        ++run->counters.wire_wrong_role;
    }
    return true;
}

void mp_npc_copies_run_spawned(mp_npc_copies_run_t *run, uint32_t k, uintptr_t actor,
                               bool parked)
{
    uint8_t state;

    if (run == NULL || run->role != MP_NPC_RUN_CLIENT || k >= MP_WIRE_COPY_MAX) {
        return;
    }
    state = mp_npc_copies_client_state(&run->client, k);
    if (state != MP_NPC_HELD_HANDED && state != MP_NPC_HELD_BUILT) {
        ++run->counters.spawned_unknown;
        return;
    }
    if (parked) {
        mp_npc_copies_client_parked(&run->client, k, actor);
        ++run->counters.parked;
        return;
    }
    /* A replica that runs its own script can be killed here and removed by this machine's
     * engine, which is a removal past the overlay: it is cancelled at once instead. */
    mp_npc_copies_client_give_up(&run->client, k, run->client.held[k].generation,
                                 MP_NPC_GIVE_UP_PARKING);
    ++run->counters.park_failed;
}

void mp_npc_copies_run_removed(mp_npc_copies_run_t *run, uint32_t k, uint8_t generation,
                               bool built)
{
    if (run == NULL || run->role != MP_NPC_RUN_CLIENT) {
        return;
    }
    mp_npc_copies_client_give_up(&run->client, k, generation,
                                 built ? MP_NPC_GIVE_UP_DESPAWN : MP_NPC_GIVE_UP_TOMBSTONE);
    ++run->counters.removed;
}

bool mp_npc_copies_run_next_granted(mp_npc_copies_run_t *run, uint32_t *k)
{
    uint32_t word;

    if (run == NULL || k == NULL) {
        return false;
    }
    for (word = 0; word < MP_WIRE_COPY_MAX / 32u; ++word) {
        uint32_t bits = run->granted[word];
        uint32_t bit;

        if (bits == 0u) {
            continue;
        }
        for (bit = 0; (bits & (1u << bit)) == 0u; ++bit) {
        }
        run->granted[word] &= ~(1u << bit);
        *k = word * 32u + bit;
        return true;
    }
    return false;
}

const mp_npc_copies_t *mp_npc_copies_run_host_table(const mp_npc_copies_run_t *run)
{
    return (run != NULL && run->role == MP_NPC_RUN_HOST) ? &run->host : NULL;
}

mp_npc_copies_client_t *mp_npc_copies_run_client_table(mp_npc_copies_run_t *run)
{
    return (run != NULL && run->role == MP_NPC_RUN_CLIENT) ? &run->client : NULL;
}

const npc_spawn_grant_record_t *mp_npc_copies_run_record(const mp_npc_copies_run_t *run)
{
    return run != NULL ? &run->record : NULL;
}

void mp_npc_copies_run_published(mp_npc_copies_run_t *run, bool taken)
{
    if (run == NULL) {
        return;
    }
    if (taken) {
        run->dirty = false;
        ++run->counters.published;
    } else {
        ++run->counters.publish_refused;   /* dirty still: it is tried again next substep */
    }
}
