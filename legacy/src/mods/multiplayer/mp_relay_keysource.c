/* mp_relay_keysource.c: which relay key a handshake uses. See the header. */
#include "mp_relay_keysource.h"

#include "common/text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

void mp_relay_keysource_init(mp_relay_keysource_t *source)
{
    memset(source, 0, sizeof *source);
}

void mp_relay_keysource_set_cache(mp_relay_keysource_t *source, const mp_relay_key_t *key,
                                  int64_t cached_at)
{
    source->cache      = *key;
    source->cached_at  = cached_at;
    source->have_cache = true;
}

mp_relay_key_step_t mp_relay_keysource_step(const mp_relay_keysource_t *source, int64_t now)
{
    if (source->have && now - source->fetched_at < MP_RELAY_KEY_MAX_AGE_S &&
        now >= source->fetched_at) {
        return MP_RELAY_KEY_USE;
    }
    return MP_RELAY_KEY_FETCH;
}

mp_relay_refetch_step_t mp_relay_keysource_refetch(mp_relay_keysource_t *source, int64_t now)
{
    if (source->tried && now - source->last_try < MP_RELAY_KEY_REFETCH_GAP_S) {
        return source->have ? MP_RELAY_REFETCH_HOLD : MP_RELAY_REFETCH_NONE;
    }
    if (source->refetches >= MP_RELAY_KEY_REFETCHES) {
        return MP_RELAY_REFETCH_SPENT;
    }
    ++source->refetches;
    return MP_RELAY_REFETCH_GO;
}

void mp_relay_keysource_fetch_began(mp_relay_keysource_t *source, int64_t now)
{
    source->tried    = true;
    source->last_try = now;
    ++source->fetches;
}

mp_relay_fetch_end_t mp_relay_keysource_fetch_ended(mp_relay_keysource_t *source, bool fetched,
                                                    const mp_relay_key_t *key, int64_t now)
{
    int64_t age;

    if (fetched && key != NULL) {
        source->key        = *key;
        source->have       = true;
        source->fetched_at = now;
        source->cache      = *key;
        source->cached_at  = now;
        source->have_cache = true;
        return MP_RELAY_FETCH_FRESH;
    }
    if (!source->have_cache) {
        return MP_RELAY_FETCH_FAILED;
    }
    age = now - source->cached_at;
    if (age < 0) {
        return MP_RELAY_FETCH_FUTURE;
    }
    if (age > MP_RELAY_KEY_CACHE_MAX_S) {
        return MP_RELAY_FETCH_TOO_OLD;
    }
    source->key        = source->cache;
    source->have       = true;
    source->fetched_at = now;
    return MP_RELAY_FETCH_STAND_IN;
}

/* ==============================================================================================
 * The line kept on disk.
 * ============================================================================================ */

size_t mp_relay_cache_line_write(const mp_relay_key_t *key, int64_t at, char *out, size_t capacity)
{
    char   hex[65];
    size_t i;
    size_t n;

    if (key == NULL || out == NULL || capacity < MP_RELAY_CACHE_LINE_BYTES || at < 0) {
        return 0u;
    }
    for (i = 0; i < 32u; ++i) {
        (void)text_format(hex + 2u * i, 3u, "%02x", key->public_key[i]);
    }
    n = text_format(out, capacity, "%u:%s:%u:%lld", (unsigned)key->id, hex, (unsigned)key->suite,
                    (long long)at);
    return n != 0u && n + 1u < capacity ? n : 0u;   /* a full buffer may be a cut line */
}

static bool read_number(const char **at, uint64_t limit, uint64_t *out)
{
    uint64_t value  = 0;
    size_t   digits = 0;

    while (**at >= '0' && **at <= '9') {
        value = value * 10u + (uint64_t)(**at - '0');
        if (value > limit || ++digits > 19u) {
            return false;
        }
        ++*at;
    }
    *out = value;
    return digits != 0u;
}

bool mp_relay_cache_line_read(const char *line, mp_relay_key_t *key, int64_t *at)
{
    mp_relay_key_t parsed;
    const char    *p = line;
    uint64_t       id = 0;
    uint64_t       suite = 0;
    uint64_t       when = 0;
    uint8_t        any = 0;
    size_t         i;

    if (line == NULL || key == NULL || at == NULL) {
        return false;
    }
    memset(&parsed, 0, sizeof parsed);
    if (!read_number(&p, 255u, &id) || id == 0u || *p++ != ':') {
        return false;
    }
    for (i = 0; i < 32u; ++i) {
        uint8_t byte = 0;
        size_t  k;

        for (k = 0; k < 2u; ++k) {
            char c = *p++;

            if (c >= '0' && c <= '9') {
                byte = (uint8_t)((byte << 4) | (uint8_t)(c - '0'));
            } else if (c >= 'a' && c <= 'f') {
                byte = (uint8_t)((byte << 4) | (uint8_t)(c - 'a' + 10));
            } else {
                return false;
            }
        }
        parsed.public_key[i] = byte;
        any = (uint8_t)(any | byte);
    }
    if (any == 0u || *p++ != ':' || !read_number(&p, 255u, &suite) || suite != 0u ||
        *p++ != ':' || !read_number(&p, (uint64_t)INT64_MAX, &when) || *p != '\0') {
        return false;
    }
    parsed.id    = (uint8_t)id;
    parsed.suite = (uint8_t)suite;
    *key = parsed;
    *at  = (int64_t)when;
    return true;
}

/* ==============================================================================================
 * The response.
 * ============================================================================================ */

static bool same_name(const char *a, size_t a_bytes, const char *name)
{
    size_t length = strlen(name);
    size_t i;

    if (a_bytes != length) {
        return false;
    }
    for (i = 0; i < length; ++i) {
        char c = a[i];

        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        if (c != name[i]) {
            return false;
        }
    }
    return true;
}

mp_relay_http_result_t mp_relay_http_body(const char *response, size_t bytes, unsigned *status,
                                          const char **body, size_t *body_bytes)
{
    size_t  end = 0;
    size_t  at;
    bool    have_length = false;
    bool    chunked     = false;
    int64_t length      = -1;

    *status = 0u;
    if (response == NULL) {
        return MP_RELAY_HTTP_INCOMPLETE;
    }
    while (end + 3u < bytes && memcmp(response + end, "\r\n\r\n", 4u) != 0) {
        ++end;
    }
    if (end + 3u >= bytes) {
        return MP_RELAY_HTTP_INCOMPLETE;
    }
    if (bytes < 12u || memcmp(response, "HTTP/1.", 7u) != 0 || response[8] != ' ' ||
        response[9] < '1' || response[9] > '5' || response[10] < '0' || response[10] > '9' ||
        response[11] < '0' || response[11] > '9') {
        return MP_RELAY_HTTP_STATUS;
    }
    *status = (unsigned)((response[9] - '0') * 100 + (response[10] - '0') * 10 +
                         (response[11] - '0'));
    if (*status != 200u) {
        return MP_RELAY_HTTP_STATUS;
    }
    at = 0;
    while (at < end && memcmp(response + at, "\r\n", 2u) != 0) {
        ++at;
    }
    at += 2u;
    while (at < end + 2u) {
        size_t line_end = at;
        size_t colon;
        size_t value;

        while (line_end < end + 2u && memcmp(response + line_end, "\r\n", 2u) != 0) {
            ++line_end;
        }
        colon = at;
        while (colon < line_end && response[colon] != ':') {
            ++colon;
        }
        if (colon < line_end) {
            value = colon + 1u;
            while (value < line_end && (response[value] == ' ' || response[value] == '\t')) {
                ++value;
            }
            if (same_name(response + at, colon - at, "transfer-encoding")) {
                chunked = true;
            } else if (same_name(response + at, colon - at, "content-length")) {
                size_t digits = 0;

                length = 0;
                while (value < line_end && response[value] >= '0' && response[value] <= '9' &&
                       digits < 9u) {
                    length = length * 10 + (response[value] - '0');
                    ++value;
                    ++digits;
                }
                while (value < line_end && (response[value] == ' ' || response[value] == '\t')) {
                    ++value;
                }
                have_length = digits != 0u && value == line_end && !have_length;
            }
        }
        at = line_end + 2u;
    }
    if (chunked) {
        return MP_RELAY_HTTP_CHUNKED;
    }
    if (!have_length || length < 0) {
        return MP_RELAY_HTTP_NO_LENGTH;
    }
    if (length > (int64_t)MP_RELAY_KEYDOC_MAX) {
        return MP_RELAY_HTTP_TOO_LONG;
    }
    if (bytes - (end + 4u) < (size_t)length) {
        return MP_RELAY_HTTP_SHORT;
    }
    *body       = response + end + 4u;
    *body_bytes = (size_t)length;
    return MP_RELAY_HTTP_OK;
}
