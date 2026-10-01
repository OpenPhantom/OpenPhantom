/* mp_relay_listing.c: the public list asked for page by page, on a clock the test turns. */
#include "unittest.h"

#include "mp_relay_fake.h"
#include "mp_relay_listing.h"

#include "common/text.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define T0 100000u

static uint16_t le16(const uint8_t *at)
{
    return (uint16_t)(at[0] | (at[1] << 8));
}

static void put16(uint8_t *at, uint16_t value)
{
    at[0] = (uint8_t)value;
    at[1] = (uint8_t)(value >> 8);
}

/* A page of `count` rows named "Host <first + i>", or with an unreadable announce at `broken`. */
static size_t page_of(uint16_t page, uint16_t pages, size_t count, size_t first, size_t broken,
                      uint8_t *out)
{
    size_t i;

    memset(out, 0, MP_RELAY_LIST_PAGE_HEADER_BYTES + count * MP_RELAY_LIST_ENTRY_BYTES);
    out[0] = (uint8_t)MP_RELAY_TYPE_LIST_PAGE;
    memcpy(out + 1, "MPRL", 4u);
    out[5] = (uint8_t)MP_RELAY_VERSION;
    put16(out + 8, page);
    put16(out + 10, pages);
    out[12] = (uint8_t)count;
    for (i = 0; i < count; ++i) {
        uint8_t      *entry = out + MP_RELAY_LIST_PAGE_HEADER_BYTES + i * MP_RELAY_LIST_ENTRY_BYTES;
        mp_announce_t announce;

        memset(&announce, 0, sizeof announce);
        announce.wire      = 1u;
        announce.game_port = 27970u;
        announce.players   = 1u;
        announce.slots     = 4u;
        (void)text_format(announce.name, sizeof announce.name, "Host %u", (unsigned)(first + i));
        entry[0] = (uint8_t)(first + i);
        (void)mp_announce_encode(&announce, entry + MP_RELAY_CODE_BYTES, MP_RELAY_ANNOUNCE_BYTES);
        if (i == broken) {
            entry[MP_RELAY_CODE_BYTES] = 0u;   /* the magic */
        }
    }
    return MP_RELAY_LIST_PAGE_HEADER_BYTES + count * MP_RELAY_LIST_ENTRY_BYTES;
}

/* The listing's Hello at `now` answered with its cookie. */
static bool cookie_for(mp_relay_listing_t *l, fake_relay_t *relay, uint32_t now)
{
    uint8_t  out[MP_RELAY_LIST_QUERY_BYTES];
    uint8_t  in[MP_RELAY_COOKIE_BYTES];
    uint8_t  role  = 0u;
    uint64_t nonce = 0u;

    return fake_hello_read(out, mp_relay_listing_tick(l, now, out, sizeof out), &role, &nonce) &&
           role == MP_RELAY_ROLE_LIST &&
           mp_relay_listing_receive(l, now, in, fake_cookie(relay, role, nonce, in));
}

static void test_the_cookie(void)
{
    mp_relay_listing_t l;
    fake_relay_t       relay;
    uint8_t            out[MP_RELAY_LIST_QUERY_BYTES];
    uint8_t            in[MP_RELAY_COOKIE_BYTES];
    uint8_t            role  = 0u;
    uint64_t           nonce = 0u;
    uint64_t           again = 0u;

    ut_section("a cookie for the list's role");
    fake_relay_init(&relay);
    mp_relay_listing_init(&l, fake_random);
    ut_check(mp_relay_listing_tick(&l, T0, out, 100u) == 0u,
             "a buffer too small for a query is refused");
    ut_check(fake_hello_read(out, mp_relay_listing_tick(&l, T0, out, sizeof out), &role, &nonce) &&
                 role == MP_RELAY_ROLE_LIST,
             "a Hello for role 3");
    ut_check(mp_relay_listing_state(&l, T0) == MP_RELAY_LISTING_ASKING, "asking");
    ut_check(mp_relay_listing_tick(&l, T0 + 999u, out, sizeof out) == 0u &&
                 fake_hello_read(out, mp_relay_listing_tick(&l, T0 + 1000u, out, sizeof out),
                                 &role, &again) &&
                 again == nonce,
             "unanswered, the same Hello a second later");
    ut_check(!mp_relay_listing_receive(
                 &l, T0 + 1001u, in, fake_cookie(&relay, (uint8_t)MP_RELAY_ROLE_MEMBER, nonce, in)),
             "a cookie for another role is not the list's");
    ut_check(mp_relay_listing_receive(&l, T0 + 1002u, in, fake_cookie(&relay, role, nonce, in)),
             "its own is");
    ut_check(mp_relay_listing_tick(&l, T0 + 1002u, out, sizeof out) == MP_RELAY_LIST_QUERY_BYTES &&
                 out[0] == MP_RELAY_TYPE_LIST_QUERY && le16(out + 36) == 0u,
             "and the first query, for page 0, goes out at once");
    ut_check(mp_relay_listing_state(&l, T0 + 1002u) == MP_RELAY_LISTING_ANSWERING,
             "the relay is answering");
    ut_check(mp_relay_listing_state(&l, T0 + 7002u) == MP_RELAY_LISTING_SILENT,
             "six seconds without a word: silent");
}

static void test_pages(void)
{
    static mp_relay_listing_t l;
    fake_relay_t              relay;
    uint8_t                   out[MP_RELAY_LIST_QUERY_BYTES];
    uint8_t                   page[MP_RELAY_LIST_PAGE_HEADER_BYTES +
                                   MP_RELAY_LIST_PAGE_ENTRIES * MP_RELAY_LIST_ENTRY_BYTES];
    uint8_t                   role  = 0u;
    uint64_t                  nonce = 0u;
    size_t                    n;

    ut_section("a list of two pages");
    fake_relay_init(&relay);
    mp_relay_listing_init(&l, fake_random);
    ut_check(cookie_for(&l, &relay, T0), "a cookie");
    (void)mp_relay_listing_tick(&l, T0, out, sizeof out);
    n = page_of(0u, 2u, MP_RELAY_LIST_PAGE_ENTRIES, 0u, 99u, page);
    (void)mp_relay_listing_receive(&l, T0 + 10u, page, n);
    ut_check(l.count == 0u, "one page of two is not shown yet");
    ut_check(mp_relay_listing_tick(&l, T0 + 10u, out, sizeof out) == MP_RELAY_LIST_QUERY_BYTES &&
                 le16(out + 36) == 1u,
             "page 1 is asked for at once");
    n = page_of(1u, 2u, 3u, 24u, 1u, page);
    (void)mp_relay_listing_receive(&l, T0 + 20u, page, n);
    ut_check(l.count == 26u && l.unreadable == 1u,
             "both pages make the list: 27 rows, the one that does not read left out");
    ut_check(strcmp(l.row[0].announce.name, "Host 0") == 0 && l.row[0].code[0] == 0u &&
                 strcmp(l.row[25].announce.name, "Host 26") == 0,
             "in the relay's order, each with its code");

    ut_section("the next round");
    ut_check(mp_relay_listing_tick(&l, T0 + 1999u, out, sizeof out) == 0u,
             "not before two seconds from the last round's start");
    ut_check(mp_relay_listing_tick(&l, T0 + 2000u, out, sizeof out) == MP_RELAY_LIST_QUERY_BYTES &&
                 le16(out + 36) == 0u,
             "then page 0 again");
    ut_check(mp_relay_listing_tick(&l, T0 + 2999u, out, sizeof out) == 0u &&
                 mp_relay_listing_tick(&l, T0 + 3000u, out, sizeof out) ==
                     MP_RELAY_LIST_QUERY_BYTES,
             "unanswered, asked again a second later");
    n = page_of(0u, 2u, 2u, 50u, 99u, page);
    (void)mp_relay_listing_receive(&l, T0 + 3010u, page, n);
    ut_check(l.count == 26u, "the old list stands while the round is half done");
    n = page_of(0u, 1u, 2u, 50u, 99u, page);
    (void)mp_relay_listing_receive(&l, T0 + 3020u, page, n);
    ut_check(l.count == 2u && strcmp(l.row[1].announce.name, "Host 51") == 0,
             "an answer for page 0 when page 1 was asked ends the round with what came");

    ut_section("a cookie refused, and a cookie grown old");
    (void)mp_relay_listing_receive(&l, T0 + 3030u, page, fake_open_nack(6u, 0u, page));
    ut_check(!l.have_cookie && l.nacks == 1u, "a Nack 6 drops the cookie");
    ut_check(cookie_for(&l, &relay, T0 + 3040u), "and the next tick asks for a new one");
    ut_check(mp_relay_listing_tick(&l, T0 + 63039u, out, sizeof out) == MP_RELAY_LIST_QUERY_BYTES,
             "a round a minute on still uses it");
    ut_check(fake_hello_read(out, mp_relay_listing_tick(&l, T0 + 65040u, out, sizeof out),
                             &role, &nonce),
             "past a minute a Hello comes before the next query");
}

int main(void)
{
    test_the_cookie();
    test_pages();
    return ut_summary("mp_relay_listing");
}
