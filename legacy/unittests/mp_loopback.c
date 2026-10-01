/* mp_loopback.c: the medium on its own, then the whole reliability chain proven over it.
 *
 * The first half checks the emulator does what it says: a packet sent arrives, a delayed packet
 * waits for the clock, a fully lossy link delivers nothing, and a packet too big for the reader is
 * dropped rather than torn. The second half is the one that matters: two channels talk across the
 * loopback while it drops, duplicates, reorders and delays, and every reliable message still
 * arrives exactly once and in order. A session that connects and transfers nothing while the
 * protocol reports success is the failure this whole feature is most likely to produce, and this
 * is the test that would catch it.
 */
#include "unittest.h"

#include "mp_channel.h"
#include "mp_loopback.h"
#include "mp_transport.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void check_medium(void)
{
    mp_loopback_t net;
    mp_transport_t a;
    mp_transport_t b;
    uint8_t  got[MP_LOOPBACK_PACKET_BYTES];
    uint32_t from = 0xFFFFFFFFu;

    ut_section("the medium, with no losses");

    mp_loopback_init(&net, NULL, 1u);
    a = mp_loopback_transport(&net, MP_LOOPBACK_ENDPOINT_A);
    b = mp_loopback_transport(&net, MP_LOOPBACK_ENDPOINT_B);

    ut_check(mp_transport_send(&a, MP_LOOPBACK_ENDPOINT_B, "hello", 5), "a send is accepted");
    ut_check(mp_transport_recv(&a, &from, got, sizeof got) == 0,
             "the sender does not receive its own packet");
    ut_check(mp_transport_recv(&b, &from, got, sizeof got) == 5, "the peer receives it, whole");
    ut_check(from == MP_LOOPBACK_ENDPOINT_A, "and learns who sent it");
    ut_check(memcmp(got, "hello", 5) == 0, "with its bytes intact");
    ut_check(mp_transport_recv(&b, &from, got, sizeof got) == 0, "and nothing waits after it");

    ut_check(!mp_transport_send(&a, MP_LOOPBACK_ENDPOINT_B, "x", 0),
             "a zero byte packet is a local refusal, not a lost packet");
    ut_check(!mp_transport_send(&a, MP_LOOPBACK_ENDPOINT_B, got, sizeof got + 1u),
             "so is one past the medium's size");
}

static void check_delay(void)
{
    mp_loopback_t net;
    mp_loopback_conditions_t cond;
    mp_transport_t a;
    mp_transport_t b;
    uint8_t  got[16];
    uint32_t from = 0;

    ut_section("a delayed packet waits for the clock");

    memset(&cond, 0, sizeof cond);
    cond.delay_ms = 50u;
    mp_loopback_init(&net, &cond, 1u);
    a = mp_loopback_transport(&net, MP_LOOPBACK_ENDPOINT_A);
    b = mp_loopback_transport(&net, MP_LOOPBACK_ENDPOINT_B);

    mp_loopback_pump(&net, 1000u);
    mp_transport_send(&a, MP_LOOPBACK_ENDPOINT_B, "z", 1);
    ut_check(mp_transport_recv(&b, &from, got, sizeof got) == 0,
             "it is not receivable before its delay is up");
    mp_loopback_pump(&net, 1049u);
    ut_check(mp_transport_recv(&b, &from, got, sizeof got) == 0, "not one millisecond early");
    mp_loopback_pump(&net, 1050u);
    ut_check(mp_transport_recv(&b, &from, got, sizeof got) == 1, "and receivable once it is");
}

static void check_extremes(void)
{
    mp_loopback_t net;
    mp_loopback_conditions_t cond;
    mp_transport_t a;
    mp_transport_t b;
    uint8_t  got[4];
    uint32_t from = 0;
    int      i;

    ut_section("a fully lossy link, and a packet too big to read");

    memset(&cond, 0, sizeof cond);
    cond.loss_percent = 100u;
    mp_loopback_init(&net, &cond, 7u);
    a = mp_loopback_transport(&net, MP_LOOPBACK_ENDPOINT_A);
    b = mp_loopback_transport(&net, MP_LOOPBACK_ENDPOINT_B);
    for (i = 0; i < 20; ++i) {
        ut_check(mp_transport_send(&a, MP_LOOPBACK_ENDPOINT_B, "aaaa", 4),
                 "a send into a lossy link is still locally accepted");
    }
    ut_check(mp_transport_recv(&b, &from, got, sizeof got) == 0, "and nothing arrives");

    memset(&cond, 0, sizeof cond);
    mp_loopback_init(&net, &cond, 1u);
    a = mp_loopback_transport(&net, MP_LOOPBACK_ENDPOINT_A);
    b = mp_loopback_transport(&net, MP_LOOPBACK_ENDPOINT_B);
    mp_transport_send(&a, MP_LOOPBACK_ENDPOINT_B, "aaaa", 4);
    ut_check(mp_transport_recv(&b, &from, got, 3u) == 0,
             "a packet too big for the reader's buffer is dropped, not torn");
    ut_check(mp_transport_recv(&b, &from, got, sizeof got) == 0, "and it is gone, not requeued");
}

/* The whole chain. Two channels, a hostile link, and the one property that has to hold: every
 * reliable message arrives once and in order, however much the medium abuses the packets. */
static void check_reliable_over_loss(void)
{
    enum { MESSAGE_COUNT = 50, TICK_MS = 16, MAX_TICKS = 4000 };

    mp_loopback_t net;
    mp_loopback_conditions_t cond;
    mp_transport_t ta;
    mp_transport_t tb;
    mp_channel_t   ca;
    mp_channel_t   cb;
    uint32_t now = 0;
    int      queued;
    int      received = 0;
    uint8_t  expected = 0;
    int      tick;
    bool     order_held = true;

    ut_section("every reliable message survives a hostile link");

    cond.loss_percent      = 25u;
    cond.duplicate_percent = 10u;
    cond.reorder_percent   = 20u;
    cond.delay_ms          = 24u;
    mp_loopback_init(&net, &cond, 0xC0FFEEu);
    ta = mp_loopback_transport(&net, MP_LOOPBACK_ENDPOINT_A);
    tb = mp_loopback_transport(&net, MP_LOOPBACK_ENDPOINT_B);
    mp_channel_init(&ca);
    mp_channel_init(&cb);

    for (queued = 0; queued < MESSAGE_COUNT; ++queued) {
        uint8_t body = (uint8_t)queued;

        ut_check(mp_channel_send(&ca, &body, 1u), "a message queues into the send window");
    }

    for (tick = 0; tick < MAX_TICKS && received < MESSAGE_COUNT; ++tick) {
        uint8_t  packet[MP_LOOPBACK_PACKET_BYTES];
        size_t   packet_bytes;
        uint32_t from;
        size_t   size;

        now += TICK_MS;
        mp_loopback_pump(&net, now);

        if (mp_channel_packet_build(&ca, now, NULL, 0, packet, sizeof packet, &packet_bytes)) {
            mp_transport_send(&ta, MP_LOOPBACK_ENDPOINT_B, packet, packet_bytes);
        }
        if (mp_channel_packet_build(&cb, now, NULL, 0, packet, sizeof packet, &packet_bytes)) {
            mp_transport_send(&tb, MP_LOOPBACK_ENDPOINT_A, packet, packet_bytes);
        }

        while ((size = mp_transport_recv(&ta, &from, packet, sizeof packet)) > 0) {
            const uint8_t *payload;
            size_t         payload_bytes;

            mp_channel_packet_receive(&ca, packet, size, &payload, &payload_bytes);
        }
        while ((size = mp_transport_recv(&tb, &from, packet, sizeof packet)) > 0) {
            const uint8_t *payload;
            size_t         payload_bytes;

            if (mp_channel_packet_receive(&cb, packet, size, &payload, &payload_bytes)) {
                uint8_t message[8];
                size_t  message_bytes;

                while (mp_channel_message_read(&cb, message, sizeof message, &message_bytes)) {
                    if (message_bytes != 1u || message[0] != expected) {
                        order_held = false;
                    }
                    ++expected;
                    ++received;
                }
            }
        }
    }

    ut_check(received == MESSAGE_COUNT, "all fifty reliable messages arrive despite the losses");
    ut_check(order_held, "each arrives exactly once and in the order it was sent");
    ut_check(net.delivered > 0 && net.dropped > 0,
             "and the link really was hostile: it both delivered and dropped");
}

int main(void)
{
    check_medium();
    check_delay();
    check_extremes();
    check_reliable_over_loss();

    return ut_summary("mp_loopback");
}
