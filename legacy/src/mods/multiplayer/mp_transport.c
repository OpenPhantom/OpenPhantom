/* mp_transport.c: the guards that let the rest of the session skip them. */
#include "mp_transport.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool mp_transport_send(const mp_transport_t *transport, uint32_t endpoint, const void *packet,
                       size_t bytes)
{
    if (transport == NULL || transport->send == NULL) {
        return false;
    }
    return transport->send(transport->context, endpoint, packet, bytes);
}

size_t mp_transport_recv(const mp_transport_t *transport, uint32_t *from, void *buffer,
                         size_t capacity)
{
    if (transport == NULL || transport->recv == NULL) {
        return 0;
    }
    return transport->recv(transport->context, from, buffer, capacity);
}

void mp_transport_hold(const mp_transport_t *transport, uint32_t endpoint, bool held)
{
    if (transport == NULL || transport->hold == NULL) {
        return;
    }
    transport->hold(transport->context, endpoint, held);
}

uint64_t mp_transport_address(const mp_transport_t *transport, uint32_t endpoint)
{
    if (transport == NULL || transport->address == NULL) {
        return (uint64_t)endpoint;
    }
    return transport->address(transport->context, endpoint);
}
