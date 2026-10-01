/* mp_relay_keysource.h: which relay key a handshake uses, when it is fetched, what may stand in.
 *
 * Layer 1, pure: no clock, no thread, no socket. The relay's reference client keeps these rules in
 * one object per start of the public mode, shared by a host and every one of its players
 * (internal/client/fetch.go, HTTPKeys), and so does this one:
 *
 *   - The key in hand is used while it is younger than an hour; after that a handshake fetches.
 *   - A fetch that fails may use the key kept on disk, but only while that key is at most seven
 *     days old and never when its age is negative, which means the clock moved back. A stand-in
 *     counts as fetched now, so the next attempt comes an hour later.
 *   - A refusal of the key by the relay (open Nack 10, or answers that never open) fetches again
 *     at most once a minute and three times per start; within the minute the key in hand stands.
 *   - No key ever comes from anywhere but the document or the key the document gave last time.
 *
 * The response of the fetch and the line kept on disk are read here too, so that everything the
 * fetch's thread hands back is judged by code the tests can reach.
 */
#ifndef MULTIPLAYER_MP_RELAY_KEYSOURCE_H
#define MULTIPLAYER_MP_RELAY_KEYSOURCE_H

#include "mp_relay_keydoc.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MP_RELAY_KEY_MAX_AGE_S     3600
#define MP_RELAY_KEY_CACHE_MAX_S   (7 * 24 * 3600)
#define MP_RELAY_KEY_REFETCH_GAP_S 60
#define MP_RELAY_KEY_REFETCHES     3u

typedef struct mp_relay_keysource {
    bool           have;
    mp_relay_key_t key;
    int64_t        fetched_at;   /* unix seconds */
    bool           tried;
    int64_t        last_try;
    bool           have_cache;
    mp_relay_key_t cache;
    int64_t        cached_at;
    uint32_t       refetches;
    uint32_t       fetches;
} mp_relay_keysource_t;

/* What a handshake about to begin does. */
typedef enum mp_relay_key_step {
    MP_RELAY_KEY_USE = 0,   /* the key in hand is young enough */
    MP_RELAY_KEY_FETCH      /* fetch, and wait for the answer before any handshake */
} mp_relay_key_step_t;

/* What a refusal of the key by the relay does. */
typedef enum mp_relay_refetch_step {
    MP_RELAY_REFETCH_GO = 0,  /* fetch again */
    MP_RELAY_REFETCH_HOLD,    /* a fetch was a moment ago: the key in hand stands */
    MP_RELAY_REFETCH_NONE,    /* a moment ago, and no key in hand: nothing to try */
    MP_RELAY_REFETCH_SPENT    /* three fetches again this start: public mode gives up */
} mp_relay_refetch_step_t;

/* How a fetch ended. */
typedef enum mp_relay_fetch_end {
    MP_RELAY_FETCH_FRESH = 0,   /* the document was read */
    MP_RELAY_FETCH_STAND_IN,    /* it failed, and the key kept on disk stands in */
    MP_RELAY_FETCH_FAILED,      /* it failed, and nothing may stand in */
    MP_RELAY_FETCH_FUTURE,      /* it failed, and the kept key is from the future */
    MP_RELAY_FETCH_TOO_OLD      /* it failed, and the kept key is over seven days old */
} mp_relay_fetch_end_t;

void mp_relay_keysource_init(mp_relay_keysource_t *source);

/* The key read from disk at the start of public mode. */
void mp_relay_keysource_set_cache(mp_relay_keysource_t *source, const mp_relay_key_t *key,
                                  int64_t cached_at);

mp_relay_key_step_t mp_relay_keysource_step(const mp_relay_keysource_t *source, int64_t now);

/* Asked when the relay refused the key; GO also counts the refetch against the budget. */
mp_relay_refetch_step_t mp_relay_keysource_refetch(mp_relay_keysource_t *source, int64_t now);

/* A fetch is sent off: it is the last try from here on. */
void mp_relay_keysource_fetch_began(mp_relay_keysource_t *source, int64_t now);

/* A fetch came back. `key` is the document's key when `fetched`, and is ignored otherwise. */
mp_relay_fetch_end_t mp_relay_keysource_fetch_ended(mp_relay_keysource_t *source, bool fetched,
                                                    const mp_relay_key_t *key, int64_t now);

/* ==============================================================================================
 * The line kept on disk: "<id>:<64 lower case hex>:<suite>:<unix seconds>".
 * ============================================================================================ */

#define MP_RELAY_CACHE_LINE_BYTES 96u

size_t mp_relay_cache_line_write(const mp_relay_key_t *key, int64_t at, char *out,
                                 size_t capacity);
bool   mp_relay_cache_line_read(const char *line, mp_relay_key_t *key, int64_t *at);

/* ==============================================================================================
 * The fetch's response.
 * ============================================================================================ */

typedef enum mp_relay_http_result {
    MP_RELAY_HTTP_OK = 0,
    MP_RELAY_HTTP_INCOMPLETE,    /* the headers never ended */
    MP_RELAY_HTTP_STATUS,        /* not "HTTP/1.x 200" */
    MP_RELAY_HTTP_CHUNKED,       /* a Transfer-Encoding: the answer has no length of its own */
    MP_RELAY_HTTP_NO_LENGTH,     /* no Content-Length, or not a number */
    MP_RELAY_HTTP_TOO_LONG,      /* a Content-Length over 4096 */
    MP_RELAY_HTTP_SHORT          /* fewer body bytes than the length said */
} mp_relay_http_result_t;

/* Reads a whole HTTP/1.1 response the server closed after. On MP_RELAY_HTTP_OK `*body` points into
 * `response` and `*body_bytes` is the Content-Length. `*status` is the status code when one was
 * read, 0 otherwise. */
mp_relay_http_result_t mp_relay_http_body(const char *response, size_t bytes, unsigned *status,
                                          const char **body, size_t *body_bytes);

#endif /* MULTIPLAYER_MP_RELAY_KEYSOURCE_H */
