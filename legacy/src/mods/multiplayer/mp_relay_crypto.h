/* mp_relay_crypto.h: the primitives the relay protocol is made of.
 *
 * Layer 0. X25519, ChaCha20-Poly1305 and SHA-256 are HACL*'s, formally verified and unchanged
 * (src/third_party/hacl). HMAC-SHA-256 and the two-output HKDF the Noise framework uses are a few
 * lines here over that SHA-256, because every input the protocol hands them is short and bounded,
 * and HACL*'s own HMAC would link every hash it has. Randomness is the system's (mp_entropy).
 *
 * The relay protocol never needs more than these, and nothing above this file names HACL*.
 */
#ifndef MULTIPLAYER_MP_RELAY_CRYPTO_H
#define MULTIPLAYER_MP_RELAY_CRYPTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MP_RELAY_KEY_BYTES   32u
#define MP_RELAY_HASH_BYTES  32u
#define MP_RELAY_TAG_BYTES   16u
#define MP_RELAY_NONCE_BYTES 12u

/* The longest message the hash and the MAC take in one piece. The protocol's longest is a mixed
 * hash of 32 bytes of state and an 80 byte ciphertext; a caller past this is refused, not
 * truncated. */
#define MP_RELAY_HASH_INPUT_MAX 256u

/* X25519 of a private key and the base point, with the private key clamped as RFC 7748 says. */
void mp_relay_x25519_public(uint8_t out[MP_RELAY_KEY_BYTES],
                            const uint8_t priv[MP_RELAY_KEY_BYTES]);

/* X25519 of a private key and a peer's public key. False when the result is all zero, which a
 * point of small order produces; the comparison takes the same time whatever the result is. */
bool mp_relay_x25519_shared(uint8_t out[MP_RELAY_KEY_BYTES], const uint8_t priv[MP_RELAY_KEY_BYTES],
                            const uint8_t peer[MP_RELAY_KEY_BYTES]);

/* SHA-256 of the two pieces one after the other; either may be empty. False past
 * MP_RELAY_HASH_INPUT_MAX. */
bool mp_relay_sha256(uint8_t out[MP_RELAY_HASH_BYTES], const uint8_t *a, size_t a_bytes,
                     const uint8_t *b, size_t b_bytes);

/* SHA-256 of one piece of any length. */
void mp_relay_sha256_of(uint8_t out[MP_RELAY_HASH_BYTES], const uint8_t *data, size_t bytes);

/* HMAC-SHA-256 (RFC 2104) of the two pieces one after the other under a key of at most 64 bytes.
 * False for a longer key or a message past MP_RELAY_HASH_INPUT_MAX. */
bool mp_relay_hmac_sha256(uint8_t out[MP_RELAY_HASH_BYTES], const uint8_t *key, size_t key_bytes,
                          const uint8_t *a, size_t a_bytes, const uint8_t *b, size_t b_bytes);

/* The Noise framework's HKDF with two outputs: HMAC-SHA-256 extract with the chaining key as
 * salt, then expand with empty info. The two outputs are RFC 5869's T(1) and T(2). */
bool mp_relay_hkdf2(uint8_t out1[MP_RELAY_HASH_BYTES], uint8_t out2[MP_RELAY_HASH_BYTES],
                    const uint8_t chaining_key[MP_RELAY_HASH_BYTES], const uint8_t *ikm,
                    size_t ikm_bytes);

/* ChaCha20-Poly1305 (RFC 8439) under a full 12 byte nonce. `out` may be `plain`. */
void mp_relay_aead_seal_nonce(uint8_t *out, uint8_t tag[MP_RELAY_TAG_BYTES],
                              const uint8_t key[MP_RELAY_KEY_BYTES],
                              const uint8_t nonce[MP_RELAY_NONCE_BYTES], const uint8_t *ad,
                              size_t ad_bytes, const uint8_t *plain, size_t bytes);

/* The opening half. False when the tag does not authenticate, and then `out` is untouched. */
bool mp_relay_aead_open_nonce(uint8_t *out, const uint8_t key[MP_RELAY_KEY_BYTES],
                              const uint8_t nonce[MP_RELAY_NONCE_BYTES], const uint8_t *ad,
                              size_t ad_bytes, const uint8_t *cipher, size_t bytes,
                              const uint8_t tag[MP_RELAY_TAG_BYTES]);

/* The nonce the protocol uses for a counter: four zero bytes, then the counter little endian. A
 * big endian counter would open exactly the first packet, the one with counter zero. */
void mp_relay_nonce_of(uint8_t nonce[MP_RELAY_NONCE_BYTES], uint64_t counter);

void mp_relay_aead_seal(uint8_t *out, uint8_t tag[MP_RELAY_TAG_BYTES],
                        const uint8_t key[MP_RELAY_KEY_BYTES], uint64_t counter, const uint8_t *ad,
                        size_t ad_bytes, const uint8_t *plain, size_t bytes);
bool mp_relay_aead_open(uint8_t *out, const uint8_t key[MP_RELAY_KEY_BYTES], uint64_t counter,
                        const uint8_t *ad, size_t ad_bytes, const uint8_t *cipher, size_t bytes,
                        const uint8_t tag[MP_RELAY_TAG_BYTES]);

/* Bytes from the system's generator. False when it refused, and `out` must not be used. */
bool mp_relay_random(uint8_t *out, size_t bytes);

/* Overwrites a secret in a way the compiler does not remove. */
void mp_relay_wipe(void *secret, size_t bytes);

/* Whether two byte strings are equal, in a time that does not depend on where they differ. */
bool mp_relay_equal(const uint8_t *a, const uint8_t *b, size_t bytes);

/* Runs every primitive against a published vector: RFC 7748 section 6.1 with a point of small
 * order refused, RFC 8439 section 2.8.2 with a forged tag refused, RFC 4231 cases 1 and 2,
 * RFC 5869 case A.3 and SHA-256 of "abc". NULL when all of them pass, otherwise the name of the
 * first one that did not. Public mode runs it once before anything is sent. */
const char *mp_relay_crypto_self_test(void);

#endif /* MULTIPLAYER_MP_RELAY_CRYPTO_H */
