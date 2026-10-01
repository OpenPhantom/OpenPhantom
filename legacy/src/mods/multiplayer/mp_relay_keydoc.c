/* mp_relay_keydoc.c: the relay's published key document, read. See the header. */
#include "mp_relay_keydoc.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define TAG           "MPRLKEY2"
#define KEY_HEX_CHARS 64u
#define KNOWN_SUITE   0u

typedef struct span {
    const char *at;
    size_t      bytes;
} span_t;

static bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\v' || c == '\f' || c == '\r' || c == '\n';
}

static bool span_is(span_t s, const char *word)
{
    size_t length = strlen(word);

    return s.bytes == length && memcmp(s.at, word, length) == 0;
}

/* A decimal number from 0 to 255, leading zeros allowed, nothing else. */
static bool parse_byte(span_t s, uint32_t *out)
{
    uint32_t value = 0;
    size_t   i;

    if (s.bytes == 0u) {
        return false;
    }
    for (i = 0; i < s.bytes; ++i) {
        if (s.at[i] < '0' || s.at[i] > '9') {
            return false;
        }
        value = value * 10u + (uint32_t)(s.at[i] - '0');
        if (value > 255u) {
            return false;
        }
    }
    *out = value;
    return true;
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return -1;
}

/* The one line that is not blank, trimmed; false when there are two. */
static bool the_line(const char *text, size_t bytes, span_t *line)
{
    size_t start = 0;

    line->at    = NULL;
    line->bytes = 0u;
    while (start <= bytes) {
        size_t end = start;
        span_t candidate;

        while (end < bytes && text[end] != '\n') {
            ++end;
        }
        candidate.at    = text + start;
        candidate.bytes = end - start;
        while (candidate.bytes != 0u && is_space(candidate.at[0])) {
            ++candidate.at;
            --candidate.bytes;
        }
        while (candidate.bytes != 0u && is_space(candidate.at[candidate.bytes - 1u])) {
            --candidate.bytes;
        }
        if (candidate.bytes != 0u) {
            if (line->at != NULL) {
                return false;
            }
            *line = candidate;
        }
        start = end + 1u;
    }
    return true;
}

/* The next field of the line from `*at` on, whitespace separated. False when none is left. */
static bool next_field(span_t line, size_t *at, span_t *field)
{
    size_t end;

    while (*at < line.bytes && is_space(line.at[*at])) {
        ++*at;
    }
    if (*at >= line.bytes) {
        return false;
    }
    end = *at;
    while (end < line.bytes && !is_space(line.at[end])) {
        ++end;
    }
    field->at    = line.at + *at;
    field->bytes = end - *at;
    *at          = end;
    return true;
}

static mp_relay_keydoc_result_t read_key(span_t value, uint8_t out[32])
{
    uint8_t any = 0u;
    size_t  i;

    for (i = 0; i < value.bytes; ++i) {
        if (value.at[i] >= 'A' && value.at[i] <= 'Z') {
            return MP_RELAY_KEYDOC_KEY;   /* one key, one spelling */
        }
    }
    if (value.bytes != KEY_HEX_CHARS) {
        return MP_RELAY_KEYDOC_KEY;
    }
    for (i = 0; i < 32u; ++i) {
        int high = hex_value(value.at[2u * i]);
        int low  = hex_value(value.at[2u * i + 1u]);

        if (high < 0 || low < 0) {
            return MP_RELAY_KEYDOC_KEY;
        }
        out[i] = (uint8_t)((high << 4) | low);
        any    = (uint8_t)(any | out[i]);
    }
    return any == 0u ? MP_RELAY_KEYDOC_ZERO_KEY : MP_RELAY_KEYDOC_OK;
}

static mp_relay_keydoc_result_t read_suites(span_t value, uint8_t *suite)
{
    bool   seen[256];
    bool   known = false;
    size_t at    = 0;

    memset(seen, 0, sizeof seen);
    for (;;) {
        span_t   item;
        uint32_t number = 0;
        size_t   end    = at;

        while (end < value.bytes && value.at[end] != ',') {
            ++end;
        }
        item.at    = value.at + at;
        item.bytes = end - at;
        if (!parse_byte(item, &number) || seen[number]) {
            return MP_RELAY_KEYDOC_SUITES;
        }
        seen[number] = true;
        if (number == KNOWN_SUITE && !known) {
            known  = true;
            *suite = (uint8_t)number;
        }
        if (end >= value.bytes) {
            break;
        }
        at = end + 1u;
    }
    return known ? MP_RELAY_KEYDOC_OK : MP_RELAY_KEYDOC_NO_SUITE;
}

mp_relay_keydoc_result_t mp_relay_keydoc_parse(const char *text, size_t bytes,
                                               mp_relay_key_t *out)
{
    span_t                   line;
    span_t                   tag;
    span_t                   number;
    span_t                   field;
    span_t                   public_value = { NULL, 0u };
    span_t                   suites_value = { NULL, 0u };
    mp_relay_key_t           key;
    mp_relay_keydoc_result_t result;
    uint32_t                 id = 0;
    size_t                   at = 0;

    if (out == NULL || text == NULL || bytes == 0u) {
        return MP_RELAY_KEYDOC_NOT_A_DOCUMENT;
    }
    if (bytes > MP_RELAY_KEYDOC_MAX) {
        return MP_RELAY_KEYDOC_TOO_LONG;
    }
    if (!the_line(text, bytes, &line)) {
        return MP_RELAY_KEYDOC_LINES;
    }
    if (!next_field(line, &at, &tag) || !next_field(line, &at, &number) || !span_is(tag, TAG)) {
        return MP_RELAY_KEYDOC_NOT_A_DOCUMENT;
    }
    if (!parse_byte(number, &id) || id == 0u) {
        return MP_RELAY_KEYDOC_ID;
    }
    while (next_field(line, &at, &field)) {
        const char *equals = memchr(field.at, '=', field.bytes);
        span_t      name;
        span_t      value;

        if (equals == NULL) {
            return MP_RELAY_KEYDOC_FIELD;
        }
        name.at     = field.at;
        name.bytes  = (size_t)(equals - field.at);
        value.at    = equals + 1;
        value.bytes = field.bytes - name.bytes - 1u;
        /* "Twice" only when the first value was not empty, which is how the relay's parser reads
         * it too. */
        if (span_is(name, "x25519")) {
            if (public_value.bytes != 0u) {
                return MP_RELAY_KEYDOC_TWICE;
            }
            public_value = value;
        } else if (span_is(name, "suites")) {
            if (suites_value.bytes != 0u) {
                return MP_RELAY_KEYDOC_TWICE;
            }
            suites_value = value;
        }
    }
    if (public_value.bytes == 0u || suites_value.bytes == 0u) {
        return MP_RELAY_KEYDOC_NOT_A_DOCUMENT;
    }
    memset(&key, 0, sizeof key);
    key.id = (uint8_t)id;
    result = read_key(public_value, key.public_key);
    if (result == MP_RELAY_KEYDOC_OK) {
        result = read_suites(suites_value, &key.suite);
    }
    if (result == MP_RELAY_KEYDOC_OK) {
        *out = key;
    }
    return result;
}

const char *mp_relay_keydoc_result_text(mp_relay_keydoc_result_t result)
{
    switch (result) {
    case MP_RELAY_KEYDOC_OK:             return "it is a key document";
    case MP_RELAY_KEYDOC_TOO_LONG:       return "it is longer than 4096 bytes";
    case MP_RELAY_KEYDOC_LINES:          return "it holds more than one line";
    case MP_RELAY_KEYDOC_NOT_A_DOCUMENT: return "it is not a key document";
    case MP_RELAY_KEYDOC_ID:             return "its key id is not 1 to 255";
    case MP_RELAY_KEYDOC_FIELD:          return "a field is not of the form name=value";
    case MP_RELAY_KEYDOC_TWICE:          return "it names a field twice";
    case MP_RELAY_KEYDOC_KEY:            return "its key is not 64 lower case hex characters";
    case MP_RELAY_KEYDOC_ZERO_KEY:       return "its key is all zeros";
    case MP_RELAY_KEYDOC_SUITES:         return "its suites are not distinct numbers to 255";
    case MP_RELAY_KEYDOC_NO_SUITE:       return "it announces no suite this build speaks";
    case MP_RELAY_KEYDOC_RESULT_COUNT:
    default:                             return "it was refused for an unnamed reason";
    }
}
