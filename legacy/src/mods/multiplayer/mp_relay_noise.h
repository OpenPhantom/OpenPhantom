/* mp_relay_noise.h: the relay handshake, the legs it leaves behind, and the two proofs.
 *
 * Layer 1, pure. A PC opens a leg to the relay with the Noise NK pattern
 * (Noise_NK_25519_ChaChaPoly_SHA256): it knows the relay's static key from the key document, sends
 * its ephemeral key and a sealed body, and the relay answers with its own ephemeral key and a
 * sealed body. Both sides then hold a key for each direction, and a leg is those two keys with a
 * send counter and a window of the counters that arrived.
 *
 * Four rules keep a leg honest, and each is a place where the code below does one thing and not
 * the obvious other:
 *   - An answer that does not open leaves the handshake exactly as it was, so a forged answer
 *     cannot end a handshake the real one would still finish (mp_relay_nk_read2 works on copies).
 *   - A leg's keys never change and its counter never goes back: a new handshake makes a new leg.
 *   - A counter is taken into the window only after its packet opened, so a forged packet cannot
 *     spend a counter the relay is about to use.
 *   - The nonce is four zero bytes and the counter little endian (mp_relay_nonce_of).
 */
#ifndef MULTIPLAYER_MP_RELAY_NOISE_H
#define MULTIPLAYER_MP_RELAY_NOISE_H

#include "mp_relay_crypto.h"
#include "mp_relay_inner.h"
#include "mp_relay_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* What a finished handshake leaves: a key for each direction and the handshake hash. */
typedef struct mp_relay_keys {
    uint8_t initiator_to_responder[MP_RELAY_KEY_BYTES];
    uint8_t responder_to_initiator[MP_RELAY_KEY_BYTES];
    uint8_t hash[MP_RELAY_HASH_BYTES];
} mp_relay_keys_t;

/* The initiator's side of one handshake. */
typedef struct mp_relay_nk {
    uint8_t h[MP_RELAY_HASH_BYTES];
    uint8_t ck[MP_RELAY_HASH_BYTES];
    uint8_t e_private[MP_RELAY_KEY_BYTES];
    uint8_t e_public[MP_RELAY_KEY_BYTES];
    uint8_t rs[MP_RELAY_KEY_BYTES];
    uint8_t step;   /* 0 begun, 1 the request is written, 2 finished */
} mp_relay_nk_t;

/* Begins a handshake bound to `prologue` against the relay's static key `rs`, with `e_private`
 * as the ephemeral key: random bytes in play, a fixed key in a test. */
bool mp_relay_nk_begin(mp_relay_nk_t *nk, const uint8_t prologue[MP_RELAY_PROLOGUE_BYTES],
                       const uint8_t rs[MP_RELAY_KEY_BYTES],
                       const uint8_t e_private[MP_RELAY_KEY_BYTES]);

/* Writes "e, es" and the body sealed under es: 32 + body + 16 bytes at `out`. 0 on a refusal. */
size_t mp_relay_nk_write1(mp_relay_nk_t *nk, const uint8_t *body, size_t body_bytes, uint8_t *out,
                          size_t capacity);

/* Reads "e, ee" and the sealed body out of an answer from its ephemeral key on, and finishes the
 * handshake. False when the answer does not open, and then `nk` is as it was before the call. */
bool mp_relay_nk_read2(mp_relay_nk_t *nk, const uint8_t *answer, size_t bytes, uint8_t *body,
                       size_t body_capacity, size_t *body_bytes, mp_relay_keys_t *keys);

void mp_relay_nk_wipe(mp_relay_nk_t *nk);

/* A whole Register or Join: the head of `type` for `cookie`, `nonce` and the relay key `key_id`,
 * then the handshake's first message carrying `body`. `nk` is left waiting for the answer. The
 * request is MP_RELAY_REQUEST_BYTES; 0 on a refusal. */
size_t mp_relay_build_request(uint8_t type, const mp_relay_cookie_t *cookie, uint64_t nonce,
                              uint8_t key_id, const uint8_t relay_public[MP_RELAY_KEY_BYTES],
                              const uint8_t *body, size_t body_bytes,
                              const uint8_t e_private[MP_RELAY_KEY_BYTES], mp_relay_nk_t *nk,
                              uint8_t *out, size_t capacity);

/* ==============================================================================================
 * The replay window: RFC 6479, 16 words of 64 bits. The first counter is taken whatever it is, a
 * counter above the newest moves the window, and one 960 or more behind the newest is refused.
 * ============================================================================================ */

#define MP_RELAY_WINDOW_WORDS 16u
#define MP_RELAY_WINDOW_SPAN  960u

typedef struct mp_relay_window {
    uint64_t top;
    bool     seen;
    uint64_t bits[MP_RELAY_WINDOW_WORDS];
} mp_relay_window_t;

bool mp_relay_window_check(const mp_relay_window_t *window, uint64_t counter);
bool mp_relay_window_accept(mp_relay_window_t *window, uint64_t counter);

/* ==============================================================================================
 * The leg.
 * ============================================================================================ */

typedef struct mp_relay_leg {
    bool              live;
    uint8_t           send_key[MP_RELAY_KEY_BYTES];
    uint8_t           receive_key[MP_RELAY_KEY_BYTES];
    uint64_t          next;      /* the counter the next sealed packet takes */
    uint64_t          failed;    /* packets that did not authenticate */
    mp_relay_window_t window;
} mp_relay_leg_t;

/* The initiator's leg out of a finished handshake: it sends under the initiator-to-responder key
 * and receives under the other. The keys in `keys` are copied, not kept. */
void mp_relay_leg_init(mp_relay_leg_t *leg, const mp_relay_keys_t *keys);

/* Seals `plain` behind `header` under the leg's next counter, which it writes into the header.
 * Answers the datagram's size, 32 more than `bytes`, or 0 when it does not fit. */
size_t mp_relay_leg_seal(mp_relay_leg_t *leg, const mp_relay_sealed_header_t *header,
                         const uint8_t *plain, size_t bytes, uint8_t *out, size_t capacity);

/* Opens a sealed datagram into `plain`: the window first, then the tag with the received header
 * as associated data, then the window again, taking the counter. The caller has already checked
 * the header's type and index. */
bool mp_relay_leg_open(mp_relay_leg_t *leg, const uint8_t *datagram, size_t bytes, uint8_t *plain,
                       size_t capacity, size_t *plain_bytes);

void mp_relay_leg_wipe(mp_relay_leg_t *leg);

/* ==============================================================================================
 * The proofs. MAC16 is the first 16 bytes of HMAC-SHA-256 over "MPRLMAC1" and the message. A
 * resume proof binds a host's secret to a fresh cookie and the relay key the handshake runs
 * under; a seat proof does the same with a player's seat secret.
 * ============================================================================================ */

bool mp_relay_mac16(const uint8_t key[MP_RELAY_SECRET_BYTES], const uint8_t *message, size_t bytes,
                    uint8_t out[MP_RELAY_PROOF_BYTES]);
bool mp_relay_resume_proof(const uint8_t host_secret[MP_RELAY_SECRET_BYTES], uint32_t epoch,
                           const uint8_t cookie[MP_RELAY_COOKIE_MAC_BYTES], uint64_t nonce,
                           uint8_t key_id, uint8_t out[MP_RELAY_PROOF_BYTES]);
bool mp_relay_seat_proof(const uint8_t seat_secret[MP_RELAY_SECRET_BYTES], uint32_t epoch,
                         const uint8_t cookie[MP_RELAY_COOKIE_MAC_BYTES], uint64_t nonce,
                         uint8_t key_id, uint8_t out[MP_RELAY_PROOF_BYTES]);

#endif /* MULTIPLAYER_MP_RELAY_NOISE_H */
