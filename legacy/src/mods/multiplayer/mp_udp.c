/* mp_udp.c: the non-blocking UDP socket, and the Windows edges the loopback never had.
 *
 * The vtable is filled the same as the loopback's: send hands a datagram to the network, recv takes
 * the next waiting one, both without blocking. What is here that the loopback did not need is the
 * Windows UDP behaviour the 1999 prototype got wrong and this must not repeat: the socket handle is
 * unsigned so it is tested against INVALID_SOCKET rather than a sign; a previous send that drew an
 * ICMP unreachable makes the next recv report WSAECONNRESET on a socket that is not dead, so that
 * and its kin are swallowed; and an oversized datagram is dropped whole rather than torn.
 *
 * An address is parsed by hand into four octets and a port, so the parser needs no OS call and runs
 * on any Windows the game does. Addresses are interned into a small table and the caller only ever
 * sees the index, so the whole address never passes through the 32-bit endpoint.
 *
 * The shipped executable brings no Winsock of its own: its import strings carry neither WS2_32 nor
 * WSAStartup, so this feature is the only thing that loads the library and the counted startup in
 * mp_socket is the whole of its lifetime. The prototype's Winsock lived in a separate editor build.
 */
#include "mp_udp.h"

#include "mp_socket.h"

#include "common/logging.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The control code that turns off the connection-reset report on a UDP socket. It lives in
 * mstcpip.h, which is not always reachable under this toolset, so it is defined here when the
 * header did not bring it. */
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif

static uint32_t intern(mp_udp_t *udp, uint32_t ip, uint16_t port)
{
    uint32_t i;

    size_t oldest = 0;

    for (i = 0; i < MP_UDP_MAX_ENDPOINTS; ++i) {
        if (udp->slots[i].used && udp->slots[i].ip == ip && udp->slots[i].port == port) {
            udp->slots[i].last_used = ++udp->use_clock;
            return i + 1u;
        }
    }
    for (i = 0; i < MP_UDP_MAX_ENDPOINTS; ++i) {
        if (!udp->slots[i].used) {
            udp->slots[i].used      = true;
            udp->slots[i].ip        = ip;
            udp->slots[i].port      = port;
            udp->slots[i].last_used = ++udp->use_clock;
            return i + 1u;
        }
    }
    /* Full: recycle the least-recently-used slot the session does not hold. A held slot carries
     * a peer, and the number it is known by must not change hands while it does, whatever a burst
     * of strangers has done to the recency order. With no unheld slot left the newcomer is
     * dropped, which is the deafness the recency rule exists to avoid, but only towards strangers
     * and only while every slot is a peer. */
    oldest = MP_UDP_MAX_ENDPOINTS;
    for (i = 0; i < MP_UDP_MAX_ENDPOINTS; ++i) {
        if (udp->slots[i].held) {
            continue;
        }
        if (oldest == MP_UDP_MAX_ENDPOINTS ||
            udp->slots[i].last_used < udp->slots[oldest].last_used) {
            oldest = i;
        }
    }
    if (oldest == MP_UDP_MAX_ENDPOINTS) {
        return 0u;
    }
    udp->slots[oldest].ip        = ip;
    udp->slots[oldest].port      = port;
    udp->slots[oldest].last_used = ++udp->use_clock;
    return oldest + 1u;
}

static bool udp_send(void *context, uint32_t endpoint, const void *packet, size_t bytes)
{
    mp_udp_t          *udp = (mp_udp_t *)context;
    struct sockaddr_in to;
    int                sent;

    if (udp == NULL || !udp->up || bytes == 0u || bytes > MP_UDP_PACKET_BYTES) {
        return false;   /* an unsendable size is a local refusal, not a lost packet */
    }
    if (endpoint == 0u || endpoint > MP_UDP_MAX_ENDPOINTS || !udp->slots[endpoint - 1u].used) {
        return false;
    }
    /* Stamp on send too: this is what keeps a connected peer's slot the freshest in the table,
     * because the session sends to every connected peer every tick even when it is silent. */
    udp->slots[endpoint - 1u].last_used = ++udp->use_clock;

    memset(&to, 0, sizeof to);
    to.sin_family      = AF_INET;
    to.sin_addr.s_addr = udp->slots[endpoint - 1u].ip;
    to.sin_port        = udp->slots[endpoint - 1u].port;

    sent = sendto((SOCKET)udp->socket, (const char *)packet, (int)bytes, 0,
                  (struct sockaddr *)&to, sizeof to);
    if (sent == (int)bytes) {
        ++udp->sent_datagrams;
    } else {
        ++udp->send_failures;
    }
    if (sent == SOCKET_ERROR) {
        udp->last_error = WSAGetLastError();
        return false;   /* a full send buffer is a local refusal; the packet was not sent */
    }
    return true;
}

/* How much refuse one call reads past before it gives the caller back its thread. The loop ends
 * on its own when the queue is empty; this only bounds a flood of it. */
#define MP_UDP_REFUSE_PER_CALL 64

static size_t udp_recv(void *context, uint32_t *from, void *buffer, size_t capacity)
{
    mp_udp_t          *udp = (mp_udp_t *)context;
    struct sockaddr_in src;
    int                src_len;
    int                got;
    int                cap;
    int                refuse;

    if (udp == NULL || !udp->up || capacity == 0u) {
        return 0u;
    }
    cap = capacity > (size_t)INT_MAX ? INT_MAX : (int)capacity;

    for (refuse = 0; refuse < MP_UDP_REFUSE_PER_CALL; ++refuse) {
        memset(&src, 0, sizeof src);
        src_len = (int)sizeof src;
        got = recvfrom((SOCKET)udp->socket, (char *)buffer, cap, 0, (struct sockaddr *)&src,
                       &src_len);
        if (got == SOCKET_ERROR) {
            int error = WSAGetLastError();

            if (error == WSAEWOULDBLOCK) {
                return 0u;   /* the one zero a caller may read as "nothing waits" */
            }
            /* An oversized datagram the stack has already dropped, or a stale ICMP unreachable
             * from an earlier send: neither is a dead socket, and the datagram behind it may be
             * a peer's. Anything else is the socket itself, and the call ends there. */
            if (error != WSAEMSGSIZE && error != WSAECONNRESET && error != WSAENETRESET) {
                udp->last_error = error;
                return 0u;
            }
            ++udp->recv_dropped;
            continue;
        }
        if (got <= 0) {
            ++udp->recv_dropped;   /* an empty datagram is somebody's, and not a packet */
            continue;
        }

        /* Counted before anything can refuse it, because the question this answers is whether
         * the datagram reached this process at all. */
        ++udp->recv_datagrams;
        udp->recv_bytes += (uint32_t)got;
        udp->last_source_ip = ntohl(src.sin_addr.s_addr);
        udp->last_source_port = ntohs(src.sin_port);

        if (from != NULL) {
            *from = intern(udp, src.sin_addr.s_addr, src.sin_port);
            if (*from == 0u) {
                ++udp->recv_unnumbered;
                continue;   /* every endpoint is a held peer; a stranger's has no number */
            }
        }
        return (size_t)got;
    }
    return 0u;   /* a run of refuse long enough to stop for; the next call reads on */
}

static void udp_hold(void *context, uint32_t endpoint, bool held)
{
    mp_udp_t *udp = (mp_udp_t *)context;

    if (udp == NULL || endpoint == 0u || endpoint > MP_UDP_MAX_ENDPOINTS ||
        !udp->slots[endpoint - 1u].used) {
        return;
    }
    udp->slots[endpoint - 1u].held = held;
}

/* The address and port behind an endpoint, packed, for a cookie that has to outlive a recycled
 * endpoint number. An endpoint nobody has now answers 0, which no cookie was made for. */
static uint64_t udp_address(void *context, uint32_t endpoint)
{
    const mp_udp_t *udp = (const mp_udp_t *)context;

    if (udp == NULL || endpoint == 0u || endpoint > MP_UDP_MAX_ENDPOINTS ||
        !udp->slots[endpoint - 1u].used) {
        return 0u;
    }
    return ((uint64_t)udp->slots[endpoint - 1u].ip << 16) | udp->slots[endpoint - 1u].port;
}

/* Parse "a.b.c.d", four decimal octets 0..255, into a network-order address. No OS call, so it runs
 * on any Windows and cannot pull in a version-gated resolver: the modern one, InetPtonA, is Vista
 * and later, and this engine is from 1999. */
static bool parse_ipv4(const char *host, uint32_t *ip_network)
{
    uint32_t octet[4];
    int      index;

    for (index = 0; index < 4; ++index) {
        char         *end = NULL;
        unsigned long value;

        if (*host < '0' || *host > '9') {
            return false;
        }
        value = strtoul(host, &end, 10);
        if (value > 255u || end == host) {
            return false;
        }
        octet[index] = (uint32_t)value;
        host = end;
        if (index < 3) {
            if (*host != '.') {
                return false;
            }
            ++host;
        }
    }
    if (*host != '\0') {
        return false;   /* trailing junk after the fourth octet */
    }
    *ip_network = htonl((octet[0] << 24) | (octet[1] << 16) | (octet[2] << 8) | octet[3]);
    return true;
}

uint32_t mp_udp_resolve(mp_udp_t *udp, const char *address)
{
    char          host[64];
    const char   *colon;
    size_t        host_len;
    uint32_t      ip = 0;
    char         *end = NULL;
    unsigned long port;

    if (udp == NULL || address == NULL) {
        return 0u;
    }
    colon = strrchr(address, ':');
    if (colon == NULL) {
        return 0u;
    }
    host_len = (size_t)(colon - address);
    if (host_len == 0u || host_len >= sizeof host) {
        return 0u;
    }
    memcpy(host, address, host_len);
    host[host_len] = '\0';

    if (!parse_ipv4(host, &ip)) {
        return 0u;
    }
    if (colon[1] < '0' || colon[1] > '9') {
        return 0u;
    }
    port = strtoul(colon + 1, &end, 10);
    if (port == 0u || port > 65535u || end == NULL || *end != '\0') {
        return 0u;
    }
    return intern(udp, ip, htons((uint16_t)port));
}

bool mp_udp_init(mp_udp_t *udp, uint16_t port)
{
    SOCKET             sock;
    struct sockaddr_in me;
    struct sockaddr_in bound;
    int                bound_len = (int)sizeof bound;
    BOOL               connreset = FALSE;
    DWORD              returned = 0;
    u_long             nonblocking = 1u;

    memset(udp, 0, sizeof(*udp));

    if (!mp_socket_startup()) {
        udp->last_error = mp_socket_last_error();
        log_warning("the udp socket could not start winsock");
        return false;
    }
    udp->holds_winsock = true;

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {   /* SOCKET is unsigned, so this is the test, never < 0 */
        udp->last_error = WSAGetLastError();
        mp_socket_shutdown();
        udp->holds_winsock = false;
        log_warning("the udp socket could not be created, error %d", udp->last_error);
        return false;
    }

    /* Stop a stale ICMP unreachable from a send turning the next recv into a reported reset. The
     * call is allowed to fail on stacks that do not know the code; the recv path swallows the reset
     * either way. */
    (void)WSAIoctl(sock, SIO_UDP_CONNRESET, &connreset, sizeof connreset, NULL, 0, &returned, NULL,
                   NULL);

    /* Room for a burst, and what the stack actually granted read back, because it may grant
     * less than asked and a report that quotes the request would be quoting a wish. */
    {
        int want = MP_UDP_SOCKET_BUFFER_BYTES;
        int len  = (int)sizeof udp->rcvbuf_bytes;

        (void)setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (const char *)&want, (int)sizeof want);
        (void)setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (const char *)&want, (int)sizeof want);
        (void)getsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char *)&udp->rcvbuf_bytes, &len);
        len = (int)sizeof udp->sndbuf_bytes;
        (void)getsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char *)&udp->sndbuf_bytes, &len);
    }
    /* A fixed port is this socket's alone. Without it a second process bound with address
     * reuse shares the port, and which of the two receives a datagram is undefined; with it
     * the second bind fails and says so. An ephemeral port has nothing to share. */
    if (port != 0u) {
        BOOL exclusive = TRUE;

        (void)setsockopt(sock, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *)&exclusive,
                         (int)sizeof exclusive);
    }

    if (ioctlsocket(sock, FIONBIO, &nonblocking) != 0) {
        udp->last_error = WSAGetLastError();
        closesocket(sock);
        mp_socket_shutdown();
        udp->holds_winsock = false;
        return false;
    }

    memset(&me, 0, sizeof me);   /* sin_zero must be clear, a defect the prototype shipped */
    me.sin_family      = AF_INET;
    me.sin_addr.s_addr = htonl(INADDR_ANY);
    me.sin_port        = htons(port);
    if (bind(sock, (struct sockaddr *)&me, sizeof me) != 0) {
        udp->last_error = WSAGetLastError();
        closesocket(sock);
        mp_socket_shutdown();
        udp->holds_winsock = false;
        log_warning("the udp socket could not bind port %u, error %d", (unsigned)port,
                    udp->last_error);
        return false;
    }

    memset(&bound, 0, sizeof bound);
    if (getsockname(sock, (struct sockaddr *)&bound, &bound_len) == 0) {
        udp->local_port = ntohs(bound.sin_port);
    }

    udp->socket = (uintptr_t)sock;
    udp->up     = true;
    log_info("the udp socket on port %u holds %d byte(s) to receive and %d to send%s",
             (unsigned)udp->local_port, udp->rcvbuf_bytes, udp->sndbuf_bytes,
             port != 0u ? ", the port its own" : "");
    return true;
}

mp_transport_t mp_udp_transport(mp_udp_t *udp)
{
    mp_transport_t transport;

    transport.context = udp;
    transport.send    = &udp_send;
    transport.recv    = &udp_recv;
    transport.hold    = &udp_hold;
    transport.address = &udp_address;
    return transport;
}

uint16_t mp_udp_local_port(const mp_udp_t *udp)
{
    return udp->local_port;
}

uint32_t mp_udp_recv_datagrams(const mp_udp_t *udp)
{
    return (udp != NULL) ? udp->recv_datagrams : 0u;
}

uint32_t mp_udp_recv_bytes(const mp_udp_t *udp)
{
    return (udp != NULL) ? udp->recv_bytes : 0u;
}

uint32_t mp_udp_recv_unnumbered(const mp_udp_t *udp)
{
    return (udp != NULL) ? udp->recv_unnumbered : 0u;
}

uint32_t mp_udp_recv_dropped(const mp_udp_t *udp)
{
    return (udp != NULL) ? udp->recv_dropped : 0u;
}

void mp_udp_buffer_bytes(const mp_udp_t *udp, int *receive, int *send)
{
    if (receive != NULL) {
        *receive = udp != NULL ? udp->rcvbuf_bytes : 0;
    }
    if (send != NULL) {
        *send = udp != NULL ? udp->sndbuf_bytes : 0;
    }
}

uint32_t mp_udp_sent_datagrams(const mp_udp_t *udp)
{
    return (udp != NULL) ? udp->sent_datagrams : 0u;
}

uint32_t mp_udp_send_failures(const mp_udp_t *udp)
{
    return (udp != NULL) ? udp->send_failures : 0u;
}

uint32_t mp_udp_last_source_ip(const mp_udp_t *udp)
{
    return (udp != NULL) ? udp->last_source_ip : 0u;
}

uint16_t mp_udp_last_source_port(const mp_udp_t *udp)
{
    return (udp != NULL) ? udp->last_source_port : 0u;
}

int mp_udp_last_error(const mp_udp_t *udp)
{
    return udp->last_error;
}

void mp_udp_shutdown(mp_udp_t *udp)
{
    if (udp->up) {
        closesocket((SOCKET)udp->socket);
        udp->up = false;
    }
    if (udp->holds_winsock) {
        mp_socket_shutdown();
        udp->holds_winsock = false;
    }
}
