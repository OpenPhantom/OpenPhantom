/* mp_payload_prefix.c: the client's prefix and the host's enemy length. See the header. */
#include "mp_payload_prefix.h"

#include "mp_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

void mp_payload_ack_from(const mp_snapshot_history_t *history, mp_payload_ack_t *out)
{
    const mp_snapshot_t *newest = history != NULL ? mp_snapshot_history_newest(history) : NULL;
    uint32_t             k;

    memset(out, 0, sizeof *out);
    if (newest == NULL || newest->tick == 0u) {
        return;   /* nought is what a side that holds nothing says, and it names no bits */
    }
    out->newest = newest->tick;
    /* The history answers for a tick only while its place still holds that tick, so a tick the
     * ring has written over reads as not held, which is what it is by now. */
    for (k = 0; k < MP_PAYLOAD_ACK_WINDOW; ++k) {
        uint32_t tick = out->newest - 1u - k;

        if (tick != 0u && mp_snapshot_history_get(history, tick) != NULL) {
            out->bits |= 1u << k;
        }
    }
}

bool mp_payload_ack_holds(const mp_payload_ack_t *ack, uint32_t tick)
{
    uint32_t k;

    if (ack == NULL || ack->newest == 0u || tick == ack->newest) {
        return false;
    }
    /* The distance back from the newest, wrapping like every tick on this wire. */
    k = ack->newest - 1u - tick;
    return k < MP_PAYLOAD_ACK_WINDOW && (ack->bits & (1u << k)) != 0u;
}

uint32_t mp_payload_ack_widest_gap(const mp_payload_ack_t *ack)
{
    uint32_t widest = 0u;
    uint32_t run    = 0u;
    uint32_t k;

    if (ack == NULL || ack->newest == 0u) {
        return 0u;
    }
    for (k = 0; k < MP_PAYLOAD_ACK_WINDOW; ++k) {
        if ((ack->bits & (1u << k)) == 0u) {
            ++run;
            continue;
        }
        if (run > widest) {
            widest = run;   /* a run closed by a held tick on both sides */
        }
        run = 0u;
    }
    return widest;
}

/* Bit 31, and bits with nothing held to count them from, are what no history produces. */
static bool ack_is_sound(const mp_payload_ack_t *ack)
{
    return (ack->bits & MP_PAYLOAD_ACK_UNUSED) == 0u && (ack->newest != 0u || ack->bits == 0u);
}

bool mp_payload_put_ack(uint8_t *buffer, size_t capacity, const mp_payload_ack_t *ack)
{
    mp_wire_writer_t w;

    if (ack == NULL || !ack_is_sound(ack)) {
        return false;
    }
    mp_wire_writer_init(&w, buffer, capacity);
    (void)mp_wire_put_u32(&w, ack->newest);
    (void)mp_wire_put_u32(&w, ack->bits);
    return !w.overflowed;
}

bool mp_payload_get_ack(const uint8_t *payload, size_t bytes, mp_payload_ack_t *out)
{
    mp_wire_reader_t r;
    mp_payload_ack_t ack;

    if (out == NULL) {
        return false;
    }
    mp_wire_reader_init(&r, payload, bytes);
    (void)mp_wire_get_u32(&r, &ack.newest);
    (void)mp_wire_get_u32(&r, &ack.bits);
    if (r.overran || !ack_is_sound(&ack)) {
        return false;
    }
    *out = ack;
    return true;
}

size_t mp_payload_world_reserve(size_t bodies)
{
    return MP_PAYLOAD_SNAPSHOT_HEADER_BYTES + MP_WIRE_BODY_MAX_BYTES * bodies;
}

bool mp_payload_put_enemy_length(uint8_t *buffer, size_t capacity, size_t enemy_bytes)
{
    if (buffer == NULL || capacity < MP_PAYLOAD_ENEMY_LENGTH_BYTES ||
        enemy_bytes > MP_PAYLOAD_ENEMY_LENGTH_MAX) {
        return false;
    }
    buffer[0] = (uint8_t)(enemy_bytes & 0xFFu);
    buffer[1] = (uint8_t)((enemy_bytes >> 8) & 0xFFu);
    return true;
}

bool mp_payload_split_world(const uint8_t *payload, size_t bytes, size_t *enemy_bytes,
                            size_t *snapshot_at)
{
    size_t length;

    if (payload == NULL || bytes < MP_PAYLOAD_ENEMY_LENGTH_BYTES) {
        return false;
    }
    length = (size_t)payload[0] | ((size_t)payload[1] << 8);
    if (MP_PAYLOAD_ENEMY_LENGTH_BYTES + length > bytes) {
        return false;
    }
    *enemy_bytes = length;
    *snapshot_at = MP_PAYLOAD_ENEMY_LENGTH_BYTES + length;
    return true;
}
