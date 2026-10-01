/* mp_bridge_drain.h: what the bridge takes OUT of a session, and where it puts it.
 *
 * The seam mp_bridge.c named for itself: the two payload drains, the reliable notes and the
 * puppet placement read from the sessions and write into the interpolator, the puppet and the
 * map, and hold nothing of the bridge's own beyond their counters. They are gathered here with
 * the counters they move, so the bridge keeps the halves, the modes and the sockets and this
 * file keeps the reading.
 *
 * The split is also what the drain between substeps needed. Only two of the five calls below are
 * safe outside a substep, and stating which is a property of this file rather than a habit of
 * the caller: a payload drain decodes bytes and stores them, while the reliable notes perform
 * shots and open doors and the placement writes a body into the engine.
 */
#ifndef MULTIPLAYER_MP_BRIDGE_DRAIN_H
#define MULTIPLAYER_MP_BRIDGE_DRAIN_H

#include "mp_bank.h"
#include "mp_bridge_far.h"
#include "mp_interp.h"
#include "mp_session.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Which of the three shapes the bridge stands in. It lives here because the drains gate on it and
 * the two files have to mean the same thing by it. */
typedef enum mp_bridge_mode {
    MP_BRIDGE_LOOPBACK,
    MP_BRIDGE_UDP_HOST,
    MP_BRIDGE_UDP_CLIENT
} mp_bridge_mode_t;

/* Which world slot a client holds before its host has said: a listen host's first client's. The
 * host sits at 0 and tells every client its own slot in one reliable byte, as a dedicated server
 * does, so this is the answer only until that byte arrives. */
#define MP_BRIDGE_LISTEN_HOST_CLIENT_SLOT 1u

/* And which one the host itself takes. It used to be nowhere, because the only readers of a slot
 * were the client's, and the bind gave every role the client's convention. The moment a message
 * names a WORLD SLOT that stopped being harmless: a host holding 1 reads a client's report about
 * slot 1 as a report about itself and drops it as an echo. */
#define MP_BRIDGE_HOST_SLOT 0u

/* The first far bank. A client shows its host there, and a host the peer of each bank's own number
 * in that bank; a client seats every other foreign slot of the world by mp_bridge_far's rule. */
#define MP_BRIDGE_DRAIN_FAR_BANK 1u

/* What the drains work on and what they tally. The two sessions and the mode are the bridge's
 * and are set once; everything under them is written here and only read elsewhere, which is why
 * the bridge's report can quote them without the two files sharing a state header. */
typedef struct mp_bridge_drain {
    mp_bridge_mode_t mode;
    mp_session_t    *host;
    mp_session_t    *client;

    uint32_t my_slot;         /* this side's own echo in a snapshot; the host's note sets it */
    uint32_t far_slot;        /* the world slot a client's first far bank stands for */
    uint32_t far_missing[MP_BANK_FAR_MAX + 1u];   /* worlds in a row without that bank's player */
    uint32_t unshown;         /* foreign bodies a world carried and no far bank was left for */
    uint32_t states_in;       /* every client's own states that landed on the host */
    uint32_t events_in;       /* far moments taken off the reliable channel and queued */
    uint32_t events_unplaced; /* moments naming a slot no far bank here shows, or this side's */
    uint32_t notes_refused;   /* a client's note only a host says, or one in another's name */
    uint32_t puppet_applies;  /* bank windows a puppet was written through, every bank */

    /* This side's substep, which a listen host stamps every client's moment with when it passes
     * it on; the bridge writes it before anything in the substep reads it. */
    uint32_t tick;

    /* When a world last arrived from the far side, on the wall clock. A world arrives once per
     * substep of the sender, so the gap between two of them is the sender's own simulation
     * standing still: a pause screen, a load, the cheat menu, a dragged window. The connection
     * itself says nothing about that, because an idle pump goes on answering with keepalives, and
     * that is exactly why the two numbers are kept apart. */
    uint32_t last_world_ms;
    bool     world_seen;
    uint64_t world_connection; /* the connection that world arrived on */
    uint32_t stills;           /* stretches the far world stood still while packets arrived */
    uint32_t still_total_ms;
    uint32_t still_worst_ms;
    bool     still_said;
} mp_bridge_drain_t;

/* Ties the drains to the bridge's sessions; the far bodies' histories are mp_bridge_far's. Called
 * once the mode is decided; every counter starts at zero and `my_slot` at the listen-host
 * convention. */
void mp_bridge_drain_bind(mp_bridge_drain_t *drain, mp_bridge_mode_t mode, mp_session_t *host,
                          mp_session_t *client);

/* The host's read of every client's own state, each into the far bank of its slot, and the
 * client's read of the host's world. Both return how many payloads they took off the ring, which
 * is what tells a caller outside a substep that it did any work at all; the report prints that
 * count, and a zero there beside a nonzero overrun count means the drain is installed and not
 * reached. Refused payloads count as taken: they came off the ring and the world module counts
 * the refusal apart, and counting only the decoded ones would make a run of bad packets look like
 * an idle pump.
 *
 * The client state drain only decodes and stores, and greets a client whose connection is new or
 * who has just entered this host's world (mp_bridge_drain_host.h, where it lives). The host world
 * drain only decoded and stored too, until the enemy
 * block rode in front of the snapshot: applying that block parks actors that exist here, writes
 * their poses and starts their clips, and it does so from wherever this runs, the idle pump
 * included. What it never does is CREATE a body; the sync remembers what is missing and the
 * bridge creates it from the task half, where a substep is certainly running. Whether the
 * writes into existing bodies should move there as well is an open question and not a promise.
 *
 * The loopback's read of the host's world verifies the decoded snapshot against the one it built
 * in this same process; that is the in-process proof and it belongs to a substep. And the UDP
 * client's read closes the substep's count of the host's worlds (mp_cadence.h), which is why a
 * caller outside a substep takes mp_bridge_drain_between_substeps instead. */
uint32_t mp_bridge_drain_client_state(mp_bridge_drain_t *drain);
uint32_t mp_bridge_drain_host_world(mp_bridge_drain_t *drain);

/* The payload drain for a caller that is NOT inside a substep: the mode gate and the loopback
 * exclusion in one place, so the frame hook and the thread timer cannot get it wrong. Returns the
 * payloads taken. */
uint32_t mp_bridge_drain_between_substeps(mp_bridge_drain_t *drain);

/* The reliable channel, both directions. The slot note a host sends and every far moment behind
 * it, and before those, this side's own appearance onto the queue the bridge sends from, and on a
 * listen host the slot note to every peer that has not had one. Inside a substep only. A moment
 * taken here is queued for the puppet and a mover message is handed to the map, and both of those
 * act on the engine.
 *
 * The appearance is here rather than with the other senders because of what it has to carry. The
 * event names the WORLD SLOT it belongs to, and this file is the only one that holds it: the slot
 * is the drain's, learned from the host's note or taken from the role's own convention.
 * This is also the one call per substep that is guaranteed to be inside a substep with a peer. */
void mp_bridge_drain_reliable_notes(mp_bridge_drain_t *drain);

/* The same, for a lobby, which has no substep: the idle pump drives it while a lobby screen is
 * open. A host's lobby takes every note. A client's takes only a lobby's own (mp_lobby_note_rule),
 * because a client that joins a running session sits in its lobby while its host sends everything
 * its level says; the rest is dropped and counted, and the report says what it was. */
void mp_bridge_drain_lobby_notes(mp_bridge_drain_t *drain);
void mp_bridge_drain_lobby_report(void);

/* The notes a client's lobby dropped over the run; `taken` receives the lobby notes it took. */
uint32_t mp_bridge_drain_lobby_counts(uint32_t *taken);

/* How often the call above has run, which is the nearest thing to the host's own substep counter
 * that anything outside the bridge can see. The bridge counts substeps in a field of its own and
 * offers no way to read it, and this call is made from the bridge's post-tick half exactly once
 * per substep with a peer. It is therefore the clock a round is measured in: it counts the same
 * substeps the simulation runs, and it counts them for as long as there is somebody to play
 * against, which is as long as a round means anything. Not the wall clock: two machines have
 * two starting points for theirs, and the engine clamps the frame delta feeding its simulation,
 * so under load simulated time falls behind the wall clock and never ahead, and a round timed
 * against it would end at a different point in the simulation on every machine.
 *
 * It runs a second time per idle pump while a LOBBY is open, which is before any round exists, and
 * a round measures the difference from the count it started on. */
uint32_t mp_bridge_drain_substeps(void);

/* How long the far world has not moved, in milliseconds, for a client whose host is still
 * answering. 0 when this side is not a client, when no world has arrived yet, or when one arrived
 * in this millisecond. */
uint32_t mp_bridge_drain_world_still_ms(void);

/* The counters behind the line, for the report. */
void mp_bridge_drain_still_counts(uint32_t *stretches, uint32_t *total_ms, uint32_t *worst_ms);

/* Every payload of the host's that this client took off its ring, decoded or refused, counted for
 * the life of the process and never reset. With the connection it came on, it is how a movie on
 * this machine learns that the host's movie is over: the host sends payloads only from its
 * substeps, and a host in its movie runs none. 0 on a host and on the loopback.
 *
 * It is a second answer to "does the host's world move", beside the still clock above that the
 * notice on screen reads, and it counts differently on purpose. The still clock counts worlds
 * that decoded, because a world this side could not decode says nothing about where the host
 * stands. This counts every payload, the refused ones included, because the question here is
 * whether the host has left its movie, and a client whose own level is not open yet refuses
 * exactly the payloads that answer it. */
uint32_t mp_bridge_drain_host_payloads(void);

/* The connection a client stands on to its host, named by the id both salts make; 0 when there
 * is none, on a host and on the loopback. */
uint64_t mp_bridge_drain_host_connection(void);

/* Whether one of those payloads arrived in the last half second, on the connection that is up
 * now. */
bool mp_bridge_drain_host_moving(void);

/* Where the first far bank's player is, which on a client is the host, asked by the arrival. The
 * pose type and every bank's own are mp_bridge_far's; this pair keeps the readers that have always
 * meant the one far player. False means no pose of this world: none resolved yet, the player gone,
 * or the one kept is another world's. */
bool mp_bridge_drain_far_pose(mp_bridge_far_pose_t *out);

/* The same pose, position only, in the shape a module that knows nothing of poses takes: the
 * conversation relay plays the far player's reply at it. False when no pose of this world is
 * resolved. */
bool mp_bridge_drain_far_place(float out[3]);

/* Whether the first far bank's player stands in another world: the pose kept for the host is one
 * of the world before, or of one this side has not entered yet. The arrival tells that wait from a
 * host that has sent nothing at all. */
bool mp_bridge_drain_host_elsewhere(void);

/* Where the host's history stands at this moment: the newest tick it holds, if it holds one, and
 * how many times it began for a new player. The arrival notes it as a level begins, so that a pose
 * it takes afterwards is one sampled after that moment rather than one from before it.
 *
 * It replaces the forgetting of the pose that stood here. The pose is resolved out of the
 * history in every substep and came straight back, the level before's included, so forgetting
 * it guarded nothing; a mark on the history is what can tell an old sample from a new one. */
bool mp_bridge_drain_host_history_mark(uint32_t *newest_tick, uint32_t *starts);

/* Which world slot this machine holds. A listen host is 0 and tells each client its own from 1 up,
 * as a dedicated server does; whoever judges a message that names a slot has to ask rather than
 * assume, because a host holding 1 reads a client's report about slot 1 as one about itself. */
uint8_t mp_bridge_drain_my_slot(void);

/* Whether this machine is a client of somebody else's world. It is NOT the same question as
 * "is my slot not the host's": the loopback holds the client's slot convention while being both
 * ends of itself in one process, and a rule that acts on what a HOST did must not act in one. */
bool mp_bridge_drain_is_client(void);

/* Whether a client has been told its slot by its host since the transport went up. Before that it
 * holds the client's default, which a second client shares. */
bool mp_bridge_drain_slot_told(void);

/* Whether the transport under the session is a real socket, host or client, rather than the
 * in-process loopback. It is the honest form of "there are two machines here", and it is the one
 * to ask when a rule turns on that and on nothing else.
 *
 * NOT `config.net_role != 0` and NOT the game mode. The first is a configuration field the ini
 * writes at startup and the lobby writes minutes later, at the moment it binds its socket, so
 * eleven readers of it saw "no session" for every session started from the menu: such a session
 * sent no state at all, and its far body was ticked through the player pipeline instead of being
 * placed. The second is written for both games, and keying the enemy replication on it left a
 * co-operative campaign with two unsynchronised worlds for a day. This answers the transport, and
 * the transport is settled the moment there is one, whichever way it was armed. */
bool mp_bridge_drain_is_udp(void);

/* An arm on the reliable channel for a module the drain may not name. `taker` is offered every
 * note the drain does not recognise itself, before the event codec sees it, and answers whether it
 * was one of its own. One at a time; NULL takes it out again. It exists because the round, which
 * keeps the score, lives one layer up with the game mode and the rule set, and this file is
 * compiled into a unit test target whose source list does not carry that module: a direct call
 * would be an unresolved external at test link time. */
typedef bool (*mp_bridge_drain_note_fn_t)(const uint8_t *note, size_t bytes);
void mp_bridge_drain_set_note_taker(mp_bridge_drain_note_fn_t taker);

/* The same arm with the PEER the note came from, for a module that answers the sender rather
 * than the room: a host handing a client the savegame it asked for has to know which channel to
 * lay the chunks into. Asked before the taker above. One at a time; NULL takes it out again. */
typedef bool (*mp_bridge_drain_peer_note_fn_t)(size_t peer_index, const uint8_t *note,
                                               size_t bytes);
void mp_bridge_drain_set_peer_note_taker(mp_bridge_drain_peer_note_fn_t taker);

/* Every far body at the render moment, each into its puppet, and each puppet into its own bank's
 * window. Inside a substep only, and after the drain of this substep's arrivals: it writes the
 * engine. */
void mp_bridge_drain_place_puppet(mp_bridge_drain_t *drain);

#endif /* MULTIPLAYER_MP_BRIDGE_DRAIN_H */
