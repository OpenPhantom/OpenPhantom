/* mp_bridge_drain.c: everything the bridge takes out of a session.
 *
 * Split from mp_bridge.c on 2026-09-05, at the seam that file had named for itself. The reason
 * it had to be this seam and not another is in the header: the payload drains touch no engine
 * and the two calls below them do, and the drain between substeps needed that line drawn where a
 * compiler and a reader can both see it.
 */
/* SIZE NOTE: past 600 lines since the far world's own clock moved in here, which is where a
 * world actually arrives and therefore the only place that can measure one standing still. The
 * first of the two reading halves it named, the host's read of every client's own state, left as
 * mp_bridge_drain_host.c when the greeting at a client's entry was added. The next seam is the
 * client's read of the host's world, take_world with the two clocks beside it, about 180 lines
 * that share only the ring with the reliable notes.
 */
#include "mp_bridge_drain.h"

#include "mp_bridge_drain_host.h"
#include "mp_lobby_note_rule.h"

#include "mp_actions.h"
#include "mp_bank.h"
#include "mp_blade_draw.h"
#include "mp_bridge_appearance.h"
#include "mp_bridge_lobby.h"
#include "mp_bridge_far.h"
#include "mp_bridge_relay.h"
#include "mp_bridge_roster.h"
#include "mp_bridge_seats.h"
#include "mp_body.h"
#include "mp_bridge_world.h"
#include "mp_cadence.h"
#include "mp_cells.h"
#include "mp_chat.h"
#include "mp_chat_rule.h"
#include "mp_crate.h"
#include "mp_enemy_relay.h"
#include "mp_npc_copies_bridge.h"
#include "mp_npc_shot_relay.h"
#include "mp_events.h"
#include "mp_host_settings.h"
#include "mp_pickup_relay.h"
#include "mp_player_sound_play.h"
#include "mp_dialog_relay.h"
#include "mp_quest_relay.h"
#include "mp_puppet.h"
#include "mp_scratch_wire.h"
#include "mp_hit_relay.h"
#include "mp_interp.h"
#include "mp_level_state.h"
#include "mp_payload_prefix.h"
#include "mp_scene_client.h"
#include "mp_snapshot.h"
#include "mp_stopwatch.h"
#include "mp_trust.h"
#include "mp_world.h"

#include "common/logging.h"
#include "mp_wallclock.h"
#include "common/memory.h"

#include <string.h>

/* A mover event is due on the clock of the far bank its player is shown in. */
_Static_assert(MP_WORLD_CLOCKS == MP_BANK_FAR_MAX + 1u, "one mover clock for every far bank");

/* Module state because a process holds one bridge, and neither of the two readers below is ever
 * handed a drain. */
static uint32_t                       substeps;
static mp_bridge_drain_note_fn_t      note_taker;
static mp_bridge_drain_peer_note_fn_t peer_note_taker;

/* The first far bank's pose, which on a client is the host's: the one far player the readers of
 * this pair, the arrival and the conversation relay, have always meant. */
bool mp_bridge_drain_far_pose(mp_bridge_far_pose_t *out)
{
    return mp_bridge_far_pose(MP_BRIDGE_DRAIN_FAR_BANK, MP_FAR_READER_ARRIVAL, out);
}

bool mp_bridge_drain_far_place(float out[3])
{
    mp_bridge_far_pose_t pose;

    if (out == NULL ||
        !mp_bridge_far_pose(MP_BRIDGE_DRAIN_FAR_BANK, MP_FAR_READER_OTHER, &pose)) {
        return false;
    }
    memcpy(out, pose.position, sizeof pose.position);
    return true;
}

bool mp_bridge_drain_host_elsewhere(void)
{
    return mp_bridge_far_in_another_world(MP_BRIDGE_DRAIN_FAR_BANK, MP_FAR_READER_ARRIVAL, NULL);
}

bool mp_bridge_drain_host_history_mark(uint32_t *newest_tick, uint32_t *starts)
{
    *starts = mp_bridge_far_starts(MP_BRIDGE_DRAIN_FAR_BANK);
    return mp_interp_newest(mp_bridge_far_interp(MP_BRIDGE_DRAIN_FAR_BANK), NULL, newest_tick);
}

uint32_t mp_bridge_drain_substeps(void)
{
    return substeps;
}

void mp_bridge_drain_set_note_taker(mp_bridge_drain_note_fn_t taker)
{
    note_taker = taker;
}

void mp_bridge_drain_set_peer_note_taker(mp_bridge_drain_peer_note_fn_t taker)
{
    peer_note_taker = taker;
}

/* This side's own world slot, mirrored out of the drain so that a reader which is never handed one
 * can still ask. It is written wherever the slots are published, which is the one place either of
 * them changes. */
static uint8_t my_slot = (uint8_t)MP_BRIDGE_LISTEN_HOST_CLIENT_SLOT;
static bool    slot_told;

/* And whether this side is a client of somebody else's world, mirrored for the same reason. It is
 * written from the MODE rather than derived from the slot: the loopback carries the client's slot
 * convention while being both ends of itself, so a reader that worked it out from the slot would
 * treat an in-process verification run as somebody's client. */
static bool client_of_a_host;

/* And whether the transport under all of this is a real socket rather than the in-process
 * loopback, mirrored here for the third time for the same reason and out of the same word.
 *
 * It exists because "is there a session" has been asked of two other things and both answered
 * wrongly. `config.net_role` is written by the ini at startup and by the lobby only when it binds
 * its socket, so a rule read off it was false for every session out of the menu; the game mode is
 * written for both games, so a rule read off THAT turned a co-operative campaign into two
 * unsynchronised worlds. The mode word here is settled in the same statement that decides what
 * the transport is, and it is settled before `installed` goes up. */
static bool over_a_socket;

/* The host's payloads as this client takes them off its ring, counted raw; the header says why
 * this is a second answer beside the still clock further down, and why it counts what that one
 * does not. The count lives as long as the process and only grows; the connection says which
 * session the newest payload belongs to. */
typedef struct host_payload_clock {
    const mp_bridge_drain_t *drain;        /* the one the bridge bound, which the readers ask */
    uint32_t                 count;
    uint32_t                 last_ms;      /* on the multiplayer's own wall clock */
    uint64_t                 connection;   /* the connection the newest one came on */
    bool                     seen;
} host_payload_clock_t;

static host_payload_clock_t host_payloads;

/* Which world slot is this side's own and which one each far body stands for, told to the two
 * modules that cannot work it out for themselves: the body module needs it to say who died, and
 * a hit or a death from the wire is judged against it. */
static void publish_slots(const mp_bridge_drain_t *drain)
{
    size_t  bank;
    uint8_t slot = 0;

    mp_body_set_bank_slot(0u, (uint8_t)drain->my_slot);
    for (bank = 1u; bank <= MP_BANK_FAR_MAX; ++bank) {
        if (mp_bridge_far_slot_of(bank, &slot)) {
            mp_body_set_bank_slot(bank, slot);
        }
    }
    my_slot = (uint8_t)drain->my_slot;
}

uint8_t mp_bridge_drain_my_slot(void)
{
    return my_slot;
}

bool mp_bridge_drain_slot_told(void)
{
    return slot_told;
}

bool mp_bridge_drain_is_client(void)
{
    return client_of_a_host;
}

bool mp_bridge_drain_is_udp(void)
{
    return over_a_socket;
}

void mp_bridge_drain_bind(mp_bridge_drain_t *drain, mp_bridge_mode_t mode, mp_session_t *host,
                          mp_session_t *client)
{
    memset(drain, 0, sizeof *drain);
    drain->mode   = mode;
    drain->host   = host;
    drain->client = client;

    /* A host is world slot 0 and tells each client its own; a client holds 1 until that note
     * arrives, and the body it shows is the host's own at 0. The role decides both, and giving
     * every role the client's convention was what made a host read a client's death as its own. */
    drain->my_slot  = mode == MP_BRIDGE_UDP_HOST ? MP_BRIDGE_HOST_SLOT
                                                 : MP_BRIDGE_LISTEN_HOST_CLIENT_SLOT;
    drain->far_slot = mode == MP_BRIDGE_UDP_HOST ? MP_BRIDGE_LISTEN_HOST_CLIENT_SLOT
                                                 : MP_BRIDGE_HOST_SLOT;
    client_of_a_host = mode == MP_BRIDGE_UDP_CLIENT;
    host_payloads.drain = drain;   /* and the count goes on, because it only ever grows */
    over_a_socket    = mode != MP_BRIDGE_LOOPBACK;
    slot_told        = false;
    mp_bridge_far_set_relaying(mode == MP_BRIDGE_UDP_HOST);   /* a listen host passes moments on */

    /* And which slot each far bank shows. A listen host seats every bank on the slot of its own
     * number, the slot its peer of that index is told; a client shows the one far player it
     * has in its first bank, and moves that seat when the world names a different one. */
    mp_bridge_far_unseat_all();
    if (mode == MP_BRIDGE_UDP_HOST) {
        size_t bank;

        for (bank = 1u; bank <= MP_BANK_FAR_MAX; ++bank) {
            mp_bridge_far_seat(bank, (uint8_t)bank);
        }
    } else {
        mp_bridge_far_seat(MP_BRIDGE_DRAIN_FAR_BANK, (uint8_t)drain->far_slot);
    }
    publish_slots(drain);
    mp_bridge_seats_reset();   /* nobody of this session has been told a seat yet */
}

/* How many worlds in a row a player may be missing from before its bank is given up: a second.
 * A host that relays a player sends that player in every world while it has a sample, so a gap
 * of a second is a player who left, and a gap of a packet or two is the wire. */
#define FAR_MISSING_WORLDS 32u

/* One decoded world into every far bank it concerns. Each foreign body goes into the bank its
 * slot is seated in, seated now if it was not; more players than far banks leaves the extra
 * ones unshown and counted. It used to take the lowest foreign body alone and break, so a
 * client of three others showed one of them.
 *
 * A seat whose player has been missing for a second is given up, which empties its bank and
 * so takes its body down. The host's seat never is: a world without the host's own body is a
 * host between two levels, not a host that left, and that departure the session says. */
static void take_world(mp_bridge_drain_t *drain, const mp_snapshot_t *world)
{
    static bool unshown_logged;
    bool        moved = false;
    size_t      slot;
    size_t      bank;
    uint8_t     shown = 0;

    for (slot = 0; slot < MP_SNAPSHOT_MAX_BODIES; ++slot) {
        if (slot == drain->my_slot || !mp_snapshot_has_body(world, slot)) {
            continue;
        }
        bank = mp_bridge_far_bank_of_slot((uint8_t)slot);
        if (bank == 0u) {
            bank = mp_bridge_far_seat_slot((uint8_t)slot, (uint8_t)drain->my_slot);
            if (bank == 0u) {
                ++drain->unshown;
                if (!unshown_logged) {
                    unshown_logged = true;
                    log_warning("the host's world carries world slot %u and every far bank "
                                "here already shows somebody: this build shows three other "
                                "players. Later ones are counted", (unsigned)slot);
                }
                continue;
            }
            moved = true;
        }
        mp_interp_receive(mp_bridge_far_interp(bank), &world->body[slot], world->tick);
        mp_bridge_far_note_state(bank);
        drain->far_missing[bank] = 0u;
    }
    for (bank = 1u; bank <= MP_BANK_FAR_MAX; ++bank) {
        if (!mp_bridge_far_slot_of(bank, &shown) || shown == (uint8_t)MP_BRIDGE_HOST_SLOT ||
            mp_snapshot_has_body(world, shown)) {
            continue;
        }
        if (++drain->far_missing[bank] >= FAR_MISSING_WORLDS) {
            drain->far_missing[bank] = 0u;
            mp_bridge_far_unseat(bank);
            moved = true;
        }
    }
    if (moved) {
        if (mp_bridge_far_slot_of(MP_BRIDGE_DRAIN_FAR_BANK, &shown)) {
            drain->far_slot = shown;
        }
        publish_slots(drain);
    }
}

/* The far world's own clock. Stamped where a world really arrives rather than where a packet
 * does: a host in its pause screen goes on sending keepalives from its idle pump, and its client
 * used to have no number at all that said the world behind them had stopped. */
static mp_bridge_drain_t *still_drain;

/* The connection a client stands on, named by the id both salts make, so a world from a session
 * that has ended, or from before a rejoin, is never read as a world of the one that is up now. 0
 * when this side holds no connection at all. */
static uint64_t current_connection(const mp_bridge_drain_t *drain)
{
    const mp_peer_t *host;

    if (drain->client == NULL || !mp_session_is_connected(drain->client)) {
        return 0u;
    }
    host = mp_session_peer(drain->client, 0);
    return host != NULL ? host->connection_id : 0u;
}

static void note_world_arrived(mp_bridge_drain_t *drain)
{
    uint32_t now = mp_wallclock_ms();
    uint64_t connection = current_connection(drain);

    still_drain = drain;
    if (drain->world_seen && drain->still_said && drain->world_connection == connection) {
        uint32_t stood = now - drain->last_world_ms;

        ++drain->stills;
        drain->still_total_ms += stood;
        if (stood > drain->still_worst_ms) {
            drain->still_worst_ms = stood;
        }
        log_info("the host's world moves again after %u ms", (unsigned)stood);
    }
    drain->still_said       = false;
    drain->world_seen       = true;
    drain->world_connection = connection;
    drain->last_world_ms    = now;
}

/* Said once per stretch, by whoever asks. The picture asks every frame, so the line is written
 * from here rather than from there. */
#define WORLD_STILL_MS 1000u

uint32_t mp_bridge_drain_world_still_ms(void)
{
    mp_bridge_drain_t *drain = still_drain;
    uint32_t           stood;

    /* Only a world of the connection that is up now can stand still. Without this the clock of a
     * session that had ended ran on, and its notice, which is asked before the one that says why
     * a session ended, stood on the screen in that one's place. */
    if (drain == NULL || drain->mode != MP_BRIDGE_UDP_CLIENT || !drain->world_seen ||
        current_connection(drain) == 0u || drain->world_connection != current_connection(drain)) {
        return 0u;
    }
    stood = mp_wallclock_ms() - drain->last_world_ms;
    if (stood >= WORLD_STILL_MS && !drain->still_said) {
        drain->still_said = true;
        log_info("the host's world has stood still for %u ms while its packets keep arriving: the "
                 "host is in a menu, loading or held, and the notice says so on screen",
                 (unsigned)stood);
    }
    return stood;
}

void mp_bridge_drain_still_counts(uint32_t *stretches, uint32_t *total_ms, uint32_t *worst_ms)
{
    mp_bridge_drain_t *drain = still_drain;

    if (stretches != NULL) {
        *stretches = drain != NULL ? drain->stills : 0u;
    }
    if (total_ms != NULL) {
        *total_ms = drain != NULL ? drain->still_total_ms : 0u;
    }
    if (worst_ms != NULL) {
        *worst_ms = drain != NULL ? drain->still_worst_ms : 0u;
    }
}

/* How long after its newest payload the host's world still counts as moving: sixteen of its
 * substeps, long enough for a packet or two lost on the wire, and short enough that a host which
 * has just left its level and stopped sending is not taken for one that plays. */
#define HOST_MOVING_MS 500u

static void note_host_payload(const mp_bridge_drain_t *drain)
{
    if (drain->mode != MP_BRIDGE_UDP_CLIENT) {
        return;
    }
    ++host_payloads.count;
    host_payloads.last_ms    = mp_wallclock_ms();
    host_payloads.connection = current_connection(drain);
    host_payloads.seen       = true;
}

uint32_t mp_bridge_drain_host_payloads(void)
{
    return host_payloads.count;
}

uint64_t mp_bridge_drain_host_connection(void)
{
    const mp_bridge_drain_t *drain = host_payloads.drain;

    return (drain != NULL && drain->mode == MP_BRIDGE_UDP_CLIENT) ? current_connection(drain)
                                                                  : 0u;
}

bool mp_bridge_drain_host_moving(void)
{
    uint64_t connection = mp_bridge_drain_host_connection();

    return host_payloads.seen && connection != 0u && host_payloads.connection == connection &&
           mp_wallclock_ms() - host_payloads.last_ms < HOST_MOVING_MS;
}

/* The client's read of the host's world, decoded and stored through the world module: the
 * loopback holds it against the host's stored one at the same tick, and the UDP client takes
 * every other player in it into the far bank that shows that player. */
static uint32_t take_host_worlds(mp_bridge_drain_t *drain)
{
    mp_snapshot_t          decoded;
    uint32_t               taken = 0;
    mp_bridge_world_read_t read;

    while ((read = mp_bridge_world_receive(drain->client, &decoded)) != MP_BRIDGE_WORLD_NOTHING) {
        ++taken;
        note_host_payload(drain);   /* before the refusal below, which it counts as well */
        if (read != MP_BRIDGE_WORLD_DECODED) {
            continue;   /* counted by the world module; the ring behind it is newer */
        }
        if (drain->mode == MP_BRIDGE_LOOPBACK) {
            mp_bridge_world_verify(&decoded);
            continue;
        }
        note_world_arrived(drain);
        take_world(drain, &decoded);
        mp_cadence_payload_taken();
    }
    return taken;
}

/* Inside a substep the drain also closes the substep's count of the host's worlds, the ones the
 * idle pump took since the last substep and the ones taken now: what this substep has to show. */
uint32_t mp_bridge_drain_host_world(mp_bridge_drain_t *drain)
{
    uint32_t taken = take_host_worlds(drain);

    if (drain->mode == MP_BRIDGE_UDP_CLIENT) {
        mp_cadence_substep_closed();
    }
    return taken;
}

/* The ring was filled from three places and emptied from one. receive_all runs from the frame
 * hook and the thirty millisecond timer besides the substep, while the drain ran only inside a
 * substep; a stretch with no substep in it, which is every level load and every menu, therefore
 * had a filling half and no emptying half. A ring of sixteen was measured overflowing a hundred
 * and eighteen times in one field run over a link that loses nothing.
 *
 * What is safe out here is exactly what the two drains above do and no more. The three things a
 * substep does around them stay where they are: the reliable notes perform shots and open doors,
 * the placement writes the far body into the engine, and the map's due work moves movers.
 *
 * The loopback is left out for two reasons of its own. Its client-to-host direction is the
 * command path, whose drain applies input to the engine, and both of its ends are this one
 * process: it cannot stall against itself and its ring cannot overrun. */
uint32_t mp_bridge_drain_between_substeps(mp_bridge_drain_t *drain)
{
    if (drain->mode == MP_BRIDGE_UDP_HOST) {
        return mp_bridge_drain_host_between_substeps(drain);
    }
    if (drain->mode == MP_BRIDGE_UDP_CLIENT) {
        return take_host_worlds(drain);
    }
    return 0u;
}

/* Which far bank a peer's word belongs to: on a host the bank of that peer's slot, on a client the
 * first, which shows the host. A reported hit is judged by it, and a mover event is due on that
 * bank's clock, because every moment reaches a client restamped on the host's. */
static size_t bank_of_sender(const mp_bridge_drain_t *drain, size_t peer_index)
{
    return drain->mode == MP_BRIDGE_UDP_HOST ? (size_t)mp_session_slot_of_peer(peer_index)
                                             : (size_t)MP_BRIDGE_DRAIN_FAR_BANK;
}

/* And the world slot of that peer: on a host the slot it was told, on a client the host's. */
static uint8_t slot_of_sender(const mp_bridge_drain_t *drain, size_t peer_index)
{
    return drain->mode == MP_BRIDGE_UDP_HOST ? mp_session_slot_of_peer(peer_index)
                                             : (uint8_t)MP_BRIDGE_HOST_SLOT;
}

/* Which far bank one of a player's moments belongs to. On a host, the bank of the peer that sent
 * it, whatever slot the moment names: a peer's word about its own slot settles nothing. On a
 * client, the bank that shows the slot the host wrote in when it passed the moment on; none for
 * this side's own slot, which nobody sends back, and none for a slot no bank here shows. Every
 * moment used to go to the first bank on a client, so against a dedicated server one body
 * performed every other player's shots. */
static size_t bank_of_moment(const mp_bridge_drain_t *drain, size_t peer_index,
                             const mp_event_t *event)
{
    if (drain->mode == MP_BRIDGE_UDP_HOST) {
        return bank_of_sender(drain, peer_index);
    }
    if (event->source_slot == (uint8_t)drain->my_slot) {
        return 0u;
    }
    return mp_bridge_far_bank_of_slot(event->source_slot);
}

/* The reliable channels carry a slot note (server to client), events and the map's once a second
 * digest, and the recognisers tell them apart by length and tag. Each event carries the sender's
 * tick and is performed when the render tick gets there; a mover event goes to the map rather
 * than to the puppet, because a door is not a body. On a host a player's moment goes on to the
 * other clients before anything here reads it (mp_bridge_relay.c). */
/* The connection a note came in on: a host's peer's, or the host's on a client. */
static uint64_t sender_connection(const mp_bridge_drain_t *drain, size_t peer_index)
{
    const mp_peer_t *peer;

    if (drain->mode != MP_BRIDGE_UDP_HOST) {
        return current_connection(drain);
    }
    peer = mp_session_peer(drain->host, peer_index);
    return (peer != NULL && peer->state == MP_PEER_CONNECTED) ? peer->connection_id : 0u;
}

static void receive_event(mp_bridge_drain_t *drain, size_t peer_index, const uint8_t *note,
                          size_t bytes)
{
    mp_event_t event;

    if (drain->mode == MP_BRIDGE_UDP_HOST) {
        if (!mp_trust_takes_from(note, bytes, slot_of_sender(drain, peer_index))) {
            ++drain->notes_refused;
            return;   /* only what a player says, about itself; nobody here or there hears it */
        }
        mp_bridge_relay_pass_on(drain->host, peer_index, drain->tick, note, bytes);
    }
    /* Until the host has told this client its slot, `my_slot` is the default 1, which is somebody's
     * real slot; the chat is handed a slot no line names instead, so no line counts as this side's
     * own before the slot byte has arrived. */
    if (drain->mode == MP_BRIDGE_UDP_CLIENT
            ? mp_chat_take_line(slot_told ? (uint8_t)drain->my_slot : (uint8_t)MP_CHAT_SLOTS,
                                note, bytes)
            : mp_chat_take_say(peer_index, slot_of_sender(drain, peer_index), note, bytes)) {
        return;   /* chat: a player's line the host answers, or the host's line for everybody */
    }
    if (mp_world_take_message(bank_of_sender(drain, peer_index), note, bytes)) {
        return;   /* a mover event or a digest: the map's, and never the puppet's */
    }
    if (mp_level_state_take(drain->mode == MP_BRIDGE_UDP_CLIENT, note, bytes)) {
        return;   /* what the host's scripts switched: kept, and applied from the next substep */
    }
    if (mp_scene_client_take(drain->mode == MP_BRIDGE_UDP_CLIENT, note, bytes)) {
        return;   /* the host's scene: kept, and followed in this substep's post-tick half */
    }
    if (mp_crate_take(slot_of_sender(drain, peer_index), note, bytes)) {
        return;   /* a push block: a client's wish, or the host's note or fall */
    }
    if (mp_scratch_wire_take_message(note, bytes)) {
        return;   /* the campaign, which belongs to the level and so to the host */
    }
    if (mp_hit_relay_take_message(bank_of_sender(drain, peer_index), note, bytes)) {
        return;   /* somebody hit something the host owns, and the host performs it */
    }
    if (mp_enemy_relay_take_message(note, bytes) || mp_npc_shot_relay_take(note, bytes)) {
        return;   /* an actor the host removed, or a bolt one of its actors fired */
    }
    if (mp_pickup_relay_take_message(bank_of_sender(drain, peer_index),
                                     slot_of_sender(drain, peer_index), note, bytes)) {
        return;   /* a claim on a pickup, or the grant for one */
    }
    if (mp_npc_copies_bridge_take(slot_of_sender(drain, peer_index),
                                  sender_connection(drain, peer_index), note, bytes)) {
        return;   /* a wish for an NPC copy, or the host's word about one */
    }
    if (mp_quest_relay_take_message(note, bytes)) {
        return;   /* the story every player shares, or a client's claim on one bit of it */
    }
    if (mp_dialog_relay_take_message(note, bytes)) {
        return;   /* a line the host was told, said again here out of this side's own book */
    }
    if (mp_bridge_roster_take(note, bytes)) {
        mp_bridge_appearance_take_roster();
        return;   /* who is here, from the authority, and what each of them wears */
    }
    if (mp_bridge_roster_take_lobby(peer_index, note, bytes)) {
        return;   /* a peer's team, ready and hero; the host puts them in its next roster */
    }
    if (mp_host_settings_take(drain->mode == MP_BRIDGE_UDP_CLIENT, note, bytes)) {
        return;   /* the host's world settings, which it sends in front of every setup */
    }
    if (mp_bridge_lobby_take_setup(note, bytes)) {
        return;   /* what the host has chosen, and whether the moment has come to load it */
    }
    if (mp_bridge_lobby_take_content(peer_index, note, bytes)) {
        return;   /* the far side's content fingerprint, now that it has a level */
    }
    if (peer_note_taker != NULL && peer_note_taker(peer_index, note, bytes)) {
        return;   /* the host's savegame, asked for by this peer or handed to this side */
    }
    if (note_taker != NULL && note_taker(note, bytes)) {
        return;   /* the round's repeated table, from the side that keeps the score */
    }
    if (mp_event_decode(note, bytes, &event)) {
        size_t bank;

        /* An appearance is not a puppet event. The puppet performs moments inside a bank window,
         * and this one takes a body down and builds another out of a different actor, which may
         * only happen with no window open. So it is answered here and never queued. */
        if (event.kind == MP_EVENT_SKIN) {
            mp_bridge_appearance_take_event((uint8_t)drain->my_slot, peer_index, &event);
            return;
        }
        bank = bank_of_moment(drain, peer_index, &event);
        if (bank == 0u) {
            ++drain->events_unplaced;
            mp_player_sound_unplaced(&event);
            return;
        }
        mp_puppet_queue_event(bank, &event);
        mp_bridge_far_note_moment(bank);
        ++drain->events_in;
    }
}

/* What a client's lobby dropped of the notes a running level sends, by class, and what it took. */
static uint32_t lobby_dropped[MP_LOBBY_NOTE_CLASSES];
static uint32_t lobby_taken;
static bool     lobby_drop_said;

/* Whether a client in its lobby takes this note. One that is not taken is counted and goes no
 * further: a level's note in a lobby with no level either waits and fires into the next level
 * loaded, or names an actor that is not there. */
static bool lobby_takes(const uint8_t *note, size_t bytes)
{
    mp_lobby_note_class_t note_class = mp_lobby_note_class(note, bytes);

    if (mp_lobby_note_taken_in_lobby(note_class)) {
        ++lobby_taken;
        return true;
    }
    ++lobby_dropped[note_class];
    if (!lobby_drop_said) {
        lobby_drop_said = true;
        log_info("the lobby drops what the host's running level says, because this side has no "
                 "level yet: the first was tag 0x%02X; the report counts them",
                 (unsigned)(bytes != 0u ? note[0] : 0u));
    }
    return false;
}

static void drain_notes(mp_bridge_drain_t *drain, bool lobby)
{
    mp_session_t *session = drain->mode == MP_BRIDGE_UDP_CLIENT ? drain->client : drain->host;
    uint8_t       note[MP_CHANNEL_MESSAGE_BYTES];
    size_t        bytes = 0;
    /* A client has exactly one peer, which is the host. A host has as many as it admitted, and
     * this used to read only the first of them: everything the second and every later client
     * said on the reliable channel stayed in its queue for good. That is not a starvation that
     * clears itself, it is a queue nobody empties, and the visible half of it was that team,
     * ready and hero of every client past the first never reached anybody's roster. */
    size_t        peers = drain->mode == MP_BRIDGE_UDP_CLIENT ? 1u : (size_t)MP_SESSION_MAX_PEERS;
    size_t        peer;
    char          worn[MP_EVENT_ASSET_MAX];
    uint8_t       worn_kind = MP_SKIN_CHARACTER;

    /* The round's clock. The bridge calls this once per substep from its post-tick half, and its
     * own substep counter cannot be read from out here. */
    ++substeps;

    /* This side's own appearance first, so a change caught in this substep rides the same packet
     * as the state of this substep. It goes out twice over: once as an event, which is what a
     * peer that is already here acts on, and once into the table the authority repeats, which is
     * what a peer that arrives later reads. An edge alone loses everybody who was not there. */
    if (mp_actions_note_appearance((uint8_t)drain->my_slot, worn, sizeof worn, &worn_kind)) {
        mp_bridge_appearance_note_own(worn, worn_kind);
    }

    /* A listen host tells each peer its slot before reading what the peer said. A dedicated
     * server always told its clients; a listen host told nobody, so every one of its clients
     * held slot 1 and two of them held it together. */
    if (drain->mode == MP_BRIDGE_UDP_HOST) {
        (void)mp_bridge_seats_update(drain->host);
    }

    for (peer = 0; peer < peers; ++peer) {
        /* Any message up to the channel's maximum is taken, so none can jam the channel. */
        while (mp_session_read_reliable(session, peer, note, sizeof note, &bytes)) {
            if (drain->mode == MP_BRIDGE_UDP_CLIENT && bytes == 1u && note[0] > 0u &&
                note[0] < MP_SNAPSHOT_MAX_BODIES) {
                slot_told = true;
                if (drain->my_slot != note[0]) {
                    drain->my_slot = note[0];
                    /* Published rather than only stored: everything that judges a message by the
                     * slot it names reads the copy, and a stale copy makes this machine answer
                     * for somebody else's death. */
                    publish_slots(drain);
                    log_info("the server assigned this player world slot %u",
                             (unsigned)drain->my_slot);
                }
            } else if (!lobby || lobby_takes(note, bytes)) {
                receive_event(drain, peer, note, bytes);
            }
        }
    }
}

void mp_bridge_drain_reliable_notes(mp_bridge_drain_t *drain)
{
    drain_notes(drain, false);
}

/* A host's lobby takes every note, as a substep would: what its clients say in a lobby is the
 * lobby's. A client's takes only the lobby's own, because its host may already be in a level. */
void mp_bridge_drain_lobby_notes(mp_bridge_drain_t *drain)
{
    drain_notes(drain, drain->mode == MP_BRIDGE_UDP_CLIENT);
}

uint32_t mp_bridge_drain_lobby_counts(uint32_t *taken)
{
    uint32_t dropped = 0u;
    size_t   i;

    for (i = 0; i < MP_LOBBY_NOTE_CLASSES; ++i) {
        dropped += lobby_dropped[i];
    }
    if (taken != NULL) {
        *taken = lobby_taken;
    }
    return dropped;
}

void mp_bridge_drain_lobby_report(void)
{
    uint32_t dropped = mp_bridge_drain_lobby_counts(NULL);

    log_info("  the lobby drained %u note(s) of a running level with no level here: dropped "
             "(movers %u, removals %u, bolts %u, level state %u, scenes %u, other %u); %u lobby "
             "note(s) taken", (unsigned)dropped, (unsigned)lobby_dropped[MP_LOBBY_NOTE_MOVER],
             (unsigned)lobby_dropped[MP_LOBBY_NOTE_REMOVAL],
             (unsigned)lobby_dropped[MP_LOBBY_NOTE_BOLT],
             (unsigned)lobby_dropped[MP_LOBBY_NOTE_LEVEL_STATE],
             (unsigned)lobby_dropped[MP_LOBBY_NOTE_SCENE],
             (unsigned)(lobby_dropped[MP_LOBBY_NOTE_LEVEL_OTHER] +
                        lobby_dropped[MP_LOBBY_NOTE_NEVER_TO_CLIENT] +
                        lobby_dropped[MP_LOBBY_NOTE_UNKNOWN]),
             (unsigned)lobby_taken);
}

/* The timeline advances once here, after this substep's arrivals; the pose is resolved every
 * substep, whether or not a packet arrived, because the render moment lies a lag behind the
 * newest sample and the history covers a lost one. Once a pose has been resolved the window
 * runs every substep with the newest sample even when none resolves (the warm up after a rejoin,
 * or an underrun after a stall), so the commit rolls the draw interpolation's pair forward and
 * the events that are due are performed; the puppet then stands on its last sample, which is
 * the interpolator's refusal to extrapolate.
 *
 * A pose of another world is kept for the readers to refuse and is not given to the puppet, which
 * goes on showing the last sample of this world it had; the clocks still take its render tick,
 * because the moments due on them run on the far side's substeps whatever world it stands in. */
static void place_bank(mp_bridge_drain_t *drain, size_t bank)
{
    mp_interp_t     *interp = mp_bridge_far_interp(bank);
    mp_interp_pose_t pose;

    (void)mp_interp_advance(interp);
    if (mp_interp_resolve(interp, &pose)) {
        mp_puppet_sample_t   sample;
        mp_bridge_far_pose_t kept;
        uint8_t              slot = 0;

        /* The same pose, kept for the readers that are not the puppet. The heading is the yaw the
         * interpolator blended, which is the hero block's own field and therefore the unit the
         * engine's re-entry takes. */
        memset(&kept, 0, sizeof kept);
        kept.slot = mp_bridge_far_slot_of(bank, &slot) ? slot : (uint8_t)drain->far_slot;
        memcpy(kept.position, pose.position, sizeof kept.position);
        kept.heading    = pose.yaw;
        kept.alive      = pose.state.alive;
        kept.dead       = pose.state.dead;
        kept.health     = pose.state.health;
        kept.world      = pose.state.world;
        kept.state_tick = pose.state_tick;
        if (mp_bridge_far_note_pose(bank, &kept)) {
            memset(&sample, 0, sizeof sample);
            sample.state_tick = pose.state_tick;
            memcpy(sample.position, pose.position, sizeof sample.position);
            sample.yaw   = pose.yaw;
            sample.state = pose.state;
            memcpy(sample.twist, pose.twist, sizeof sample.twist);
            sample.twist_count = pose.twist_count;
            mp_puppet_set_sample(bank, &sample);
        }
        mp_puppet_note_render_tick(bank, pose.tick);
        /* The map's clock for this player's doors. A host's players each count their own
         * substeps, so each bank is a clock of its own; on a client every far player arrives
         * on the host's worlds, so every bank tells the one clock the host's moments run on. */
        if (drain->mode == MP_BRIDGE_UDP_HOST) {
            mp_world_note_render_tick(bank, pose.tick);
        } else {
            mp_world_note_render_tick(MP_BRIDGE_DRAIN_FAR_BANK, pose.tick);
        }
    }
    if (mp_bridge_far_placed(bank) && mp_body_exists_at(bank)) {
        /* Nothing in the window may write the blade mesh the body draws: it is shared by every
         * body of that model. The witness holds the mesh against itself across the window. */
        mp_blade_draw_window_open(bank, MP_BLADE_WINDOW_PUPPET);
        mp_stopwatch_enter(MP_WATCH_FAR_WINDOW);
        if (mp_bank_run_at(bank, &mp_puppet_apply_window)) {
            ++drain->puppet_applies;
        }
        mp_stopwatch_leave(MP_WATCH_FAR_WINDOW);
        mp_blade_draw_window_close();
        mp_player_sound_after_window(bank);   /* the burst refuses a body inside its window */
    }
}

/* Every far bank, in turn. Each has its own history, its own puppet and its own window, and a
 * bank nobody sits in resolves nothing and opens no window. */
void mp_bridge_drain_place_puppet(mp_bridge_drain_t *drain)
{
    size_t bank;

    for (bank = 1u; bank <= MP_BANK_FAR_MAX; ++bank) {
        place_bank(drain, bank);
    }
}

