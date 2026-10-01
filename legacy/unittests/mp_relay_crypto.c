/* mp_relay_crypto.c: the primitives of the relay protocol.
 *
 * The published vectors run through the same self-test public mode runs before it sends anything,
 * and the rest pins what the protocol builds on top of them: the nonce the counter becomes, a seal
 * that opens only under its own counter, and the bounds the hash and the MAC refuse past.
 */
#include "unittest.h"

#include "mp_relay_crypto.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static void test_the_published_vectors(void)
{
    const char *failed = mp_relay_crypto_self_test();

    ut_section("the published vectors");
    ut_check(failed == NULL, "RFC 7748, RFC 8439, RFC 4231, RFC 5869 and SHA-256 all pass");
    if (failed != NULL) {
        ut_check(false, failed);
    }
}

static void test_the_nonce_is_little_endian(void)
{
    static const uint8_t WANT[MP_RELAY_NONCE_BYTES] = {
        0x00, 0x00, 0x00, 0x00, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01
    };
    uint8_t nonce[MP_RELAY_NONCE_BYTES];

    ut_section("a counter becomes four zero bytes and the counter little endian");
    memset(nonce, 0xFF, sizeof nonce);
    mp_relay_nonce_of(nonce, 0x0102030405060708ull);
    ut_check(memcmp(nonce, WANT, sizeof WANT) == 0,
             "0x0102030405060708 is 00 00 00 00 08 07 06 05 04 03 02 01");
}

static void test_a_seal_opens_under_its_own_counter(void)
{
    static const uint8_t HEADER[16] = { 0x12, 0, 3, 0, 44, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8 };
    static const char    GAME[]     = "a game packet";
    uint8_t key[MP_RELAY_KEY_BYTES];
    uint8_t cipher[sizeof GAME];
    uint8_t back[sizeof GAME];
    uint8_t tag[MP_RELAY_TAG_BYTES];
    uint8_t header[sizeof HEADER];

    ut_section("a seal opens under its counter and its header, and under nothing else");
    memset(key, 0x42, sizeof key);
    memcpy(header, HEADER, sizeof header);
    mp_relay_aead_seal(cipher, tag, key, 7u, header, sizeof header, (const uint8_t *)GAME,
                       sizeof GAME);
    ut_check(mp_relay_aead_open(back, key, 7u, header, sizeof header, cipher, sizeof GAME, tag) &&
                 memcmp(back, GAME, sizeof GAME) == 0,
             "the counter it was sealed under opens it");
    ut_check(!mp_relay_aead_open(back, key, 8u, header, sizeof header, cipher, sizeof GAME, tag),
             "the next counter does not");
    header[1] = 1u;
    ut_check(!mp_relay_aead_open(back, key, 7u, header, sizeof header, cipher, sizeof GAME, tag),
             "a header changed by one byte does not: the header is the associated data");
}

static void test_the_bounds_are_refused(void)
{
    uint8_t key[65];
    uint8_t big[MP_RELAY_HASH_INPUT_MAX + 1u];
    uint8_t out[MP_RELAY_HASH_BYTES];

    ut_section("the hash and the MAC refuse what does not fit rather than cut it");
    memset(key, 1, sizeof key);
    memset(big, 2, sizeof big);
    ut_check(!mp_relay_hmac_sha256(out, key, sizeof key, big, 1u, NULL, 0u),
             "a key longer than a block is refused");
    ut_check(mp_relay_hmac_sha256(out, key, 64u, big, MP_RELAY_HASH_INPUT_MAX - 64u, NULL, 0u),
             "a block-long key and the longest message are taken");
    ut_check(!mp_relay_hmac_sha256(out, key, 64u, big, MP_RELAY_HASH_INPUT_MAX - 64u, big, 1u),
             "one byte more is refused");
    ut_check(mp_relay_sha256(out, big, MP_RELAY_HASH_INPUT_MAX, NULL, 0u),
             "the hash takes its longest input");
    ut_check(!mp_relay_sha256(out, big, MP_RELAY_HASH_INPUT_MAX, big, 1u),
             "and refuses one byte more");
}

static void test_equal_and_wipe(void)
{
    uint8_t a[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    uint8_t b[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    size_t  i;
    bool    zero = true;

    ut_section("comparing and wiping");
    ut_check(mp_relay_equal(a, b, sizeof a), "equal bytes compare equal");
    b[7] = 9u;
    ut_check(!mp_relay_equal(a, b, sizeof a), "a difference in the last byte is seen");
    mp_relay_wipe(a, sizeof a);
    for (i = 0; i < sizeof a; ++i) {
        zero = zero && a[i] == 0u;
    }
    ut_check(zero, "a wiped secret is all zero");
}

int main(void)
{
    test_the_published_vectors();
    test_the_nonce_is_little_endian();
    test_a_seal_opens_under_its_own_counter();
    test_the_bounds_are_refused();
    test_equal_and_wipe();
    return ut_summary("mp_relay_crypto");
}
