/* mp_npc_copies_run_client.c: the NPC copies' protocol on a client. See mp_npc_copies_run.h.
 *
 * A client asks its host for what its overlay wished, hands its overlay what the host announced,
 * and writes the overlay every answer a wish is owed. It builds nothing without an overlay that
 * builds and a way to hold a copy still, and asks for nothing outside a level or before its host
 * has told it its slot.
 */
#include "mp_npc_copies_run.h"

#include "mp_npc_copies_client.h"
#include "mp_npc_copies_run_internal.h"
#include "mp_npc_copy_wire.h"
#include "mp_wire.h"

#include "common/npc_spawn_note.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* More turns than duties can be owed at once: three a row, 128 rows. */
#define DUTY_TURNS (3u * MP_WIRE_COPY_MAX + 1u)

bool mp_npc_copies_run_client_wish(mp_npc_copies_run_t *run, const mp_npc_copies_run_in_t *in,
                                   const npc_spawn_wish_t *wish, uint32_t serial)
{
    mp_npc_copy_wish_t out;
    uint8_t            desc[MP_NPC_COPY_DESC_BYTES];
    uint8_t            bytes[MP_NPC_COPY_WISH_BYTES];
    size_t             count;
    bool               spawn = wish->kind == NPC_SPAWN_WISH_SPAWN;

    /* Described in another world: the overlay ends it itself when it reads the new epoch. */
    if (wish->epoch != run->record.epoch) {
        ++run->counters.wishes_stale;
        return true;
    }
    if (!spawn && wish->kind != NPC_SPAWN_WISH_REMOVE_OWN) {
        mp_npc_copies_run_refuse_here(run, serial, (uint8_t)NPC_SPAWN_REFUSED_UNSOUND);
        return true;   /* a restore and a removal of all are the host's own */
    }
    if (!in->joined || !run->in_level || run->own_slot == 0u) {
        mp_npc_copies_run_refuse_here(run, serial, (uint8_t)NPC_SPAWN_REFUSED_NO_LEVEL);
        return true;
    }
    memset(desc, 0, sizeof desc);
    if (spawn && !run->can_park) {
        mp_npc_copies_run_refuse_here(run, serial, (uint8_t)NPC_SPAWN_REFUSED_NO_PARKING);
        return true;
    }
    if (spawn && !mp_npc_copy_desc_put(&wish->desc, desc)) {
        mp_npc_copies_run_refuse_here(run, serial, (uint8_t)NPC_SPAWN_REFUSED_UNSOUND);
        return true;
    }
    memset(&out, 0, sizeof out);
    out.kind   = wish->kind;
    out.serial = (uint16_t)serial;
    out.level  = run->level;
    out.world  = run->world;
    if (spawn) {
        out.desc = wish->desc;
    }
    count = mp_npc_copy_wish_encode(&out, bytes, sizeof bytes);
    if (count == 0u) {
        mp_npc_copies_run_refuse_here(run, serial, (uint8_t)NPC_SPAWN_REFUSED_UNSOUND);
        return true;
    }
    /* Opened only once it is out, so a wish the channel would not take is not waited for. */
    if (in->broadcast == NULL || !in->broadcast(in->user, bytes, count)) {
        ++run->counters.unsent;
        ++run->counters.wishes_held;
        return false;
    }
    (void)mp_npc_copies_client_open_wish(&run->client, serial, wish->kind, spawn ? desc : NULL);
    ++run->counters.wishes_sent;
    return true;
}

void mp_npc_copies_run_client_take(mp_npc_copies_run_t *run, const uint8_t *note, size_t bytes)
{
    mp_npc_copy_entry_t entry;

    if (!mp_npc_copy_entry_decode(note, bytes, &entry)) {
        ++run->counters.wire_torn;
        return;
    }
    if (!run->in_level || entry.level != run->level || entry.world != run->world) {
        ++run->counters.entries_stale;
        return;
    }
    if (entry.kind == NPC_SPAWN_GRANT_REFUSED) {
        (void)mp_npc_copies_client_take_refusal(&run->client, &entry, run->own_slot);
        return;
    }
    /* Nothing is handed to an overlay that cannot build, nor built where it cannot be held still:
     * the host repeats every live copy, so one dropped here comes again once both are there. */
    if (!run->builder) {
        ++run->counters.entries_no_panel;
        return;
    }
    if (!run->can_park) {
        ++run->counters.entries_no_parking;
        return;
    }
    mp_npc_copies_client_take_entry(&run->client, &entry, run->own_slot);
}

void mp_npc_copies_run_client_duties(mp_npc_copies_run_t *run)
{
    mp_npc_refusal_t refusal;
    uint32_t         k    = 0;
    uint32_t         turn;
    uint8_t          duty = 0;

    mp_npc_copies_run_write_refusals(run);
    for (turn = 0; turn < DUTY_TURNS && run->record.count < NPC_SPAWN_GRANT_SLOTS &&
                   mp_npc_copies_client_next_duty(&run->client, &k, &duty);
         ++turn) {
        npc_spawn_grant_t grant;
        uint32_t          serial = 0;

        /* A duty whose note cannot be made would stop every one behind it: its life is given
         * up instead, which answers a wish it carried, and counted. */
        if (!mp_npc_copies_client_note_for(&run->client, k, duty, &grant)) {
            mp_npc_copies_client_give_up(&run->client, k, run->client.held[k].generation,
                                         MP_NPC_GIVE_UP_RELEASE);
            ++run->counters.duties_dropped;
            continue;
        }
        if (!mp_npc_copies_run_append(run, &grant, &serial)) {
            break;
        }
        mp_npc_copies_client_duty_done(&run->client, k, duty, serial);
    }
    if (run->record.count >= NPC_SPAWN_GRANT_SLOTS &&
        mp_npc_copies_client_next_duty(&run->client, &k, &duty)) {
        ++run->counters.ring_full;
    }
    while (run->record.count < NPC_SPAWN_GRANT_SLOTS && run->client.refusals > 0u &&
           mp_npc_copies_client_next_refusal(&run->client, &refusal)) {
        npc_spawn_grant_t grant;
        uint32_t          serial = 0;

        memset(&grant, 0, sizeof grant);
        grant.kind   = (uint8_t)NPC_SPAWN_GRANT_REFUSED;
        grant.wish   = refusal.note;
        grant.reason = refusal.reason;
        ++run->counters.refused_here[refusal.reason < MP_NPC_COPIES_REASONS ? refusal.reason
                                                                             : 0u];
        (void)mp_npc_copies_run_append(run, &grant, &serial);
    }
}
