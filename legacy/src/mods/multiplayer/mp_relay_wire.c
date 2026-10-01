/* mp_relay_wire.c: the relay protocol's open messages and handshake bodies. See the header. */
#include "mp_relay_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static const uint8_t MAGIC[4] = { 'M', 'P', 'R', 'L' };
static const char PROLOGUE_LABEL[8] = { 'M', 'P', 'R', 'L', '2', 'N', 'K', '\0' };
static const char CROCKFORD[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

static void put16(uint8_t *at, uint16_t value)
{
    at[0] = (uint8_t)value;
    at[1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *at, uint32_t value)
{
    size_t i;

    for (i = 0; i < 4u; ++i) {
        at[i] = (uint8_t)(value >> (8u * i));
    }
}

static void put64(uint8_t *at, uint64_t value)
{
    size_t i;

    for (i = 0; i < 8u; ++i) {
        at[i] = (uint8_t)(value >> (8u * i));
    }
}

static uint16_t get16(const uint8_t *at)
{
    return (uint16_t)(at[0] | (at[1] << 8));
}

static uint32_t get32(const uint8_t *at)
{
    return (uint32_t)at[0] | ((uint32_t)at[1] << 8) | ((uint32_t)at[2] << 16) |
           ((uint32_t)at[3] << 24);
}

static uint64_t get64(const uint8_t *at)
{
    return (uint64_t)get32(at) | ((uint64_t)get32(at + 4) << 32);
}

static bool all_zero(const uint8_t *at, size_t bytes)
{
    uint8_t any = 0u;
    size_t  i;

    for (i = 0; i < bytes; ++i) {
        any = (uint8_t)(any | at[i]);
    }
    return any == 0u;
}

/* Zeroes `size` bytes and writes the common eight: type, magic, version, two zero bytes. */
static void put_control_header(uint8_t *out, size_t size, uint8_t type)
{
    memset(out, 0, size);
    out[0] = type;
    memcpy(out + 1, MAGIC, sizeof MAGIC);
    out[5] = (uint8_t)MP_RELAY_VERSION;
}

/* The common header of an open control message at least `minimum` bytes long. */
static bool header_ok(const uint8_t *in, size_t bytes, uint8_t type, size_t minimum)
{
    return in != NULL && bytes >= minimum && minimum >= MP_RELAY_CONTROL_HEADER_BYTES &&
           in[0] == type && memcmp(in + 1, MAGIC, sizeof MAGIC) == 0 &&
           in[5] == (uint8_t)MP_RELAY_VERSION && in[6] == 0u && in[7] == 0u;
}

bool mp_relay_check_control(const uint8_t *in, size_t bytes, uint8_t type, size_t size)
{
    return header_ok(in, bytes, type, size) && bytes == size;
}

size_t mp_relay_hello_encode(uint8_t role, uint64_t nonce, uint8_t *out, size_t capacity)
{
    if (out == NULL || capacity < MP_RELAY_HELLO_BYTES || role < MP_RELAY_ROLE_HOST ||
        role > MP_RELAY_ROLE_LIST) {
        return 0u;
    }
    put_control_header(out, MP_RELAY_HELLO_BYTES, (uint8_t)MP_RELAY_TYPE_HELLO);
    out[8] = role;
    put64(out + 12, nonce);
    return MP_RELAY_HELLO_BYTES;
}

bool mp_relay_cookie_decode(const uint8_t *in, size_t bytes, mp_relay_cookie_t *out)
{
    if (out == NULL ||
        !mp_relay_check_control(in, bytes, (uint8_t)MP_RELAY_TYPE_COOKIE, MP_RELAY_COOKIE_BYTES) ||
        !all_zero(in + 9, 3u)) {
        return false;
    }
    out->role  = in[8];
    out->nonce = get64(in + 12);
    out->epoch = get32(in + 20);
    memcpy(out->cookie, in + 24, sizeof out->cookie);
    return true;
}

size_t mp_relay_head_encode(const mp_relay_head_t *head, uint8_t type, uint8_t *out,
                            size_t capacity)
{
    if (head == NULL || out == NULL || capacity < MP_RELAY_REQUEST_BYTES ||
        (type != MP_RELAY_TYPE_REGISTER && type != MP_RELAY_TYPE_JOIN)) {
        return 0u;
    }
    put_control_header(out, MP_RELAY_REQUEST_BYTES, type);
    put32(out + 8, head->epoch);
    memcpy(out + 12, head->cookie, sizeof head->cookie);
    put64(out + 28, head->nonce);
    out[36] = head->key_id;
    out[37] = head->suite;
    return MP_RELAY_REQUEST_BYTES;
}

void mp_relay_prologue(const uint8_t request[MP_RELAY_HEAD_BYTES],
                       uint8_t out[MP_RELAY_PROLOGUE_BYTES])
{
    memcpy(out, PROLOGUE_LABEL, sizeof PROLOGUE_LABEL);
    memcpy(out + sizeof PROLOGUE_LABEL, request, MP_RELAY_HEAD_BYTES);
}

size_t mp_relay_register_body_encode(const mp_relay_register_body_t *body, uint8_t *out,
                                     size_t capacity)
{
    if (body == NULL || out == NULL || capacity < MP_RELAY_REGISTER_BODY_BYTES ||
        (body->flags & (uint8_t)(0xFFu ^ (MP_RELAY_REGISTER_RESUME | MP_RELAY_REGISTER_HAS_KEY))) !=
            0u) {
        return 0u;
    }
    memset(out, 0, MP_RELAY_REGISTER_BODY_BYTES);
    out[0] = body->flags;
    out[1] = body->seats;
    memcpy(out + 4, body->key, sizeof body->key);
    put64(out + 36, body->resume_id);
    memcpy(out + 44, body->resume_proof, sizeof body->resume_proof);
    return MP_RELAY_REGISTER_BODY_BYTES;
}

bool mp_relay_registered_body_decode(const uint8_t *in, size_t bytes,
                                     mp_relay_registered_body_t *out)
{
    if (in == NULL || out == NULL || bytes != MP_RELAY_REGISTERED_BODY_BYTES ||
        !all_zero(in + 49, 3u)) {
        return false;
    }
    out->session_index = get32(in);
    out->session_id    = get64(in + 4);
    memcpy(out->host_secret, in + 12, sizeof out->host_secret);
    memcpy(out->code, in + 44, sizeof out->code);
    out->refresh_ms        = get32(in + 52);
    out->member_refresh_ms = get32(in + 56);
    out->relay_epoch       = get32(in + 60);
    return true;
}

size_t mp_relay_join_body_encode(const mp_relay_join_body_t *body, uint8_t *out,
                                 size_t capacity)
{
    if (body == NULL || out == NULL || capacity < MP_RELAY_JOIN_BODY_BYTES ||
        (body->flags & (uint8_t)(0xFFu ^ MP_RELAY_JOIN_HAS_PROOF)) != 0u) {
        return 0u;
    }
    memset(out, 0, MP_RELAY_JOIN_BODY_BYTES);
    memcpy(out, body->code, sizeof body->code);
    out[5] = body->flags;
    memcpy(out + 8, body->r, sizeof body->r);
    memcpy(out + 24, body->proof, sizeof body->proof);
    return MP_RELAY_JOIN_BODY_BYTES;
}

bool mp_relay_joined_body_decode(const uint8_t *in, size_t bytes, mp_relay_joined_body_t *out)
{
    if (in == NULL || out == NULL || bytes != MP_RELAY_JOINED_BODY_BYTES ||
        (in[7] & (uint8_t)(0xFFu ^ MP_RELAY_JOINED_CONTINUED)) != 0u) {
        return false;
    }
    out->member_index      = get32(in);
    out->gen               = get16(in + 4);
    out->seats             = in[6];
    out->flags             = in[7];
    out->member_refresh_ms = get32(in + 8);
    memcpy(out->r, in + 12, sizeof out->r);
    memcpy(out->seat_secret, in + 28, sizeof out->seat_secret);
    return true;
}

bool mp_relay_nudge_decode(const uint8_t *in, size_t bytes, mp_relay_nudge_t *out)
{
    if (out == NULL ||
        !mp_relay_check_control(in, bytes, (uint8_t)MP_RELAY_TYPE_NUDGE, MP_RELAY_NUDGE_BYTES)) {
        return false;
    }
    out->session_index = get32(in + 8);
    out->relay_epoch   = get32(in + 12);
    return true;
}

size_t mp_relay_list_query_encode(const mp_relay_list_query_t *query, uint8_t *out,
                                  size_t capacity)
{
    if (query == NULL || out == NULL || capacity < MP_RELAY_LIST_QUERY_BYTES) {
        return 0u;
    }
    put_control_header(out, MP_RELAY_LIST_QUERY_BYTES, (uint8_t)MP_RELAY_TYPE_LIST_QUERY);
    put32(out + 8, query->epoch);
    memcpy(out + 12, query->cookie, sizeof query->cookie);
    put64(out + 28, query->nonce);
    put16(out + 36, query->page);
    return MP_RELAY_LIST_QUERY_BYTES;
}

bool mp_relay_list_page_decode(const uint8_t *in, size_t bytes, mp_relay_list_page_t *out)
{
    size_t count;
    size_t i;

    if (out == NULL ||
        !header_ok(in, bytes, (uint8_t)MP_RELAY_TYPE_LIST_PAGE, MP_RELAY_LIST_PAGE_HEADER_BYTES)) {
        return false;
    }
    count = in[12];
    if (count > MP_RELAY_LIST_PAGE_ENTRIES || !all_zero(in + 13, 3u) ||
        bytes != MP_RELAY_LIST_PAGE_HEADER_BYTES + count * MP_RELAY_LIST_ENTRY_BYTES) {
        return false;
    }
    out->page  = get16(in + 8);
    out->pages = get16(in + 10);
    out->count = (uint8_t)count;
    for (i = 0; i < count; ++i) {
        const uint8_t *entry = in + MP_RELAY_LIST_PAGE_HEADER_BYTES + i * MP_RELAY_LIST_ENTRY_BYTES;

        memcpy(out->entry[i].code, entry, MP_RELAY_CODE_BYTES);
        memcpy(out->entry[i].announce, entry + MP_RELAY_CODE_BYTES, MP_RELAY_ANNOUNCE_BYTES);
    }
    return true;
}

bool mp_relay_nack_decode(const uint8_t *in, size_t bytes, mp_relay_nack_t *out)
{
    if (out == NULL ||
        !mp_relay_check_control(in, bytes, (uint8_t)MP_RELAY_TYPE_NACK, MP_RELAY_NACK_BYTES) ||
        !all_zero(in + 9, 3u) || !all_zero(in + 16, 8u)) {
        return false;
    }
    out->reason = in[8];
    out->ref    = get32(in + 12);
    return true;
}

void mp_relay_code_text(const uint8_t code[MP_RELAY_CODE_BYTES],
                        char out[MP_RELAY_CODE_TEXT_BYTES])
{
    uint64_t value = 0;
    size_t   i;
    size_t   at = 8u;

    for (i = 0; i < MP_RELAY_CODE_BYTES; ++i) {
        value = (value << 8) | code[i];
    }
    out[9] = '\0';
    for (i = 0; i < 8u; ++i) {
        if (i == 4u) {
            out[at--] = '-';
        }
        out[at] = CROCKFORD[value & 31u];
        value >>= 5;
        if (at != 0u) {
            --at;
        }
    }
}

bool mp_relay_code_parse(const char *text, uint8_t code[MP_RELAY_CODE_BYTES])
{
    uint64_t value   = 0;
    size_t   symbols = 0;
    size_t   i;

    if (text == NULL || code == NULL) {
        return false;
    }
    for (; *text != '\0'; ++text) {
        char        c = *text;
        const char *found;

        if (c == '-' || c == ' ') {
            continue;
        }
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
        if (c == 'I' || c == 'L') {
            c = '1';
        } else if (c == 'O') {
            c = '0';
        }
        found = strchr(CROCKFORD, c);
        if (c == '\0' || found == NULL || symbols == 8u) {
            return false;
        }
        value = (value << 5) | (uint64_t)(found - CROCKFORD);
        ++symbols;
    }
    if (symbols != 8u) {
        return false;
    }
    for (i = MP_RELAY_CODE_BYTES; i-- != 0u;) {
        code[i] = (uint8_t)value;
        value >>= 8;
    }
    return true;
}
