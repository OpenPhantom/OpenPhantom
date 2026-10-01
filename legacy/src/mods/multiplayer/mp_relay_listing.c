/* mp_relay_listing.c: the relay's public list, as a value. See the header. */
#include "mp_relay_listing.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static bool reached(uint32_t now, uint32_t at)
{
    return now - at < 0x80000000u;
}

void mp_relay_listing_init(mp_relay_listing_t *listing, mp_relay_random_fn random)
{
    memset(listing, 0, sizeof *listing);
    listing->random = random;
}

static size_t hello(mp_relay_listing_t *listing, uint32_t now, uint8_t *out)
{
    if (!listing->hello_out) {
        uint8_t nonce[8];
        size_t  i;

        if (!listing->random(nonce, sizeof nonce)) {
            return 0u;
        }
        listing->nonce = 0u;
        for (i = 0; i < sizeof nonce; ++i) {
            listing->nonce |= (uint64_t)nonce[i] << (8u * i);
        }
        (void)mp_relay_hello_encode((uint8_t)MP_RELAY_ROLE_LIST, listing->nonce, listing->hello,
                                    sizeof listing->hello);
        listing->hello_out = true;
    } else if (!reached(now, listing->hello_at + MP_RELAY_LIST_TRY_MS)) {
        return 0u;
    }
    listing->hello_at = now;
    memcpy(out, listing->hello, MP_RELAY_HELLO_BYTES);
    return MP_RELAY_HELLO_BYTES;
}

static size_t query(mp_relay_listing_t *listing, uint32_t now, uint8_t *out, size_t capacity)
{
    mp_relay_list_query_t q;

    memset(&q, 0, sizeof q);
    q.epoch = listing->cookie.epoch;
    q.nonce = listing->nonce;
    q.page  = listing->page;
    memcpy(q.cookie, listing->cookie.cookie, sizeof q.cookie);
    listing->query_at  = now;
    listing->query_due = false;
    return mp_relay_list_query_encode(&q, out, capacity);
}

size_t mp_relay_listing_tick(mp_relay_listing_t *listing, uint32_t now, uint8_t *out,
                             size_t capacity)
{
    if (out == NULL || capacity < MP_RELAY_LIST_QUERY_BYTES) {
        return 0u;
    }
    if (listing->first_ask_at == 0u && !listing->started) {
        listing->first_ask_at = now;
        listing->started      = true;
    }
    if (listing->have_cookie && reached(now, listing->cookie_at + MP_RELAY_LIST_COOKIE_MS)) {
        listing->have_cookie = false;   /* a minute old: the next round asks for a new one */
    }
    if (!listing->have_cookie) {
        return hello(listing, now, out);
    }
    if (!listing->round_open) {
        if (listing->rounds != 0u && !reached(now, listing->round_at + MP_RELAY_LIST_ROUND_MS)) {
            return 0u;
        }
        listing->round_open = true;
        listing->round_at   = now;
        listing->page       = 0u;
        listing->building   = 0u;
        ++listing->rounds;
        return query(listing, now, out, capacity);
    }
    if (listing->query_due || reached(now, listing->query_at + MP_RELAY_LIST_TRY_MS)) {
        return query(listing, now, out, capacity);
    }
    return 0u;
}

static void publish(mp_relay_listing_t *listing)
{
    memcpy(listing->row, listing->built, listing->building * sizeof listing->row[0]);
    listing->count      = listing->building;
    listing->round_open = false;
}

static void take_page(mp_relay_listing_t *listing, uint32_t now, const mp_relay_list_page_t *page)
{
    size_t i;

    if (!listing->round_open) {
        return;
    }
    listing->heard    = true;
    listing->heard_at = now;
    ++listing->pages;
    if (page->page != listing->page) {
        publish(listing);   /* the list got shorter under the round: what came is the list */
        return;
    }
    for (i = 0; i < page->count; ++i) {
        mp_relay_list_row_t *row;

        if (listing->building >= MP_RELAY_LIST_ROWS_MAX) {
            break;
        }
        row = &listing->built[listing->building];
        if (!mp_announce_decode(page->entry[i].announce, MP_RELAY_ANNOUNCE_BYTES, &row->announce)) {
            ++listing->unreadable;
            continue;
        }
        memcpy(row->code, page->entry[i].code, sizeof row->code);
        ++listing->building;
    }
    if ((uint32_t)listing->page + 1u < page->pages &&
        listing->building < MP_RELAY_LIST_ROWS_MAX) {
        ++listing->page;
        listing->query_due = true;
        return;
    }
    publish(listing);
}

bool mp_relay_listing_receive(mp_relay_listing_t *listing, uint32_t now, const uint8_t *datagram,
                              size_t bytes)
{
    mp_relay_cookie_t    cookie;
    mp_relay_list_page_t page;
    mp_relay_nack_t      nack;

    if (datagram == NULL || bytes == 0u) {
        return false;
    }
    switch (datagram[0]) {
    case MP_RELAY_TYPE_COOKIE:
        if (!listing->hello_out || !mp_relay_cookie_decode(datagram, bytes, &cookie) ||
            cookie.role != MP_RELAY_ROLE_LIST || cookie.nonce != listing->nonce) {
            return false;
        }
        listing->cookie      = cookie;
        listing->have_cookie = true;
        listing->cookie_at   = now;
        listing->hello_out   = false;
        listing->heard       = true;
        listing->heard_at    = now;
        if (listing->round_open) {
            listing->query_due = true;   /* a round that lost its cookie asks again with this one */
        }
        return true;
    case MP_RELAY_TYPE_LIST_PAGE:
        if (listing->have_cookie && mp_relay_list_page_decode(datagram, bytes, &page)) {
            take_page(listing, now, &page);
        }
        return false;
    case MP_RELAY_TYPE_NACK:
        if (mp_relay_nack_decode(datagram, bytes, &nack)) {
            ++listing->nacks;
            if (nack.reason == MP_RELAY_NACK_BAD_COOKIE && listing->have_cookie) {
                listing->have_cookie = false;
            }
        }
        return false;
    default:
        return false;
    }
}

mp_relay_listing_state_t mp_relay_listing_state(const mp_relay_listing_t *listing, uint32_t now)
{
    uint32_t since = listing->heard ? listing->heard_at : listing->first_ask_at;

    if (listing->started && reached(now, since + MP_RELAY_LIST_SILENT_MS)) {
        return MP_RELAY_LISTING_SILENT;
    }
    return listing->heard ? MP_RELAY_LISTING_ANSWERING : MP_RELAY_LISTING_ASKING;
}
