/* mp_relay_keydoc.c: the relay's published key document.
 *
 * The cases are the relay server's own parser tests, one for one:
 * the live document and a trailing newline are read, a later field and an unknown suite are passed
 * over, and every broken document the relay's tests refuse is refused here. A run of damaged
 * documents after that must never be taken with a key that is not in the text it came from.
 */
#include "unittest.h"

#include "mp_relay_keydoc.h"

#include "common/text.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define LIVE "MPRLKEY2 1 x25519=bdf1e8c30c484d1de950e66b215fd453b7c2ae523e66a350168b8cdefd2fbd74 " \
             "suites=0"
#define GOOD "abababababababababababababababababababababababababababababababab"

static mp_relay_keydoc_result_t parse(const char *text, mp_relay_key_t *key)
{
    return mp_relay_keydoc_parse(text, strlen(text), key);
}

static void test_the_documents_that_are_read(void)
{
    mp_relay_key_t key;

    ut_section("documents that are read");
    ut_check(parse(LIVE, &key) == MP_RELAY_KEYDOC_OK && key.id == 1u &&
                 key.public_key[0] == 0xBDu && key.public_key[31] == 0x74u && key.suite == 0u,
             "the live document names key 1 and suite 0");
    ut_check(parse(LIVE "\n", &key) == MP_RELAY_KEYDOC_OK, "with a trailing newline");
    ut_check(parse("\r\n  " LIVE "  \r\n\n", &key) == MP_RELAY_KEYDOC_OK,
             "with blank lines and spaces around it");
    ut_check(parse("MPRLKEY2 7 x25519=" GOOD " mlkem768=beef suites=9,0", &key) ==
                     MP_RELAY_KEYDOC_OK &&
                 key.id == 7u && key.suite == 0u,
             "a later field is passed over, and of suites 9 and 0 this build takes 0");
    ut_check(parse("MPRLKEY2 007 x25519=" GOOD " suites=0", &key) == MP_RELAY_KEYDOC_OK &&
                 key.id == 7u,
             "an id with leading zeros, as the relay's parser takes it");
    ut_check(parse("MPRLKEY2 1 x25519=" GOOD " suites=9,200", &key) == MP_RELAY_KEYDOC_NO_SUITE,
             "a document announcing no suite this build speaks is not taken");
}

static void test_the_documents_that_are_refused(void)
{
    static const struct {
        const char *name;
        const char *text;
    } BROKEN[] = {
        { "empty", "" },
        { "another tag", "MPRLKEY1 1 x25519=" GOOD " suites=0" },
        { "no id", "MPRLKEY2 x25519=" GOOD " suites=0" },
        { "id 0", "MPRLKEY2 0 x25519=" GOOD " suites=0" },
        { "id 256", "MPRLKEY2 256 x25519=" GOOD " suites=0" },
        { "no key", "MPRLKEY2 1 suites=0" },
        { "no suites", "MPRLKEY2 1 x25519=" GOOD },
        { "a short key", "MPRLKEY2 1 x25519="
                         "ababababababababababababababababababababababababababababababab"
                         " suites=0" },
        { "a key not hex", "MPRLKEY2 1 x25519="
                           "zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz"
                           " suites=0" },
        { "a key in upper case", "MPRLKEY2 1 x25519=ABABABABABABABABABABABABABABABABABABABABABABAB"
                                 "ABABABABABABABABAB suites=0" },
        { "a key of zeros", "MPRLKEY2 1 x25519=0000000000000000000000000000000000000000000000000000"
                            "000000000000 suites=0" },
        { "the key twice", "MPRLKEY2 1 x25519=" GOOD " x25519=" GOOD " suites=0" },
        { "suites twice", "MPRLKEY2 1 x25519=" GOOD " suites=0 suites=1" },
        { "a suite twice", "MPRLKEY2 1 x25519=" GOOD " suites=0,0" },
        { "a suite not a number", "MPRLKEY2 1 x25519=" GOOD " suites=zero" },
        { "a suite too large", "MPRLKEY2 1 x25519=" GOOD " suites=256" },
        { "a field without =", "MPRLKEY2 1 x25519=" GOOD " suites=0 later" },
        { "two lines", "MPRLKEY2 1 x25519=" GOOD " suites=0\nMPRLKEY2 2 x25519=" GOOD " suites=0" },
    };
    char           long_text[MP_RELAY_KEYDOC_MAX + 128u];
    mp_relay_key_t key;
    size_t         i;
    size_t         at;

    ut_section("documents the relay's own tests refuse");
    for (i = 0; i < sizeof BROKEN / sizeof BROKEN[0]; ++i) {
        mp_relay_keydoc_result_t result = parse(BROKEN[i].text, &key);

        ut_check(result != MP_RELAY_KEYDOC_OK, BROKEN[i].name);
    }
    at = text_format(long_text, sizeof long_text, "MPRLKEY2 1 x25519=" GOOD " suites=0 pad=");
    while (at < MP_RELAY_KEYDOC_MAX + 1u) {
        long_text[at++] = 'x';
    }
    ut_check(mp_relay_keydoc_parse(long_text, at, &key) == MP_RELAY_KEYDOC_TOO_LONG,
             "a document sound in every other way but longer than 4096 bytes");
}

/* Damaged copies of the live document, one byte changed at every position to every value of a
 * small set. Whatever is taken must carry a key that is written in the text it came from. */
static void test_damaged_documents(void)
{
    static const char VALUES[] = { '0', 'A', 'z', ' ', '\n', '=', ',', '\0' };
    char           text[sizeof LIVE];
    char           hex[65];
    mp_relay_key_t key;
    size_t         at;
    size_t         v;
    size_t         i;
    bool           honest = true;
    uint32_t       taken  = 0;

    ut_section("damaged documents");
    for (at = 0; at + 1u < sizeof LIVE; ++at) {
        for (v = 0; v < sizeof VALUES; ++v) {
            memcpy(text, LIVE, sizeof LIVE);
            text[at] = VALUES[v];
            if (mp_relay_keydoc_parse(text, sizeof LIVE - 1u, &key) != MP_RELAY_KEYDOC_OK) {
                continue;
            }
            ++taken;
            for (i = 0; i < 32u; ++i) {
                (void)text_format(hex + 2u * i, 3u, "%02x", key.public_key[i]);
            }
            honest = honest && key.id != 0u && memchr(text, 0, sizeof LIVE - 1u) == NULL &&
                     strstr(text, hex) != NULL;
        }
    }
    ut_check(honest, "every damaged document taken carries a key written in its own text");
    ut_check(taken != 0u, "and the damage that leaves a sound document is still read");
}

int main(void)
{
    test_the_documents_that_are_read();
    test_the_documents_that_are_refused();
    test_damaged_documents();
    return ut_summary("mp_relay_keydoc");
}
