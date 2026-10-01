/* mp_peers_net.h: the three client harness the session tests share. See mp_peers_net.c.
 *
 * Test code only. The sessions and the network are globals because every test in both files
 * steps the same four sessions, and the harness is what builds them.
 */
#ifndef UNITTESTS_MP_PEERS_NET_H
#define UNITTESTS_MP_PEERS_NET_H

#include "mp_channel.h"
#include "mp_session.h"
#include "mp_transport.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The host at endpoint 0 and a client at each of the three after it. */
#define ENDPOINTS 4u
#define CLIENTS   3u

/* A round of the harness is one substep: every session receives and services once. A round moves
 * at most two packets per peer and direction, so a queue that is drained every round never holds
 * more than a few dozen; the headroom is for a test that stops stepping a client. */
#define NET_SLOTS 512u
#define ROUND_MS  31u

typedef struct net_packet {
    uint32_t to;
    uint32_t from;
    size_t   bytes;
    uint8_t  data[MP_CHANNEL_PACKET_BYTES];
} net_packet_t;

typedef struct net {
    net_packet_t queue[NET_SLOTS];
    size_t       count;
    bool         muted[ENDPOINTS];     /* packets sent from here are dropped */
    bool         deaf[ENDPOINTS];      /* packets sent to here are dropped */
    uint32_t     sent[ENDPOINTS];      /* packets handed in from each endpoint */
    uint32_t     dropped[ENDPOINTS];   /* of those, the ones a mute here or a deaf peer took */
    uint32_t     overflowed;           /* handed in with the queue full; a harness fault */
} net_t;

/* A numbered event of forty bytes. The first byte is an event tag and no state, so nothing in the
 * session may treat one copy as standing for another. */
#define EVENT_TAG   0x81u
#define EVENT_BYTES 40u

typedef struct broadcast_run {
    uint32_t next;       /* the number the next broadcast carries */
    uint32_t taken;      /* broadcasts some peer took */
    uint32_t refused;    /* broadcasts no peer took */
    size_t   last_count; /* what the last one answered */
} broadcast_run_t;

/* What a client has read, in order: the next number it expects, how many came in order, how many
 * did not, and how many were not numbered events at all. */
typedef struct reader {
    uint32_t expected;
    uint32_t in_order;
    uint32_t out_of_order;
    uint32_t others;
} reader_t;

extern net_t          s_net;
extern mp_transport_t s_transport[ENDPOINTS];
extern mp_session_t   s_host;
extern mp_session_t   s_client[CLIENTS];
extern uint32_t       s_now;

/* A host and `clients` clients, built and connected one after the other, so the host's peer
 * index of client k is k. False when one of them did not connect. */
bool net_build(size_t clients);

/* One substep: the host receives, `between` acts on it, the host services, and every client
 * receives and services. */
void net_round(size_t clients, void (*between)(void *), void *context);

/* A numbered event of EVENT_BYTES. */
size_t make_event(uint32_t number, uint8_t *out);

/* One numbered broadcast, a `broadcast_run_t` as the context; a refused one is offered again. */
void broadcast_one(void *context);

/* Everything client k can read now, counted against the numbering. */
void read_all(size_t k, reader_t *reader);

#endif /* UNITTESTS_MP_PEERS_NET_H */
