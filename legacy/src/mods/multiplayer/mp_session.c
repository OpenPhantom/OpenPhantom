/* mp_session.c: the join handshake, the peer table, and one channel per connected peer.
 *
 * SIZE NOTE: over 600 lines. The handshake handlers on both roles, the cookie and the proof, the
 * leave logic and the peer service loop are one state machine, and each half reads the other's
 * fields. What touches no handshake state has left: the accessors and pass-throughs as
 * mp_session_io.c, the packet builders and readers as mp_session_packets.c.
 *
 * Every packet opens with a magic word and a type. Handshake packets carry their salts directly;
 * a payload packet carries a connection id and then a whole channel packet, which is why the
 * channel packet is built into a capacity reduced by the session header, so the wrapped packet
 * still fits the transport's budget. The salts, the cookie and the connection id are the
 * anti-spoofing: a peer that never saw the challenge cannot produce the cookie, and the host
 * keeps nothing for it until it has. The two client-sent packets are padded so the port is never
 * an amplifier.
 */
#include "mp_session.h"
#include "mp_session_packets.h"

#include "mp_session_bulk.h"
#include "mp_session_hold.h"

#include "mp_entropy.h"
#include "mp_roster.h"

#include "mp_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The envelope this module writes decides how large a reliable message can be, so the channel
 * derives its sizes from a budget with the envelope already taken off, and the two are asserted
 * equal rather than believed: drifted apart, the channel accepts a message no packet built here
 * can seat, and an unseatable message at the head of an ordered queue stops it in both directions.
 * */
_Static_assert(MP_SESSION_PAYLOAD_HEADER == MP_CHANNEL_ENVELOPE_BYTES,
               "the channel budgets for an envelope this module no longer writes; every size it "
               "derives is now wrong and the largest legal message cannot be seated");
_Static_assert(MP_SESSION_CHANNEL_CAP == MP_CHANNEL_BUDGET_BYTES,
               "the capacity handed to the builder is not the budget it sized itself against");
_Static_assert(MP_SESSION_PAYLOAD_BYTES <= MP_CHANNEL_PAYLOAD_BYTES,
               "this module offers a payload larger than the builder will take, so every packet "
               "carrying a full one is refused and the peer falls silent");

/* Every peer at its rate stays under the 768 KiB a second a whole session is kept to, below the
 * relay's own megabyte a second for a session, which counts both ways and the savegame too. */
_Static_assert(MP_SESSION_MAX_PEERS * MP_BUDGET_RATE_BYTES_A_SECOND <= 768u * 1024u,
               "the peers' rates add up past what a session may send");

/* mp_session_bulk.c names its two envelope bytes for itself, because the lane shares this
 * module's envelope and nothing else of it. Asserted against those NAMES, not against literals:
 * a magic or a type that drifted on either side would make every bulk note foreign to the
 * dispatch below, and a transfer would simply never start. */
_Static_assert(MP_SESSION_MAGIC == MP_SESSION_BULK_MAGIC,
               "the magic mp_session_bulk.c writes on a bulk note is not this module's");
_Static_assert((int)PKT_BULK == (int)MP_SESSION_BULK_TYPE,
               "the type byte mp_session_bulk.c writes is not the one dispatched here");

/* ============================================================================================== */

static mp_peer_t *free_peer(mp_session_t *session)
{
    size_t     i;
    size_t     in_use = 0;
    mp_peer_t *found  = NULL;

    for (i = 0; i < MP_SESSION_MAX_PEERS; ++i) {
        if (session->peers[i].state == MP_PEER_FREE) {
            if (found == NULL) {
                found = &session->peers[i];
            }
        } else {
            ++in_use;   /* a slot in a handshake counts: it is spoken for */
        }
    }
    if (session->capacity != 0u && in_use >= session->capacity) {
        return NULL;
    }
    return found;
}

/* Every slot enters and leaves use through these two, so the transport's hold on the endpoint
 * matches the peer's life exactly. Peers are keyed by endpoint number, and a transport that
 * recycled a number mid-handshake or mid-connection would address this peer's packets, connection
 * id included, to whoever got the number next. */
static void claim_peer(mp_session_t *session, mp_peer_t *peer, mp_peer_state_t state,
                       uint32_t endpoint)
{
    if (peer->state != MP_PEER_FREE && peer->endpoint != endpoint) {
        mp_transport_hold(session->transport, peer->endpoint, false);
    }
    peer->state    = state;
    peer->endpoint = endpoint;
    mp_transport_hold(session->transport, endpoint, true);
}

static void release_peer(mp_session_t *session, mp_peer_t *peer)
{
    mp_transport_hold(session->transport, peer->endpoint, false);
    peer->state = MP_PEER_FREE;
}

/* A fresh connection on this slot: a new channel, and nothing of a previous occupant's payloads,
 * because a restarted peer's packets count from its own first sequence. Nothing it was held for
 * either: a replacement in place comes through here too, and a held message of the old
 * connection would otherwise reach the new one before its slot byte. */
static void connect_peer(mp_session_t *session, mp_peer_t *peer)
{
    peer->state             = MP_PEER_CONNECTED;
    peer->last_recv_ms      = session->now_ms;
    peer->out_payload_bytes = 0u;
    peer->in_head           = 0u;
    peer->in_count          = 0u;
    peer->in_have_sequence  = false;
    peer->dropped_behind    = false;
    peer->empty_notes       = 0u;
    peer->packets_with_payload    = 0u;
    peer->packets_without_payload = 0u;
    peer->overflow_packets        = 0u;
    peer->held_by_budget          = 0u;
    memset(&peer->meter, 0, sizeof peer->meter);
    mp_budget_bucket_start(&peer->budget, session->now_ms);
    mp_channel_init(&peer->channel);
    mp_inbox_init(&peer->inbox);
    mp_hold_init(&peer->hold);
    ++session->joins;
}

/* ===================================== Host handlers ========================================== */

/* The window a cookie is made in: the host's own clock in steps of the window's length. */
static uint32_t cookie_window(const mp_session_t *session)
{
    return session->now_ms / MP_SESSION_COOKIE_WINDOW_MS;
}

/* The cookie for one address, one pair of salts and one window, under this session's secret. The
 * address is the transport's and not the endpoint number: a host that keeps nothing holds no
 * endpoint either, and a number it did not hold may belong to somebody else by the time the
 * response arrives. */
static bool make_cookie(mp_session_t *session, uint32_t endpoint, uint64_t client_salt,
                        uint64_t server_salt, uint32_t window,
                        uint8_t out[MP_SESSION_COOKIE_BYTES])
{
    uint8_t          message[8 + 8 + 8 + 8 + 4];
    uint8_t          digest[MP_ENTROPY_HMAC_BYTES];
    mp_wire_writer_t w;

    mp_wire_writer_init(&w, message, sizeof message);
    mp_session_put_u64(&w, 0x4F4249434F4F4B31ull);   /* "OBICOOK1", so it is no other digest */
    mp_session_put_u64(&w, mp_transport_address(session->transport, endpoint));
    mp_session_put_u64(&w, client_salt);
    mp_session_put_u64(&w, server_salt);
    mp_wire_put_u32(&w, window);
    if (w.overflowed ||
        !mp_entropy_hmac(session->secret, sizeof session->secret, message, sizeof message,
                         digest)) {
        ++session->digest_faults;
        return false;
    }
    memcpy(out, digest, MP_SESSION_COOKIE_BYTES);
    return true;
}

/* Made in this window or the one before, so a cookie handed out a moment before the window turned
 * is still good. */
static bool cookie_is_good(mp_session_t *session, uint32_t endpoint, uint64_t client_salt,
                           uint64_t server_salt, const uint8_t cookie[MP_SESSION_COOKIE_BYTES])
{
    uint8_t  expected[MP_SESSION_COOKIE_BYTES];
    uint32_t window = cookie_window(session);
    uint32_t back;

    for (back = 0u; back < 2u; ++back) {
        if (make_cookie(session, endpoint, client_salt, server_salt, window - back, expected) &&
            mp_entropy_equal(expected, cookie, MP_SESSION_COOKIE_BYTES)) {
            return true;
        }
    }
    return false;
}

/* A request is answered with a challenge and nothing else: no slot, no timer, no memory of the
 * address. A forged request per slot every few seconds used to hold every slot of a listen host,
 * because each one claimed a slot for the handshake timeout; now a flood of them costs the host a
 * digest each and sends the flood thirty seven bytes for every thousand and twenty four. */
/* What a request states, as the dispatch read it off the wire. */
typedef struct request_statement {
    uint8_t bytes[MP_SESSION_STATEMENT_BYTES];
    size_t  length;
} request_statement_t;

/* The judge, when both sides stated something. A side that stated nothing is not compared, so a
 * test, the dedicated server and a build that cannot read its own data yet still join. */
static bool judge_refuses(mp_session_t *session, uint32_t endpoint, uint64_t client_salt,
                          const request_statement_t *far)
{
    uint8_t detail[MP_SESSION_DENY_DETAIL_BYTES];
    size_t  detail_bytes = 0u;
    uint8_t reason;

    if (session->judge == NULL || session->statement_bytes == 0u || far->length == 0u) {
        return false;
    }
    reason = session->judge(session->statement, session->statement_bytes, far->bytes,
                            far->length, detail, sizeof detail, &detail_bytes);
    if (reason == (uint8_t)MP_DENY_NONE) {
        return false;
    }
    mp_session_send_deny_detail(session, endpoint, client_salt, (mp_deny_reason_t)reason, detail,
                                detail_bytes <= sizeof detail ? detail_bytes : 0u);
    return true;
}

static void host_on_request(mp_session_t *session, uint32_t endpoint, uint64_t client_salt,
                            uint32_t protocol, const request_statement_t *statement, uint8_t mode)
{
    mp_peer_t *peer = mp_session_peer_at(session, endpoint);
    uint64_t   server_salt;
    uint8_t    cookie[MP_SESSION_COOKIE_BYTES];

    /* Refused before anything is made. The version check is exact; what the two sides play with is
     * the judge's, see judge_refuses. */
    if (protocol != MP_WIRE_VERSION) {
        mp_session_send_deny(session, endpoint, client_salt, MP_DENY_PROTOCOL);
        return;
    }
    if (judge_refuses(session, endpoint, client_salt, statement)) {
        return;
    }
    /* The mode is its own refusal and not folded into the content, because the two say different
     * things to whoever reads the log: a content mismatch means "your files differ from mine" and
     * a mode mismatch means "you picked a different game". Folding them would have sent every
     * player who chose the wrong entry in the menu off to compare their mod folders. */
    if (mode != 0u && session->mode != 0u && mode != session->mode) {
        mp_session_send_deny(session, endpoint, client_salt, MP_DENY_MODE);
        return;
    }
    /* A client that missed its accept asks again under the salt it is connected with. */
    if (peer != NULL && peer->state == MP_PEER_CONNECTED && client_salt == peer->client_salt) {
        mp_session_send_accept(session, endpoint, peer->connection_id);
        return;
    }
    /* Full is said now, before a challenge, to an address that holds no slot here. An address the
     * host already holds takes its own slot back with a fresh handshake, so it is not full for it.
     * The denial carries the client's own salt back, so a forged denial from anyone who did not
     * see the request cannot talk a joining client out of its handshake. */
    if (peer == NULL && free_peer(session) == NULL) {
        mp_session_send_deny(session, endpoint, client_salt, MP_DENY_FULL);
        return;
    }
    server_salt = mp_session_next_salt(session);
    if (!make_cookie(session, endpoint, client_salt, server_salt, cookie_window(session), cookie)) {
        return;   /* counted: with no digest nobody can join, and the report says so */
    }
    mp_session_send_challenge(session, endpoint, client_salt, server_salt, cookie);
}

/* A slot is made here, and only for a response that echoes a cookie this host made for this
 * address and these salts in the last window or two. A response from a connected address under
 * other salts is that address back with a fresh handshake, a client that restarted, and it takes
 * its slot over in place: the cookie proves the challenge reached that address, which a forger
 * elsewhere cannot fake, and the old connection's packets carry the old id and are dropped from
 * here on. The password is checked by its proof, after the cookie, so a stranger without one
 * learns nothing about it. */
static void host_on_response(mp_session_t *session, uint32_t endpoint, uint64_t client_salt,
                             uint64_t server_salt, const uint8_t cookie[MP_SESSION_COOKIE_BYTES],
                             const uint8_t proof[MP_SESSION_PROOF_BYTES], const char *name)
{
    mp_peer_t *peer = mp_session_peer_at(session, endpoint);
    uint64_t   connection_id = client_salt ^ server_salt;
    uint8_t    expected[MP_SESSION_PROOF_BYTES];

    if (!cookie_is_good(session, endpoint, client_salt, server_salt, cookie)) {
        ++session->cookies_refused;
        ++session->denied;
        return;
    }
    if (session->password[0] != '\0' &&
        (!mp_session_proof(session->password, client_salt, server_salt, expected) ||
         !mp_entropy_equal(expected, proof, MP_SESSION_PROOF_BYTES))) {
        ++session->proofs_refused;
        mp_session_send_deny(session, endpoint, client_salt, MP_DENY_PASSWORD);
        return;
    }
    if (peer != NULL && peer->state == MP_PEER_CONNECTED) {
        if (peer->connection_id == connection_id) {
            peer->last_recv_ms = session->now_ms;
            mp_session_send_accept(session, endpoint, connection_id);   /* lost accept */
            return;
        }
        ++session->replaced;
    } else {
        if (peer == NULL) {
            peer = free_peer(session);
        }
        if (peer == NULL) {
            mp_session_send_deny(session, endpoint, client_salt, MP_DENY_FULL);
            return;
        }
        claim_peer(session, peer, MP_PEER_CHALLENGED, endpoint);
    }
    peer->client_salt   = client_salt;
    peer->server_salt   = server_salt;
    peer->connection_id = connection_id;
    mp_roster_name_clean(name, peer->name);
    connect_peer(session, peer);
    mp_session_send_accept(session, endpoint, connection_id);
}

/* ===================================== Client handlers ======================================= */

static void client_on_challenge(mp_session_t *session, uint32_t endpoint, uint64_t client_salt,
                                uint64_t server_salt,
                                const uint8_t cookie[MP_SESSION_COOKIE_BYTES])
{
    mp_peer_t *peer = &session->peers[0];

    if (peer->state != MP_PEER_CONNECTING || peer->endpoint != endpoint ||
        peer->client_salt != client_salt) {
        return;
    }
    peer->server_salt   = server_salt;
    peer->connection_id = client_salt ^ server_salt;
    memcpy(peer->cookie, cookie, MP_SESSION_COOKIE_BYTES);
    peer->state         = MP_PEER_CHALLENGED;
    peer->last_recv_ms  = session->now_ms;
    mp_session_send_response(session, peer);
}

static void client_on_accept(mp_session_t *session, uint32_t endpoint, uint64_t connection_id,
                             const char *host_name)
{
    mp_peer_t *peer = &session->peers[0];

    if (peer->state != MP_PEER_CHALLENGED || peer->endpoint != endpoint ||
        peer->connection_id != connection_id) {
        return;
    }
    connect_peer(session, peer);
    memcpy(peer->name, host_name, MP_SESSION_NAME_MAX);
    session->connect_wanted = false;
}

/* The detail is kept as it came, up to its field, for the feature to read; the session does not
 * know what it says. */
static void client_on_deny(mp_session_t *session, uint32_t endpoint, uint64_t client_salt,
                           mp_deny_reason_t reason, const uint8_t *detail, size_t detail_bytes)
{
    mp_peer_t *peer = &session->peers[0];

    if (peer->state != MP_PEER_FREE && peer->endpoint == endpoint &&
        peer->client_salt == client_salt) {
        release_peer(session, peer);
        session->connect_wanted = false;
        session->rejoin_until_ms = session->now_ms;  /* a refusal is an answer; do not retry it */
        session->last_deny = reason;
        session->deny_detail_bytes = 0u;
        if (detail_bytes != 0u && detail_bytes <= sizeof session->deny_detail) {
            memcpy(session->deny_detail, detail, detail_bytes);
            session->deny_detail_bytes = (uint8_t)detail_bytes;
        }
        ++session->denied;
    }
}

/* A client starts the handshake over with a fresh salt for as long as the window of its first
 * connect is open, because a host that has not opened its lobby yet is not a wrong address. A
 * connection that was lost opens no window: see lose_peer. */
static bool rejoin_open(const mp_session_t *session)
{
    return session->role == MP_SESSION_CLIENT &&
           (uint32_t)(session->rejoin_until_ms - session->now_ms) < 0x80000000u &&
           session->rejoin_until_ms != session->now_ms;
}

static void begin_handshake(mp_session_t *session, mp_peer_t *peer, uint32_t endpoint)
{
    claim_peer(session, peer, MP_PEER_CONNECTING, endpoint);
    peer->client_salt  = mp_session_next_salt(session);
    peer->last_recv_ms = session->now_ms;
    /* One resend interval in the past, so the first update sends now. */
    peer->last_send_ms = session->now_ms - MP_SESSION_RESEND_MS;
    session->host_endpoint  = endpoint;
    session->connect_wanted = true;
}

/* The connection is gone, by timeout or by the far side's leave notice. A client gives its host up;
 * a host frees the slot, and a handshake that was coming for it keeps nothing here to find: its
 * response makes a slot of its own. */
static void lose_peer(mp_session_t *session, mp_peer_t *peer)
{
    release_peer(session, peer);
    if (session->role != MP_SESSION_CLIENT) {
        return;
    }
    /* Given up, whether the host quit on purpose or went silent for the whole connected timeout.
     * A silent host used to be asked again for a further minute: a host whose process had died
     * cost its client ninety seconds in a level that answered nothing, with nothing on screen.
     * The idle pump services the session through a level load, so a host that says nothing for
     * that long is not loading; the picture says so from the second second on (mp_hud), and a
     * player who wants back connects again, which the host takes in at once. */
    session->connect_wanted = false;
}

static void on_bye(mp_session_t *session, uint32_t endpoint, uint64_t connection_id)
{
    mp_peer_t *peer = mp_session_peer_at(session, endpoint);

    /* Only a connected peer that proves the shared id can be taken off; an address alone cannot. */
    if (peer == NULL || peer->state != MP_PEER_CONNECTED || peer->connection_id != connection_id) {
        return;
    }
    ++session->leaves;
    lose_peer(session, peer);
}

/* ===================================== Payload =============================================== */

uint32_t mp_session_note_payload_leaving(mp_session_t *session, const mp_session_payload_t *slot)
{
    uint32_t waited = session->now_ms - slot->held_ms;

    if (session->longest_wait_ms < waited) {
        session->longest_wait_ms = waited;
    }
    return waited;
}

/* Into the ring, only when the packet is newer than the newest payload held: an older payload
 * arriving late carries state a newer one has already replaced. A full ring drops its oldest for
 * the same reason. A zero-length payload is not news and enters nothing. A network duplicate of a
 * packet refused for the order window comes with the same sequence and is counted as reordered.
 *
 * A discard is counted twice over: once plainly, and once more when the payload it threw away had
 * waited less than a stall, which is the only case that says the drain was running and was outrun
 * anyway. Everything else was thrown away while nothing drained, and the depth of the ring has no
 * bearing on that. */
static bool hold_payload(mp_session_t *session, mp_peer_t *peer, const uint8_t *payload,
                         size_t payload_len, uint16_t sequence)
{
    mp_session_payload_t *slot;

    if (payload_len == 0u || payload_len > MP_SESSION_PAYLOAD_BYTES) {
        return false;
    }
    if (peer->in_have_sequence && !mp_channel_sequence_newer(sequence, peer->in_newest_sequence)) {
        ++session->payloads_reordered;
        return false;
    }
    if (peer->in_count == MP_SESSION_PAYLOAD_RING) {
        mp_session_payload_t *oldest = &peer->in_ring[peer->in_head];

        if (mp_session_note_payload_leaving(session, oldest) < MP_SESSION_DRAIN_STALL_MS) {
            ++session->payloads_overrun_fresh;
        }
        peer->in_head = (peer->in_head + 1u) % MP_SESSION_PAYLOAD_RING;
        --peer->in_count;
        ++session->payloads_overrun;
    }
    slot = &peer->in_ring[(peer->in_head + peer->in_count) % MP_SESSION_PAYLOAD_RING];
    memcpy(slot->data, payload, payload_len);
    slot->bytes = payload_len;
    slot->held_ms = session->now_ms;
    ++peer->in_count;
    if (session->deepest_backlog < (uint32_t)peer->in_count) {
        session->deepest_backlog = (uint32_t)peer->in_count;
    }
    peer->in_have_sequence  = true;
    peer->in_newest_sequence = sequence;
    return true;
}

static void on_payload(mp_session_t *session, uint32_t endpoint, uint64_t connection_id,
                       const uint8_t *channel_bytes, size_t channel_len)
{
    mp_peer_t           *peer = mp_session_peer_at(session, endpoint);
    const uint8_t       *payload = NULL;
    size_t               payload_len = 0;
    uint16_t             sequence = 0;
    mp_channel_receipt_t receipt;

    if (peer == NULL || peer->state != MP_PEER_CONNECTED ||
        peer->connection_id != connection_id) {
        /* Dropped, and answered only when it comes from a peer this host sent away a moment ago,
         * which is then told why once more. */
        if (session->role == MP_SESSION_HOST) {
            mp_session_hold_answer(session, endpoint, connection_id);
        }
        return;
    }
    peer->last_recv_ms = session->now_ms;
    receipt = mp_channel_packet_take(&peer->channel, session->now_ms, channel_bytes, channel_len,
                                     &payload, &payload_len, &sequence);
    /* Whatever the receipt, what it stored moves on at once, so the order window counts from what
     * arrived rather than from what a reader has acted on. */
    mp_session_move_notes(session, peer);
    if (receipt != MP_CHANNEL_DROPPED &&
        hold_payload(session, peer, payload, payload_len, sequence) &&
        receipt == MP_CHANNEL_PAST_WINDOW) {
        ++session->payloads_past_window;
    }
}

static void dispatch(mp_session_t *session, uint32_t endpoint, const uint8_t *bytes, size_t len)
{
    mp_wire_reader_t r;
    uint32_t         magic = 0;
    uint8_t          type = 0;
    uint64_t         a = 0;
    uint64_t         b = 0;

    mp_wire_reader_init(&r, bytes, len);
    if (!mp_wire_get_u32(&r, &magic) || magic != MP_SESSION_MAGIC || !mp_wire_get_u8(&r, &type)) {
        ++session->refused_foreign;
        return;     /* foreign or truncated */
    }

    if (session->role == MP_SESSION_HOST) {
        /* The anti-amplification promise is only as good as this gate: a request or response
         * shorter than the padded size gets no reply at all, so no forged small packet can
         * ever draw a larger one out of the port. The client pads, and the host insists on it. */
        bool padded = (len >= MP_SESSION_REQUEST_BYTES);

        if (type == PKT_REQUEST && padded && mp_session_get_u64(&r, &a)) {
            uint32_t            protocol = 0;
            uint32_t            reserved = 0;
            uint8_t             mode = 0;
            uint16_t            stated = 0;
            char                name[MP_SESSION_NAME_MAX];
            request_statement_t statement;

            mp_wire_get_u32(&r, &protocol);
            mp_wire_get_u32(&r, &reserved);
            /* A build that predates the mode byte padded a zero here, and a zero skips the check,
             * so the read needs no version gate of its own. */
            mp_wire_get_u8(&r, &mode);
            mp_session_get_name(&r, name);
            /* A length past the field or past the packet is no statement at all: it is compared
             * as none, like a build that could not read its own data. */
            mp_wire_get_u16(&r, &stated);
            statement.length = 0u;
            if (!r.overran && stated <= MP_SESSION_STATEMENT_BYTES && stated <= len - r.at) {
                mp_session_get_bytes(&r, statement.bytes, stated);
                statement.length = stated;
            }
            host_on_request(session, endpoint, a, protocol, &statement, mode);
        } else if (type == PKT_RESPONSE && padded && mp_session_get_u64(&r, &a) &&
                   mp_session_get_u64(&r, &b)) {
            uint8_t cookie[MP_SESSION_COOKIE_BYTES];
            uint8_t proof[MP_SESSION_PROOF_BYTES];
            char    name[MP_SESSION_NAME_MAX];

            mp_session_get_bytes(&r, cookie, sizeof cookie);
            mp_session_get_bytes(&r, proof, sizeof proof);
            mp_session_get_name(&r, name);
            host_on_response(session, endpoint, a, b, cookie, proof, name);
        } else if (type == PKT_PAYLOAD && mp_session_get_u64(&r, &a) && !r.overran) {
            on_payload(session, endpoint, a, bytes + r.at, len - r.at);
        } else if (type == PKT_BULK && mp_session_get_u64(&r, &a) && !r.overran) {
            mp_session_bulk_arrived(session, endpoint, a, bytes + r.at, len - r.at);
        } else if (type == PKT_BYE && mp_session_get_u64(&r, &a)) {
            on_bye(session, endpoint, a);
        } else {
            ++session->refused_unhandled;
        }
        return;
    }

    /* client */
    if (type == PKT_CHALLENGE && mp_session_get_u64(&r, &a) && mp_session_get_u64(&r, &b)) {
        uint8_t cookie[MP_SESSION_COOKIE_BYTES];

        mp_session_get_bytes(&r, cookie, sizeof cookie);
        if (!r.overran) {
            client_on_challenge(session, endpoint, a, b, cookie);
        }
    } else if (type == PKT_ACCEPT && mp_session_get_u64(&r, &a)) {
        char host_name[MP_SESSION_NAME_MAX];

        mp_session_get_name(&r, host_name);
        client_on_accept(session, endpoint, a, host_name);
    } else if (type == PKT_DENY && mp_session_get_u64(&r, &a)) {
        uint8_t reason = (uint8_t)MP_DENY_FULL;   /* no reason byte: an older build's denial */
        size_t  detail = 0u;

        mp_wire_get_u8(&r, &reason);
        if (!r.overran && len > r.at) {
            detail = len - r.at;
            detail = detail < MP_SESSION_DENY_DETAIL_BYTES ? detail : MP_SESSION_DENY_DETAIL_BYTES;
        }
        client_on_deny(session, endpoint, a, (mp_deny_reason_t)reason, bytes + r.at, detail);
    } else if (type == PKT_PAYLOAD && mp_session_get_u64(&r, &a) && !r.overran) {
        on_payload(session, endpoint, a, bytes + r.at, len - r.at);
    } else if (type == PKT_BULK && mp_session_get_u64(&r, &a) && !r.overran) {
        mp_session_bulk_arrived(session, endpoint, a, bytes + r.at, len - r.at);
    } else if (type == PKT_BYE && mp_session_get_u64(&r, &a)) {
        on_bye(session, endpoint, a);
    } else {
        ++session->refused_unhandled;
    }
}

/* ===================================== Driving ================================================ */

/* One connected packet: the payload's, or with `overflow` the second of a substep, messages only.
 * Every packet takes its bytes from the peer's rate; only the second is ever held back by it. */
static void send_connected_packet(mp_session_t *session, mp_peer_t *peer, bool overflow)
{
    uint8_t          buffer[MP_CHANNEL_PACKET_BYTES];
    uint8_t          channel[MP_SESSION_CHANNEL_CAP];
    size_t           channel_bytes = 0;
    mp_wire_writer_t w;
    bool             built;
    size_t           payload_bytes = overflow ? 0u : peer->out_payload_bytes;

    built = overflow ? mp_channel_packet_build_overflow(&peer->channel, session->now_ms, channel,
                                                        sizeof channel, &channel_bytes)
                     : mp_channel_packet_build(&peer->channel, session->now_ms, peer->out_payload,
                                               peer->out_payload_bytes, channel, sizeof channel,
                                               &channel_bytes);
    if (!built) {
        return;
    }
    if (peer->out_payload_bytes != 0u && !overflow) {
        ++session->packets_with_payload;
        ++peer->packets_with_payload;
        peer->out_payload_bytes = 0;   /* sent once; a caller that wants it every tick sets it */
    } else {
        ++session->packets_without_payload;
        ++peer->packets_without_payload;
        peer->overflow_packets += overflow ? 1u : 0u;
    }
    mp_budget_bucket_fill(&peer->budget, session->now_ms);
    mp_budget_bucket_spend(&peer->budget, MP_SESSION_PAYLOAD_HEADER + channel_bytes);
    mp_wire_writer_init(&w, buffer, sizeof buffer);
    mp_wire_put_u32(&w, MP_SESSION_MAGIC);
    mp_wire_put_u8(&w, PKT_PAYLOAD);
    mp_session_put_u64(&w, peer->connection_id);
    if (w.overflowed || w.at + channel_bytes > sizeof buffer) {
        return;
    }
    memcpy(buffer + w.at, channel, channel_bytes);
    mp_session_send_bytes(session, peer->endpoint, buffer, w.at + channel_bytes);
    peer->last_send_ms = session->now_ms;   /* the keepalive gate reads this */
    if (payload_bytes != 0u) {
        mp_session_meter_payload_packet(&peer->meter, w.at + channel_bytes,
                                        channel_bytes - MP_CHANNEL_HEADER_BYTES - payload_bytes);
    }
}

/* The second packet of a substep, when the one with the payload left a due message behind for
 * want of room: acks and messages, nothing laid in the first packet again, never past the far
 * window, and only when the rate has the tokens for it. At most two packets a peer and substep:
 * the service sends this only right after the payload's. */
static void send_overflow_packet(mp_session_t *session, mp_peer_t *peer)
{
    size_t due = mp_channel_overflow_bytes(&peer->channel, session->now_ms);

    if (due == 0u) {
        return;   /* what was left waits for the throttle or the window, not for room */
    }
    mp_budget_bucket_fill(&peer->budget, session->now_ms);
    if (!mp_budget_bucket_allows(&peer->budget,
                                 MP_SESSION_PAYLOAD_HEADER + MP_CHANNEL_HEADER_BYTES + due)) {
        ++peer->held_by_budget;
        return;
    }
    send_connected_packet(session, peer, true);
}

/* A handshake nobody answered. A client inside the window of its first connect starts another
 * with a fresh salt, because the host may not have opened its lobby yet; anyone else frees the
 * slot. */
static void handshake_timed_out(mp_session_t *session, mp_peer_t *peer)
{
    if (rejoin_open(session)) {
        begin_handshake(session, peer, session->host_endpoint);
        return;
    }
    release_peer(session, peer);
    if (session->role == MP_SESSION_CLIENT) {
        session->connect_wanted = false;
    }
}

/* A connected peer: the timeout, the expiry of a pending handshake, and the send. A packet with a
 * payload leaves now, every time, because the payload is this substep's state and a gate would
 * let the next substep overwrite it unsent. An empty packet is a keepalive and waits for silence,
 * so a caller servicing between substeps does not multiply the packet rate. */
static void service_connected(mp_session_t *session, mp_peer_t *peer, uint32_t idle,
                              uint32_t since_send)
{
    if (idle >= MP_SESSION_CONNECTED_TIMEOUT_MS) {
        ++session->drops;
        lose_peer(session, peer);
        return;
    }
    /* What was held for the peer goes to its channel first, so an empty hold and an empty
     * channel mean the same thing to everything below. */
    mp_session_hold_flush(session, peer);
    if (mp_session_hold_overdue(session, peer)) {
        return;
    }
    /* Reliable traffic paces itself when there is no payload: in a lobby a queued message would
     * otherwise leave only with the keepalive, ten times a second, and a series of them crawled.
     * The watch after the send is the other half of the same question, asked in a level: whether
     * a message that does ride beside a payload ever finds a seat there. */
    if (peer->out_payload_bytes != 0u || since_send >= MP_SESSION_KEEPALIVE_MS ||
        (mp_channel_send_pending(&peer->channel) != 0u &&
         since_send >= MP_SESSION_RELIABLE_PACE_MS)) {
        bool with_payload = peer->out_payload_bytes != 0u;

        send_connected_packet(session, peer, false);
        mp_session_watch_seat_wait(session, peer);
        if (with_payload && mp_channel_left_behind(&peer->channel)) {
            send_overflow_packet(session, peer);
        }
    }
}

static void service_peer(mp_session_t *session, mp_peer_t *peer)
{
    uint32_t idle = session->now_ms - peer->last_recv_ms;
    uint32_t since_send = session->now_ms - peer->last_send_ms;

    switch (peer->state) {
    case MP_PEER_CONNECTING:
        if (idle >= MP_SESSION_HANDSHAKE_TIMEOUT_MS) {
            handshake_timed_out(session, peer);
        } else if (since_send >= MP_SESSION_RESEND_MS) {
            mp_session_send_request(session, peer);
        }
        break;
    case MP_PEER_CHALLENGED:
        if (idle >= MP_SESSION_HANDSHAKE_TIMEOUT_MS) {
            handshake_timed_out(session, peer);   /* half-open, on either side */
        } else if (session->role == MP_SESSION_CLIENT && since_send >= MP_SESSION_RESEND_MS) {
            mp_session_send_response(session, peer);
        }
        break;
    case MP_PEER_CONNECTED:
        service_connected(session, peer, idle, since_send);
        break;
    case MP_PEER_FREE:
    default:
        break;
    }
}

void mp_session_init(mp_session_t *session, mp_session_role_t role, const mp_transport_t *transport,
                     uint32_t seed)
{
    size_t at;

    memset(session, 0, sizeof(*session));
    session->role      = role;
    session->transport = transport;
    session->rng       = (seed != 0) ? seed : 0x1234567u;
    /* The cookie's secret, fresh for every session, so a cookie from the last one is nothing. */
    for (at = 0; at + 8u <= sizeof session->secret; at += 8u) {
        uint64_t part = mp_session_next_salt(session);

        memcpy(session->secret + at, &part, sizeof part);
    }
}

void mp_session_connect(mp_session_t *session, uint32_t endpoint)
{
    mp_peer_t *peer = &session->peers[0];

    if (session->role != MP_SESSION_CLIENT || peer->state == MP_PEER_CONNECTED) {
        return;
    }
    session->rejoin_until_ms = session->now_ms + MP_SESSION_JOIN_WINDOW_MS; /* a window too */
    session->last_deny = MP_DENY_NONE;   /* a refusal belongs to the request it answered */
    session->deny_detail_bytes = 0u;
    begin_handshake(session, peer, endpoint);
}

void mp_session_disconnect(mp_session_t *session)
{
    size_t i;

    for (i = 0; i < MP_SESSION_MAX_PEERS; ++i) {
        mp_peer_t *peer = &session->peers[i];

        if (peer->state == MP_PEER_CONNECTED) {
            mp_session_send_bye(session, peer);
        }
        if (peer->state != MP_PEER_FREE) {
            release_peer(session, peer);
        }
    }
    session->connect_wanted  = false;
    session->rejoin_until_ms = session->now_ms;
}

bool mp_session_drop(mp_session_t *session, size_t index, mp_deny_reason_t reason)
{
    mp_peer_t *peer;

    if (session == NULL || session->role != MP_SESSION_HOST || index >= MP_SESSION_MAX_PEERS) {
        return false;
    }
    peer = &session->peers[index];
    if (peer->state != MP_PEER_CONNECTED) {
        return false;
    }
    /* A denial rather than a leave notice: the client keeps a denial's reason as the answer to
     * its join, where a leave reads as the host going away. */
    mp_session_send_deny(session, peer->endpoint, peer->client_salt, reason);
    release_peer(session, peer);
    return true;
}

/* The same exit as a drop, with the notice twice because nothing repeats it otherwise, and the
 * connection remembered so its later packets can be told once more. Its own count: a peer that
 * fell behind was admitted and played, and a denial counts refused requests. */
void mp_session_send_away_behind(mp_session_t *session, mp_peer_t *peer)
{
    if (session->role != MP_SESSION_HOST || peer->state != MP_PEER_CONNECTED) {
        return;
    }
    mp_session_send_notice(session, peer->endpoint, peer->client_salt, MP_DENY_BEHIND);
    mp_session_send_notice(session, peer->endpoint, peer->client_salt, MP_DENY_BEHIND);
    mp_session_hold_remember(session, peer);
    peer->dropped_behind = true;
    ++session->dropped_behind;
    release_peer(session, peer);
}

void mp_session_receive(mp_session_t *session, uint32_t now_ms)
{
    uint8_t  packet[MP_CHANNEL_PACKET_BYTES];
    uint32_t from = 0;
    size_t   size;

    session->now_ms = now_ms;
    while ((size = mp_transport_recv(session->transport, &from, packet, sizeof packet)) > 0) {
        dispatch(session, from, packet, size);
    }
}

void mp_session_service(mp_session_t *session, uint32_t now_ms)
{
    size_t i;

    session->now_ms = now_ms;
    for (i = 0; i < MP_SESSION_MAX_PEERS; ++i) {
        if (session->peers[i].state != MP_PEER_FREE) {
            service_peer(session, &session->peers[i]);
        }
    }
}

void mp_session_update(mp_session_t *session, uint32_t now_ms)
{
    mp_session_receive(session, now_ms);
    mp_session_service(session, now_ms);
}
