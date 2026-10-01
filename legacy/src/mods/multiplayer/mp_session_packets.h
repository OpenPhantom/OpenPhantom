/* mp_session_packets.h: the handshake packets of the session, built and read.
 *
 * Internal to the session: mp_session.c, mp_session_packets.c and mp_session_hold.c include it
 * and nothing else does. The magic and the packet types live here because they write or read
 * them.
 */
#ifndef MULTIPLAYER_MP_SESSION_PACKETS_H
#define MULTIPLAYER_MP_SESSION_PACKETS_H

#include "mp_session.h"
#include "mp_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MP_SESSION_MAGIC 0x4F504D53u    /* not the channel's, so the two never mistake a packet */

enum {
    PKT_REQUEST = 1,
    PKT_CHALLENGE = 2,
    PKT_RESPONSE = 3,
    PKT_ACCEPT = 4,
    PKT_DENY = 5,
    PKT_PAYLOAD = 6,
    PKT_BYE = 7,
    /* A bulk note: the same envelope as a payload and nothing else. It carries no sequence and no
     * acknowledgement, because what rides here answers for itself one layer up. It is refused
     * from anyone whose connection id does not match, exactly like a payload, so the anti
     * amplification promise at the head of mp_session.c is untouched: nothing is answered, and
     * nothing is sent to an address that has not completed the handshake. */
    PKT_BULK = 8
};

/* A fresh 64-bit salt from the session's own generator. */
uint64_t mp_session_next_salt(mp_session_t *session);

/* A u64 as two little-endian u32, high half first, which is how every salt and token travels. */
bool mp_session_put_u64(mp_wire_writer_t *w, uint64_t v);
bool mp_session_get_u64(mp_wire_reader_t *r, uint64_t *v);

/* The sixteen byte name field of a request, a response or an accept, cleaned and terminated on
 * this side whatever a stranger sent. */
void mp_session_get_name(mp_wire_reader_t *r, char out[MP_SESSION_NAME_MAX]);

/* Raw bytes, for the cookie and the proof. Zeros where the packet ran short. */
void mp_session_get_bytes(mp_wire_reader_t *r, uint8_t *out, size_t bytes);

/* The proof a client gives that it knows the host's password: HMAC-SHA256 over a fixed label and
 * both salts under the password as typed. Both ends compute it, one to send and one to compare.
 * False when the provider refused. */
bool mp_session_proof(const char *password, uint64_t client_salt, uint64_t server_salt,
                      uint8_t out[MP_SESSION_PROOF_BYTES]);

/* Hands finished bytes to the transport. */
void mp_session_send_bytes(mp_session_t *session, uint32_t endpoint, const uint8_t *bytes,
                           size_t len);

/* The six handshake packets. A request and a response are padded to MP_SESSION_REQUEST_BYTES, so
 * no reply is ever larger than what drew it. */
void mp_session_send_request(mp_session_t *session, mp_peer_t *peer);
void mp_session_send_challenge(mp_session_t *session, uint32_t endpoint, uint64_t client_salt,
                               uint64_t server_salt,
                               const uint8_t cookie[MP_SESSION_COOKIE_BYTES]);
void mp_session_send_response(mp_session_t *session, mp_peer_t *peer);
void mp_session_send_accept(mp_session_t *session, uint32_t endpoint, uint64_t connection_id);
void mp_session_send_deny(mp_session_t *session, uint32_t endpoint, uint64_t client_salt,
                          mp_deny_reason_t reason);

/* A denial with the judge's detail behind the reason, at most MP_SESSION_DENY_DETAIL_BYTES of it:
 * the packet stays far smaller than the request that drew it. */
void mp_session_send_deny_detail(mp_session_t *session, uint32_t endpoint, uint64_t client_salt,
                                 mp_deny_reason_t reason, const uint8_t *detail,
                                 size_t detail_bytes);

/* The same packet without counting it as a refused request: what a host sends a peer it is
 * sending away after having admitted it. */
void mp_session_send_notice(mp_session_t *session, uint32_t endpoint, uint64_t client_salt,
                            mp_deny_reason_t reason);
void mp_session_send_bye(mp_session_t *session, mp_peer_t *peer);

#endif /* MULTIPLAYER_MP_SESSION_PACKETS_H */
