/* mp_entropy.h: what the session takes from the system's cryptography: random bytes, and
 * HMAC-SHA256.
 *
 * Layer 0. The session's salts are what a stranger has to guess to finish a handshake from an
 * address that is not his, so they come from here and not from a seeded generator whose next
 * output anybody who has seen one can compute. The handshake's two keyed digests come from here
 * as well, the cookie a host hands a joining address instead of a slot and the proof a client
 * gives instead of its password: HMAC-SHA256 (RFC 2104 over FIPS 180-4) from the same provider,
 * which ships with every Windows this game runs on and is delay loaded with the first handshake.
 */
#ifndef MULTIPLAYER_MP_ENTROPY_H
#define MULTIPLAYER_MP_ENTROPY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Fills `out` with `bytes` bytes of the system's generator. False when it would not, and then
 * `out` holds nothing the caller may use; the caller decides what to fall back to. */
bool mp_entropy_fill(void *out, size_t bytes);

/* How many fills the system refused since the process began. Zero on every Windows this runs on
 * as far as anybody has seen, which is exactly why it is counted rather than assumed. */
uint32_t mp_entropy_failures(void);

#define MP_ENTROPY_HMAC_BYTES 32u

/* HMAC-SHA256 of `message` under `key`. An empty key is handed to the provider as one zero byte,
 * which is the same key, because HMAC pads every key shorter than a block with zeros; the
 * provider is not asked to take a secret of no length. False when the provider refused, which
 * is counted with the fills above. */
bool mp_entropy_hmac(const void *key, size_t key_bytes, const void *message,
                     size_t message_bytes, uint8_t out[MP_ENTROPY_HMAC_BYTES]);

/* Whether two digests agree, in a time that does not depend on where they first differ. */
bool mp_entropy_equal(const uint8_t *a, const uint8_t *b, size_t bytes);

#endif /* MULTIPLAYER_MP_ENTROPY_H */
