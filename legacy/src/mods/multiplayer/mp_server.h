/* mp_server.h: the dedicated server's whole logic, engine-free on purpose.
 *
 * In this feature's topology every player owns its body and sends its state, so a dedicated server
 * needs no simulation at all: it is the session host, a slot ledger, a state aggregator and a
 * relay. Each connected peer is told its world slot once, reliably; each incoming payload is that
 * peer's own body state; each reliable message a peer sends is passed on to every other connected
 * peer, a player's moment with the sender's slot and this server's tick written in; and once per
 * tick the server composes a world snapshot from every peer's latest state and broadcasts it. A
 * peer's own body comes back in its assigned slot, which is how a client knows to skip its echo.
 *
 * Slot 0 is deliberately left empty: it is the listen host's own player in the peer-to-peer
 * topology, so a client built for that topology works against this server unchanged, and the day
 * the server simulates a world of its own, slot 0 is where its bodies begin.
 *
 * The world leaves once per 31.25 ms tick however often the caller's loop runs. The clients'
 * ladder is thirty-two substeps a second and their histories and clocks are indexed by tick, so
 * a world per loop iteration would stamp several worlds with one tick and a client would blend
 * between identical positions. A relayed moment is restamped onto the same clock, because a
 * client sees the sender's body on this server's worlds; it used to keep the sender's own count,
 * which made it due at a time that had nothing to do with the body it belonged to.
 *
 * Nothing here names a socket, a clock or the engine: the transport is handed in, time is a
 * parameter, and the caller owns the loop. That is what lets one unit test run this server and
 * two clients over real localhost sockets in a single process with no game.
 */
#ifndef MULTIPLAYER_MP_SERVER_H
#define MULTIPLAYER_MP_SERVER_H

#include "mp_chat_rule.h"
#include "mp_match.h"
#include "mp_session.h"
#include "mp_snapshot.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct mp_server_peer {
    bool           slot_told;      /* the one reliable byte, sent once after the join */
    bool           have_state;
    uint64_t       connection_id;  /* whose ledger this is; a replacement in place changes it */
    mp_wire_body_t state;
    uint32_t       states_received;
} mp_server_peer_t;

typedef struct mp_server {
    mp_session_t     session;
    mp_server_peer_t peers[MP_SESSION_MAX_PEERS];
    bool             have_base;        /* the tick counter's zero, set at the first tick */
    uint32_t         base_ms;
    bool             have_world_tick;  /* a world was composed for last_world_tick already */
    uint32_t         last_world_tick;
    uint32_t         worlds_sent;
    uint32_t         payloads_refused;
    uint32_t         relayed;          /* reliable messages passed on, one count per recipient */
    uint32_t         relay_refused;    /* a recipient could not be held for and was sent away */
    uint32_t         setups_refused;   /* a client's setup note, which only the authority writes */
    uint32_t         notes_refused;    /* any other note only an authority says, or a death or an
                                        * appearance in another player's name */
    uint32_t         copy_wishes;      /* wishes for an NPC copy, which a server cannot build */
    mp_chat_host_t   chat;             /* the chat's bucket for every slot, and its counts; the
                                        * server answers a say as a listen host does, has no
                                        * player and so no name, and says nothing itself */
    uint32_t         last_roster_ms;   /* when the roster last went to everyone */
    uint32_t         rosters_sent;

    /* The match, and the two notes that carry it. Without these the server is a relay: nobody
     * ever authored a setup, so no client learned what to play, and nobody counted, so no round
     * could end. A server with `match_on` false is exactly the relay it used to be. */
    mp_match_t match;
    bool       match_on;
    uint32_t   last_setup_ms;
    uint32_t   last_board_ms;
    uint32_t   setups_sent;
    uint32_t   boards_sent;
    uint32_t   deaths_seen;
    uint32_t   team_requests;
} mp_server_t;

/* Binds the server logic to a transport. `seed` feeds the session's salt generator; the caller
 * hands in entropy in the field and a fixed value in a test. */
void mp_server_init(mp_server_t *server, const mp_transport_t *transport, uint32_t seed);

/* Turns this server into the authority for a deathmatch: it authors the setup note every client
 * learns the level and the rules from, hands out balanced teams, counts the deaths its players
 * report to each other, and publishes the table. Without this call the server relays and
 * aggregates and decides nothing, which is what it did before the match existed. */
void mp_server_host_match(mp_server_t *server, const mp_lobby_setup_t *setup);

/* One step at `now_ms`: receive, tell fresh peers their slot, relay their reliable messages to
 * everyone else, take their newest state, compose and broadcast the world if this step opened a
 * new tick, then service so that everything set in this step leaves in this step. The world rate
 * is the tick's whatever the loop's is; a faster loop only shortens how long a handshake round
 * trip or a relayed message waits, so call it faster than the tick. */
void mp_server_tick(mp_server_t *server, uint32_t now_ms);

/* The world slot a peer index is told: peer 0 is slot 1, because slot 0 belongs to a listen
 * host's own player and stays empty on a dedicated server. The rule is the session's, which a
 * listen host now tells its clients by as well. */
uint32_t mp_server_slot_of_peer(size_t peer_index);

size_t   mp_server_connected(const mp_server_t *server);

#endif /* MULTIPLAYER_MP_SERVER_H */
