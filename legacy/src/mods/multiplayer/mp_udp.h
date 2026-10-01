/* mp_udp.h: a non-blocking UDP socket behind the mp_transport vtable.
 *
 * Layer 0: no engine address, no engine header, and driven in the unit test without the game. It is
 * the real network the loopback stands in for, and it fills the same vtable, so the session and the
 * channel drive it unchanged: the only thing a client does differently is turn an "ip:port" string
 * into an endpoint through mp_udp_resolve before it connects.
 *
 * An endpoint is an index into a small table of addresses this socket has seen or been asked to
 * reach, so the whole 48 bits of an address and port never pass through the 32-bit endpoint the
 * transport carries. Index zero means none, so it doubles as the failure value of resolve and the
 * "nothing received" case.
 *
 * When the table is full a new source recycles the least-recently-used slot that the session does
 * not HOLD. The session holds every endpoint it has a peer on, from the first handshake packet to
 * the slot's release, and a held slot is never recycled; when every slot is held the datagram is
 * dropped instead. Recency alone was not enough: sixteen one-byte datagrams from sixteen ports
 * inside one tick are all fresher than a connected peer that last spoke a tick ago, and recycling
 * its slot would have addressed that peer's packets, connection id included, to a stranger. The
 * hold is what makes the number the session keys its peers by mean one address for the whole
 * life of the peer. (The design before recency dropped every newcomer once the table was full,
 * which was a permanent self-deafen after sixteen forged sources.)
 *
 * The header keeps no Winsock type: the socket is held as a uintptr_t and an address as a packed
 * ipv4 and port, so nothing above this includes winsock2.h. IPv6 is a later widening of the address
 * slot and the parser, not a change to this interface.
 */
#ifndef MULTIPLAYER_MP_UDP_H
#define MULTIPLAYER_MP_UDP_H

#include "mp_transport.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The largest datagram this transport sends or accepts, the channel's budget, equal to the
 * loopback's so the two are interchangeable. */
#define MP_UDP_PACKET_BYTES 1200u

/* Addresses this socket holds at once. A full table recycles the least recently used one the
 * session does not hold, and turns a newcomer away only while every slot is a held peer. */
#define MP_UDP_MAX_ENDPOINTS 16u

/* What the socket asks the stack for, each way. The default is small and not the same on every
 * Windows, and a side that stops reading for the length of a level load finds its queue full
 * and the rest of a burst gone without a word. */
#define MP_UDP_SOCKET_BUFFER_BYTES (256 * 1024)

typedef struct mp_udp_slot {
    bool     used;
    uint32_t ip;        /* network order, as it comes off the wire */
    uint16_t port;      /* network order */
    uint32_t last_used; /* the use clock at the last send or recv, for LRU eviction */
    bool     held;      /* the session has a peer on this endpoint; never recycled while set */
} mp_udp_slot_t;

typedef struct mp_udp {
    uintptr_t     socket;      /* a SOCKET, held opaquely so the header stays Winsock-free */
    bool          up;
    bool          holds_winsock;
    uint16_t      local_port;  /* host order, resolved after an ephemeral bind */
    int           last_error;
    uint32_t      use_clock;   /* monotonic, bumped on every intern and send; stamps slots */
    /* What arrived here, whatever it turned out to hold. A run that ends with none of
     * these has been told nothing at all, which is a different fault from one that was
     * told something and refused it, and no other number in the report separates them. */
    uint32_t      recv_datagrams;
    uint32_t      recv_bytes;
    uint32_t      recv_unnumbered;  /* arrived while every endpoint slot was a held peer */
    uint32_t      recv_dropped;     /* taken off and thrown away: too large, empty, a stale reset */
    int           rcvbuf_bytes;     /* what the stack granted, read back after asking */
    int           sndbuf_bytes;
    uint32_t      sent_datagrams;   /* sendto returned the whole packet */
    uint32_t      send_failures;    /* sendto refused it, with the reason in last_error */
    uint32_t      last_source_ip;   /* host order, the last sender, for the report */
    uint16_t      last_source_port; /* host order */
    mp_udp_slot_t slots[MP_UDP_MAX_ENDPOINTS];
} mp_udp_t;

/* Brings up a non-blocking UDP socket. `port` binds the host's listening port; zero binds an
 * ephemeral one, which a client uses and reads back with mp_udp_local_port. Takes one Winsock
 * reference through mp_socket and holds it until shutdown. False on any failure, with the reason in
 * mp_udp_last_error and nothing left half open. */
bool mp_udp_init(mp_udp_t *udp, uint16_t port);

/* The transport face of this socket. Its context points at *udp, which must outlive it. */
mp_transport_t mp_udp_transport(mp_udp_t *udp);

/* Turns "a.b.c.d:port" (numeric IPv4) into an endpoint, interning the address. Zero on a malformed
 * string or a full table; a valid endpoint is never zero. The one call a client makes before
 * mp_session_connect. */
uint32_t mp_udp_resolve(mp_udp_t *udp, const char *address);

/* The port actually bound, after an ephemeral bind resolved it. Zero before init. */
uint16_t mp_udp_local_port(const mp_udp_t *udp);

int mp_udp_last_error(const mp_udp_t *udp);

/* What this socket has been told since it came up. The address is the last sender, in host
 * order, and is zero until something arrives. */
uint32_t mp_udp_recv_datagrams(const mp_udp_t *udp);
uint32_t mp_udp_recv_bytes(const mp_udp_t *udp);
uint32_t mp_udp_recv_unnumbered(const mp_udp_t *udp);
uint32_t mp_udp_recv_dropped(const mp_udp_t *udp);
/* The two buffer sizes the stack granted, in bytes, as it reports them. */
void     mp_udp_buffer_bytes(const mp_udp_t *udp, int *receive, int *send);
uint32_t mp_udp_sent_datagrams(const mp_udp_t *udp);
uint32_t mp_udp_send_failures(const mp_udp_t *udp);
uint32_t mp_udp_last_source_ip(const mp_udp_t *udp);
uint16_t mp_udp_last_source_port(const mp_udp_t *udp);

/* Closes the socket and returns the Winsock reference. Idempotent. */
void mp_udp_shutdown(mp_udp_t *udp);

#endif /* MULTIPLAYER_MP_UDP_H */
