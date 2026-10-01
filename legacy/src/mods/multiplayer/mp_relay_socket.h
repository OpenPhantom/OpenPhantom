/* mp_relay_socket.h: the two UDP sockets a PC talks to the relay through, one per address family.
 *
 * Layer 0. The LAN's socket (mp_udp) speaks IPv4 to whoever writes to it; this one speaks to one
 * peer, the relay, over IPv6 and IPv4 both, because which of the two reaches the relay from a
 * given network is only known by trying. Each family has its own non-blocking socket on an
 * ephemeral port and the relay's address in that family, when the lookup found one. A datagram
 * from any other address or port is taken off and counted, never handed on: the relay is the only
 * peer a public session has.
 *
 * The header keeps no Winsock type, like mp_udp.h.
 */
#ifndef MULTIPLAYER_MP_RELAY_SOCKET_H
#define MULTIPLAYER_MP_RELAY_SOCKET_H

#include "mp_relay_keyfetch.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum mp_relay_family {
    MP_RELAY_IPV6 = 0,
    MP_RELAY_IPV4 = 1,
    MP_RELAY_FAMILIES = 2
} mp_relay_family_t;

typedef struct mp_relay_family_socket {
    uintptr_t socket;         /* a SOCKET, opaque here */
    bool      open;
    bool      have_relay;
    uint8_t   relay[16];      /* the relay's address in this family; IPv4 in the first four */
    uint16_t  relay_port;     /* host order */
    uint16_t  local_port;     /* host order */
    int       open_error;     /* why the socket did not open, 0 when it did */
    uint32_t  sent;
    uint32_t  send_failures;
    uint32_t  received;
    uint32_t  foreign;        /* from an address or port that is not the relay's */
    uint32_t  dropped;        /* too large, empty, or a stale error report */
} mp_relay_family_socket_t;

typedef struct mp_relay_socket {
    bool                     holds_winsock;
    int                      last_error;
    mp_relay_family_socket_t family[MP_RELAY_FAMILIES];
} mp_relay_socket_t;

/* Opens a socket in each family the system has. False when neither opened, with nothing left
 * open. */
bool mp_relay_socket_open(mp_relay_socket_t *s);

/* The relay's addresses from a lookup: the first of each family is kept. A family the lookup
 * found no address in keeps the one it had, so an answer that lost its AAAA record does not strand
 * the leg that runs over IPv6. */
void mp_relay_socket_set_relay(mp_relay_socket_t *s, const mp_relay_address_t *address,
                               size_t count);

/* Whether a family can reach the relay: its socket is open and its address known. */
bool mp_relay_socket_usable(const mp_relay_socket_t *s, mp_relay_family_t family);

/* One datagram to the relay in `family`. False when it was not handed to the stack. */
bool mp_relay_socket_send(mp_relay_socket_t *s, mp_relay_family_t family, const uint8_t *data,
                          size_t bytes);

/* The next datagram from the relay in `family`, or 0 when none waits. Anything else that waits is
 * taken off and counted on the way. */
size_t mp_relay_socket_recv(mp_relay_socket_t *s, mp_relay_family_t family, uint8_t *buffer,
                            size_t capacity);

/* Closes both sockets and returns the Winsock reference. Idempotent. */
void mp_relay_socket_close(mp_relay_socket_t *s);

const char *mp_relay_family_name(mp_relay_family_t family);

#endif /* MULTIPLAYER_MP_RELAY_SOCKET_H */
