/* mp_relay_crypto.c: the primitives the relay protocol is made of. See the header. */
#include "mp_relay_crypto.h"

#include "mp_entropy.h"

#include "Hacl_AEAD_Chacha20Poly1305.h"
#include "Hacl_Curve25519_51.h"
#include "Hacl_Hash_SHA2.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define HMAC_BLOCK_BYTES 64u
#define HMAC_IPAD        0x36u
#define HMAC_OPAD        0x5Cu

/* HACL*'s interfaces take their inputs through pointers to non-const and do not write through
 * them; the casts below say so once each. */

void mp_relay_x25519_public(uint8_t out[MP_RELAY_KEY_BYTES], const uint8_t priv[MP_RELAY_KEY_BYTES])
{
    Hacl_Curve25519_51_secret_to_public(out, (uint8_t *)priv);
}

bool mp_relay_x25519_shared(uint8_t out[MP_RELAY_KEY_BYTES], const uint8_t priv[MP_RELAY_KEY_BYTES],
                            const uint8_t peer[MP_RELAY_KEY_BYTES])
{
    /* HACL*'s own check folds all 32 bytes into one mask before it compares, so it is constant
     * time, and it answers false for the all-zero result. */
    return Hacl_Curve25519_51_ecdh(out, (uint8_t *)priv, (uint8_t *)peer);
}

bool mp_relay_sha256(uint8_t out[MP_RELAY_HASH_BYTES], const uint8_t *a, size_t a_bytes,
                     const uint8_t *b, size_t b_bytes)
{
    uint8_t joined[MP_RELAY_HASH_INPUT_MAX];

    if (a_bytes > MP_RELAY_HASH_INPUT_MAX || b_bytes > MP_RELAY_HASH_INPUT_MAX - a_bytes ||
        (a == NULL && a_bytes != 0u) || (b == NULL && b_bytes != 0u)) {
        return false;
    }
    if (a_bytes != 0u) {
        memcpy(joined, a, a_bytes);
    }
    if (b_bytes != 0u) {
        memcpy(joined + a_bytes, b, b_bytes);
    }
    Hacl_Hash_SHA2_hash_256(out, joined, (uint32_t)(a_bytes + b_bytes));
    mp_relay_wipe(joined, a_bytes + b_bytes);
    return true;
}

void mp_relay_sha256_of(uint8_t out[MP_RELAY_HASH_BYTES], const uint8_t *data, size_t bytes)
{
    Hacl_Hash_SHA2_hash_256(out, (uint8_t *)data, (uint32_t)bytes);
}

bool mp_relay_hmac_sha256(uint8_t out[MP_RELAY_HASH_BYTES], const uint8_t *key, size_t key_bytes,
                          const uint8_t *a, size_t a_bytes, const uint8_t *b, size_t b_bytes)
{
    uint8_t pad[HMAC_BLOCK_BYTES];
    uint8_t message[MP_RELAY_HASH_INPUT_MAX];
    uint8_t inner[MP_RELAY_HASH_BYTES];
    size_t  i;

    if (key_bytes > HMAC_BLOCK_BYTES || (key == NULL && key_bytes != 0u) ||
        a_bytes > MP_RELAY_HASH_INPUT_MAX - HMAC_BLOCK_BYTES ||
        b_bytes > MP_RELAY_HASH_INPUT_MAX - HMAC_BLOCK_BYTES - a_bytes ||
        (a == NULL && a_bytes != 0u) || (b == NULL && b_bytes != 0u)) {
        return false;
    }
    /* A key shorter than the block is padded with zeros, which is the whole of RFC 2104's key
     * rule for keys this protocol uses. */
    memset(pad, 0, sizeof pad);
    if (key_bytes != 0u) {
        memcpy(pad, key, key_bytes);
    }
    for (i = 0; i < HMAC_BLOCK_BYTES; ++i) {
        message[i] = (uint8_t)(pad[i] ^ HMAC_IPAD);
    }
    if (a_bytes != 0u) {
        memcpy(message + HMAC_BLOCK_BYTES, a, a_bytes);
    }
    if (b_bytes != 0u) {
        memcpy(message + HMAC_BLOCK_BYTES + a_bytes, b, b_bytes);
    }
    Hacl_Hash_SHA2_hash_256(inner, message, (uint32_t)(HMAC_BLOCK_BYTES + a_bytes + b_bytes));
    for (i = 0; i < HMAC_BLOCK_BYTES; ++i) {
        message[i] = (uint8_t)(pad[i] ^ HMAC_OPAD);
    }
    memcpy(message + HMAC_BLOCK_BYTES, inner, sizeof inner);
    Hacl_Hash_SHA2_hash_256(out, message, (uint32_t)(HMAC_BLOCK_BYTES + sizeof inner));
    mp_relay_wipe(pad, sizeof pad);
    mp_relay_wipe(message, sizeof message);
    mp_relay_wipe(inner, sizeof inner);
    return true;
}

bool mp_relay_hkdf2(uint8_t out1[MP_RELAY_HASH_BYTES], uint8_t out2[MP_RELAY_HASH_BYTES],
                    const uint8_t chaining_key[MP_RELAY_HASH_BYTES], const uint8_t *ikm,
                    size_t ikm_bytes)
{
    static const uint8_t ONE = 0x01u;
    static const uint8_t TWO = 0x02u;
    uint8_t              prk[MP_RELAY_HASH_BYTES];
    bool                 done;

    done = mp_relay_hmac_sha256(prk, chaining_key, MP_RELAY_HASH_BYTES, ikm, ikm_bytes, NULL, 0u) &&
           mp_relay_hmac_sha256(out1, prk, sizeof prk, &ONE, 1u, NULL, 0u) &&
           mp_relay_hmac_sha256(out2, prk, sizeof prk, out1, MP_RELAY_HASH_BYTES, &TWO, 1u);
    mp_relay_wipe(prk, sizeof prk);
    return done;
}

void mp_relay_aead_seal_nonce(uint8_t *out, uint8_t tag[MP_RELAY_TAG_BYTES],
                              const uint8_t key[MP_RELAY_KEY_BYTES],
                              const uint8_t nonce[MP_RELAY_NONCE_BYTES], const uint8_t *ad,
                              size_t ad_bytes, const uint8_t *plain, size_t bytes)
{
    Hacl_AEAD_Chacha20Poly1305_encrypt(out, tag, (uint8_t *)plain, (uint32_t)bytes, (uint8_t *)ad,
                                       (uint32_t)ad_bytes, (uint8_t *)key, (uint8_t *)nonce);
}

bool mp_relay_aead_open_nonce(uint8_t *out, const uint8_t key[MP_RELAY_KEY_BYTES],
                              const uint8_t nonce[MP_RELAY_NONCE_BYTES], const uint8_t *ad,
                              size_t ad_bytes, const uint8_t *cipher, size_t bytes,
                              const uint8_t tag[MP_RELAY_TAG_BYTES])
{
    /* HACL* compares the tag in constant time and leaves the output untouched when it fails. */
    return Hacl_AEAD_Chacha20Poly1305_decrypt(out, (uint8_t *)cipher, (uint32_t)bytes,
                                              (uint8_t *)ad, (uint32_t)ad_bytes, (uint8_t *)key,
                                              (uint8_t *)nonce, (uint8_t *)tag) == 0u;
}

void mp_relay_nonce_of(uint8_t nonce[MP_RELAY_NONCE_BYTES], uint64_t counter)
{
    size_t i;

    nonce[0] = nonce[1] = nonce[2] = nonce[3] = 0u;
    for (i = 0; i < 8u; ++i) {
        nonce[4u + i] = (uint8_t)(counter >> (8u * i));
    }
}

void mp_relay_aead_seal(uint8_t *out, uint8_t tag[MP_RELAY_TAG_BYTES],
                        const uint8_t key[MP_RELAY_KEY_BYTES], uint64_t counter, const uint8_t *ad,
                        size_t ad_bytes, const uint8_t *plain, size_t bytes)
{
    uint8_t nonce[MP_RELAY_NONCE_BYTES];

    mp_relay_nonce_of(nonce, counter);
    mp_relay_aead_seal_nonce(out, tag, key, nonce, ad, ad_bytes, plain, bytes);
}

bool mp_relay_aead_open(uint8_t *out, const uint8_t key[MP_RELAY_KEY_BYTES], uint64_t counter,
                        const uint8_t *ad, size_t ad_bytes, const uint8_t *cipher, size_t bytes,
                        const uint8_t tag[MP_RELAY_TAG_BYTES])
{
    uint8_t nonce[MP_RELAY_NONCE_BYTES];

    mp_relay_nonce_of(nonce, counter);
    return mp_relay_aead_open_nonce(out, key, nonce, ad, ad_bytes, cipher, bytes, tag);
}

bool mp_relay_random(uint8_t *out, size_t bytes)
{
    return mp_entropy_fill(out, bytes);
}

void mp_relay_wipe(void *secret, size_t bytes)
{
    volatile uint8_t *at = (volatile uint8_t *)secret;

    while (secret != NULL && bytes-- != 0u) {
        *at++ = 0u;
    }
}

bool mp_relay_equal(const uint8_t *a, const uint8_t *b, size_t bytes)
{
    uint8_t differ = 0u;
    size_t  i;

    for (i = 0; i < bytes; ++i) {
        differ = (uint8_t)(differ | (a[i] ^ b[i]));
    }
    return differ == 0u;
}

/* ==============================================================================================
 * The self-test.
 * ============================================================================================ */

static uint8_t nibble(char c)
{
    return (uint8_t)(c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10);
}

/* The hex of a vector into bytes. Every vector below is written in full lower case hex. */
static size_t unhex(const char *hex, uint8_t *out, size_t capacity)
{
    size_t n = 0;

    while (hex[0] != '\0' && hex[1] != '\0' && n < capacity) {
        out[n++] = (uint8_t)((nibble(hex[0]) << 4) | nibble(hex[1]));
        hex += 2;
    }
    return n;
}

static bool same_as(const uint8_t *got, const char *hex)
{
    uint8_t want[128];
    size_t  n = unhex(hex, want, sizeof want);

    return memcmp(got, want, n) == 0;
}

static const char *test_x25519(void)
{
    uint8_t alice[MP_RELAY_KEY_BYTES];
    uint8_t bob_public[MP_RELAY_KEY_BYTES];
    uint8_t out[MP_RELAY_KEY_BYTES];
    uint8_t zero[MP_RELAY_KEY_BYTES];

    (void)unhex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a", alice,
                sizeof alice);
    (void)unhex("de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f", bob_public,
                sizeof bob_public);
    mp_relay_x25519_public(out, alice);
    if (!same_as(out, "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a")) {
        return "RFC 7748 6.1, the public key";
    }
    if (!mp_relay_x25519_shared(out, alice, bob_public) ||
        !same_as(out, "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742")) {
        return "RFC 7748 6.1, the shared secret";
    }
    memset(zero, 0, sizeof zero);
    if (mp_relay_x25519_shared(out, alice, zero)) {
        return "X25519 with a point of small order";
    }
    return NULL;
}

static const char *test_aead(void)
{
    static const char PLAIN[] = "Ladies and Gentlemen of the class of '99: If I could offer you "
                                "only one tip for the future, sunscreen would be it.";
    static const char CIPHER[] =
        "d31a8d34648e60db7b86afbc53ef7ec2a4aded51296e08fea9e2b5a736ee62d63dbea45e8ca9671282fafb69da"
        "92728b1a71de0a9e060b2905d6a5b67ecd3b3692ddbd7f2d778b8c9803aee328091b58fab324e4fad675945585"
        "808b4831d7bc3ff4def08e4b7a9de576d26586cec64b6116";
    uint8_t key[MP_RELAY_KEY_BYTES];
    uint8_t nonce[MP_RELAY_NONCE_BYTES];
    uint8_t ad[12];
    uint8_t cipher[sizeof PLAIN];
    uint8_t want[sizeof PLAIN];
    uint8_t back[sizeof PLAIN];
    uint8_t tag[MP_RELAY_TAG_BYTES];
    size_t  bytes = sizeof PLAIN - 1u;

    (void)unhex("808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f", key,
                sizeof key);
    (void)unhex("070000004041424344454647", nonce, sizeof nonce);
    (void)unhex("50515253c0c1c2c3c4c5c6c7", ad, sizeof ad);
    if (unhex(CIPHER, want, sizeof want) != bytes) {
        return "RFC 8439 2.8.2, the vector itself";
    }
    mp_relay_aead_seal_nonce(cipher, tag, key, nonce, ad, sizeof ad, (const uint8_t *)PLAIN, bytes);
    if (memcmp(cipher, want, bytes) != 0 || !same_as(tag, "1ae10b594f09e26a7e902ecbd0600691")) {
        return "RFC 8439 2.8.2, the seal";
    }
    if (!mp_relay_aead_open_nonce(back, key, nonce, ad, sizeof ad, cipher, bytes, tag) ||
        memcmp(back, PLAIN, bytes) != 0) {
        return "RFC 8439 2.8.2, the open";
    }
    tag[15] = (uint8_t)(tag[15] ^ 0x01u);
    if (mp_relay_aead_open_nonce(back, key, nonce, ad, sizeof ad, cipher, bytes, tag)) {
        return "RFC 8439 2.8.2 with a forged tag";
    }
    return NULL;
}

static const char *test_hash(void)
{
    static const char DATA2[] = "what do ya want for nothing?";
    uint8_t key[22];
    uint8_t out1[MP_RELAY_HASH_BYTES];
    uint8_t out2[MP_RELAY_HASH_BYTES];
    uint8_t zero[MP_RELAY_HASH_BYTES];

    if (!mp_relay_sha256(out1, (const uint8_t *)"abc", 3u, NULL, 0u) ||
        !same_as(out1, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")) {
        return "SHA-256 of abc";
    }
    memset(key, 0x0b, sizeof key);
    if (!mp_relay_hmac_sha256(out1, key, 20u, (const uint8_t *)"Hi ", 3u,
                              (const uint8_t *)"There", 5u) ||
        !same_as(out1, "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7")) {
        return "RFC 4231 case 1";
    }
    if (!mp_relay_hmac_sha256(out1, (const uint8_t *)"Jefe", 4u, (const uint8_t *)DATA2,
                              sizeof DATA2 - 1u, NULL, 0u) ||
        !same_as(out1, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843")) {
        return "RFC 4231 case 2";
    }
    /* A.3 has an empty salt, which RFC 5869 makes 32 zero bytes, and empty info, which is the
     * Noise form; its 42 bytes are all of T(1) and the first ten of T(2). */
    memset(zero, 0, sizeof zero);
    if (!mp_relay_hkdf2(out1, out2, zero, key, sizeof key) ||
        !same_as(out1, "8da4e775a563c18f715f802a063c5a31b8a11f5c5ee1879ec3454e5f3c738d2d") ||
        !same_as(out2, "9d201395faa4b61a96c8")) {
        return "RFC 5869 A.3";
    }
    return NULL;
}

const char *mp_relay_crypto_self_test(void)
{
    const char *failed = test_x25519();

    if (failed == NULL) {
        failed = test_aead();
    }
    if (failed == NULL) {
        failed = test_hash();
    }
    return failed;
}
