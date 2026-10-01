/* mp_relay_socket.c: the relay's two UDP sockets. See the header. */
#include "mp_relay_socket.h"

#include "mp_relay_wire.h"
#include "mp_socket.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif

/* How much refuse one call reads past before it gives the thread back, as in mp_udp. */
#define REFUSE_PER_CALL 64

static void open_family(mp_relay_family_socket_t *f, int af)
{
    SOCKET sock = socket(af, SOCK_DGRAM, IPPROTO_UDP);
    BOOL   connreset = FALSE;
    DWORD  returned = 0;
    u_long nonblocking = 1u;
    int    bound_len;

    f->socket = (uintptr_t)INVALID_SOCKET;
    if (sock == INVALID_SOCKET) {
        f->open_error = WSAGetLastError();
        return;
    }
    (void)WSAIoctl(sock, SIO_UDP_CONNRESET, &connreset, sizeof connreset, NULL, 0, &returned, NULL,
                   NULL);
    if (af == AF_INET6) {
        DWORD only = 1;

        /* Its own family only: an IPv4 relay address is the other socket's. */
        (void)setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, (const char *)&only, (int)sizeof only);
    }
    if (ioctlsocket(sock, FIONBIO, &nonblocking) != 0) {
        f->open_error = WSAGetLastError();
        (void)closesocket(sock);
        return;
    }
    if (af == AF_INET6) {
        struct sockaddr_in6 me;
        struct sockaddr_in6 bound;

        memset(&me, 0, sizeof me);
        me.sin6_family = AF_INET6;
        bound_len      = (int)sizeof bound;
        if (bind(sock, (struct sockaddr *)&me, sizeof me) != 0 ||
            getsockname(sock, (struct sockaddr *)&bound, &bound_len) != 0) {
            f->open_error = WSAGetLastError();
            (void)closesocket(sock);
            return;
        }
        f->local_port = ntohs(bound.sin6_port);
    } else {
        struct sockaddr_in me;
        struct sockaddr_in bound;

        memset(&me, 0, sizeof me);
        me.sin_family = AF_INET;
        bound_len     = (int)sizeof bound;
        if (bind(sock, (struct sockaddr *)&me, sizeof me) != 0 ||
            getsockname(sock, (struct sockaddr *)&bound, &bound_len) != 0) {
            f->open_error = WSAGetLastError();
            (void)closesocket(sock);
            return;
        }
        f->local_port = ntohs(bound.sin_port);
    }
    f->socket = (uintptr_t)sock;
    f->open   = true;
}

bool mp_relay_socket_open(mp_relay_socket_t *s)
{
    memset(s, 0, sizeof *s);
    s->family[MP_RELAY_IPV6].socket = (uintptr_t)INVALID_SOCKET;
    s->family[MP_RELAY_IPV4].socket = (uintptr_t)INVALID_SOCKET;
    if (!mp_socket_startup()) {
        s->last_error = mp_socket_last_error();
        return false;
    }
    s->holds_winsock = true;
    open_family(&s->family[MP_RELAY_IPV6], AF_INET6);
    open_family(&s->family[MP_RELAY_IPV4], AF_INET);
    if (!s->family[MP_RELAY_IPV6].open && !s->family[MP_RELAY_IPV4].open) {
        s->last_error = s->family[MP_RELAY_IPV4].open_error;
        mp_relay_socket_close(s);
        return false;
    }
    return true;
}

void mp_relay_socket_set_relay(mp_relay_socket_t *s, const mp_relay_address_t *address,
                               size_t count)
{
    bool   taken[MP_RELAY_FAMILIES] = { false, false };
    size_t i;

    for (i = 0; i < count; ++i) {
        mp_relay_family_t         family = address[i].ipv6 ? MP_RELAY_IPV6 : MP_RELAY_IPV4;
        mp_relay_family_socket_t *f      = &s->family[family];

        if (!taken[family] && address[i].port != 0u) {
            memcpy(f->relay, address[i].bytes, sizeof f->relay);
            f->relay_port  = address[i].port;
            f->have_relay  = true;
            taken[family]  = true;
        }
    }
}

bool mp_relay_socket_usable(const mp_relay_socket_t *s, mp_relay_family_t family)
{
    return family < MP_RELAY_FAMILIES && s->family[family].open && s->family[family].have_relay;
}

bool mp_relay_socket_send(mp_relay_socket_t *s, mp_relay_family_t family, const uint8_t *data,
                          size_t bytes)
{
    mp_relay_family_socket_t *f;
    int                       sent;

    if (!mp_relay_socket_usable(s, family) || bytes == 0u || bytes > MP_RELAY_DATAGRAM_MAX) {
        return false;
    }
    f = &s->family[family];
    if (family == MP_RELAY_IPV6) {
        struct sockaddr_in6 to;

        memset(&to, 0, sizeof to);
        to.sin6_family = AF_INET6;
        to.sin6_port   = htons(f->relay_port);
        memcpy(&to.sin6_addr, f->relay, 16u);
        sent = sendto((SOCKET)f->socket, (const char *)data, (int)bytes, 0,
                      (struct sockaddr *)&to, sizeof to);
    } else {
        struct sockaddr_in to;

        memset(&to, 0, sizeof to);
        to.sin_family = AF_INET;
        to.sin_port   = htons(f->relay_port);
        memcpy(&to.sin_addr, f->relay, 4u);
        sent = sendto((SOCKET)f->socket, (const char *)data, (int)bytes, 0,
                      (struct sockaddr *)&to, sizeof to);
    }
    if (sent != (int)bytes) {
        ++f->send_failures;
        s->last_error = WSAGetLastError();
        return false;
    }
    ++f->sent;
    return true;
}

static bool from_relay(const mp_relay_family_socket_t *f, mp_relay_family_t family,
                       const struct sockaddr_storage *from)
{
    if (family == MP_RELAY_IPV6) {
        const struct sockaddr_in6 *in6 = (const struct sockaddr_in6 *)from;

        return from->ss_family == AF_INET6 && ntohs(in6->sin6_port) == f->relay_port &&
               memcmp(&in6->sin6_addr, f->relay, 16u) == 0;
    }
    {
        const struct sockaddr_in *in4 = (const struct sockaddr_in *)from;

        return from->ss_family == AF_INET && ntohs(in4->sin_port) == f->relay_port &&
               memcmp(&in4->sin_addr, f->relay, 4u) == 0;
    }
}

size_t mp_relay_socket_recv(mp_relay_socket_t *s, mp_relay_family_t family, uint8_t *buffer,
                            size_t capacity)
{
    mp_relay_family_socket_t *f;
    int                       cap;
    int                       refuse;

    if (family >= MP_RELAY_FAMILIES || !s->family[family].open || buffer == NULL ||
        capacity == 0u) {
        return 0u;
    }
    f   = &s->family[family];
    cap = capacity > (size_t)INT_MAX ? INT_MAX : (int)capacity;
    for (refuse = 0; refuse < REFUSE_PER_CALL; ++refuse) {
        struct sockaddr_storage from;
        int                     from_len = (int)sizeof from;
        int                     got;

        memset(&from, 0, sizeof from);
        got = recvfrom((SOCKET)f->socket, (char *)buffer, cap, 0, (struct sockaddr *)&from,
                       &from_len);
        if (got == SOCKET_ERROR) {
            int error = WSAGetLastError();

            if (error == WSAEWOULDBLOCK) {
                return 0u;
            }
            if (error != WSAEMSGSIZE && error != WSAECONNRESET && error != WSAENETRESET) {
                s->last_error = error;
                return 0u;
            }
            ++f->dropped;
            continue;
        }
        if (got <= 0) {
            ++f->dropped;
            continue;
        }
        if (!f->have_relay || !from_relay(f, family, &from)) {
            ++f->foreign;
            continue;
        }
        ++f->received;
        return (size_t)got;
    }
    return 0u;
}

void mp_relay_socket_close(mp_relay_socket_t *s)
{
    size_t i;

    for (i = 0; i < MP_RELAY_FAMILIES; ++i) {
        if (s->family[i].open) {
            (void)closesocket((SOCKET)s->family[i].socket);
            s->family[i].open   = false;
            s->family[i].socket = (uintptr_t)INVALID_SOCKET;
        }
    }
    if (s->holds_winsock) {
        mp_socket_shutdown();
        s->holds_winsock = false;
    }
}

const char *mp_relay_family_name(mp_relay_family_t family)
{
    return family == MP_RELAY_IPV6 ? "IPv6" : family == MP_RELAY_IPV4 ? "IPv4" : "no family";
}
