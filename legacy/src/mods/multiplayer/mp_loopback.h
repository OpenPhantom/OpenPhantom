/* mp_loopback.h: a network in a lump of memory, with a loss dial.
 *
 * Layer 0. Two endpoints, a queue of packets in flight between them, and a small emulator that can
 * lose, duplicate, reorder and delay on purpose. It is the transport the whole session is proven
 * over: a self test in one process that puts two sessions on the two endpoints and makes them
 * complete a handshake and exchange reliable messages while the medium drops a quarter of what it
 * carries. That run comes before any real network line, because a session that connects and
 * transfers nothing while the protocol reports success is the failure this whole feature is most
 * likely to produce.
 *
 * Time is a millisecond count the caller supplies to the pump, never read from a clock, so delay
 * and reordering are as reproducible as everything else here. The emulator's randomness is a
 * seeded generator, so a failing run repeats exactly.
 *
 * Nothing here allocates. The medium holds a fixed ring of packets; when it is full the next send
 * is dropped, which is a lost packet like any other and is the reliability layer's problem to
 * heal, not a crash.
 */
#ifndef MULTIPLAYER_MP_LOOPBACK_H
#define MULTIPLAYER_MP_LOOPBACK_H

#include "mp_transport.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The two endpoints, named so the test reads. Either may send to the other. */
#define MP_LOOPBACK_ENDPOINT_A 0u
#define MP_LOOPBACK_ENDPOINT_B 1u

/* Largest packet the medium carries, the channel's own budget. */
#define MP_LOOPBACK_PACKET_BYTES 1200u

/* Packets in flight at once across both directions. A generous multiple of the channel's send
 * window, so ordinary traffic never fills it and a full ring is a real overload rather than a
 * tuning artefact. */
#define MP_LOOPBACK_RING 256u

/* The emulator's dials, all off by default. Percentages are 0..100. Reorder swaps a delivered
 * packet with the one behind it; delay holds a packet for a number of pump milliseconds before it
 * can be received. The chain test runs at 25 percent loss, 10 percent duplication, 20 percent
 * reorder and 24 ms delay and requires every reliable message to still arrive once and in order;
 * its last check asserts that the link both delivered and dropped, so a run in which the emulator
 * did nothing fails rather than passing quietly. */
typedef struct mp_loopback_conditions {
    uint32_t loss_percent;
    uint32_t duplicate_percent;
    uint32_t reorder_percent;
    uint32_t delay_ms;
} mp_loopback_conditions_t;

typedef struct mp_loopback_packet {
    bool     used;
    uint32_t to;             /* the endpoint that may receive it */
    uint32_t deliver_at_ms;  /* not receivable before this pump time */
    uint32_t sequence;       /* arrival order tie-breaker, so reordering is deliberate not chance */
    size_t   bytes;
    uint8_t  data[MP_LOOPBACK_PACKET_BYTES];
} mp_loopback_packet_t;

/* One binding per endpoint, held inside the net so a transport's context is a stable pointer into
 * it rather than a global. The net back-pointer is refreshed when the transport is taken. */
struct mp_loopback;
typedef struct mp_loopback_binding {
    struct mp_loopback *net;
    uint32_t            endpoint;
} mp_loopback_binding_t;

typedef struct mp_loopback {
    uint32_t                 now_ms;
    uint32_t                 rng;
    uint32_t                 next_sequence;
    uint32_t                 dropped;      /* packets the emulator or a full ring discarded */
    uint32_t                 delivered;
    mp_loopback_conditions_t conditions;
    mp_loopback_binding_t    binding[2];
    mp_loopback_packet_t     ring[MP_LOOPBACK_RING];
} mp_loopback_t;

/* Sets up an empty medium under the given conditions and random seed. A zero seed is replaced, so
 * that "no seed" is not a generator that returns the same number forever. */
void mp_loopback_init(mp_loopback_t *net, const mp_loopback_conditions_t *conditions,
                      uint32_t seed);

/* Advances the medium's clock, which is what lets delayed packets become receivable. */
void mp_loopback_pump(mp_loopback_t *net, uint32_t now_ms);

/* A transport bound to one of the two endpoints. The returned transport's context points into the
 * net, so the net must outlive it. */
mp_transport_t mp_loopback_transport(mp_loopback_t *net, uint32_t endpoint);

#endif /* MULTIPLAYER_MP_LOOPBACK_H */
