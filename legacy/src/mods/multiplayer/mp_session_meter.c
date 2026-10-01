/* mp_session_meter.c: bytes and packets sent, per peer and in all. See the header. */
#include "mp_session_meter.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void mp_meter_second_add(mp_meter_second_t *second, size_t bytes, uint32_t now_ms)
{
    /* A second is counted from its first datagram; the next datagram a second later or more
     * closes it and opens the next. A quiet stretch opens nothing, so it cannot read as a peak. */
    if (!second->started || now_ms - second->at_ms >= 1000u) {
        second->started = true;
        second->at_ms   = now_ms;
        second->bytes   = 0u;
        second->packets = 0u;
    }
    second->bytes   += (uint32_t)bytes;
    second->packets += 1u;
    if (second->most_bytes < second->bytes) {
        second->most_bytes = second->bytes;
    }
    if (second->most_packets < second->packets) {
        second->most_packets = second->packets;
    }
}

void mp_session_meter_payload_packet(mp_session_meter_t *meter, size_t packet_bytes,
                                     size_t message_bytes)
{
    size_t step = packet_bytes / MP_METER_STEP_BYTES;

    meter->packets      += 1u;
    meter->bytes_sum    += (uint32_t)packet_bytes;
    meter->messages_sum += (uint32_t)message_bytes;
    if (meter->bytes_most < packet_bytes) {
        meter->bytes_most = (uint32_t)packet_bytes;
    }
    meter->by_size[step < MP_METER_STEPS ? step : MP_METER_STEPS - 1u] += 1u;
}

void mp_session_meter_parts(mp_session_meter_t *meter, size_t bodies_bytes, size_t enemy_bytes)
{
    meter->parts     += 1u;
    meter->enemy_sum += (uint32_t)enemy_bytes;
    if (meter->bodies_most < bodies_bytes) {
        meter->bodies_most = (uint32_t)bodies_bytes;
    }
    if (meter->enemy_most < enemy_bytes) {
        meter->enemy_most = (uint32_t)enemy_bytes;
    }
}

uint32_t mp_session_meter_percentile(const mp_session_meter_t *meter, uint32_t percent)
{
    uint32_t wanted;
    uint32_t seen = 0u;
    size_t   step;

    if (meter->packets == 0u) {
        return 0u;
    }
    wanted = (uint32_t)(((uint64_t)meter->packets * percent + 99u) / 100u);
    for (step = 0; step < MP_METER_STEPS; ++step) {
        seen += meter->by_size[step];
        if (seen >= wanted) {
            return (uint32_t)((step + 1u) * MP_METER_STEP_BYTES);
        }
    }
    return meter->bytes_most;
}
