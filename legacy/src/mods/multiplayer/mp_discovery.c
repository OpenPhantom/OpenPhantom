/* mp_discovery.c: the announce socket. See mp_discovery.h. */
#include "mp_discovery.h"

#include "mp_socket.h"

#include "common/logging.h"
#include "common/text.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET 0x9800000CUL
#endif

static void give_up(mp_discovery_t *d, SOCKET sock)
{
    d->last_error = WSAGetLastError();
    if (sock != INVALID_SOCKET) {
        closesocket(sock);
    }
    if (d->holds_winsock) {
        mp_socket_shutdown();
        d->holds_winsock = false;
    }
}

/* The two roles differ in three socket options and nothing else, so they share the body. */
static bool open_socket(mp_discovery_t *d, uint16_t port, bool listener)
{
    SOCKET             sock;
    struct sockaddr_in me;
    BOOL               yes = TRUE;
    BOOL               connreset = FALSE;
    DWORD              returned = 0;
    u_long             nonblocking = 1u;

    if (d == NULL) {
        return false;
    }
    memset(d, 0, sizeof *d);
    d->port = port;

    if (!mp_socket_startup()) {
        d->last_error = mp_socket_last_error();
        log_warning("the discovery socket could not start winsock");
        return false;
    }
    d->holds_winsock = true;

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        give_up(d, INVALID_SOCKET);
        log_warning("the discovery socket could not be created, error %d", d->last_error);
        return false;
    }
    (void)WSAIoctl(sock, SIO_UDP_CONNRESET, &connreset, sizeof connreset, NULL, 0, &returned, NULL,
                   NULL);
    if (ioctlsocket(sock, FIONBIO, &nonblocking) != 0) {
        give_up(d, sock);
        return false;
    }
    if (listener) {
        /* Two instances on one machine both listen here; without this the second bind fails. */
        if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof yes) != 0) {
            give_up(d, sock);
            log_warning("the discovery listener could not share its port, error %d", d->last_error);
            return false;
        }
    } else {
        if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (const char *)&yes, sizeof yes) != 0) {
            give_up(d, sock);
            log_warning("the discovery sender was refused broadcast, error %d", d->last_error);
            return false;
        }
    }

    memset(&me, 0, sizeof me);
    me.sin_family      = AF_INET;
    me.sin_addr.s_addr = htonl(INADDR_ANY);
    me.sin_port        = htons(listener ? port : 0u);
    if (bind(sock, (struct sockaddr *)&me, sizeof me) != 0) {
        give_up(d, sock);
        log_warning("the discovery socket could not bind port %u, error %d",
                    (unsigned)(listener ? port : 0u), d->last_error);
        return false;
    }

    d->socket    = (uintptr_t)sock;
    d->up        = true;
    d->listening = listener;
    return true;
}

bool mp_discovery_open_sender(mp_discovery_t *d, uint16_t port)
{
    return open_socket(d, port, false);
}

bool mp_discovery_open_listener(mp_discovery_t *d, uint16_t port)
{
    return open_socket(d, port, true);
}

void mp_discovery_close(mp_discovery_t *d)
{
    if (d == NULL) {
        return;
    }
    if (d->up) {
        closesocket((SOCKET)d->socket);
        d->up = false;
    }
    if (d->holds_winsock) {
        mp_socket_shutdown();
        d->holds_winsock = false;
    }
}

static bool send_to_ip(mp_discovery_t *d, uint32_t ip_network_order, const void *data, size_t bytes)
{
    struct sockaddr_in to;
    int                sent;

    if (d == NULL || !d->up || data == NULL || bytes == 0u || bytes > 1200u) {
        return false;
    }
    memset(&to, 0, sizeof to);
    to.sin_family      = AF_INET;
    to.sin_addr.s_addr = ip_network_order;
    to.sin_port        = htons(d->port);
    sent = sendto((SOCKET)d->socket, (const char *)data, (int)bytes, 0, (struct sockaddr *)&to,
                  sizeof to);
    if (sent != (int)bytes) {
        d->last_error = WSAGetLastError();
        return false;
    }
    ++d->sent;
    return true;
}

bool mp_discovery_broadcast(mp_discovery_t *d, const void *data, size_t bytes)
{
    return send_to_ip(d, htonl(INADDR_BROADCAST), data, bytes);
}

/* "a.b.c.d" to a network order address: four decimal parts, each 0..255, nothing else. */
static bool parse_dotted(const char *text, uint32_t *ip_network)
{
    uint32_t octet[4];
    int      index;

    for (index = 0; index < 4; ++index) {
        uint32_t value = 0;
        int      digits = 0;

        while (*text >= '0' && *text <= '9') {
            value = value * 10u + (uint32_t)(*text - '0');
            if (value > 255u) {
                return false;
            }
            ++text;
            ++digits;
        }
        if (digits == 0 || digits > 3) {
            return false;
        }
        octet[index] = value;
        if (index < 3) {
            if (*text != '.') {
                return false;
            }
            ++text;
        }
    }
    if (*text != '\0') {
        return false;
    }
    *ip_network = htonl((octet[0] << 24) | (octet[1] << 16) | (octet[2] << 8) | octet[3]);
    return true;
}

bool mp_discovery_send_to(mp_discovery_t *d, const char *address, const void *data, size_t bytes)
{
    uint32_t ip;

    if (address == NULL || !parse_dotted(address, &ip)) {
        return false;
    }
    return send_to_ip(d, ip, data, bytes);
}

size_t mp_discovery_receive(mp_discovery_t *d, void *buffer, size_t capacity,
                            char from[MP_DISCOVERY_ADDRESS_MAX])
{
    struct sockaddr_in src;
    int                src_len;
    int                got;
    int                refuse;
    const uint8_t     *ip;

    if (d == NULL || !d->up || buffer == NULL || capacity == 0u || from == NULL) {
        return 0u;
    }
    /* A zero means nothing waits, as the game socket promises: refuse ahead of an announce is
     * read past rather than ending the caller's loop, bounded so a flood of it cannot hold the
     * screen that called. */
    for (refuse = 0; ; ++refuse) {
        if (refuse == 64) {
            return 0u;
        }
        src_len = (int)sizeof src;
        got = recvfrom((SOCKET)d->socket, (char *)buffer, (int)capacity, 0,
                       (struct sockaddr *)&src, &src_len);
        if (got == SOCKET_ERROR) {
            int error = WSAGetLastError();

            /* A datagram larger than the buffer is reported as an error and discarded by the
             * stack; it is counted so a log can say it happened, and the next one is read. */
            if (error == WSAEMSGSIZE || error == WSAECONNRESET) {
                d->dropped += error == WSAEMSGSIZE ? 1u : 0u;
                continue;
            }
            if (error != WSAEWOULDBLOCK) {
                d->last_error = error;
            }
            return 0u;
        }
        if (got > 0) {
            break;
        }
        ++d->dropped;   /* an empty datagram, which no announce is */
    }
    ip = (const uint8_t *)&src.sin_addr.s_addr;
    text_format(from, MP_DISCOVERY_ADDRESS_MAX, "%u.%u.%u.%u", (unsigned)ip[0], (unsigned)ip[1],
                (unsigned)ip[2], (unsigned)ip[3]);
    ++d->received;
    return (size_t)got;
}

int mp_discovery_last_error(const mp_discovery_t *d)
{
    return d != NULL ? d->last_error : 0;
}
