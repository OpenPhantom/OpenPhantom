/* mp_session_meter.h: what a session sends, measured in bytes and packets, per peer and in all.
 *
 * The rate a second packet in a substep is held to (mp_budget_rule.h) was set without a
 * measurement: nobody knew what a payload weighs, how many bytes a second go to one client or to
 * all of them, and how far that stands from the relay's ceilings. This counts it, and does nothing
 * else: the report prints it, and no decision reads it.
 */
#ifndef MULTIPLAYER_MP_SESSION_METER_H
#define MULTIPLAYER_MP_SESSION_METER_H

#include "mp_channel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Packet sizes in steps of sixteen bytes, for the 95th percentile. */
#define MP_METER_STEP_BYTES 16u
#define MP_METER_STEPS      (MP_CHANNEL_PACKET_BYTES / MP_METER_STEP_BYTES + 1u)

/* One second of sending, and the most any second held. */
typedef struct mp_meter_second {
    bool     started;
    uint32_t at_ms;
    uint32_t bytes;
    uint32_t packets;
    uint32_t most_bytes;
    uint32_t most_packets;
} mp_meter_second_t;

/* One peer's packets with a payload, and every datagram it was sent. */
typedef struct mp_session_meter {
    uint32_t          packets;         /* with a payload */
    uint32_t          bytes_sum;       /* their sizes on the wire */
    uint32_t          bytes_most;
    uint32_t          messages_sum;    /* the messages seated beside the payload */
    uint32_t          by_size[MP_METER_STEPS];
    mp_meter_second_t second;          /* every datagram to the peer */

    /* What the payload was made of, as the one who built it says. */
    uint32_t          parts;
    uint32_t          bodies_most;
    uint32_t          enemy_sum;
    uint32_t          enemy_most;
} mp_session_meter_t;

void mp_meter_second_add(mp_meter_second_t *second, size_t bytes, uint32_t now_ms);

/* A packet with a payload left for the peer: its size on the wire and the messages beside it. */
void mp_session_meter_payload_packet(mp_session_meter_t *meter, size_t packet_bytes,
                                     size_t message_bytes);

/* A payload was set: its bodies and its enemy block, in bytes. */
void mp_session_meter_parts(mp_session_meter_t *meter, size_t bodies_bytes, size_t enemy_bytes);

/* The size below which `percent` of the packets with a payload stayed, to a step of sixteen. */
uint32_t mp_session_meter_percentile(const mp_session_meter_t *meter, uint32_t percent);

#endif /* MULTIPLAYER_MP_SESSION_METER_H */
