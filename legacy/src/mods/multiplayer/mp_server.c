/* mp_server.c: session host, slot ledger, state aggregator, relay. No engine, no socket, no
 * clock of its own; see the header for why that is the whole point.
 */
#include "mp_server.h"

#include "mp_chat_rule.h"
#include "mp_chat_wire.h"
#include "mp_death.h"
#include "mp_events.h"
#include "mp_lobby.h"
#include "mp_npc_copy_wire.h"
#include "mp_roster.h"
#include "mp_server_log.h"
#include "mp_trust.h"

#include "mp_channel.h"
#include "mp_clock.h"
#include "mp_payload_prefix.h"
#include "mp_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* How often the two match notes are repeated. The setup is what a joiner learns the level and the
 * rules from and the board is what everybody reads the score off, so both go out on the same
 * once-a-second cadence the roster already uses. */
#define MATCH_REPEAT_MS 1000u

void mp_server_init(mp_server_t *server, const mp_transport_t *transport, uint32_t seed)
{
    memset(server, 0, sizeof *server);
    mp_session_init(&server->session, MP_SESSION_HOST, transport, seed);
}

uint32_t mp_server_slot_of_peer(size_t peer_index)
{
    return mp_session_slot_of_peer(peer_index);
}

size_t mp_server_connected(const mp_server_t *server)
{
    return mp_session_peer_count(&server->session);
}

static bool peer_is_connected(const mp_server_t *server, size_t index)
{
    const mp_peer_t *peer = mp_session_peer(&server->session, index);

    return peer != NULL && peer->state == MP_PEER_CONNECTED;
}

/* A seat that changes hands starts with a full chat bucket, so the newcomer does not inherit the
 * pace of the one who left. It also keeps a seat that rested for weeks, on a server that runs for
 * weeks, from being measured against a fill made before the clock wrapped. */
static void forget_the_seats_pace(mp_server_t *server, size_t i)
{
    uint32_t slot = mp_server_slot_of_peer(i);

    if (slot < MP_CHAT_SLOTS) {
        memset(&server->chat.pace[slot], 0, sizeof server->chat.pace[slot]);
    }
}

/* A peer that dropped and rejoined into the same index must be told its slot again and must not
 * inherit the ghost of the previous occupant's state. The connected flag is the session's; the
 * ledger resets on the edge where a slot stops being connected. A client that restarts from the
 * same address never crosses that edge: the session replaces it in place through the parallel
 * handshake and the slot stays connected throughout, so the ledger is also keyed on the
 * connection id, which is the one thing a replacement always changes. */
static void reset_departed(mp_server_t *server)
{
    size_t i;

    for (i = 0; i < MP_SESSION_MAX_PEERS; ++i) {
        const mp_peer_t *peer = mp_session_peer(&server->session, i);

        if (peer == NULL || peer->state != MP_PEER_CONNECTED ||
            peer->connection_id != server->peers[i].connection_id) {
            /* Somebody who WAS here is gone: a different connection id in the same slot is a
             * replacement, and no peer at all is a departure. Either way the one who was here
             * leaves the match and the table, so a slot that comes back is a new player. */
            if (server->peers[i].connection_id != 0u) {
                mp_server_log_leave((uint8_t)mp_server_slot_of_peer(i),
                                    mp_session_peer_name(&server->session, i),
                                    mp_session_peer_dropped_behind(&server->session, i)
                                        ? MP_SERVER_LEAVE_SENT_AWAY
                                        : MP_SERVER_LEAVE_GONE);
                if (server->match_on) {
                    mp_match_player_left(&server->match, (uint8_t)mp_server_slot_of_peer(i));
                }
            }
            server->peers[i].slot_told     = false;
            server->peers[i].have_state    = false;
            server->peers[i].connection_id = (peer != NULL) ? peer->connection_id : 0u;
            forget_the_seats_pace(server, i);
        }
    }
}

/* The one reliable byte a fresh peer gets: which world slot is yours. */
static void tell_the_slot(mp_server_t *server, size_t i)
{
    uint8_t slot = (uint8_t)mp_server_slot_of_peer(i);

    if (server->peers[i].slot_told) {
        return;
    }
    if (!mp_session_send_reliable(&server->session, i, &slot, sizeof slot)) {
        return;
    }
    server->peers[i].slot_told = true;
    /* The slot byte is the moment a peer becomes a PLAYER: it is the one thing it cannot act
     * without, and it is sent exactly once per connection, which makes it the honest place to
     * seat somebody. */
    mp_server_log_join(slot, mp_session_peer_name(&server->session, i));
    if (server->match_on) {
        mp_match_player_joined(&server->match, slot);
        mp_server_log_team(slot, mp_session_peer_name(&server->session, i),
                           mp_match_team_of(&server->match, slot), false);
    }
}

/* ==============================================================================================
 * The match: reading what passes through, and publishing what only this machine knows.
 * ============================================================================================ */

void mp_server_host_match(mp_server_t *server, const mp_lobby_setup_t *setup)
{
    size_t i;

    if (server == NULL || setup == NULL) {
        return;
    }
    mp_match_start(&server->match, setup);
    server->match_on = true;
    /* Anybody already here is seated now. A server told to host a match while people are on it
     * must not wait for them to reconnect before it knows they exist. */
    for (i = 0; i < MP_SESSION_MAX_PEERS; ++i) {
        if (peer_is_connected(server, i)) {
            mp_match_player_joined(&server->match, (uint8_t)mp_server_slot_of_peer(i));
        }
    }
}

/* One relayed message, read on the way past.
 *
 * The server does not intercept these and does not answer them: what goes on to the other peers
 * and what stops here is mp_trust_passes_between_players, whatever this makes of it. What it
 * takes is the two things only an authority can do anything with, a death to be counted and a
 * team to be granted, and everything else it merely writes down. */
static void read_in_passing(mp_server_t *server, size_t from, const uint8_t *note, size_t bytes)
{
    uint8_t         slot = (uint8_t)mp_server_slot_of_peer(from);
    mp_death_note_t death;
    mp_lobby_t      lobby;
    mp_event_t      pickup;

    /* No overlay here builds a copy, so the wish is not answered: it runs out at its asker, which
     * tells its own overlay it was not built. */
    if (mp_npc_copy_wish_is(note, bytes)) {
        ++server->copy_wishes;
        return;
    }
    if (mp_death_decode(note, bytes, &death)) {
        ++server->deaths_seen;
        mp_server_log_death(death.victim_slot, death.killer_slot, death.reason);
        if (server->match_on) {
            (void)mp_match_take_death(&server->match, &death);
        }
        return;
    }
    if (mp_lobby_decode(note, bytes, &lobby)) {
        ++server->team_requests;
        if (server->match_on && mp_match_request_team(&server->match, slot, lobby.team)) {
            mp_server_log_team(slot, mp_session_peer_name(&server->session, from),
                               mp_match_team_of(&server->match, slot), true);
        }
        return;
    }
    /* A pickup event is a fact about the world rather than about the match, so it is written
     * down and passed on. The placement comes out of the decoded message: it used to be read at
     * a fixed offset that was a byte of the sender's tick. */
    if (mp_event_decode(note, bytes, &pickup) && pickup.kind == MP_EVENT_PICKUP) {
        mp_server_log_pickup(slot, pickup.actor_index);
    }
}

/* The two notes only this machine can write. Both are repeated rather than sent once, because
 * both are what a player who joined a minute ago has to be told. */
static void send_the_match(mp_server_t *server, uint32_t now_ms)
{
    uint8_t note[MP_SCORE_BYTES > MP_LOBBY_SETUP_BYTES ? MP_SCORE_BYTES : MP_LOBBY_SETUP_BYTES];
    size_t  bytes;

    if (!server->match_on) {
        return;
    }
    if (server->last_setup_ms == 0u || now_ms - server->last_setup_ms >= MATCH_REPEAT_MS) {
        server->last_setup_ms = now_ms == 0u ? 1u : now_ms;
        bytes = mp_match_setup_note(&server->match, note, sizeof note);
        if (bytes != 0u && mp_session_broadcast_reliable(&server->session, note, bytes) != 0u) {
            ++server->setups_sent;
        }
    }
    if (server->last_board_ms == 0u || now_ms - server->last_board_ms >= MATCH_REPEAT_MS) {
        server->last_board_ms = now_ms == 0u ? 1u : now_ms;
        bytes = mp_match_score_note(&server->match, note, sizeof note);
        if (bytes != 0u && mp_session_broadcast_reliable(&server->session, note, bytes) != 0u) {
            ++server->boards_sent;
        }
    }
}

/* A player's line of chat is answered, not passed on: the listen host's rule decides who said it
 * and what that player is called, and the line it builds goes to every connected player, the one
 * who said it included. Nobody says a line twice, so a copy a player's channel cannot take now is
 * held for that player; only a copy for a player whose hold is full as well, who is sent away for
 * it, counts as unsent. Nothing of the text or the name goes into this server's log. */
static void answer_a_say(mp_server_t *server, size_t from, const uint8_t *say, size_t bytes,
                         uint32_t now_ms)
{
    uint8_t line[MP_CHAT_LINE_MAX_BYTES];
    size_t  line_bytes = 0;
    size_t  peers;

    if (mp_chat_rule_host_line(&server->chat, say, bytes, (uint8_t)mp_server_slot_of_peer(from),
                               mp_session_peer_name(&server->session, from), now_ms, line,
                               sizeof line, &line_bytes) != MP_CHAT_HOST_LINE) {
        return;
    }
    peers = mp_session_peer_count(&server->session);
    mp_chat_rule_host_sent(&server->chat, peers,
                           mp_session_broadcast_or_hold(&server->session, line, line_bytes));
}

/* This server's clock, in the ladder's ticks since its first step. Every world is stamped with it,
 * and so is every moment passed on, because a client sees every other player on it. */
static uint32_t server_tick(mp_server_t *server, uint32_t now_ms)
{
    if (!server->have_base) {
        server->have_base = true;
        server->base_ms   = now_ms;
    }
    return mp_clock_ms_to_ticks(now_ms - server->base_ms);
}

/* Reliable messages from a peer: what a player may say and says about itself is read, and what
 * another player acts on goes on to every other connected peer (mp_trust.h). A client used to have
 * everything it sent passed on, so it could write the roster, the score or a hit on another
 * player in this server's name, and a death in another player's. A player's moment is rewritten
 * on the way: it names the slot of the peer it came from, which only this server can vouch for,
 * and it carries this server's tick instead of the sender's, because every other client sees the
 * sender's body on this server's worlds and so on this server's clock. It used to go on
 * unchanged, and a client then dated another client's shot on that client's own count, a clock
 * that has nothing to do with the world it is shown in. Everything else that goes on goes as it
 * came. The buffer takes any message the channel can carry, because a message longer than the
 * reader's buffer is refused whole and stays at the head of the inbox, which stops every message
 * behind it for the life of the connection; an earlier drain loop used the payload buffer, 1168
 * bytes against a channel maximum of 1170, and nothing sends a message that size today, but the
 * margin was two bytes. A recipient whose channel is full has that copy held for it, behind
 * whatever it is owed already, and gets it as its channel empties; one whose hold is full as well
 * is sent away, and only that copy counts as refused. */
static void relay_reliable(mp_server_t *server, size_t from, uint32_t tick, uint32_t now_ms)
{
    uint8_t       message[MP_CHANNEL_MESSAGE_BYTES];
    size_t        bytes = 0;
    const uint8_t slot = (uint8_t)mp_server_slot_of_peer(from);

    while (mp_session_read_reliable(&server->session, from, message, sizeof message, &bytes)) {
        size_t to;

        /* The setup is the authority's word. A client's is not passed on: it could end the
         * session, change the level or start the round for everybody else, until this
         * server's own next repeat undid it a second later. */
        if (mp_lobby_is_setup(message, bytes)) {
            ++server->setups_refused;
            continue;
        }
        if (!mp_trust_takes_from(message, bytes, slot)) {
            ++server->notes_refused;
            continue;
        }
        if (mp_chat_is_say(message, bytes)) {
            answer_a_say(server, from, message, bytes, now_ms);
            continue;
        }
        /* Read before it is passed on, so that a message the channel later refuses to deliver to
         * somebody has still been counted by the authority that had to see it. */
        read_in_passing(server, from, message, bytes);
        if (!mp_trust_passes_between_players(message, bytes)) {
            continue;   /* a player's word to its host, which this server has now read */
        }
        (void)mp_event_restamp(message, bytes, slot, tick);

        for (to = 0; to < MP_SESSION_MAX_PEERS; ++to) {
            if (to == from || !peer_is_connected(server, to)) {
                continue;
            }
            if (mp_session_send_or_hold(&server->session, to, message, bytes)) {
                ++server->relayed;
            } else {
                ++server->relay_refused;
            }
        }
    }
}

/* The peer's payload is its own body in the slot this server told it, a full-encoded snapshot
 * behind the acknowledgement every client prefixes for a listen host; this server encodes no
 * deltas, so it reads the prefix only to be sure it is one and goes past it. A client sends in
 * slot 1 until the slot byte reaches it, so its first payloads after a join or a restart are
 * refused and counted, and land once it has been told. The first build read every body from
 * slot 1 by that client convention, which was right for peer index 0 alone: a client told slot 2
 * or 3 sent every state in that slot and was refused for the whole session. The ring is drained
 * so a burst after a stall leaves the newest state, not the oldest. Anything that does not decode
 * is counted and dropped; a lossy link heals itself. */
static void take_the_newest_state(mp_server_t *server, size_t i)
{
    uint8_t  payload[MP_SESSION_PAYLOAD_BYTES];
    size_t   bytes = 0;
    uint32_t slot = mp_server_slot_of_peer(i);

    while (mp_session_read_payload(&server->session, i, payload, sizeof payload, &bytes)) {
        uint32_t         baseline_tick = 0;
        mp_snapshot_t    decoded;
        mp_payload_ack_t ack;

        if (bytes > MP_PAYLOAD_ACK_BYTES && mp_payload_get_ack(payload, bytes, &ack) &&
            mp_snapshot_baseline_tick(payload + MP_PAYLOAD_ACK_BYTES,
                                      bytes - MP_PAYLOAD_ACK_BYTES, &baseline_tick) &&
            baseline_tick == 0u &&
            mp_snapshot_decode(payload + MP_PAYLOAD_ACK_BYTES, bytes - MP_PAYLOAD_ACK_BYTES, NULL,
                               &decoded) &&
            slot < MP_SNAPSHOT_MAX_BODIES && mp_snapshot_has_body(&decoded, slot)) {
            server->peers[i].state      = decoded.body[slot];
            server->peers[i].have_state = true;
            ++server->peers[i].states_received;
        } else {
            ++server->payloads_refused;
        }
    }
}

/* The world: every peer's newest state in its assigned slot, full encoded, to everyone, once per
 * tick. A peer sees its own body in its own slot, which is the echo its slot byte lets it skip.
 *
 * The tick is a real 32 Hz counter derived from the wall clock, not the millisecond count itself:
 * a client's clock and history are indexed by tick, so a millisecond stamp would advance
 * thirty-one times too fast and make delta baselines and the interpolation ring meaningless. And
 * one world per tick, not one per call: the loop runs faster than the ladder so that handshakes
 * and relays do not wait a tick, and a world on every call would stamp several with one tick. A
 * step that opens no new tick sends nothing; a loop that stalls past a tick skips it, which a
 * client treats like a lost packet, and never repeats one. Under the test's five millisecond
 * loop that is 32 worlds in a second where the old code sent 200, and no two consecutive worlds
 * a client decodes carry one tick. The console loop sleeps fifteen milliseconds, which under the
 * default timer resolution wakes after about sixteen and now and then after thirty one, so an
 * occasional skipped tick is expected there. */
static void send_the_world(mp_server_t *server, uint32_t now_ms)
{
    uint8_t       payload[MP_SESSION_PAYLOAD_BYTES];
    mp_snapshot_t world;
    size_t        bytes = 0;
    uint32_t      tick;
    size_t        i;

    tick = server_tick(server, now_ms);
    if (server->have_world_tick && tick == server->last_world_tick) {
        return;
    }
    server->have_world_tick = true;
    server->last_world_tick = tick;

    /* The match advances HERE and nowhere else, because this is the only place that knows a
     * substep has passed. Its clock is the same ladder the time limit and the respawn delay are
     * written in, so a server whose loop runs fast or slow still ends a round after the number of
     * seconds somebody typed. */
    if (server->match_on) {
        uint8_t before = mp_match_outcome(&server->match, NULL);
        uint8_t after;
        uint8_t winner = 0;

        mp_match_tick(&server->match);
        after = mp_match_outcome(&server->match, &winner);
        if (after != before) {
            mp_server_log_round(after, winner, server->match.setup.generation);
        }
    }

    mp_snapshot_clear(&world);
    world.tick = tick;
    for (i = 0; i < MP_SESSION_MAX_PEERS; ++i) {
        if (server->peers[i].have_state) {
            mp_snapshot_set_body(&world, mp_server_slot_of_peer(i), &server->peers[i].state);
        }
    }
    if (mp_server_log_wants(MP_SERVER_LOG_POSITION)) {
        for (i = 0; i < MP_SESSION_MAX_PEERS; ++i) {
            if (server->peers[i].have_state) {
                mp_server_log_position((uint8_t)mp_server_slot_of_peer(i),
                                       server->peers[i].state.position);
            }
        }
    }
    /* A world opens with the length of its enemy block, as a listen host's does, and this server
     * has no enemies: a length of nought. The client reads the length first, and a world sent
     * without one had the low half of its tick read as a length and was refused whole. */
    if (world.present_mask != 0u &&
        mp_payload_put_enemy_length(payload, sizeof payload, 0u) &&
        mp_snapshot_encode(&world, NULL, payload + MP_PAYLOAD_ENEMY_LENGTH_BYTES,
                           sizeof payload - MP_PAYLOAD_ENEMY_LENGTH_BYTES, &bytes)) {
        if (mp_session_broadcast_payload(&server->session, payload,
                                         MP_PAYLOAD_ENEMY_LENGTH_BYTES + bytes) > 0u) {
            ++server->worlds_sent;
        }
    }
}

/* Who is here, once a second, to everyone: each connected peer in the slot this server told it,
 * with the name it gave in its request and the round trip its channel measures. The server has
 * no line of its own, because it has no body. */
#define ROSTER_INTERVAL_MS 1000u

static void send_the_roster(mp_server_t *server, uint32_t now_ms)
{
    mp_roster_t table;
    uint8_t     note[MP_ROSTER_BYTES];
    size_t      i;
    size_t      bytes;

    if (server->last_roster_ms != 0u && now_ms - server->last_roster_ms < ROSTER_INTERVAL_MS) {
        return;
    }
    memset(&table, 0, sizeof table);
    for (i = 0; i < MP_SESSION_MAX_PEERS && table.count < MP_ROSTER_MAX_ENTRIES; ++i) {
        mp_roster_entry_t *e;
        uint32_t           rtt;

        if (!peer_is_connected(server, i)) {
            continue;
        }
        e = &table.entry[table.count++];
        e->slot  = (uint8_t)mp_server_slot_of_peer(i);
        e->ready = 1u;
        /* THE SIDE, which used to be left at zero by the memset above. Every player therefore
         * arrived on the same team, and a team deathmatch with everybody on one side is not one:
         * the damage rule refused every shot between players, and the table had one column. */
        e->team = server->match_on ? mp_match_team_of(&server->match, e->slot)
                                   : (uint8_t)MP_LOBBY_TEAM_NONE;
        rtt = mp_session_peer_rtt_ms(&server->session, i);
        e->rtt_ms = rtt > 65535u ? 65535u : (uint16_t)rtt;
        mp_roster_name_clean(mp_session_peer_name(&server->session, i), e->name);
        /* The round trip is measured here anyway, so the log gets it for free rather than
         * asking the session a second time. */
        mp_server_log_ping(e->slot, e->name, rtt);
    }
    server->last_roster_ms = now_ms == 0u ? 1u : now_ms;
    if (table.count == 0u) {
        return;
    }
    /* THE ENCODED LENGTH, not the buffer's. The note is variable length and the buffer is not,
     * and the receiver tests the length against the count the note declares, exactly. Quoting
     * `sizeof note` sent a table of two players that measured eight hundred and sixty six bytes
     * and was refused by everybody, without even being counted as torn. The listen host had the
     * same defect and it was found in the field; this one is the same line in the other host. */
    bytes = mp_roster_encode(&table, note, sizeof note);
    if (bytes == MP_ROSTER_BYTES_FOR(table.count) &&
        mp_session_broadcast_reliable(&server->session, note, bytes) != 0u) {
        ++server->rosters_sent;
    }
}

void mp_server_tick(mp_server_t *server, uint32_t now_ms)
{
    size_t   i;
    uint32_t tick;

    mp_session_receive(&server->session, now_ms);
    reset_departed(server);
    tick = server_tick(server, now_ms);

    for (i = 0; i < MP_SESSION_MAX_PEERS; ++i) {
        if (!peer_is_connected(server, i)) {
            continue;
        }
        tell_the_slot(server, i);
        relay_reliable(server, i, tick, now_ms);
        take_the_newest_state(server, i);
    }

    send_the_roster(server, now_ms);
    send_the_match(server, now_ms);
    send_the_world(server, now_ms);
    /* Service last, so the world set in this step and every message queued in it leave in this
     * step rather than riding the next one. The first version put the combined receive and
     * service at the top and the world at the bottom, so every world and every relayed message
     * rode the NEXT step's service: fifteen milliseconds of latency for nothing. The ledger reset
     * stands between the receive and the per peer work on purpose: a peer the last step's service
     * dropped is seen as gone here and cleared before anything is told or taken on its index, and
     * a rejoin cannot complete inside the receive, because the handshake needs a service between
     * the request and the response. */
    mp_session_service(&server->session, now_ms);
}
