/* mp_relay_anchors.c: the two certificates the relay key fetch trusts.
 *
 * The pinned hashes are the ones the relay server records beside each certificate, typed out here
 * once more, so the bytes, the hashes beside them and the relay's own record all have to agree
 * before a build trusts anything.
 */
#include "unittest.h"

#include "mp_relay_anchors.h"
#include "mp_relay_crypto.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static bool hex_is(const uint8_t *bytes, const char *hex)
{
    size_t i;

    for (i = 0; i < 32u; ++i) {
        char pair[3];

        pair[0] = hex[2u * i];
        pair[1] = hex[2u * i + 1u];
        pair[2] = '\0';
        if ((uint8_t)strtoul(pair, NULL, 16) != bytes[i]) {
            return false;
        }
    }
    return true;
}

int main(void)
{
    uint8_t digest[MP_RELAY_HASH_BYTES];

    ut_section("the anchors are the ones the relay's repository names");
    ut_check(hex_is(MP_RELAY_ANCHOR_YE_SHA256,
                    "E14FFCAD5B0025731006CAA43A121A22D8E9700F4FB9CF852F02A708AA5D5666"),
             "ISRG Root YE is pinned to the hash the relay server records");
    ut_check(hex_is(MP_RELAY_ANCHOR_X2_SHA256,
                    "69729B8E15A86EFC177A57AFB7171DFC64ADD28C2FCA8CF1507E34453CCB1470"),
             "ISRG Root X2 is pinned to the hash the relay server records");
    ut_check(mp_relay_anchors_intact(), "and both certificates hash to their pins");
    mp_relay_sha256_of(digest, MP_RELAY_ANCHOR_YE, sizeof MP_RELAY_ANCHOR_YE - 1u);
    ut_check(!mp_relay_equal(digest, MP_RELAY_ANCHOR_YE_SHA256, sizeof digest),
             "a certificate one byte short does not");
    ut_check(MP_RELAY_ANCHOR_YE[0] == 0x30u && MP_RELAY_ANCHOR_X2[0] == 0x30u,
             "both are DER, a sequence from the first byte");
    return ut_summary("mp_relay_anchors");
}
