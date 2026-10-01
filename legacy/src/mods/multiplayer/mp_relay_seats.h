/* mp_relay_seats.h: a relay host's players as the session sees them, one endpoint each.
 *
 * Layer 1, pure. Over the relay a host has one socket and one leg, and every player's packets
 * arrive on it named by the slot and generation the relay gave that player. The session keys its
 * peers by endpoint, so this turns a (slot, generation) into an endpoint and back, following the
 * relay's notices:
 *
 *   - A MemberOpen seats a player. One whose flag says it continues a seat and whose seat value R
 *     is known takes back the endpoint it had, under its new slot and generation: that is a player
 *     whose leg the relay lost and who proved the seat again, and the session must see the same
 *     peer. It is found by R before anything else, because after a relay restart the relay hands
 *     the same small pairs to whoever proves a seat first. A pair names a seat only together with
 *     its R: another R on a known pair is a new player, and the old seat on it is over.
 *   - A MemberClosed ends a seat only when it names the seat's slot and generation and arrived
 *     after the MemberOpen that opened it. A notice from before is an old one, delivered late.
 *   - After is measured by the leg a notice came on and its counter on that leg: counters start
 *     again on every new leg, so a counter alone would read a new leg's first notice as ancient.
 *   - A game packet from a (slot, generation) no open seat has is dropped and counted; a send to
 *     an endpoint whose seat is closed is refused.
 *
 * The endpoint's address, for the session's cookie, is the first eight bytes of R: it stays the
 * same for as long as the seat does, including across a continued seat, which is exactly what the
 * cookie wants.
 */
#ifndef MULTIPLAYER_MP_RELAY_SEATS_H
#define MULTIPLAYER_MP_RELAY_SEATS_H

#include "mp_relay_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Endpoints at once. The relay seats at most 8 players in a session; the rest is room for seats
 * that closed while the session still holds their peer. */
#define MP_RELAY_SEATS_MAX 16u

typedef struct mp_relay_seat {
    bool     used;
    bool     open;
    bool     held;
    uint8_t  slot;
    uint16_t gen;
    uint8_t  r[MP_RELAY_SEAT_VALUE_BYTES];
    uint32_t open_leg;
    uint64_t open_counter;
} mp_relay_seat_t;

typedef struct mp_relay_seats {
    mp_relay_seat_t seat[MP_RELAY_SEATS_MAX];
    uint32_t        opened;
    uint32_t        continued;
    uint32_t        closed;
    uint32_t        stale;          /* notices that arrived after a newer one for the same seat */
    uint32_t        full;           /* a MemberOpen with no endpoint left for it */
    uint32_t        unknown_data;   /* game packets from no open seat */
    uint32_t        refused_sends;  /* sends to an endpoint with no open seat */
} mp_relay_seats_t;

void mp_relay_seats_init(mp_relay_seats_t *seats);

/* A MemberOpen that arrived on leg `leg` under `counter`. Answers the player's endpoint, or 0 when
 * the notice is an old one or no endpoint is left. */
uint32_t mp_relay_seats_open(mp_relay_seats_t *seats, uint8_t slot, uint16_t gen, uint8_t flags,
                             const uint8_t r[MP_RELAY_SEAT_VALUE_BYTES], uint32_t leg,
                             uint64_t counter);

/* A MemberClosed. Answers the endpoint whose seat it closed, or 0. */
uint32_t mp_relay_seats_close(mp_relay_seats_t *seats, uint8_t slot, uint16_t gen, uint32_t leg,
                              uint64_t counter);

/* The endpoint a game packet from (slot, gen) arrives under, 0 when no open seat has it. */
uint32_t mp_relay_seats_from(mp_relay_seats_t *seats, uint8_t slot, uint16_t gen);

/* Where a packet for `endpoint` goes. False when its seat is not open. */
bool mp_relay_seats_to(mp_relay_seats_t *seats, uint32_t endpoint, uint8_t *slot, uint16_t *gen);

/* The session holds an endpoint, or lets it go. A closed seat the session lets go is forgotten. */
void mp_relay_seats_hold(mp_relay_seats_t *seats, uint32_t endpoint, bool held);

/* The first eight bytes of the seat's R, never 0; 0 for an endpoint with no seat. */
uint64_t mp_relay_seats_address(const mp_relay_seats_t *seats, uint32_t endpoint);

/* Open seats, for the report and the lobby. */
uint32_t mp_relay_seats_open_count(const mp_relay_seats_t *seats);

/* The relay registered the session afresh: every seat of the old one is over. An endpoint the
 * session still holds keeps its number and its address until it is let go. */
void mp_relay_seats_close_all(mp_relay_seats_t *seats);

#endif /* MULTIPLAYER_MP_RELAY_SEATS_H */
