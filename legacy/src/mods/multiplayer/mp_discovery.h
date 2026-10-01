/* mp_discovery.h: one socket that shouts to the LAN and one that listens, for the announce.
 *
 * Layer 0: no engine address, no engine header, driven in the unit test without the game. It is
 * deliberately NOT the session's socket (mp_udp): an announce is connectionless and addressed to
 * everybody, and a browser listens for it from the main menu, where there is no session yet. A
 * second socket cannot disturb the first, and it comes up and goes down with the screen that
 * needs it.
 *
 * Two roles, one type. A HOST opens it on any port and broadcasts; a BROWSER opens it ON the
 * announce port and receives. The browser's bind allows the port to be shared, because two
 * instances on one machine (the way a field run on one machine is driven) both listen on it.
 *
 * The header keeps no Winsock type, as mp_udp does: the socket is a uintptr_t and a sender comes
 * back as dotted text, which is what the browser keys its rows by.
 */
#ifndef MULTIPLAYER_MP_DISCOVERY_H
#define MULTIPLAYER_MP_DISCOVERY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A dotted address with its terminator. */
#define MP_DISCOVERY_ADDRESS_MAX 16u

typedef struct mp_discovery {
    uintptr_t socket;
    bool      up;
    bool      holds_winsock;
    bool      listening;       /* bound to the announce port, rather than to any */
    uint16_t  port;            /* the port announces go to */
    int       last_error;
    uint32_t  sent;
    uint32_t  received;
    uint32_t  dropped;         /* datagrams too large for the caller's buffer */
} mp_discovery_t;

/* A host's socket: bound to any port, allowed to broadcast. `port` is where the announces go. */
bool mp_discovery_open_sender(mp_discovery_t *d, uint16_t port);

/* A browser's socket: bound to `port`, shared, receiving. */
bool mp_discovery_open_listener(mp_discovery_t *d, uint16_t port);

void mp_discovery_close(mp_discovery_t *d);

/* Sends one datagram to the broadcast address on the announce port. False when the socket is not
 * up or the send was refused; the reason is in mp_discovery_last_error. */
bool mp_discovery_broadcast(mp_discovery_t *d, const void *data, size_t bytes);

/* Sends one datagram to a named address on the announce port. What the test uses to reach a
 * listener on the same machine without depending on how the machine routes a broadcast. */
bool mp_discovery_send_to(mp_discovery_t *d, const char *address, const void *data, size_t bytes);

/* Takes one datagram if one is waiting: its bytes into `buffer`, its sender into `from` as dotted
 * text. Zero when nothing waits. Never blocks. */
size_t mp_discovery_receive(mp_discovery_t *d, void *buffer, size_t capacity,
                            char from[MP_DISCOVERY_ADDRESS_MAX]);

int mp_discovery_last_error(const mp_discovery_t *d);

#endif /* MULTIPLAYER_MP_DISCOVERY_H */
