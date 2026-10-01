/* mp_bridge_drain_host.c: a listen host's read of every client's own state, and the greeting of a
 * client that enters its world. See the header. */
#include "mp_bridge_drain_host.h"

#include "mp_actions.h"
#include "mp_bank.h"
#include "mp_bridge_far.h"
#include "mp_bridge_lobby.h"
#include "mp_bridge_world.h"
#include "mp_crate.h"
#include "mp_interp.h"
#include "mp_lobby.h"
#include "mp_payload_prefix.h"
#include "mp_puppet.h"
#include "mp_scratch_wire.h"
#include "mp_session.h"
#include "mp_snapshot.h"
#include "mp_wallclock.h"
#include "mp_wire.h"
#include "mp_world.h"

#include "common/logging.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* What this host knows of one peer's connection: when it appeared, whether the session's level
 * already ran then, and which world it last entered. Keyed on the connection, so a new player
 * behind the same peer index starts a row of its own. */
typedef struct peer_entry {
    uint64_t connection;     /* the connection this row describes, 0 for none */
    bool     late;           /* it appeared while the session's level ran */
    uint32_t sighted_ms;     /* when this host first saw it, on the multiplayer's wall clock */
    bool     entered;        /* a state of `world` has come from it */
    uint8_t  world;          /* the world it entered last, as this host stood in it then */
    bool     ever_entered;   /* this connection entered some world */
    bool     owed;           /* an entry noticed and not greeted yet */
} peer_entry_t;

/* Module state because a process holds one listen host; a peer past the far banks is never read. */
static peer_entry_t            entries[MP_BANK_FAR_MAX];
static mp_bridge_drain_joins_t joins;

/* Whether the session's level runs, by the host's own setup: started and not ended. A host armed
 * from the ini with no lobby has no setup and so no late joiner; its players still enter. */
static bool level_runs(mp_lobby_setup_t *setup)
{
    return mp_bridge_lobby_setup(setup) && (setup->flags & MP_LOBBY_F_STARTED) != 0u &&
           (setup->flags & MP_LOBBY_F_ENDED) == 0u;
}

/* A connection is seen for the first time, or a peer's goes. Bookkeeping only, so it runs between
 * substeps as well as inside one. */
static void sight(const mp_bridge_drain_t *drain, size_t peer, uint8_t slot, uint64_t now)
{
    peer_entry_t    *entry = &entries[peer];
    mp_lobby_setup_t setup;

    if (now == entry->connection) {
        return;
    }
    if (entry->connection != 0u && entry->late && !entry->ever_entered) {
        ++joins.left_before_entry;
    }
    memset(entry, 0, sizeof *entry);
    entry->connection = now;
    if (now == 0u) {
        return;
    }
    entry->sighted_ms = mp_wallclock_ms();
    memset(&setup, 0, sizeof setup);
    entry->late = level_runs(&setup);
    if (entry->late) {
        ++joins.joined_running;
        log_info("slot %u (peer %u) joined while this host plays %s (%s) at substep %u: it waits "
                 "in its lobby", (unsigned)slot, (unsigned)peer, setup.title, setup.level,
                 (unsigned)drain->tick);
    }
}

/* One state of a peer, sent from `world`. The first of the world this host stands in is an entry,
 * once per connection and world. */
static void note_state(size_t peer, uint8_t world)
{
    peer_entry_t *entry = &entries[peer];
    uint8_t       here  = mp_bridge_far_world();

    if (!mp_bridge_far_is_this_world(world) || (entry->entered && entry->world == here)) {
        return;
    }
    entry->entered = true;
    entry->world   = here;
    entry->owed    = true;
}

/* Whether the newest state bank `bank` holds was sent from the world this host stands in. Asked
 * of the history rather than of the entry's own flag, so the count below is a witness to the
 * entry rule and not a copy of it. */
static bool newest_is_of_this_world(size_t bank)
{
    mp_wire_body_t body;
    uint32_t       tick = 0u;

    memset(&body, 0, sizeof body);
    return mp_interp_newest(mp_bridge_far_interp(bank), &body, &tick) &&
           mp_bridge_far_is_this_world(body.world);
}

/* What a player who has entered this world is owed, and the one way it is told again: this
 * player's appearance, every mover, the push blocks, the campaign bank with the blackboard, and
 * the baseline forgotten once more so the first enemy payload after the entry is whole. A call for
 * a bank that has sent no state of this world is counted, and must never happen: it is the
 * greeting at the connection coming back in another shape. */
static void tell_again(size_t peer, size_t bank)
{
    ++joins.greeted_at_entry;
    if (!newest_is_of_this_world(bank)) {
        ++joins.greeted_unentered;
    }
    mp_bridge_world_forget_peer(peer);
    mp_actions_owe_appearance();
    mp_world_note_arrival();
    mp_crate_note_arrival();
    mp_scratch_wire_note_arrival();
}

/* An entry noticed, greeted inside a substep. One that a world change overtook is no entry any
 * more: the next state of the new world makes the next one. */
static void greet_entry(size_t peer, size_t bank, uint8_t slot)
{
    peer_entry_t *entry = &entries[peer];

    if (!entry->owed) {
        return;
    }
    entry->owed = false;
    if (!mp_bridge_far_is_this_world(entry->world)) {
        return;
    }
    tell_again(peer, bank);
    if (entry->late && !entry->ever_entered) {
        ++joins.entered_late;
        log_info("slot %u entered this world after %u ms in its lobby: the bank started over at "
                 "its connection; its appearance, the movers, the push blocks, the campaign bank "
                 "and the blackboard are told again now", (unsigned)slot,
                 (unsigned)(mp_wallclock_ms() - entry->sighted_ms));
    } else {
        log_info("slot %u entered world %u: its appearance, the movers, the push blocks, the "
                 "campaign bank and the blackboard are told again", (unsigned)slot,
                 (unsigned)entry->world);
    }
    entry->ever_entered = true;
}

/* A new connection in front of a bank, inside a substep: what belongs to the connection. The
 * bank's history starts over, which takes the pose, hero and placement with it; the peer's world
 * baseline and the enemies it holds are forgotten; the puppet of its bank starts over. What the
 * newcomer is owed waits for its entry. */
static void greet_connection(size_t peer, size_t bank, uint64_t now)
{
    mp_bridge_far_start_over(bank, now);
    if (now != 0u) {
        mp_bridge_world_forget_peer(peer);
        mp_puppet_reset(bank);
    }
}

/* Every peer's ring, oldest first, so a burst after a stall lands every sample and the last
 * acknowledgement read is the newest; a payload that was refused is skipped and the drain goes
 * on, because the payloads behind it are newer state and would otherwise wait a substep in the
 * ring. Storing only: a far body is sampled out of its history at the render moment, never at
 * arrival.
 *
 * EVERY peer, into the bank of its slot. This read peer 0 alone, in slot 1, so a second client's
 * every state stayed in its ring for good and its body never moved on the host. A bank whose
 * connection changed starts its history over before it takes anything, and only that bank; a
 * departed peer's is emptied and its ring left alone. Outside a substep a changed connection
 * waits for the next one, because greeting a newcomer resets the puppet, and that is substep
 * work; so does the greeting of an entry. */
static uint32_t drain_states(mp_bridge_drain_t *drain, bool in_substep)
{
    mp_snapshot_t          decoded;
    mp_payload_ack_t       acked;
    uint32_t               taken = 0;
    mp_bridge_world_read_t read;
    size_t                 peer;

    for (peer = 0; peer < MP_SESSION_MAX_PEERS; ++peer) {
        const mp_peer_t *seat = mp_session_peer(drain->host, peer);
        uint8_t          slot = mp_session_slot_of_peer(peer);
        size_t           bank = slot;   /* a listen host's peers and far banks share numbers */
        uint64_t         now  = (seat != NULL && seat->state == MP_PEER_CONNECTED)
                                    ? seat->connection_id
                                    : 0u;

        if (!mp_bank_index_ok(bank)) {
            break;   /* a listen host seats no peer past its far banks */
        }
        sight(drain, peer, slot, now);
        if (now != mp_bridge_far_connection(bank)) {
            if (!in_substep) {
                continue;
            }
            greet_connection(peer, bank, now);
        }
        if (now == 0u) {
            continue;
        }
        while ((read = mp_bridge_world_receive_full(drain->host, peer, slot, &decoded,
                                                    &acked)) != MP_BRIDGE_WORLD_NOTHING) {
            ++taken;
            if (read != MP_BRIDGE_WORLD_DECODED) {
                continue;   /* counted by the world module */
            }
            mp_bridge_world_acknowledged(peer, &acked);
            mp_interp_receive(mp_bridge_far_interp(bank), &decoded.body[slot], decoded.tick);
            mp_bridge_far_note_state(bank);
            note_state(peer, decoded.body[slot].world);
            ++drain->states_in;
        }
        if (in_substep) {
            greet_entry(peer, bank, slot);
        }
    }
    return taken;
}

uint32_t mp_bridge_drain_client_state(mp_bridge_drain_t *drain)
{
    return drain_states(drain, true);
}

uint32_t mp_bridge_drain_host_between_substeps(mp_bridge_drain_t *drain)
{
    return drain_states(drain, false);
}

void mp_bridge_drain_host_joins(mp_bridge_drain_joins_t *out)
{
    if (out != NULL) {
        *out = joins;
    }
}

void mp_bridge_drain_host_report(void)
{
    mp_bridge_drain_joins_t counts;

    mp_bridge_drain_host_joins(&counts);
    log_info("  the late joins (host): %u joined during a level, %u entered, %u left before "
             "entering; greeted at entry %u, at connection while a level ran %u (must be 0)",
             (unsigned)counts.joined_running, (unsigned)counts.entered_late,
             (unsigned)counts.left_before_entry, (unsigned)counts.greeted_at_entry,
             (unsigned)counts.greeted_unentered);
}
