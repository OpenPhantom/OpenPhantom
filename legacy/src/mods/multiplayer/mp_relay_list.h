/* mp_relay_list.h: the relay's public list for the join screen, over its own sockets.
 *
 * Layer 0. While the join screen is open in the public mode it asks the relay for the sessions
 * whose hosts chose to be listed (mp_relay_listing decides what and when), over a pair of sockets
 * of its own that close with the screen. The relay's addresses come from the lookup of
 * mp_relay_keyfetch, without the key: the list is open and needs none.
 *
 * A Hello goes out in every family that can reach the relay, and the first cookie decides the
 * family the queries take, because a list cookie is bound to the address it was given to.
 */
#ifndef MULTIPLAYER_MP_RELAY_LIST_H
#define MULTIPLAYER_MP_RELAY_LIST_H

#include "mp_relay_listing.h"
#include "mp_relay_socket.h"
#include "mp_relay_transport.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct mp_relay_list {
    const mp_relay_services_t *services;
    bool                       up;
    mp_relay_socket_t          socket;
    mp_relay_listing_t         listing;
    bool                       fetching;
    bool                       addresses_known;
    uint32_t                   next_fetch_at;
    uint32_t                   opened_at;
    int                        family;   /* the cookie's, -1 before one came */
    uint8_t                    datagram[MP_RELAY_DATAGRAM_MAX];
} mp_relay_list_t;

/* Opens the sockets. False when none would open; the screen then says the relay is not reached. */
bool mp_relay_list_open(mp_relay_list_t *list, const mp_relay_services_t *services);

/* One frame of the screen: the lookup, what arrived, what is due. */
void mp_relay_list_pump(mp_relay_list_t *list);

mp_relay_listing_state_t mp_relay_list_state(const mp_relay_list_t *list);
size_t                   mp_relay_list_count(const mp_relay_list_t *list);

/* The row at `index`, NULL past the end. */
const mp_relay_list_row_t *mp_relay_list_row(const mp_relay_list_t *list, size_t index);

/* Closes the sockets. Idempotent. */
void mp_relay_list_close(mp_relay_list_t *list);

#endif /* MULTIPLAYER_MP_RELAY_LIST_H */
