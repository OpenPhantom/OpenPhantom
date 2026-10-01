/* mp_session_packets.c: the handshake's wire format, written and read.
 *
 * Every function here reads a session's identity fields, a peer and a writer or a reader, and
 * nothing of the handshake's state. WHEN a packet goes out and what an arriving one means is the
 * state machine in mp_session.c, which calls these. The two files share the magic and the packet
 * types through mp_session_packets.h.
 */
#include "mp_session_packets.h"

#include "mp_entropy.h"
#include "mp_roster.h"
#include "mp_session_bulk.h"
#include "mp_session_meter.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* xorshift, seeded, not cryptographic: the fallback for a system whose generator would not
 * answer, and nothing else. Two of its outputs are one state and a function of it. */
static uint32_t next_random(mp_session_t *session)
{
    uint32_t x = session->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    session->rng = x;
    return x;
}

/* From the system's cryptographic generator. The model this was written for, a spoofer who
 * cannot see the traffic, missed that a fellow player sees his own challenge whenever he likes:
 * with the seeded generator one server salt was its whole state, so he could compute the next
 * one and finish a handshake from somebody else's address. A zero is drawn again, because the
 * connection id is the two salts together and nothing may make it look unset. */
uint64_t mp_session_next_salt(mp_session_t *session)
{
    uint64_t salt = 0;
    int      tries;

    for (tries = 0; tries < 2; ++tries) {
        if (mp_entropy_fill(&salt, sizeof salt) && salt != 0u) {
            return salt;
        }
    }
    {
        uint64_t hi = next_random(session);
        uint64_t lo = next_random(session);
        return (hi << 32) | lo;
    }
}

bool mp_session_put_u64(mp_wire_writer_t *w, uint64_t v)
{
    return mp_wire_put_u32(w, (uint32_t)(v >> 32)) && mp_wire_put_u32(w, (uint32_t)v);
}

bool mp_session_get_u64(mp_wire_reader_t *r, uint64_t *v)
{
    uint32_t hi = 0;
    uint32_t lo = 0;

    if (!mp_wire_get_u32(r, &hi) || !mp_wire_get_u32(r, &lo)) {
        return false;
    }
    *v = ((uint64_t)hi << 32) | lo;
    return true;
}

/* Zero-fill a writer out to `size`, so a request and a response are always the same length however
 * few bytes they carry, which is what makes them useless as an amplifier. */
static void pad_to(mp_wire_writer_t *w, size_t size)
{
    while (w->at < size && !w->overflowed) {
        mp_wire_put_u8(w, 0u);
    }
}

/* The one way a datagram leaves a session, so it is also where the upload is measured: the
 * session's, and the peer's when the address is one this session holds. */
void mp_session_send_bytes(mp_session_t *session, uint32_t endpoint, const uint8_t *bytes,
                           size_t len)
{
    mp_peer_t *peer = mp_session_peer_at(session, endpoint);

    mp_transport_send(session->transport, endpoint, bytes, len);
    mp_meter_second_add(&session->upload, len, session->now_ms);
    if (peer != NULL) {
        mp_meter_second_add(&peer->meter.second, len, session->now_ms);
    }
}

/* Sixteen bytes, the cleaned name and zeros behind it; an unset one is the default. */
static void put_name(mp_wire_writer_t *w, const char *name)
{
    char   clean[MP_SESSION_NAME_MAX];
    size_t i;

    mp_roster_name_clean(name, clean);
    for (i = 0; i < MP_SESSION_NAME_MAX; ++i) {
        mp_wire_put_u8(w, (uint8_t)clean[i]);
    }
}

/* The sixteen bytes back, cleaned again on this side, because a stranger wrote them. A packet
 * that ends before them, from a build that predates the name, reads as the default. */
void mp_session_get_name(mp_wire_reader_t *r, char out[MP_SESSION_NAME_MAX])
{
    char   raw[MP_SESSION_NAME_MAX];
    size_t i;

    for (i = 0; i < MP_SESSION_NAME_MAX; ++i) {
        uint8_t byte = 0;

        mp_wire_get_u8(r, &byte);
        raw[i] = (char)byte;
    }
    raw[MP_SESSION_NAME_MAX - 1u] = '\0';
    mp_roster_name_clean(r->overran ? NULL : raw, out);
}

void mp_session_get_bytes(mp_wire_reader_t *r, uint8_t *out, size_t bytes)
{
    size_t i;

    for (i = 0; i < bytes; ++i) {
        out[i] = 0u;
        mp_wire_get_u8(r, &out[i]);
    }
}

static void put_bytes(mp_wire_writer_t *w, const uint8_t *bytes, size_t count)
{
    size_t i;

    for (i = 0; i < count; ++i) {
        mp_wire_put_u8(w, bytes[i]);
    }
}

/* The label keeps a proof from ever being a digest somebody else's protocol asks for under the
 * same password; the salts make it this handshake's and no other's. The password is taken as
 * typed, up to its field, and a password nobody set is the empty key. */
bool mp_session_proof(const char *password, uint64_t client_salt, uint64_t server_salt,
                      uint8_t out[MP_SESSION_PROOF_BYTES])
{
    uint8_t          message[8 + 8 + 8];
    mp_wire_writer_t w;
    size_t           length = 0;

    while (password != NULL && length < MP_SESSION_PASSWORD_MAX && password[length] != '\0') {
        ++length;
    }
    mp_wire_writer_init(&w, message, sizeof message);
    put_bytes(&w, (const uint8_t *)"OBIJOIN1", 8u);
    mp_session_put_u64(&w, client_salt);
    mp_session_put_u64(&w, server_salt);
    return !w.overflowed &&
           mp_entropy_hmac(password, length, message, sizeof message, out);
}

void mp_session_send_request(mp_session_t *session, mp_peer_t *peer)
{
    uint8_t          buffer[MP_SESSION_REQUEST_BYTES];
    mp_wire_writer_t w;

    mp_wire_writer_init(&w, buffer, sizeof buffer);
    mp_wire_put_u32(&w, MP_SESSION_MAGIC);
    mp_wire_put_u8(&w, PKT_REQUEST);
    mp_session_put_u64(&w, peer->client_salt);
    /* The protocol version rides the request, so two builds that would misread each other's
     * packets are told apart before either allocates a channel. A build from before it padded
     * zeros here, so it reads as protocol 0 and is denied. The word behind it held a content
     * fingerprint up to wire 34; it stays, as zero, so everything behind it keeps its place. */
    mp_wire_put_u32(&w, MP_WIRE_VERSION);
    mp_wire_put_u32(&w, 0u);
    /* The mode rides here, inside the padding that was already being sent, so the request keeps its
     * fixed size and the rule that a reply is never larger than the request is untouched. */
    mp_wire_put_u8(&w, session->mode);
    /* And the name, in sixteen bytes of the same padding. No password: it used to follow here in
     * the clear, and now the response proves it instead (mp_session_proof). */
    put_name(&w, session->name);
    /* Then what this side plays with, a length and the bytes, in the same padding. */
    mp_wire_put_u16(&w, session->statement_bytes);
    put_bytes(&w, session->statement, session->statement_bytes);
    pad_to(&w, MP_SESSION_REQUEST_BYTES);
    peer->last_send_ms = session->now_ms;
    mp_session_send_bytes(session, peer->endpoint, buffer, w.at);
}

/* The challenge names the two salts it is about and the cookie made for them: thirty seven bytes
 * against a thousand and twenty four of request. */
void mp_session_send_challenge(mp_session_t *session, uint32_t endpoint, uint64_t client_salt,
                               uint64_t server_salt,
                               const uint8_t cookie[MP_SESSION_COOKIE_BYTES])
{
    uint8_t          buffer[48];
    mp_wire_writer_t w;

    mp_wire_writer_init(&w, buffer, sizeof buffer);
    mp_wire_put_u32(&w, MP_SESSION_MAGIC);
    mp_wire_put_u8(&w, PKT_CHALLENGE);
    mp_session_put_u64(&w, client_salt);
    mp_session_put_u64(&w, server_salt);
    put_bytes(&w, cookie, MP_SESSION_COOKIE_BYTES);
    mp_session_send_bytes(session, endpoint, buffer, w.at);
}

void mp_session_send_response(mp_session_t *session, mp_peer_t *peer)
{
    uint8_t          buffer[MP_SESSION_REQUEST_BYTES];
    mp_wire_writer_t w;

    mp_wire_writer_init(&w, buffer, sizeof buffer);
    mp_wire_put_u32(&w, MP_SESSION_MAGIC);
    mp_wire_put_u8(&w, PKT_RESPONSE);
    /* Both salts, the host's cookie back and the proof, so the host can check all of it with
     * nothing kept since the challenge; and the name again, because the slot it names is only
     * made now. A proof the provider would not make goes out as zeros and is refused by a host
     * with a password, which is what it is. */
    mp_session_put_u64(&w, peer->client_salt);
    mp_session_put_u64(&w, peer->server_salt);
    put_bytes(&w, peer->cookie, MP_SESSION_COOKIE_BYTES);
    {
        uint8_t proof[MP_SESSION_PROOF_BYTES];

        if (!mp_session_proof(session->password, peer->client_salt, peer->server_salt, proof)) {
            memset(proof, 0, sizeof proof);
            ++session->digest_faults;
        }
        put_bytes(&w, proof, sizeof proof);
    }
    put_name(&w, session->name);
    pad_to(&w, MP_SESSION_REQUEST_BYTES);
    peer->last_send_ms = session->now_ms;
    mp_session_send_bytes(session, peer->endpoint, buffer, w.at);
}

static void send_simple(mp_session_t *session, uint32_t endpoint, uint8_t type, uint64_t value)
{
    uint8_t          buffer[16];
    mp_wire_writer_t w;

    mp_wire_writer_init(&w, buffer, sizeof buffer);
    mp_wire_put_u32(&w, MP_SESSION_MAGIC);
    mp_wire_put_u8(&w, type);
    mp_session_put_u64(&w, value);
    mp_session_send_bytes(session, endpoint, buffer, w.at);
}

/* The accept, which carries the host's name behind the token: twenty nine bytes against a
 * thousand and twenty four of request, so the reply is still far smaller than what drew it. */
void mp_session_send_accept(mp_session_t *session, uint32_t endpoint, uint64_t connection_id)
{
    uint8_t          buffer[16 + MP_SESSION_NAME_MAX];
    mp_wire_writer_t w;

    mp_wire_writer_init(&w, buffer, sizeof buffer);
    mp_wire_put_u32(&w, MP_SESSION_MAGIC);
    mp_wire_put_u8(&w, PKT_ACCEPT);
    mp_session_put_u64(&w, connection_id);
    put_name(&w, session->name);
    mp_session_send_bytes(session, endpoint, buffer, w.at);
}

/* A denial carries the client's own salt back and then the reason, so the joining side can say why
 * it was refused rather than only that it was; a judge's reason carries its detail behind it. */
#define DENY_HEAD_BYTES 14u

_Static_assert(DENY_HEAD_BYTES + MP_SESSION_DENY_DETAIL_BYTES < MP_SESSION_REQUEST_BYTES,
               "a denial with its detail would no longer be smaller than the request that drew it, "
               "and the port could be made to answer a small forged packet with a larger one");
_Static_assert(4u + 1u + 8u + 4u + 4u + 1u + MP_SESSION_NAME_MAX + 2u +
                   MP_SESSION_STATEMENT_BYTES <= MP_SESSION_REQUEST_BYTES,
               "the statement no longer fits the request's padding");

static void send_notice_with(mp_session_t *session, uint32_t endpoint, uint64_t client_salt,
                             mp_deny_reason_t reason, const uint8_t *detail, size_t detail_bytes)
{
    uint8_t          buffer[DENY_HEAD_BYTES + MP_SESSION_DENY_DETAIL_BYTES];
    mp_wire_writer_t w;

    if (detail == NULL || detail_bytes > MP_SESSION_DENY_DETAIL_BYTES) {
        detail_bytes = 0u;
    }
    mp_wire_writer_init(&w, buffer, sizeof buffer);
    mp_wire_put_u32(&w, MP_SESSION_MAGIC);
    mp_wire_put_u8(&w, PKT_DENY);
    mp_session_put_u64(&w, client_salt);
    mp_wire_put_u8(&w, (uint8_t)reason);
    if (detail_bytes != 0u) {
        put_bytes(&w, detail, detail_bytes);
    }
    mp_session_send_bytes(session, endpoint, buffer, w.at);
}

void mp_session_send_notice(mp_session_t *session, uint32_t endpoint, uint64_t client_salt,
                            mp_deny_reason_t reason)
{
    send_notice_with(session, endpoint, client_salt, reason, NULL, 0u);
}

void mp_session_send_deny(mp_session_t *session, uint32_t endpoint, uint64_t client_salt,
                      mp_deny_reason_t reason)
{
    mp_session_send_deny_detail(session, endpoint, client_salt, reason, NULL, 0u);
}

void mp_session_send_deny_detail(mp_session_t *session, uint32_t endpoint, uint64_t client_salt,
                                 mp_deny_reason_t reason, const uint8_t *detail,
                                 size_t detail_bytes)
{
    send_notice_with(session, endpoint, client_salt, reason, detail, detail_bytes);
    ++session->denied;
}


/* The leave notice: sent twice because it is the one packet with no retransmission behind it, and
 * a peer that misses both only waits out the connected timeout as before. */
void mp_session_send_bye(mp_session_t *session, mp_peer_t *peer)
{
    send_simple(session, peer->endpoint, PKT_BYE, peer->connection_id);
    send_simple(session, peer->endpoint, PKT_BYE, peer->connection_id);
}
