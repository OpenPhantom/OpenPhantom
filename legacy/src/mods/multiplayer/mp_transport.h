/* mp_transport.h: the one thing the session asks of a network, so the session never names one.
 *
 * Layer 0. A transport carries opaque packets to and from endpoints and promises nothing about
 * them: they may be lost, duplicated, reordered or forged, which is exactly what mp_channel is
 * built to survive. Keeping the session behind this interface is what lets the whole session be
 * proven in a test where the transport is a lump of memory with a loss dial, no socket and no
 * operating system in the process.
 *
 * An endpoint is an opaque number the transport assigns meaning to. For a UDP transport it packs
 * an address and port; for the loopback it is one of two ports. The session copies it around and
 * never interprets it, so the same session code drives a test loop and a real socket unchanged.
 *
 * Both calls are non-blocking. send returns false when the packet could not be handed off at all,
 * which is a local failure and not a lost packet; a packet accepted for sending may still be lost,
 * and that is the transport being honest rather than the call failing. recv returns the size of
 * the next packet waiting, or zero when none is, and never waits for one.
 */
#ifndef MULTIPLAYER_MP_TRANSPORT_H
#define MULTIPLAYER_MP_TRANSPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct mp_transport {
    void *context;

    /* Hand one packet to the network, addressed to `endpoint`. False is a local refusal (the send
     * buffer is full, the socket is down); it is not the same as the packet being lost later. */
    bool (*send)(void *context, uint32_t endpoint, const void *packet, size_t bytes);

    /* Take the next packet waiting, writing its source into `*from` and its bytes into `buffer`.
     * Returns the packet's size, or zero when nothing more waits. A datagram that is not a
     * packet, larger than `capacity`, empty, or a stale error report, is taken off, thrown away
     * and counted, and the next one is read in the same call: every caller loops until zero,
     * and a zero for refuse ended that loop at the first piece of it, which let anybody hold
     * the real traffic back with a few empty datagrams. */
    size_t (*recv)(void *context, uint32_t *from, void *buffer, size_t capacity);

    /* Mark `endpoint` as one the session is talking to, or release it. A transport that recycles
     * endpoint numbers must never hand a held one to a new source: a session keys its peers by
     * endpoint, so a recycled number would deliver one peer's packets, and its connection id, to
     * a stranger. Optional; a transport whose endpoints are fixed leaves it null. */
    void (*hold)(void *context, uint32_t endpoint, bool held);

    /* The address behind `endpoint`, as one number that stays the same for as long as the
     * address does, whatever endpoint number it has at the moment. A host's cookie is bound
     * to it: an endpoint a host does not hold may be recycled between its challenge and the
     * response, and the response then arrives under another number from the same address.
     * Optional; a transport whose endpoints are fixed leaves it null and the endpoint is the
     * address. */
    uint64_t (*address)(void *context, uint32_t endpoint);
} mp_transport_t;

/* Convenience wrappers that tolerate a null transport or a null call slot, so session code can be
 * written without a guard at every use. A null transport sends nothing and receives nothing. */
bool   mp_transport_send(const mp_transport_t *transport, uint32_t endpoint, const void *packet,
                         size_t bytes);
size_t mp_transport_recv(const mp_transport_t *transport, uint32_t *from, void *buffer,
                         size_t capacity);
void   mp_transport_hold(const mp_transport_t *transport, uint32_t endpoint, bool held);
uint64_t mp_transport_address(const mp_transport_t *transport, uint32_t endpoint);

#endif /* MULTIPLAYER_MP_TRANSPORT_H */
