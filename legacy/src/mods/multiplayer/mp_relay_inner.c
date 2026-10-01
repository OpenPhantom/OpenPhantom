/* mp_relay_inner.c: the sealed header and the sealed control messages. See the header. */
#include "mp_relay_inner.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

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

/* A plaintext of `size` bytes whose first byte is `inner`. */
static bool exact(const uint8_t *in, size_t bytes, size_t size, uint8_t inner)
{
    return in != NULL && bytes == size && in[0] == inner;
}

void mp_relay_sealed_header_put(const mp_relay_sealed_header_t *header,
                                uint8_t out[MP_RELAY_SEALED_HEADER_BYTES])
{
    out[0] = header->type;
    out[1] = header->slot;
    put16(out + 2, header->gen);
    put32(out + 4, header->index);
    put64(out + 8, header->counter);
}

bool mp_relay_is_sealed_type(uint8_t type)
{
    return type >= MP_RELAY_TYPE_HOST_TO_RELAY && type <= MP_RELAY_TYPE_RELAY_TO_MEMBER_CONTROL;
}

bool mp_relay_sealed_header_read(const uint8_t *in, size_t bytes, mp_relay_sealed_header_t *out)
{
    if (in == NULL || out == NULL || bytes < MP_RELAY_SEALED_OVERHEAD ||
        bytes > MP_RELAY_DATAGRAM_MAX || !mp_relay_is_sealed_type(in[0])) {
        return false;
    }
    out->type    = in[0];
    out->slot    = in[1];
    out->gen     = get16(in + 2);
    out->index   = get32(in + 4);
    out->counter = get64(in + 8);
    return true;
}

bool mp_relay_inner_allowed(uint8_t outer, uint8_t inner)
{
    switch (outer) {
    case MP_RELAY_TYPE_HOST_TO_RELAY_CONTROL:
        return inner == MP_RELAY_INNER_REFRESH || inner == MP_RELAY_INNER_KICK ||
               inner == MP_RELAY_INNER_CLOSE;
    case MP_RELAY_TYPE_RELAY_TO_HOST_CONTROL:
        return inner == MP_RELAY_INNER_REFRESH_ACK || inner == MP_RELAY_INNER_MEMBER_OPEN ||
               inner == MP_RELAY_INNER_MEMBER_CLOSED || inner == MP_RELAY_INNER_NACK;
    case MP_RELAY_TYPE_MEMBER_TO_RELAY_CONTROL:
        return inner == MP_RELAY_INNER_MEMBER_REFRESH || inner == MP_RELAY_INNER_LEAVE;
    case MP_RELAY_TYPE_RELAY_TO_MEMBER_CONTROL:
        return inner == MP_RELAY_INNER_MEMBER_ACK || inner == MP_RELAY_INNER_NACK;
    default:
        return false;
    }
}

size_t mp_relay_refresh_encode(uint32_t epoch, uint8_t flags,
                               const uint8_t announce[MP_RELAY_ANNOUNCE_BYTES], uint8_t *out,
                               size_t capacity)
{
    if (out == NULL || announce == NULL || capacity < MP_RELAY_REFRESH_BYTES ||
        (flags & (uint8_t)(0xFFu ^ MP_RELAY_REFRESH_LISTED)) != 0u) {
        return 0u;
    }
    memset(out, 0, MP_RELAY_REFRESH_BYTES);
    out[0] = (uint8_t)MP_RELAY_INNER_REFRESH;
    put32(out + 1, epoch);
    out[5] = flags;
    memcpy(out + 6, announce, MP_RELAY_ANNOUNCE_BYTES);
    return MP_RELAY_REFRESH_BYTES;
}

bool mp_relay_refresh_ack_decode(const uint8_t *in, size_t bytes, mp_relay_refresh_ack_t *out)
{
    if (out == NULL ||
        !exact(in, bytes, MP_RELAY_REFRESH_ACK_BYTES, (uint8_t)MP_RELAY_INNER_REFRESH_ACK)) {
        return false;
    }
    out->epoch   = get32(in + 1);
    out->counter = get64(in + 5);
    return true;
}

bool mp_relay_member_open_decode(const uint8_t *in, size_t bytes, mp_relay_member_open_t *out)
{
    if (out == NULL ||
        !exact(in, bytes, MP_RELAY_MEMBER_OPEN_BYTES, (uint8_t)MP_RELAY_INNER_MEMBER_OPEN) ||
        (in[2] & (uint8_t)(0xFFu ^ MP_RELAY_MEMBER_OPEN_CONTINUES)) != 0u) {
        return false;
    }
    out->slot  = in[1];
    out->flags = in[2];
    out->gen   = get16(in + 3);
    memcpy(out->handle, in + 5, sizeof out->handle);
    memcpy(out->r, in + 21, sizeof out->r);
    return true;
}

bool mp_relay_member_closed_decode(const uint8_t *in, size_t bytes,
                                   mp_relay_member_closed_t *out)
{
    if (out == NULL ||
        !exact(in, bytes, MP_RELAY_MEMBER_CLOSED_BYTES, (uint8_t)MP_RELAY_INNER_MEMBER_CLOSED)) {
        return false;
    }
    out->slot   = in[1];
    out->reason = in[2];
    out->gen    = get16(in + 3);
    return true;
}

size_t mp_relay_close_encode(uint64_t session_id, uint8_t *out, size_t capacity)
{
    if (out == NULL || capacity < MP_RELAY_CLOSE_BYTES) {
        return 0u;
    }
    out[0] = (uint8_t)MP_RELAY_INNER_CLOSE;
    put64(out + 1, session_id);
    return MP_RELAY_CLOSE_BYTES;
}

size_t mp_relay_member_refresh_encode(uint8_t *out, size_t capacity)
{
    if (out == NULL || capacity < MP_RELAY_MEMBER_REFRESH_BYTES) {
        return 0u;
    }
    memset(out, 0, MP_RELAY_MEMBER_REFRESH_BYTES);
    out[0] = (uint8_t)MP_RELAY_INNER_MEMBER_REFRESH;
    return MP_RELAY_MEMBER_REFRESH_BYTES;
}

bool mp_relay_member_ack_check(const uint8_t *in, size_t bytes)
{
    return exact(in, bytes, MP_RELAY_MEMBER_ACK_BYTES, (uint8_t)MP_RELAY_INNER_MEMBER_ACK);
}

size_t mp_relay_leave_encode(uint8_t *out, size_t capacity)
{
    if (out == NULL || capacity < MP_RELAY_LEAVE_BYTES) {
        return 0u;
    }
    out[0] = (uint8_t)MP_RELAY_INNER_LEAVE;
    return MP_RELAY_LEAVE_BYTES;
}

bool mp_relay_leg_nack_decode(const uint8_t *in, size_t bytes, mp_relay_nack_t *out)
{
    if (out == NULL || !exact(in, bytes, MP_RELAY_LEG_NACK_BYTES, (uint8_t)MP_RELAY_INNER_NACK)) {
        return false;
    }
    out->reason = in[1];
    out->ref    = get32(in + 2);
    return true;
}
