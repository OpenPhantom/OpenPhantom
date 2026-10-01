/* mp_relay_listing.h: the relay's public list, asked for and put together, as a value.
 *
 * Layer 1, pure. The join screen shows the sessions whose hosts chose to be listed, and the relay
 * hands them out a page of 24 at a time to anybody with a cookie for the list's role. This asks:
 *
 *   - A Hello for role 3, sent again every second until its cookie comes. A cookie is used for a
 *     minute and then fetched again; an open Nack 6 fetches a new one at once.
 *   - A round every two seconds: page 0, and on its answer the next page, until the page count the
 *     answers name is reached. A page that does not come is asked for again after a second.
 *   - A round replaces the rows only when it is whole, so the list a player is choosing from does
 *     not shrink to one page and grow back every two seconds. A page that answers for another page
 *     than the one asked (the list got shorter under the round) ends the round with what came.
 *   - A row whose announce does not decode is left out and counted: it is a host this build
 *     cannot read, and a row it cannot draw is worth less than the count in the report.
 *
 * Nothing opens or is sealed: the list and its cookie are open messages. The clock and the
 * datagrams are handed in, and what is due goes out through the tick.
 */
#ifndef MULTIPLAYER_MP_RELAY_LISTING_H
#define MULTIPLAYER_MP_RELAY_LISTING_H

#include "mp_announce.h"
#include "mp_relay_link.h"
#include "mp_relay_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MP_RELAY_LIST_ROUND_MS  2000u
#define MP_RELAY_LIST_COOKIE_MS 60000u
#define MP_RELAY_LIST_TRY_MS    1000u
#define MP_RELAY_LIST_SILENT_MS 6000u

/* Rows kept: four pages. The screen shows eight at a time and scrolls. */
#define MP_RELAY_LIST_ROWS_MAX 96u

typedef enum mp_relay_listing_state {
    MP_RELAY_LISTING_ASKING = 0,   /* nothing has come back yet */
    MP_RELAY_LISTING_ANSWERING,    /* the relay answered within the last six seconds */
    MP_RELAY_LISTING_SILENT        /* six seconds without an answer */
} mp_relay_listing_state_t;

typedef struct mp_relay_list_row {
    uint8_t       code[MP_RELAY_CODE_BYTES];
    mp_announce_t announce;
} mp_relay_list_row_t;

typedef struct mp_relay_listing {
    mp_relay_random_fn random;

    /* The cookie. */
    bool              have_cookie;
    mp_relay_cookie_t cookie;
    uint32_t          cookie_at;
    bool              hello_out;
    uint64_t          nonce;
    uint32_t          hello_at;
    uint8_t           hello[MP_RELAY_HELLO_BYTES];

    /* The round. */
    bool     started;
    bool     round_open;
    uint32_t round_at;
    uint16_t page;          /* the page asked for */
    uint32_t query_at;
    bool     query_due;
    size_t   building;
    mp_relay_list_row_t built[MP_RELAY_LIST_ROWS_MAX];

    /* What the screen reads. */
    size_t              count;
    mp_relay_list_row_t row[MP_RELAY_LIST_ROWS_MAX];
    bool                heard;
    uint32_t            heard_at;
    uint32_t            first_ask_at;

    uint32_t rounds;
    uint32_t pages;
    uint32_t unreadable;     /* rows whose announce did not decode */
    uint32_t nacks;
} mp_relay_listing_t;

void mp_relay_listing_init(mp_relay_listing_t *listing, mp_relay_random_fn random);

/* The next datagram due for the relay, or 0. Call until it answers 0. */
size_t mp_relay_listing_tick(mp_relay_listing_t *listing, uint32_t now, uint8_t *out,
                             size_t capacity);

/* A datagram from the relay's address. True when it was a cookie for this listing: the family it
 * came in is the one the queries go out in. */
bool mp_relay_listing_receive(mp_relay_listing_t *listing, uint32_t now, const uint8_t *datagram,
                              size_t bytes);

mp_relay_listing_state_t mp_relay_listing_state(const mp_relay_listing_t *listing, uint32_t now);

#endif /* MULTIPLAYER_MP_RELAY_LISTING_H */
