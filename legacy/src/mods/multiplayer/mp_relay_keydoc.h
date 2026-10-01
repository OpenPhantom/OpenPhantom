/* mp_relay_keydoc.h: the relay's published key document, read.
 *
 * Layer 1, pure. The relay publishes one line at https://relay.swopenphantom.org/mp-relay/v2/key:
 *
 *     MPRLKEY2 <id> x25519=<64 lower case hex characters> suites=<n>[,<n>...]
 *
 * and every handshake proves the key it names. The reader insists on what it needs and passes over
 * a field it does not know, so a later field such as mlkem768= does not shut out this build. It
 * follows the relay's own parser (internal/statickey/document.go) rule for rule, whitespace
 * included, except that only ASCII whitespace separates fields.
 */
#ifndef MULTIPLAYER_MP_RELAY_KEYDOC_H
#define MULTIPLAYER_MP_RELAY_KEYDOC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The largest document read, which is also what the fetch accepts. */
#define MP_RELAY_KEYDOC_MAX 4096u

typedef struct mp_relay_key {
    uint8_t id;                /* 1 to 255 */
    uint8_t public_key[32];
    uint8_t suite;             /* the first suite the document announces that this build speaks */
} mp_relay_key_t;

typedef enum mp_relay_keydoc_result {
    MP_RELAY_KEYDOC_OK = 0,
    MP_RELAY_KEYDOC_TOO_LONG,        /* over 4096 bytes */
    MP_RELAY_KEYDOC_LINES,           /* more than one line that is not blank */
    MP_RELAY_KEYDOC_NOT_A_DOCUMENT,  /* another tag, or a required field missing */
    MP_RELAY_KEYDOC_ID,              /* the id is not a number from 1 to 255 */
    MP_RELAY_KEYDOC_FIELD,           /* a field without '=' */
    MP_RELAY_KEYDOC_TWICE,           /* x25519= or suites= named twice */
    MP_RELAY_KEYDOC_KEY,             /* not 64 lower case hex characters */
    MP_RELAY_KEYDOC_ZERO_KEY,        /* the key is all zeros */
    MP_RELAY_KEYDOC_SUITES,          /* a suite that is not a number to 255, or one named twice */
    MP_RELAY_KEYDOC_NO_SUITE,        /* no suite this build speaks */
    MP_RELAY_KEYDOC_RESULT_COUNT
} mp_relay_keydoc_result_t;

mp_relay_keydoc_result_t mp_relay_keydoc_parse(const char *text, size_t bytes,
                                               mp_relay_key_t *out);

/* Why a document was refused, for the log. */
const char *mp_relay_keydoc_result_text(mp_relay_keydoc_result_t result);

#endif /* MULTIPLAYER_MP_RELAY_KEYDOC_H */
