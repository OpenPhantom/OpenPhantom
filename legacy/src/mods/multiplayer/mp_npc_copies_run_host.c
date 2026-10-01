/* mp_npc_copies_run_host.c: the NPC copies' protocol on a host. See mp_npc_copies_run.h.
 *
 * The host decides every wish, its own overlay's from the note and its clients' from the wire,
 * writes the grants, cancels and owner changes its overlay is owed, announces each copy once it is
 * seen alive, repeats one live copy four times a second for whoever joined late, and tells a client
 * whose wish it refused, that client alone.
 */
#include "mp_npc_copies_run.h"

#include "mp_npc_copies.h"
#include "mp_npc_copies_run_internal.h"
#include "mp_npc_copy_wire.h"
#include "mp_wire.h"

#include "common/npc_spawn_note.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* More turns than duties can be owed at once: six a row, 128 rows. A turn does one or drops one. */
#define DUTY_TURNS (6u * MP_WIRE_COPY_MAX + 1u)

static void mark_granted(mp_npc_copies_run_t *run, uint32_t k)
{
    run->granted[k / 32u] |= 1u << (k % 32u);
}

/* A grant the decision allowed and the table would not make is a description the table could not
 * take: refused as unsound, never left without an answer. */
static bool grant(mp_npc_copies_run_t *run, uint32_t k, const mp_npc_copy_who_t *who,
                  const uint8_t *desc)
{
    if (mp_npc_copies_grant(&run->host, k, who, desc) == 0u) {
        ++run->counters.grant_failed;
        return false;
    }
    mark_granted(run, k);
    return true;
}

void mp_npc_copies_run_host_wish(mp_npc_copies_run_t *run, const npc_spawn_wish_t *wish,
                                 uint32_t serial)
{
    mp_npc_copy_ask_t     ask;
    mp_npc_copy_verdict_t verdict;
    mp_npc_copy_who_t     who;
    uint8_t               desc[MP_NPC_COPY_DESC_BYTES];
    bool                  builds =
        wish->kind == NPC_SPAWN_WISH_SPAWN || wish->kind == NPC_SPAWN_WISH_RESTORE;

    memset(desc, 0, sizeof desc);
    memset(&ask, 0, sizeof ask);
    ask.kind              = wish->kind;
    ask.from_wire         = false;
    ask.bucket_ok         = true;
    ask.wish_epoch        = wish->epoch;
    ask.epoch             = run->record.epoch;
    ask.in_level          = run->in_level;
    ask.has_builder       = run->builder;
    ask.description_sound = !builds || mp_npc_copy_desc_put(&wish->desc, desc);
    ask.cap               = run->cap;
    mp_npc_copies_decide(&run->host, &ask, &verdict);

    switch (verdict.outcome) {
    case MP_NPC_COPY_GRANT:
        memset(&who, 0, sizeof who);
        who.asker_own = true;
        who.wish      = serial;
        if (!grant(run, verdict.k, &who, desc)) {
            mp_npc_copies_run_refuse_here(run, serial, (uint8_t)NPC_SPAWN_REFUSED_UNSOUND);
        }
        break;
    case MP_NPC_COPY_REFUSE:
        mp_npc_copies_run_refuse_here(run, serial, verdict.reason);
        break;
    case MP_NPC_COPY_REMOVE:
        /* The host's own copies, and every copy a player who left handed on to it. */
        if (wish->kind == NPC_SPAWN_WISH_REMOVE_ALL) {
            (void)mp_npc_copies_cancel_all(&run->host);
        } else {
            (void)mp_npc_copies_cancel_owned(&run->host, 0u, 0u);
        }
        break;
    case MP_NPC_COPY_STALE:
    default:
        ++run->counters.wishes_stale;   /* the overlay ends it itself, at the epoch */
        break;
    }
}

static void queue_wire_refusal(mp_npc_copies_run_t *run, const mp_npc_copy_wish_t *wish,
                               uint8_t slot, uint64_t connection, uint8_t reason)
{
    mp_npc_copies_run_refusal_t *refusal;

    if (run->wire_refusals >= MP_NPC_RUN_REFUSALS) {
        ++run->counters.wire_overflow;
        return;
    }
    refusal = &run->wire_refusal[run->wire_refusals];
    if (mp_npc_copies_refusal_for(wish, slot, reason, &refusal->entry)) {
        refusal->slot       = slot;
        refusal->connection = connection;
        ++run->wire_refusals;
    }
}

void mp_npc_copies_run_host_take(mp_npc_copies_run_t *run, uint8_t slot, uint64_t connection,
                                 const uint8_t *note, size_t bytes, uint32_t now_ms)
{
    mp_npc_copy_wish_t    wish;
    mp_npc_copy_ask_t     ask;
    mp_npc_copy_verdict_t verdict;
    mp_npc_copy_who_t     who;
    uint8_t               desc[MP_NPC_COPY_DESC_BYTES];

    if (!mp_npc_copy_wish_decode(note, bytes, &wish)) {
        ++run->counters.wire_torn;
        return;
    }
    ++run->counters.wire_taken;
    if (slot == 0u || slot >= NPC_SPAWN_WORLD_SLOTS) {
        ++run->counters.wire_wrong_role;   /* nobody on the wire speaks for the host's slot */
        return;
    }
    /* The lobby, or a load: the wish is about a world nobody stands in. Its asker ends it with
     * its own epoch when it gets to the next one. */
    if (!run->in_level) {
        ++run->counters.wire_in_lobby;
        return;
    }
    memset(desc, 0, sizeof desc);
    memset(&ask, 0, sizeof ask);
    ask.kind              = wish.kind;
    ask.from_wire         = true;
    ask.bucket_ok         = mp_npc_copies_throttle(&run->host, slot, connection, now_ms);
    ask.in_level          = true;
    ask.wish_level        = wish.level;
    ask.level             = run->level;
    ask.wish_world        = wish.world;
    ask.world             = run->world;
    ask.has_builder       = run->builder;
    ask.description_sound = wish.kind != NPC_SPAWN_WISH_SPAWN ||
                            mp_npc_copy_desc_put(&wish.desc, desc);
    ask.cap               = run->cap;
    mp_npc_copies_decide(&run->host, &ask, &verdict);

    switch (verdict.outcome) {
    case MP_NPC_COPY_GRANT:
        memset(&who, 0, sizeof who);
        who.owner            = slot;
        who.owner_connection = connection;
        who.asker            = slot;
        who.asker_connection = connection;
        who.wish             = wish.serial;
        if (!grant(run, verdict.k, &who, desc)) {
            queue_wire_refusal(run, &wish, slot, connection, (uint8_t)NPC_SPAWN_REFUSED_UNSOUND);
        }
        break;
    case MP_NPC_COPY_REFUSE:
        if (verdict.reason == NPC_SPAWN_REFUSED_TOO_FAST &&
            !mp_npc_copies_throttle_answer(&run->host, slot, now_ms)) {
            ++run->counters.too_fast_quiet;   /* a flood is not answered in kind */
            break;
        }
        queue_wire_refusal(run, &wish, slot, connection, verdict.reason);
        break;
    case MP_NPC_COPY_REMOVE:
        (void)mp_npc_copies_cancel_owned(&run->host, slot, connection);
        break;
    default:
        break;
    }
}

static bool send_entry(mp_npc_copies_run_t *run, const mp_npc_copies_run_in_t *in, bool to_all,
                       uint8_t slot, const mp_npc_copy_entry_t *entry)
{
    uint8_t bytes[MP_NPC_COPY_ENTRY_BYTES];
    size_t  count = mp_npc_copy_entry_encode(entry, bytes, sizeof bytes);
    bool    sent;

    if (count == 0u) {
        ++run->counters.wire_torn;
        return true;   /* nothing the wire could carry; waiting would not change that */
    }
    if (to_all) {
        sent = in->broadcast != NULL && in->broadcast(in->user, bytes, count);
    } else {
        sent = in->send_to != NULL && in->send_to(in->user, slot, bytes, count);
    }
    if (!sent) {
        ++run->counters.unsent;
    }
    return sent;
}

/* One duty of the wire, counted in `sent` when it went. False when the channel refused it: the
 * rest of the wire waits. */
static bool wire_duty(mp_npc_copies_run_t *run, const mp_npc_copies_run_in_t *in, uint32_t k,
                      uint8_t duty, uint32_t *sent)
{
    mp_npc_copy_entry_t entry;

    if (duty == MP_NPC_COPY_DUTY_REFUSE_ASKER &&
        !mp_npc_copies_asker_present(&run->host, k, in->connection_of)) {
        mp_npc_copies_duty_drop(&run->host, k, duty);
        return true;
    }
    if (!mp_npc_copies_entry_for(&run->host, k, duty, run->level, run->world, &entry)) {
        mp_npc_copies_duty_drop(&run->host, k, duty);
        return true;
    }
    if (!send_entry(run, in, duty == MP_NPC_COPY_DUTY_ANNOUNCE, entry.owner, &entry)) {
        return false;
    }
    if (duty == MP_NPC_COPY_DUTY_ANNOUNCE) {
        ++run->counters.announced;
    }
    ++*sent;
    mp_npc_copies_duty_done(&run->host, k, duty, 0u);
    return true;
}

/* One duty of the note. False when the ring has no room for it. */
static bool note_duty(mp_npc_copies_run_t *run, uint32_t k, uint8_t duty)
{
    npc_spawn_grant_t grant_note;
    uint32_t          serial = 0;

    if (!mp_npc_copies_note_for(&run->host, k, duty, &grant_note)) {
        mp_npc_copies_duty_drop(&run->host, k, duty);
        return true;
    }
    if (!mp_npc_copies_run_append(run, &grant_note, &serial)) {
        return false;
    }
    mp_npc_copies_duty_done(&run->host, k, duty, serial);
    return true;
}

static void send_wire_refusals(mp_npc_copies_run_t *run, const mp_npc_copies_run_in_t *in,
                               uint32_t budget)
{
    while (in->joined && run->wire_refusals > 0u && budget > 0u) {
        const mp_npc_copies_run_refusal_t *refusal = &run->wire_refusal[0];

        if (in->connection_of == NULL ||
            in->connection_of[refusal->slot] != refusal->connection) {
            ++run->counters.wire_refusals_dropped;   /* the asker left; the slot is another's */
        } else if (!send_entry(run, in, false, refusal->slot, &refusal->entry)) {
            return;
        } else {
            ++run->counters.wire_refusals_sent;
            --budget;
        }
        --run->wire_refusals;
        memmove(&run->wire_refusal[0], &run->wire_refusal[1],
                run->wire_refusals * sizeof run->wire_refusal[0]);
    }
}

/* One live copy, four times a second, for whoever joined since it was announced. */
static void repeat_one(mp_npc_copies_run_t *run, const mp_npc_copies_run_in_t *in)
{
    mp_npc_copy_entry_t entry;
    uint32_t            k = 0;

    if (!in->joined ||
        (run->repeat_known && (int32_t)(in->now_ms - run->repeat_at_ms) < 0)) {
        return;
    }
    run->repeat_known = true;
    run->repeat_at_ms = in->now_ms + MP_NPC_RUN_REPEAT_MS;
    if (mp_npc_copies_next_live(&run->host, &run->repeat_cursor, &k) &&
        mp_npc_copies_entry_for(&run->host, k, MP_NPC_COPY_DUTY_ANNOUNCE, run->level, run->world,
                                &entry) &&
        send_entry(run, in, true, 0u, &entry)) {
        ++run->counters.repeated;
    }
}

void mp_npc_copies_run_host_duties(mp_npc_copies_run_t *run, const mp_npc_copies_run_in_t *in)
{
    uint8_t  mask;
    uint32_t turn;
    uint32_t sent   = 0;
    bool     waited = false;

    mp_npc_copies_owners(&run->host, in->connection_of);
    mp_npc_copies_run_write_refusals(run);
    mask = (uint8_t)((run->record.count < NPC_SPAWN_GRANT_SLOTS ? MP_NPC_COPY_DUTIES_NOTE : 0u) |
                     (in->joined ? MP_NPC_COPY_DUTIES_WIRE : 0u));
    for (turn = 0; mask != 0u && turn < DUTY_TURNS; ++turn) {
        uint32_t k    = 0;
        uint8_t  duty = 0;

        if (!mp_npc_copies_next_duty(&run->host, mask, &k, &duty)) {
            break;
        }
        if ((duty & MP_NPC_COPY_DUTIES_NOTE) != 0u) {
            if (!note_duty(run, k, duty)) {
                mask   = (uint8_t)(mask & ~MP_NPC_COPY_DUTIES_NOTE);
                waited = true;
            }
        } else if (!wire_duty(run, in, k, duty, &sent) || sent >= MP_NPC_RUN_WIRE_PER_SUBSTEP) {
            mask = (uint8_t)(mask & ~MP_NPC_COPY_DUTIES_WIRE);
        }
    }
    if (waited || (mask & MP_NPC_COPY_DUTIES_NOTE) == 0u) {
        uint32_t k    = 0;
        uint8_t  duty = 0;

        if (mp_npc_copies_next_duty(&run->host, MP_NPC_COPY_DUTIES_NOTE, &k, &duty)) {
            ++run->counters.ring_full;
        }
    }
    send_wire_refusals(run, in, MP_NPC_RUN_WIRE_PER_SUBSTEP - sent);
    repeat_one(run, in);
}
