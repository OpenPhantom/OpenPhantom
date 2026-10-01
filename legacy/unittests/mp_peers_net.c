/* mp_peers_net.c: one host and three clients over a network the test drives packet by packet.
 *
 * The loopback and the mailbox beside it have two endpoints, and everything a host does for one
 * peer at the expense of another needs three: a broadcast that one channel refuses while the
 * others take it, a peer that has stopped answering while the rest play on. This is the harness
 * for that, shared by the tests of the holds (mp_session_peers.c) and of the state notes, the
 * addressed hit and the overflow packet (mp_session_peers_state.c). It is deterministic to the
 * millisecond: every session is stepped in a fixed order on one clock the test advances, and the
 * network delivers in the order packets were sent unless the test mutes or deafens an endpoint.
 *
 * The network knows two faults, and both are the field's. A MUTED endpoint's packets are dropped
 * as they are sent, which is a machine in a long load or with its window dragged: it sends
 * nothing, so its acknowledgements stop while the host goes on sending, and what is addressed to it
 * still arrives, so a client that comes back holds everything it was sent meanwhile. A DEAF
 * endpoint is the other way round: it goes on sending, and nothing reaches it, which is a machine
 * that pumps its session and takes nothing in.
 */
#include "mp_peers_net.h"

#include "mp_channel.h"
#include "mp_session.h"
#include "mp_transport.h"
#include "mp_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct net_binding {
    net_t   *net;
    uint32_t endpoint;
} net_binding_t;

static net_binding_t s_binding[ENDPOINTS];

net_t          s_net;
mp_transport_t s_transport[ENDPOINTS];
mp_session_t   s_host;
mp_session_t   s_client[CLIENTS];
uint32_t       s_now;

static bool net_send(void *context, uint32_t endpoint, const void *packet, size_t bytes)
{
    net_binding_t *binding = (net_binding_t *)context;
    net_t         *net = binding->net;
    net_packet_t  *slot;

    ++net->sent[binding->endpoint];
    if (net->muted[binding->endpoint] || (endpoint < ENDPOINTS && net->deaf[endpoint])) {
        ++net->dropped[binding->endpoint];
        return true;   /* accepted and lost, which is what a silent machine looks like */
    }
    if (net->count == NET_SLOTS || bytes > sizeof slot->data || endpoint >= ENDPOINTS) {
        ++net->overflowed;
        return true;
    }
    slot = &net->queue[net->count++];
    slot->to    = endpoint;
    slot->from  = binding->endpoint;
    slot->bytes = bytes;
    memcpy(slot->data, packet, bytes);
    return true;
}

static size_t net_recv(void *context, uint32_t *from, void *buffer, size_t capacity)
{
    net_binding_t *binding = (net_binding_t *)context;
    net_t         *net = binding->net;
    size_t         index;
    size_t         bytes;

    for (index = 0; index < net->count; ++index) {
        if (net->queue[index].to == binding->endpoint) {
            break;
        }
    }
    if (index == net->count || net->queue[index].bytes > capacity) {
        return 0;
    }
    bytes = net->queue[index].bytes;
    memcpy(buffer, net->queue[index].data, bytes);
    *from = net->queue[index].from;
    memmove(&net->queue[index], &net->queue[index + 1u],
            (net->count - index - 1u) * sizeof net->queue[0]);
    --net->count;
    return bytes;
}

/* A host and `clients` clients, built and connected one after the other, so the host's peer
 * index of client k is k and the harness never has to look it up. */
bool net_build(size_t clients)
{
    size_t   endpoint;
    size_t   k;
    int      round;

    memset(&s_net, 0, sizeof s_net);
    s_now = 0;
    for (endpoint = 0; endpoint < ENDPOINTS; ++endpoint) {
        s_binding[endpoint].net      = &s_net;
        s_binding[endpoint].endpoint = (uint32_t)endpoint;
        memset(&s_transport[endpoint], 0, sizeof s_transport[endpoint]);
        s_transport[endpoint].context = &s_binding[endpoint];
        s_transport[endpoint].send    = &net_send;
        s_transport[endpoint].recv    = &net_recv;
    }
    mp_session_init(&s_host, MP_SESSION_HOST, &s_transport[0], 0x1111u);
    for (k = 0; k < clients; ++k) {
        mp_session_init(&s_client[k], MP_SESSION_CLIENT, &s_transport[k + 1u],
                        0x2222u + (uint32_t)k);
        mp_session_connect(&s_client[k], 0u);
        for (round = 0; round < 100 && mp_session_peer_count(&s_host) != k + 1u; ++round) {
            size_t j;

            s_now += 16u;
            mp_session_update(&s_host, s_now);
            for (j = 0; j <= k; ++j) {
                mp_session_update(&s_client[j], s_now);
            }
        }
        if (!mp_session_is_connected(&s_client[k]) ||
            mp_session_peer(&s_host, k)->endpoint != (uint32_t)(k + 1u)) {
            return false;
        }
    }
    return true;
}

/* One substep: the host receives, the test acts on it through `between`, the host services, and
 * every client receives and services. */
void net_round(size_t clients, void (*between)(void *), void *context)
{
    size_t k;

    s_now += ROUND_MS;
    mp_session_receive(&s_host, s_now);
    if (between != NULL) {
        between(context);
    }
    mp_session_service(&s_host, s_now);
    for (k = 0; k < clients; ++k) {
        mp_session_receive(&s_client[k], s_now);
        mp_session_service(&s_client[k], s_now);
    }
}

size_t make_event(uint32_t number, uint8_t *out)
{
    mp_wire_writer_t w;

    memset(out, 0, EVENT_BYTES);
    mp_wire_writer_init(&w, out, EVENT_BYTES);
    mp_wire_put_u8(&w, (uint8_t)EVENT_TAG);
    mp_wire_put_u32(&w, number);
    return EVENT_BYTES;
}

void broadcast_one(void *context)
{
    broadcast_run_t *run = (broadcast_run_t *)context;
    uint8_t          event[EVENT_BYTES];

    make_event(run->next, event);
    run->last_count = mp_session_broadcast_reliable(&s_host, event, sizeof event);
    if (run->last_count == 0u) {
        ++run->refused;
        return;   /* nobody took it; the number is offered again next round, as a caller would */
    }
    ++run->taken;
    ++run->next;
}

void read_all(size_t k, reader_t *reader)
{
    uint8_t          note[MP_CHANNEL_MESSAGE_BYTES];
    size_t           bytes = 0;
    uint32_t         number = 0;
    mp_wire_reader_t r;

    while (mp_session_read_reliable(&s_client[k], 0, note, sizeof note, &bytes)) {
        if (bytes != EVENT_BYTES || note[0] != (uint8_t)EVENT_TAG) {
            ++reader->others;
            continue;
        }
        mp_wire_reader_init(&r, note, bytes);
        (void)mp_wire_get_u8(&r, &note[0]);
        (void)mp_wire_get_u32(&r, &number);
        if (number == reader->expected) {
            ++reader->in_order;
        } else {
            ++reader->out_of_order;
        }
        reader->expected = number + 1u;
    }
}
