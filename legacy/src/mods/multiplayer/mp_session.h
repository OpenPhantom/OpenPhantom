/* mp_session.h: who is connected, and the handshake that lets them be.
 *
 * A session is one endpoint of a game: a host that accepts as many peers as its seats allow, at
 * most MP_SESSION_MAX_PEERS, or a client that joins one host. It owns the outer packet format, the
 * join handshake, and one reliable channel per connected peer. It drives a transport and takes time
 * as a parameter, so the whole thing, handshake included, is proven over the loopback in one
 * process with no socket.
 *
 * ================================ The handshake, and what it is for ============================
 *
 * A raw UDP port is a spoofing and amplification target. Two defences, both from the modern
 * consensus for direct-IP games, and both cheap enough to belong even in a friends' game:
 *
 *   Anti-spoofing by a salt exchange and a cookie. The client sends a request with a random
 *   client salt. The host replies with a challenge carrying that salt back, a random server salt
 *   and a cookie: a digest over the address, both salts and the time, under a secret only the
 *   host holds. The client answers with both salts and the cookie back. A spoofer who forged the
 *   source address never received the challenge, so it cannot produce the cookie, and the host
 *   keeps NOTHING for an address until it has: no slot, no timer, no channel. The two salts
 *   together become the connection id, which every later packet must carry, so a session cannot
 *   be hijacked without knowing both.
 *
 *   Anti-amplification by padding. The two packets a client sends, the request and the response,
 *   are padded to a fixed large size, the host answers neither unless it arrives at that size,
 *   and every packet the host sends in reply is smaller. So the port can never be made to answer
 *   a small forged packet with a large one.
 *
 * There are no half-open connections on a host, so a flood of requests holds no slot at all, and
 * a slot limit caps how many a host will carry at once. A response with a cookie the host never
 * made touches nothing, not even the idle stamp, and a denial carries the client's own salt back,
 * so neither a slot nor a joining client can be steered by someone who only knows an address. A
 * password never crosses the wire: the response carries a digest over both salts made with it,
 * which the host makes again with its own and compares.
 *
 * The request also names the protocol version and carries a statement of what this side plays
 * with, so two builds that would misread each other's packets, or play with other game data, are
 * refused before a slot is spent, and the refusal says why. The statement is bytes to the session:
 * what they mean and when two of them disagree is the feature's judge, handed in as a function.
 *
 * ================================ Leaving, and coming back =====================================
 *
 * A peer that quits sends a leave notice under the shared connection id, so the far side frees the
 * slot at once instead of waiting out the connected timeout. A client whose connection times out
 * gives its host up at once: a host that has said nothing for the whole connected timeout has gone,
 * since the thread timer pump is there to service a session through a level load (whether a load
 * dispatches it is what the line "the timer pump through a stall" measures). The rejoin window a
 * client opened after that cost a player whose host had crashed a further minute in a level with
 * nothing on screen. Coming back is the host's side of the matter: a client that connects again is
 * taken back in under a second, below. A first connect keeps asking for its own window, and a
 * denial ends that window: a refusal is an answer, not a loss.
 *
 * A fresh request from an address the host already holds CONNECTED starts a parallel handshake for
 * that peer: the challenge goes to the address, and only a response that echoes a good cookie for
 * the new salts, and the password's proof where the host asks for one, replaces the peer's id and
 * channel in place. The request itself refreshes nothing, so an address alone still cannot keep a
 * peer alive or knock it off; the cookie proves the sender received the challenge at that address,
 * which a forger elsewhere cannot. That is what lets a client that crashed and restarted, or
 * dropped the host during a long level load, come back inside a second instead of waiting out the
 * connected timeout. The host keeps nothing between the challenge and the response, so a forged
 * request in the middle of a rejoin changes nothing the legitimate response is checked against.
 * What is left is a flood of requests, which costs the host a digest each; a bandwidth limit per
 * address is the named remainder.
 *
 * ================================ Receiving and servicing ======================================
 *
 * Driving is two calls. Receive drains the transport and dispatches; service times out stale
 * peers, resends handshake packets, and sends the connected packets. A packet with a payload is
 * sent the moment service runs: the payload is set once per substep and must leave in that
 * substep, or a frame that services twice inside one substep would gate the second and the next
 * substep would overwrite it unsent. Only a packet with nothing to carry waits, as a keepalive
 * after a stretch of silence. Received payloads are kept in a short ring, oldest first, so two
 * packets arriving inside one receive lose nothing and an older packet arriving late never
 * replaces a newer one.
 *
 * SIZE NOTE: over 600 lines. It is the one public face of four source files (the handshake, the
 * connected peer, the bulk lane and the holds), and most of its length is the contract of each
 * call, which a caller has to read here because nothing else states it. The seam, if it grows
 * again, is the counters and their readers at the end, which no caller that drives a session needs.
 */
#ifndef MULTIPLAYER_MP_SESSION_H
#define MULTIPLAYER_MP_SESSION_H

#include "mp_budget_rule.h"
#include "mp_channel.h"
#include "mp_hold.h"
#include "mp_inbox.h"
#include "mp_session_meter.h"
#include "mp_transport.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* How many peers a host holds at once. Fifteen, so a session seats sixteen with the host in it: the
 * size a deathmatch is meant to reach. Every peer costs a reliable channel, a payload ring, a bulk
 * ring, an inbox and a hold of its own, which is the price of the number, about 263 kB each, so a
 * session is about 3.9 megabytes of static. That is nothing in a 32-bit address space and it is the
 * reason the number is written here once rather than assumed. A co-op session seats four; that
 * ceiling and its reason are mp_settings, not this one. */
#define MP_SESSION_MAX_PEERS 15u

/* The two client-sent packets are padded to this so a reply is never larger than the packet that
 * asked for it. The value is inside the channel's own packet budget.
 *
 * The packets, every one opening with the magic (4) and a type (1):
 *
 *     request    type 1  client salt (8), protocol (4), a zero word (4), mode (1), name (16),
 *                        the statement's length (2) and its bytes (up to 128), zeros to 1024
 *     challenge  type 2  client salt (8), server salt (8), cookie (16): 37 bytes
 *     response   type 3  client salt (8), server salt (8), cookie (16), proof (32), name (16),
 *                        zeros to 1024
 *     accept     type 4  connection id (8), the host's name (16): 29 bytes against 1024
 *     deny       type 5  client salt (8), reason (1), the judge's detail (up to 80); the
 *                        notice to a peer a host sends away is the same packet
 *     payload    type 6  connection id (8), then a whole channel packet
 *     bye        type 7  connection id (8), sent twice
 *     bulk       type 8  connection id (8), then one bulk note
 *
 * The salts and the ids are written as two u32 each, high half first, because the wire has no u64
 * of its own and that keeps the byte order the same as every other field. The connection id is the
 * two salts together, the client's XOR the server's. The zero word behind the protocol held a
 * content fingerprint up to wire 34 and stays, so everything behind it keeps its place. Three holes
 * a review of the host's dispatch as an attacker found and closed: a 13 byte request drew a 21 byte
 * challenge, because the padding was enforced on the sender alone, so the host now refuses both
 * client packets below the padded size with no reply, and the fuzz test sends the short request and
 * measures zero bytes back; a response that failed the host's check still refreshed the idle stamp,
 * so a spoofer who knew an address could keep a half-open slot alive for ever, and the stamp now
 * follows the check; and a denial carried nothing a client could check, so anyone who knew the
 * host's address could abort a join with one forged packet, and it now carries the client's salt
 * back. A build from before the protocol field padded zeros there, so it reads as protocol 0 and is
 * denied; a denial without the reason byte reads as "full", which every old denial was. */
#define MP_SESSION_REQUEST_BYTES 1024u

/* A player's name, as the request carries it and the accept answers it: sixteen bytes, NUL
 * terminated inside them, printable ASCII, the roster's rule. It rides the padding the request
 * was already sending, so the request keeps its size and the reply stays smaller than it. */
#define MP_SESSION_NAME_MAX 16u

/* A password, as the request carries it: sixteen bytes, NUL inside them, printable ASCII; an
 * empty one on the host means none is asked for. */
#define MP_SESSION_PASSWORD_MAX 16u

/* The statement a request carries behind the name, and the detail a refusal carries behind its
 * reason. Both ride room that was padding or is far below the request's size, so the request keeps
 * its size and every reply stays smaller than it; the session asserts both. */
#define MP_SESSION_STATEMENT_BYTES   128u
#define MP_SESSION_DENY_DETAIL_BYTES 80u

/* How long a peer may sit mid-handshake before its slot is reclaimed, and how long a connected
 * peer may go silent before it is dropped. Milliseconds. The connected timeout covers a far side
 * that sends nothing for a while: a load whose message loop does not run the timer pump, or a
 * machine that has gone; a parallel handshake brings a peer that did drop back in under a
 * second. */
#define MP_SESSION_HANDSHAKE_TIMEOUT_MS 5000u

/* The handshake's two keyed digests. The COOKIE is what a host hands a joining address instead of
 * a slot: a digest over the address, both salts and the time window under a secret only the host
 * holds, so a response that echoes it proves the challenge was received at that address, and
 * nothing is kept for an address that never answers. The PROOF is what a client gives instead of
 * its password: a digest over both salts under the password, good for this one handshake. A
 * cookie is good for its own window and the next, ten to twenty seconds. */
#define MP_SESSION_COOKIE_BYTES     16u
#define MP_SESSION_PROOF_BYTES      32u
#define MP_SESSION_SECRET_BYTES     32u
#define MP_SESSION_COOKIE_WINDOW_MS 10000u
#define MP_SESSION_CONNECTED_TIMEOUT_MS 30000u

/* When a host gives up on a peer it has been holding messages for (mp_hold). A peer whose hold
 * cannot take a message is sent away in the call that found it full; a peer whose oldest held
 * message has waited this long is sent away too, but only while something still arrives from it:
 * a peer that pumps and does not take what it is sent has stopped keeping up, while a peer that
 * sends nothing at all is loading or gone, and the connected timeout above decides that one. A
 * pause and a film both go on answering the channel, so neither holds anything this long while its
 * inbox has room; a peer that answers and no longer reads, its inbox full, is one that has stopped
 * keeping up like any other. */
#define MP_SESSION_HOLD_AGE_MS 10000u
#define MP_SESSION_PUMPING_MS   1000u

/* How long a host goes on answering the packets of a peer it sent away with the reason, and how
 * many such peers it remembers. The notice goes out twice, like a leave notice, and is not resent
 * otherwise; a client that missed both would sit out the connected timeout and then show a lost
 * host. Answering what still comes from it closes that. */
#define MP_SESSION_DROP_ANSWER_MS 5000u
#define MP_SESSION_DROPPED_RECENT 4u

/* How often a client repeats a handshake packet it has had no answer to. */
#define MP_SESSION_RESEND_MS 250u

/* How long a connected peer may go without a packet from this side before an empty keepalive is
 * sent. A packet carrying a payload is never held: it leaves on the service call that finds it. */
#define MP_SESSION_KEEPALIVE_MS 100u

/* How often a packet leaves for a peer whose channel holds unacknowledged reliable messages and
 * who is owed no payload: the substep cadence, so a lobby carries reliable traffic at the rate a
 * level does. It was put in for the savegame, which then rode this channel one chunk per packet
 * and took a quarter of a minute at the keepalive rate alone (a field run measured 51 of 78
 * chunks in about fifteen seconds, the client asking again twenty times and the host counting
 * 1308 refusals from a channel that stayed full because it did not drain); the savegame has its
 * own lane now, and the pace stays for the roster and the setup note, which a lobby is made of. */
#define MP_SESSION_RELIABLE_PACE_MS 31u

/* How long a FIRST connect keeps asking, over handshakes of MP_SESSION_HANDSHAKE_TIMEOUT_MS
 * each. It used to be nothing: one unanswered handshake ended the attempt for the rest of
 * the process, which makes joining before the host has opened its lobby unrecoverable, and
 * looks on screen exactly like a lost packet. A refusal still closes the window at once,
 * because a denial is an answer and retrying it would only ask the same question again. */
#define MP_SESSION_JOIN_WINDOW_MS 30000u

/* How many received payloads are held for a peer before the oldest is pushed out.
 *
 * Four was the first guess and the field measured what it cost: over 127.0.0.1, where NOTHING is
 * lost, one run threw away 451 of 8332 payloads and the other 404 of 8289, and both numbers match
 * the overrun counter exactly. The wire lost nothing; the ring did.
 *
 * The cause is that the engine's substep ladder runs one to four substeps in a frame, and every
 * substep sends its own packet. A sender that hitches delivers four payloads at once while the
 * receiver drains once per substep, so a ring of four overflows without anything being wrong on
 * either machine. Sixteen covers a burst of four against a receiver that is itself four behind,
 * with room over.
 *
 * Depth is cheap here and costs only memory, because the drain is a while loop that empties the
 * ring every substep rather than taking one payload per pass: a deeper ring cannot turn into
 * latency, only into fewer discards. */
#define MP_SESSION_PAYLOAD_RING 16u

/* How long a payload may wait in the ring before its discard is read as a stall rather than as a
 * burst.
 *
 * A running game drains this ring once per substep, and the engine's ladder puts one to four
 * substeps in every rendered frame, so a payload that waits is waiting tens of milliseconds. A
 * payload that waited longer than this was held while NO substep ran at all, which is a level
 * load, a menu or a freeze, and a discard there says nothing about the drain being too slow.
 * Splitting the discards on this line is the whole point of measuring the wait: the two cases ask
 * for opposite fixes, and the single overrun count cannot tell them apart. */
#define MP_SESSION_DRAIN_STALL_MS 150u

/* How long the oldest reliable message to a peer may wait for a seat before this side says so in
 * the log, once. A message is normally retired about a round trip and one throttle after it first
 * travels, a third of a second under the planning round trip of a quarter second; a message that
 * has waited a whole second for its FIRST seat is not queueing behind other riders, it is being
 * passed over by every packet, which in a level means the payload is taking the room it needs
 * and will go on taking it. That is a structural finding and not a busy moment, so it is said
 * once per session and the counters in the report carry the rest. */
#define MP_SESSION_SEAT_WAIT_WARN_MS 1000u

typedef enum mp_session_role {
    MP_SESSION_HOST,
    MP_SESSION_CLIENT
} mp_session_role_t;

/* Why a host refused a request. On the wire as one byte after the client's salt, and for the
 * judge's reasons a detail behind it. */
typedef enum mp_deny_reason {
    MP_DENY_NONE = 0,
    MP_DENY_FULL = 1,       /* no free slot */
    MP_DENY_PROTOCOL = 2,   /* the client's wire version is not this build's */
    MP_DENY_CONTENT = 3,    /* the game data differs: the judge says which file in the detail;
                             * sent without one when a deathmatch host sends a client away */
    MP_DENY_MODE = 4,       /* both sides named a game mode and they differ */
    MP_DENY_PASSWORD = 5,   /* the host asks for a password and the response's proof was not
                             * made with it */
    MP_DENY_BEHIND = 6,     /* not a refused join: the host sent a connected peer away because it
                             * could hold no more for it. A build that predates the reason reads
                             * it as a host it lost, which is the wrong sentence and no fault */
    MP_DENY_MODS = 7,       /* a mod both sides must run the same build of is missing on one
                             * side or another build there; the detail names it */
    MP_DENY_FOREIGN_DLL = 8 /* the joiner runs a DLL out of the mods folder that is not of this
                             * release and that the host's [multiplayer] AllowMods does not name,
                             * or states them unreadably or not all by name; the detail names it */
} mp_deny_reason_t;

/* The host's judge of a join: whether the statement a request carries plays with this side's own.
 * Called with both statements, only when both sides made one; answers MP_DENY_NONE to go on with
 * the handshake, or the reason to refuse with and up to `capacity` bytes of detail for the refusal.
 * The session knows nothing of what a statement means: that is the feature's, and a session that
 * was handed no judge, the dedicated server's, compares nothing. */
typedef uint8_t (*mp_session_judge_fn)(const uint8_t *own, size_t own_bytes, const uint8_t *far,
                                       size_t far_bytes, uint8_t *detail, size_t capacity,
                                       size_t *detail_bytes);

typedef enum mp_peer_state {
    MP_PEER_FREE,
    MP_PEER_CONNECTING,   /* client: request sent, waiting for the challenge */
    MP_PEER_CHALLENGED,   /* client: response sent; host: challenge sent, awaiting the response */
    MP_PEER_CONNECTED
} mp_peer_state_t;

/* The envelope this module writes in front of a channel packet: magic(4), type(1), connection
 * id(8). A channel packet is built into what a full packet has left after these, so the wrapped
 * whole never exceeds the budget.
 *
 * It lives in the header rather than in the .c because it is not private arithmetic: it decides
 * how large a reliable message can be, and a test that builds with the full budget instead of
 * this capacity proves something no caller ever sees. See MP_CHANNEL_ENVELOPE_BYTES, which is
 * the same thirteen bytes named from the other side and asserted equal to these. */
#define MP_SESSION_PAYLOAD_HEADER 13u
#define MP_SESSION_CHANNEL_CAP    (MP_CHANNEL_PACKET_BYTES - MP_SESSION_PAYLOAD_HEADER)

/* The unreliable payload carried once per outgoing packet. Snapshots and inputs ride here, not as
 * reliable messages, because the next one supersedes a lost one and reliability spent on them would
 * only make them late.
 *
 * 1168: the builder takes at most MP_CHANNEL_PAYLOAD_BYTES (1174), and six bytes are kept under
 * that so one more header byte somewhere does not silently make every full payload unbuildable.
 * The relation is asserted in mp_session.c; the thirty two is not a header of anything. */
#define MP_SESSION_PAYLOAD_BYTES (MP_CHANNEL_PACKET_BYTES - 32u)

typedef struct mp_session_payload {
    size_t   bytes;
    uint32_t held_ms;   /* when it entered the ring, so a discard can say how long it waited */
    uint8_t  data[MP_SESSION_PAYLOAD_BYTES];
} mp_session_payload_t;

/* ============================== The bulk lane, and why it exists ==============================
 *
 * A third kind of traffic, beside the reliable channel and the unreliable payload: a large block
 * that has to arrive whole but is nobody's moment of play, today the host's savegame, handed to
 * a joining client so it starts in the world the host is standing in rather than at the level's
 * first minute.
 *
 * It used to ride the reliable channel and that was the defect. Three properties of that channel
 * are right for control traffic and wrong for a block: it is ORDERED, so one message that cannot
 * be placed stops every message behind it; it reserves the payload first, so in a level, where a
 * snapshot fills most of a packet, a message of a kilobyte is offered a seat that is never large
 * enough; and it holds SIXTY FOUR unacknowledged messages, which a block of eighty pieces fills
 * on its own. In the field all three fired at once: seventy of seventy eight pieces arrived, the
 * eight that did not were unseatable for ever, and behind them the roster, the conversation and
 * the setup note stopped as well.
 *
 * So the block gets its own lane, which is what ENet's channels and QUIC's streams are for: a
 * datagram of its own, no ordering, no queue, no competition with a snapshot for room. Reliability
 * is not lost, it moves UP, the receiver answers with a bitmask of every piece it holds, and the
 * sender resends what the mask does not name. A lost piece is then a named piece rather than a
 * silence, and a transfer is finished when the RECEIVER says so.
 *
 * Nothing here knows what a piece means. This layer carries opaque bytes between two connected
 * peers and counts what it dropped. */
#define MP_SESSION_BULK_BYTES (MP_CHANNEL_PACKET_BYTES - MP_SESSION_PAYLOAD_HEADER)

/* How many arrived notes wait for a reader. A block's sender paces itself and the reader drains
 * every tick, so this only has to bridge one tick's worth; eight is several times that, and an
 * overrun costs one piece that the next mask asks for again. */
#define MP_SESSION_BULK_RING 8u

typedef struct mp_session_bulk {
    size_t  bytes;
    uint8_t data[MP_SESSION_BULK_BYTES];
} mp_session_bulk_t;

typedef struct mp_peer {
    mp_peer_state_t state;
    uint32_t        endpoint;
    uint64_t        client_salt;
    uint64_t        server_salt;
    uint64_t        connection_id;
    uint32_t        last_recv_ms;   /* when anything last arrived from this peer */
    uint32_t        last_send_ms;   /* when a packet was last sent to it */
    char            name[MP_SESSION_NAME_MAX];   /* what it called itself in its request, or
                                                  * what the host called itself in the accept */
    mp_channel_t    channel;        /* live only in MP_PEER_CONNECTED */
    mp_inbox_t      inbox;          /* its messages, taken off the channel as they arrive */
    mp_hold_t       hold;           /* what its channel had no room for, oldest first */
    bool            dropped_behind; /* host: the last connection here was sent away for it */
    uint32_t        empty_notes;    /* empty messages from it skipped: states shrunk in flight */

    /* The rate a second packet in a substep is held to, and this connection's packets. */
    mp_budget_bucket_t budget;
    uint32_t        packets_with_payload;
    uint32_t        packets_without_payload;   /* the overflow packets among them */
    uint32_t        overflow_packets;          /* a second packet with messages the first left */
    uint32_t        held_by_budget;            /* ... that the rate did not let go */
    mp_session_meter_t meter;                  /* what went to it, in bytes and packets */

    /* Client only: the host's cookie out of the challenge, echoed in the response. */
    uint8_t         cookie[MP_SESSION_COOKIE_BYTES];

    uint8_t         out_payload[MP_SESSION_PAYLOAD_BYTES];  /* rides the next packet, then clears */
    size_t          out_payload_bytes;

    /* Received payloads, oldest at in_head, newest last. Only a payload from a packet newer than
     * the newest held enters; the gate compares the packet's own sequence, from the channel. */
    mp_session_payload_t in_ring[MP_SESSION_PAYLOAD_RING];
    size_t          in_head;
    size_t          in_count;
    bool            in_have_sequence;
    uint16_t        in_newest_sequence;

    /* Arrived bulk notes, oldest at bulk_head. No sequence gate: a bulk note carries its own
     * identity above and an old one is worth as much as a new one. */
    mp_session_bulk_t bulk_ring[MP_SESSION_BULK_RING];
    size_t          bulk_head;
    size_t          bulk_count;
} mp_peer_t;

/* A peer a host sent away for falling behind, remembered for a few seconds so that its later
 * packets can be answered with the reason. The connection id is the key: only that peer has it. */
typedef struct mp_session_dropped {
    bool     used;
    uint64_t connection_id;
    uint64_t client_salt;
    uint32_t until_ms;
    uint32_t answered_ms;
} mp_session_dropped_t;

typedef struct mp_session {
    mp_session_role_t role;
    const mp_transport_t *transport;
    uint32_t          rng;
    uint32_t          now_ms;
    uint32_t          host_endpoint;   /* client only: where it is joining */
    bool              connect_wanted;  /* client only: keep trying until connected or given up */
    uint32_t          rejoin_until_ms; /* client only: a first connect's handshake restarts
                                        * until this time */
    uint8_t           statement[MP_SESSION_STATEMENT_BYTES];  /* what this side plays with: in
                                                            * a client's request, and a host's
                                                            * own for the judge */
    uint16_t          statement_bytes; /* 0 for none, which is never compared */
    mp_session_judge_fn judge;         /* host only: NULL compares nothing */
    char              name[MP_SESSION_NAME_MAX];   /* this side's own name; "Player" when unset */
    uint8_t           mode;            /* the game mode named in a request, 0 for none */
    uint8_t           capacity;        /* host only: peers it admits; 0 means every slot */
    char              password[MP_SESSION_PASSWORD_MAX];   /* what this side asks for (host) or
                                                          * offers (client); empty for none */
    mp_deny_reason_t  last_deny;       /* client only: why the last request was refused */
    uint8_t           deny_detail[MP_SESSION_DENY_DETAIL_BYTES];  /* client only: what that
                                                                 * refusal said behind its reason */
    uint8_t           deny_detail_bytes;
    uint8_t           secret[MP_SESSION_SECRET_BYTES];   /* host only: what a cookie is keyed
                                                        * with, fresh for every session */
    uint32_t          cookies_refused; /* host: a response whose cookie this host never made */
    uint32_t          proofs_refused;  /* host: a response that did not know the password */
    uint32_t          digest_faults;   /* the provider refused a digest; nobody can join */
    uint32_t          joins;           /* peers that reached CONNECTED, for the arrival counter */
    uint32_t          denied;          /* handshakes refused, so a quiet failure is still visible */
    uint32_t          refused_foreign;   /* a datagram that did not carry our magic */
    uint32_t          refused_unhandled; /* our magic, and no arm of the dispatch took it */
    uint32_t          drops;           /* connected peers lost to the timeout */
    uint32_t          leaves;          /* connected peers that sent the leave notice */
    uint32_t          replaced;        /* connected peers a parallel handshake replaced in place */
    uint32_t          payloads_reordered; /* refused: an older payload arrived after a newer one */
    uint32_t          payloads_past_window; /* taken from packets refused for the order window */
    uint32_t          payloads_overrun;   /* pushed out of the ring before a reader took them */
    uint32_t          payloads_overrun_fresh; /* of those, the ones the drain was awake for */
    uint32_t          bulk_overrun;       /* a bulk note arrived and the ring had no place */
    uint32_t          deepest_backlog;    /* the most payloads any peer held at one moment */
    uint32_t          longest_wait_ms;    /* the longest any payload waited to leave the ring */

    /* The two kinds of connected packet, counted apart. A packet with a payload leaves the
     * service that finds one; a packet without leaves as a keepalive or to pace queued reliable
     * traffic. The second kind is the escape a reliable message has from a full payload, and
     * before these two nothing said whether it was ever taken: "in a level nothing large rides
     * the channel" was plausible and not measured. */
    uint32_t          packets_with_payload;
    uint32_t          packets_without_payload;
    bool              seat_wait_said;     /* the once-only starved channel warning went out */

    /* The holds (mp_session_hold.c). A broadcast that some peers took and others had to hold for,
     * the peers sent away for falling behind, which are not denials, and the late packets of
     * those peers answered with the reason. */
    uint32_t          broadcasts_partial;
    uint32_t          singles_held;
    uint32_t          dropped_behind;
    uint32_t          behind_answers;
    mp_session_dropped_t dropped[MP_SESSION_DROPPED_RECENT];

    /* Every datagram this session put on the wire, a second at a time, against the relay's
     * ceilings for a session and for a host. */
    mp_meter_second_t upload;
    mp_peer_t         peers[MP_SESSION_MAX_PEERS];
} mp_session_t;

/* Sets up a session in one role over one transport. `seed` seeds the salt generator; the caller
 * feeds it real entropy in the field and a fixed value in a test. A zero seed is replaced. The
 * transport is held by pointer and dereferenced on every update for the whole run, so it has to
 * live at least as long as the session: the end to end test built one as a local in a setup
 * function, and the first update after that function returned crashed on the dangling pointer.
 * The transports are returned by value, so the caller owns where one lives. */
void mp_session_init(mp_session_t *session, mp_session_role_t role, const mp_transport_t *transport,
                     uint32_t seed);

/* Names the game MODE this side is playing, for the request. A host refuses a client that named a
 * different one.
 *
 * The session does not know what a mode IS, and deliberately: it compares two numbers, the way it
 * compares two content fingerprints. What the numbers mean belongs to the feature above, because
 * the rule "these two cannot play together" is the same rule whatever the modes are called.
 *
 * Zero on either side skips the check, so a build or a test that names no mode still joins. That is
 * the same shape the content check uses, and for the same reason: a guard that refuses everyone
 * when it cannot decide is worse than one that stands aside. */
void mp_session_set_mode(mp_session_t *session, uint8_t mode);

/* This side's name, cleaned by the roster's rule, for the request or the accept. Set it before
 * connecting; a host may set it any time before a client arrives. */
void mp_session_set_name(mp_session_t *session, const char *name);

/* The password: what a host asks for, or what a client proves it knows. Empty means none. A host
 * with a password denies a response whose proof was not made with the same one, with
 * MP_DENY_PASSWORD; a host without one takes anybody, whatever they proved. The password itself
 * never leaves this side. Set it before connecting or before the first request arrives. */
void mp_session_set_password(mp_session_t *session, const char *password);

/* How many peers a host admits, at most MP_SESSION_MAX_PEERS; zero means every slot. A request
 * that would go past it is refused with MP_DENY_FULL, exactly as one that finds no free slot. A
 * peer already connected is not dropped by a smaller number. */
void mp_session_set_capacity(mp_session_t *session, uint8_t peers);

/* A peer's name, as it named itself, or the default; never NULL for an index in range. */
const char *mp_session_peer_name(const mp_session_t *session, size_t index);

/* The world slot the peer at `index` of a host session plays at: its index plus one. Slot 0 is
 * a listen host's own player and stays empty on a dedicated server, so a client built for one
 * works against the other unchanged. One rule for the dedicated server, the listen host and the
 * roster, which used to spell it out once each. */
uint8_t mp_session_slot_of_peer(size_t index);

/* The other way round: the peer index that plays at world `slot`, false for slot 0, which is a
 * listen host's own player and a dedicated server's empty one, and for a slot past the peers. The
 * same one rule as above, so a note addressed to a slot and the slot a peer was told can never be
 * two different answers. */
bool mp_session_peer_of_slot(uint8_t slot, size_t *index);

/* The smoothed round trip to a connected peer in milliseconds, 0 until one is measured. */
uint32_t mp_session_peer_rtt_ms(const mp_session_t *session, size_t index);

/* What this side plays with, as the feature states it: a client's request carries it, a host holds
 * its own against every request's with the judge. False, and no statement at all, when it does not
 * fit MP_SESSION_STATEMENT_BYTES; a length of 0 takes a statement back. A side without one is never
 * compared, the same answer a build gets that cannot read its own data yet. */
bool mp_session_set_statement(mp_session_t *session, const uint8_t *bytes, size_t length);

/* Host: who decides whether a request's statement plays with this side's own. NULL compares
 * nothing. See mp_session_judge_fn. */
void mp_session_set_judge(mp_session_t *session, mp_session_judge_fn judge);

/* How many bytes this side states, 0 for none. */
size_t mp_session_statement_bytes(const mp_session_t *session);

/* Client: begin joining the host at `endpoint`. Idempotent; the session keeps trying across
 * updates until it is connected or the handshake times out. No effect on a host. */
void mp_session_connect(mp_session_t *session, uint32_t endpoint);

/* Leave: send the leave notice to every connected peer and free every slot. Call it on the way out
 * of the process, so the far side frees the slot now rather than at the connected timeout. */
void mp_session_disconnect(mp_session_t *session);

/* Host: sends one connected peer away with `reason` in the denial, so its player is told why, and
 * frees the slot at once. False on a client, and for an index that holds no connected peer. A
 * peer sent away may come back with a fresh handshake and is judged again then. */
bool mp_session_drop(mp_session_t *session, size_t index, mp_deny_reason_t reason);

/* The two halves of driving a session, each at time `now_ms`. Receive drains every waiting packet
 * and dispatches it; service times out stale peers, repeats due handshake packets, sends every
 * payload that is set, and sends a keepalive to a peer this side has been silent to. A caller
 * that sets a payload once per substep calls service in that substep; a caller that only wants
 * to keep the link alive between substeps calls both with nothing set. */
void mp_session_receive(mp_session_t *session, uint32_t now_ms);
void mp_session_service(mp_session_t *session, uint32_t now_ms);

/* Both halves in order, for a caller with one loop and no substep, such as the dedicated server. */
void mp_session_update(mp_session_t *session, uint32_t now_ms);

/* How many peers are connected right now. */
size_t mp_session_peer_count(const mp_session_t *session);

/* The client's own connected state, false for a host or a client still handshaking. */
bool mp_session_is_connected(const mp_session_t *session);

/* Client only: still asking, or connected. False once the join window has run out or a
 * refusal arrived, which is the state a lobby band has to be able to name. */
bool mp_session_connect_wanted(const mp_session_t *session);

/* Reliable messages queued and not yet acknowledged, over every connected peer, the ones a hold
 * keeps for a peer included: they are as unanswered as the rest. */
size_t mp_session_reliable_pending(const mp_session_t *session);

/* Queue a reliable message to one connected peer by index. False when the peer is not connected,
 * its channel is full, or a hold is keeping messages for it: a single message may not overtake
 * what was held before it, and a caller that repeats a refused note repeats it once the hold has
 * gone to the channel. */
bool mp_session_send_reliable(mp_session_t *session, size_t peer_index, const void *data,
                              size_t bytes);

/* The same, for a caller that has no second chance: a moment passed on from another player, a hit
 * addressed to its victim. What the channel cannot take now, or may not take because messages are
 * held in front of it, is held behind them. False only when the peer is not connected or its hold
 * is full, and in the second case the peer has been sent away in this call (MP_DENY_BEHIND). A
 * host alone holds; on a client this is the plain send above. */
bool mp_session_send_or_hold(mp_session_t *session, size_t peer_index, const void *data,
                             size_t bytes);

/* Queue a reliable message to every connected peer, and answer how many it reached.
 *
 * Each peer is its own queue. A peer whose channel takes the message gets it now; a peer whose
 * channel is full, or that has messages held already, gets it held behind them. Nought, with
 * nothing queued anywhere, only when no peer's channel could take it and no peer is being held
 * for, which with a single peer is exactly the refusal a full channel always gave: two players see
 * no change, and a caller that repeats a refused note still repeats it. A peer whose hold cannot
 * take the message either is sent away in this call and the others get it all the same, so one
 * machine that has stopped keeping up never stops a message reaching the rest. */
size_t mp_session_broadcast_reliable(mp_session_t *session, const void *data, size_t bytes);

/* mp_session_send_or_hold to every connected peer, for a note every player gets once and nobody
 * repeats, such as a line of chat, and how many peers took or hold it. Unlike the broadcast above
 * it never answers nought for all because no channel could take the note now: each peer's copy
 * goes in, is held, or, with that peer's hold full as well, is lost with the peer, which is sent
 * away in this call. A client holds nothing, so there it is the plain send to the host. */
size_t mp_session_broadcast_or_hold(mp_session_t *session, const void *data, size_t bytes);

/* The bytes of reliable messages, headers included, the next packet to one connected peer would
 * seat if it had `limit` bytes of room for them, after the peer's hold has gone to its channel and
 * a state waiting outside it has gone in. The bridge sizes the enemy block by it. 0 for a peer
 * that is not connected. */
size_t mp_session_due_bytes(mp_session_t *session, size_t peer_index, size_t limit);

/* What the holds did, over every peer slot of a session: broadcasts that had to be held for some
 * peers, single notes held, messages and bytes taken into a hold, messages handed on from one
 * later, the deepest any hold was and on which peer, the longest a message waited in one, the
 * peers sent away for falling behind, and the late packets of those peers answered. */
typedef struct mp_session_hold_totals {
    uint32_t broadcasts_partial;
    uint32_t singles_held;
    uint32_t held;
    uint32_t held_bytes;
    uint32_t delivered;
    uint32_t deepest;
    uint32_t deepest_peer;
    uint32_t longest_ms;
    uint32_t dropped_behind;
    uint32_t answers;
} mp_session_hold_totals_t;

void mp_session_hold_totals(const mp_session_t *session, mp_session_hold_totals_t *out);

/* What the payload just set for the peer at `index` was made of, for the report's payload line:
 * the bodies' bytes and the enemy block's. Changes nothing that is sent. */
void mp_session_note_payload_parts(mp_session_t *session, size_t index, size_t bodies_bytes,
                                   size_t enemy_bytes);

/* Host: whether the last connection at `index` ended because the host sent it away for falling
 * behind. Kept until a peer connects there again, so whoever notices the seat come free can say
 * why. */
bool mp_session_peer_dropped_behind(const mp_session_t *session, size_t index);

/* What the channels did with one kind of state note (mp_channel_state_kind_t's counters), added up
 * over every peer slot; zeros for a kind nobody sent. */
void mp_session_state_totals(const mp_session_t *session, uint8_t kind,
                             mp_channel_state_kind_t *out);

/* Read the next reliable message from one connected peer, in the order it sent them. False when
 * none has arrived or the buffer is too small. */
bool mp_session_read_reliable(mp_session_t *session, size_t peer_index, void *buffer,
                              size_t capacity, size_t *bytes);

/* Moves what the peer's channel holds in order into its inbox, as far as the inbox has room. The
 * receive calls it after every packet and the read before it takes, so the channel's window moves
 * with what arrives rather than with what a reader has acted on. An empty message is a state the
 * sender shrank because a younger copy follows it; it is counted here and never reaches a reader,
 * which is the one place every reader, the dedicated server's included, is spared it. */
void mp_session_move_notes(mp_session_t *session, mp_peer_t *peer);

/* The empty messages skipped that way, over every peer slot. */
uint32_t mp_session_empty_notes(const mp_session_t *session);

/* Set the unreliable payload for one peer, or for all connected peers. It rides the next outgoing
 * packet and is then cleared, so a payload set once is sent once; a caller sending every tick sets
 * it every tick. False when the peer is not connected or the payload is too large. */
bool   mp_session_set_payload(mp_session_t *session, size_t peer_index, const void *data,
                              size_t bytes);
size_t mp_session_broadcast_payload(mp_session_t *session, const void *data, size_t bytes);

/* Take the oldest unread payload received from one peer. False when none waits or the buffer is
 * too small. A caller drains the ring by calling until false: every payload that entered the ring
 * is handed out once, in the order the peer sent them. What never enters is a payload from a
 * packet older than the newest one held, which a newer state has already superseded. */
bool mp_session_read_payload(mp_session_t *session, size_t peer_index, void *buffer,
                             size_t capacity, size_t *bytes);

/* Send one bulk note to a connected peer. It leaves in a datagram of its own AT ONCE, it does not
 * wait for a packet, does not take room from a snapshot, is not ordered against anything and is
 * never resent from here. Whoever sends one is responsible for noticing that it did not arrive,
 * which is what the acknowledging mask above this layer is for. False when the peer is not
 * connected, the note is too large, or the transport refused it. */
bool mp_session_send_bulk(mp_session_t *session, size_t peer_index, const void *data, size_t bytes);

/* Take the oldest unread bulk note from one peer, draining by calling until false. */
bool mp_session_read_bulk(mp_session_t *session, size_t peer_index, void *buffer, size_t capacity,
                          size_t *bytes);

/* Bulk notes that reached this side and never reached a reader: the ring was full when one
 * arrived. A few are ordinary under load and are asked for again; a great many mean the reader is
 * not draining, which no other number would say. */
uint32_t mp_session_bulk_overrun(const mp_session_t *session);

const mp_peer_t *mp_session_peer(const mp_session_t *session, size_t index);

uint32_t mp_session_joins(const mp_session_t *session);

/* What reached dispatch and got no further. Foreign means the magic was not ours, which on
 * a quiet port is usually somebody else's broadcast; unhandled means the magic WAS ours and
 * no arm took the packet, which is two builds disagreeing about a shape. */
uint32_t mp_session_refused_foreign(const mp_session_t *session);
uint32_t mp_session_refused_unhandled(const mp_session_t *session);
uint32_t mp_session_denied(const mp_session_t *session);
uint32_t mp_session_drops(const mp_session_t *session);
uint32_t mp_session_leaves(const mp_session_t *session);
uint32_t mp_session_replaced(const mp_session_t *session);

/* Payloads that reached the session and never reached a reader. The two causes are counted apart
 * because they say opposite things about where to look: reordered means the network delivered out
 * of order, which on a local link is close to impossible and points at the transport; overrun means
 * arrivals outran the drain, which points at a stall on this side or a burst after one on the
 * peer's. A single number cannot answer either question, and both are invisible in the difference
 * between what a peer says it sent and what this side decoded. A network duplicate of a packet
 * refused for the order window is counted here as well, and as a refusal a second time. */
uint32_t mp_session_payloads_reordered(const mp_session_t *session);
uint32_t mp_session_payloads_overrun(const mp_session_t *session);

/* The discards the drain was awake for: the payload pushed out had waited less than
 * MP_SESSION_DRAIN_STALL_MS, so substeps were running and the arrivals outran them anyway. That
 * is the only reading of an overrun that asks for a deeper ring or a faster drain. The remainder,
 * the total less this, was pushed out while nothing drained at all, and the answer to those is to
 * drain between substeps rather than to add depth. */
uint32_t mp_session_payloads_overrun_fresh(const mp_session_t *session);

/* How full the ring ever got, and how long a payload ever waited in it. Both survive a fix and
 * keep saying whether the depth and the drain are enough; the overrun counts only speak when
 * something was already thrown away. */
uint32_t mp_session_deepest_backlog(const mp_session_t *session);
uint32_t mp_session_longest_wait_ms(const mp_session_t *session);

/* How many connected packets left with a payload and how many without. The second number is the
 * witness for the pacing path in the service: a queued reliable message with no payload to ride
 * beside leaves in a packet of its own, and until it was counted nobody could say whether that
 * ever happened in a level. */
uint32_t mp_session_packets_with_payload(const mp_session_t *session);
uint32_t mp_session_packets_without_payload(const mp_session_t *session);

/* The channels' own account of the payload reservation, added up over every peer slot: how many
 * times a due reliable message was refused a seat that the payload alone had taken, and the
 * longest any message waited for a seat. Summed over every slot rather than every CONNECTED peer,
 * so a peer that has left is still counted until its slot is taken again and the channel in it
 * is started over. */
uint32_t mp_session_seats_lost_to_payload(const mp_session_t *session);
uint32_t mp_session_longest_seat_wait_ms(const mp_session_t *session);

/* The least room a payload packet left for messages, and the longest any message has been out
 * unanswered. The two numbers that say which kind of wait a channel is having. */
size_t mp_session_least_room_left(const mp_session_t *session);
uint32_t mp_session_longest_unacked_ms(const mp_session_t *session);
/* The window rule summed over every slot, as the two above. */
uint32_t mp_session_held_past_window(const mp_session_t *session);
uint32_t mp_session_refused_past_window(const mp_session_t *session);
/* The inboxes, on the basis of the channel counters: the most notes and the most bytes any one
 * held (two maxima, not one moment), the stalls with notes left on a channel for want of room,
 * and the payloads handed on from packets refused for the window. */
uint32_t mp_session_inbox_most_notes(const mp_session_t *session);
uint32_t mp_session_inbox_most_bytes(const mp_session_t *session);
uint32_t mp_session_inbox_stalls(const mp_session_t *session);
uint32_t mp_session_payloads_past_window(const mp_session_t *session);

/* Shared between the two session files rather than public: the insert side of the ring lives in
 * mp_session.c and the read side in mp_session_io.c, and both let payloads go. Measuring the wait
 * in one place is what keeps the two from drifting into two different answers. Returns the wait
 * in milliseconds so a caller that has to judge the discard need not compute it again. */
uint32_t mp_session_note_payload_leaving(mp_session_t *session, const mp_session_payload_t *slot);

/* Shared the same way: the service calls it for a connected peer after it has built that peer's
 * packet, and it says once, in the log, when the channel's oldest message has waited past
 * MP_SESSION_SEAT_WAIT_WARN_MS for a seat. The judgement lives beside the counters it is made
 * from, so the line in the log and the numbers in the report cannot disagree about what a
 * starved channel is. */
void mp_session_watch_seat_wait(mp_session_t *session, const mp_peer_t *peer);

/* Why the client's last request was refused, MP_DENY_NONE when it never was. */
mp_deny_reason_t mp_session_last_deny(const mp_session_t *session);

/* What that refusal said behind its reason, as the host sent it: the byte count, and the bytes in
 * `*bytes`. 0 for a refusal without a detail, and for none. A stranger wrote them. */
size_t mp_session_last_deny_detail(const mp_session_t *session, const uint8_t **bytes);

#endif /* MULTIPLAYER_MP_SESSION_H */
