/* mp_loopback.c: the in-memory network the session is proven over.
 *
 * The medium is one ring of packets in flight in both directions. A send drops the packet, delays
 * it or duplicates it according to the emulator, and otherwise files it for its destination with an
 * arrival sequence so that reordering is a deliberate choice and never an accident of iteration
 * order. A receive takes the oldest packet due for its endpoint, or, when told to reorder, the one
 * behind it. Every random choice comes from one seeded generator, so a failing run repeats.
 */
#include "mp_loopback.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static uint32_t other_endpoint(uint32_t endpoint)
{
    return endpoint == MP_LOOPBACK_ENDPOINT_A ? MP_LOOPBACK_ENDPOINT_B : MP_LOOPBACK_ENDPOINT_A;
}

/* xorshift32, deliberately not cryptographic. It only has to make loss and reorder patterns look
 * uneven and repeat on a seed, which is all a network emulator needs. */
static uint32_t next_random(mp_loopback_t *net)
{
    uint32_t x = net->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    net->rng = x;
    return x;
}

static bool roll_percent(mp_loopback_t *net, uint32_t percent)
{
    if (percent == 0) {
        return false;
    }
    if (percent >= 100) {
        return true;
    }
    return (next_random(net) % 100u) < percent;
}

void mp_loopback_init(mp_loopback_t *net, const mp_loopback_conditions_t *conditions, uint32_t seed)
{
    memset(net, 0, sizeof(*net));
    if (conditions != NULL) {
        net->conditions = *conditions;
    }
    /* A zero seed would leave xorshift stuck at zero forever, so it is not allowed to mean that. */
    net->rng = (seed != 0) ? seed : 0x9E3779B9u;
}

void mp_loopback_pump(mp_loopback_t *net, uint32_t now_ms)
{
    net->now_ms = now_ms;
}

static mp_loopback_packet_t *free_slot(mp_loopback_t *net)
{
    size_t i;

    for (i = 0; i < MP_LOOPBACK_RING; ++i) {
        if (!net->ring[i].used) {
            return &net->ring[i];
        }
    }
    return NULL;
}

static void file_packet(mp_loopback_t *net, uint32_t to, const void *packet, size_t bytes)
{
    mp_loopback_packet_t *slot = free_slot(net);

    if (slot == NULL) {
        ++net->dropped;     /* a full ring is an overloaded link, a lost packet like any other */
        return;
    }
    slot->used          = true;
    slot->to            = to;
    slot->deliver_at_ms = net->now_ms + net->conditions.delay_ms;
    slot->sequence      = net->next_sequence++;
    slot->bytes         = bytes;
    memcpy(slot->data, packet, bytes);
}

static bool loopback_send(void *context, uint32_t endpoint, const void *packet, size_t bytes)
{
    mp_loopback_binding_t *binding = (mp_loopback_binding_t *)context;
    mp_loopback_t         *net;

    if (binding == NULL || binding->net == NULL) {
        return false;
    }
    net = binding->net;

    /* An unsendable size is a real local refusal, not a lost packet: no length of retrying fixes a
     * packet that does not fit the medium. */
    if (bytes == 0 || bytes > MP_LOOPBACK_PACKET_BYTES) {
        return false;
    }

    if (roll_percent(net, net->conditions.loss_percent)) {
        ++net->dropped;
        return true;    /* accepted for sending, then lost; the caller cannot tell, by design */
    }

    file_packet(net, endpoint, packet, bytes);

    if (roll_percent(net, net->conditions.duplicate_percent)) {
        file_packet(net, endpoint, packet, bytes);
    }
    return true;
}

/* The oldest packet due for `to`, and separately the second oldest, so a reorder can pick the
 * latter without a second scan. Either may come back NULL. */
static void two_oldest_due(mp_loopback_t *net, uint32_t to, mp_loopback_packet_t **first,
                           mp_loopback_packet_t **second)
{
    size_t i;

    *first  = NULL;
    *second = NULL;
    for (i = 0; i < MP_LOOPBACK_RING; ++i) {
        mp_loopback_packet_t *p = &net->ring[i];

        if (!p->used || p->to != to || p->deliver_at_ms > net->now_ms) {
            continue;
        }
        if (*first == NULL || p->sequence < (*first)->sequence) {
            *second = *first;
            *first  = p;
        } else if (*second == NULL || p->sequence < (*second)->sequence) {
            *second = p;
        }
    }
}

static size_t loopback_recv(void *context, uint32_t *from, void *buffer, size_t capacity)
{
    mp_loopback_binding_t *binding = (mp_loopback_binding_t *)context;
    mp_loopback_t         *net;
    mp_loopback_packet_t  *first;
    mp_loopback_packet_t  *second;
    mp_loopback_packet_t  *chosen;
    size_t                 bytes;

    if (binding == NULL || binding->net == NULL) {
        return 0;
    }
    net = binding->net;

    /* Too large for the caller's buffer: dropped rather than truncated, and counted, because
     * half a packet is not a packet, and the next one is read in the same call, as the socket
     * does. Each pass takes one packet off the queue, so the loop ends with it. */
    for (;;) {
        two_oldest_due(net, binding->endpoint, &first, &second);
        if (first == NULL) {
            return 0;
        }
        chosen = first;
        if (second != NULL && roll_percent(net, net->conditions.reorder_percent)) {
            chosen = second;    /* deliver the one behind, so the pair arrives out of order */
        }
        if (chosen->bytes <= capacity) {
            break;
        }
        chosen->used = false;
        ++net->dropped;
    }

    bytes = chosen->bytes;
    memcpy(buffer, chosen->data, bytes);
    if (from != NULL) {
        *from = other_endpoint(binding->endpoint);
    }
    chosen->used = false;
    ++net->delivered;
    return bytes;
}

mp_transport_t mp_loopback_transport(mp_loopback_t *net, uint32_t endpoint)
{
    mp_transport_t transport;
    uint32_t       slot = (endpoint == MP_LOOPBACK_ENDPOINT_B) ? 1u : 0u;

    net->binding[slot].net      = net;
    net->binding[slot].endpoint = endpoint;

    transport.context = &net->binding[slot];
    transport.send    = &loopback_send;
    transport.recv    = &loopback_recv;
    transport.hold    = NULL;   /* two fixed endpoints, nothing is ever recycled */
    transport.address = NULL;   /* and each endpoint is its own address */
    return transport;
}
