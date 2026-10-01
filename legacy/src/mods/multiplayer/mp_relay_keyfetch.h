/* mp_relay_keyfetch.h: the relay's key document fetched over TLS 1.3, and the relay's addresses.
 *
 * Layer 0. Everything here that can wait on the network runs in a thread of its own, never on the
 * game thread and never under the loader lock: the name lookup of the relay, the TCP connection,
 * the TLS 1.3 handshake proved against the two anchors of mp_relay_anchors.h and nothing else, and
 * the one HTTP request. The game thread starts a fetch and asks each frame whether it has
 * finished; it never waits for one. Whether the answer may be used is mp_relay_keysource.h's to
 * decide.
 *
 * The rules the TLS client follows are the relay reference client's: TLS 1.3 only, the server's
 * name checked, every certificate the server sends an ECDSA key, no redirect, status 200 only, a
 * length and no Transfer-Encoding, at most 4096 bytes, ten seconds for all of it.
 *
 * The key kept between runs is a line in %LOCALAPPDATA%\OpenPhantom\relay-state.ini, never in the
 * game's own ini: it is state the program learned, not a setting a player chose.
 */
#ifndef MULTIPLAYER_MP_RELAY_KEYFETCH_H
#define MULTIPLAYER_MP_RELAY_KEYFETCH_H

#include "mp_relay_keydoc.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MP_RELAY_HOST          "relay.swopenphantom.org"
#define MP_RELAY_KEY_PATH      "/mp-relay/v2/key"
#define MP_RELAY_UDP_PORT      27970u
#define MP_RELAY_FETCH_TIMEOUT_MS 10000u

/* The relay's addresses kept from one lookup, IPv6 first. */
#define MP_RELAY_ADDRESSES_MAX 4u

typedef struct mp_relay_address {
    bool     ipv6;
    uint8_t  bytes[16];   /* an IPv4 address in the first four */
    uint16_t port;        /* host order */
} mp_relay_address_t;

typedef enum mp_relay_fetch_error {
    MP_RELAY_FETCH_ERROR_NONE = 0,
    MP_RELAY_FETCH_ERROR_THREAD,     /* the thread would not start */
    MP_RELAY_FETCH_ERROR_ANCHORS,    /* the built-in anchors do not hash to their pinned values */
    MP_RELAY_FETCH_ERROR_RESOLVE,    /* the relay's name did not resolve */
    MP_RELAY_FETCH_ERROR_CONNECT,    /* no address took a TCP connection in time */
    MP_RELAY_FETCH_ERROR_TLS,        /* the TLS handshake failed; detail is Mbed TLS's code */
    MP_RELAY_FETCH_ERROR_NOT_ECDSA,  /* a certificate the server sent is not an ECDSA key */
    MP_RELAY_FETCH_ERROR_READ,       /* the request or the answer broke off */
    MP_RELAY_FETCH_ERROR_HTTP,       /* the answer is not one the rules take; detail says which */
    MP_RELAY_FETCH_ERROR_DOCUMENT,   /* the document was refused; detail is the parser's reason */
    MP_RELAY_FETCH_ERROR_COUNT
} mp_relay_fetch_error_t;

typedef struct mp_relay_fetch_result {
    bool                   wanted_key;
    bool                   key_fetched;
    mp_relay_key_t         key;
    mp_relay_fetch_error_t error;
    int                    detail;
    unsigned               status;          /* the HTTP status, 0 when none was read */
    bool                   over_ipv6;       /* the TLS connection's family */
    uint32_t               elapsed_ms;
    size_t                 addresses;
    mp_relay_address_t     address[MP_RELAY_ADDRESSES_MAX];
} mp_relay_fetch_result_t;

/* Starts a lookup of the relay's addresses and, with `want_key`, the key document fetch after it.
 * False when one is already running, or when the thread would not start. */
bool mp_relay_fetch_begin(bool want_key);

bool mp_relay_fetch_running(void);

/* True exactly once per fetch, when it has finished, with what it found. */
bool mp_relay_fetch_take(mp_relay_fetch_result_t *out);

const char *mp_relay_fetch_error_text(mp_relay_fetch_error_t error);

/* The key kept between runs. False when there is none, or the line does not read. */
bool mp_relay_state_load(mp_relay_key_t *key, int64_t *at);
bool mp_relay_state_save(const mp_relay_key_t *key, int64_t at);

/* Seconds since 1970 by the system clock. */
int64_t mp_relay_unix_now(void);

#endif /* MULTIPLAYER_MP_RELAY_KEYFETCH_H */
