/* mp_relay_seats.c: a relay host's players as endpoints, following the relay's notices. */
#include "unittest.h"

#include "mp_relay_inner.h"
#include "mp_relay_seats.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static void r_of(uint8_t *r, uint8_t first)
{
    size_t i;

    for (i = 0; i < MP_RELAY_SEAT_VALUE_BYTES; ++i) {
        r[i] = (uint8_t)(first + i);
    }
}

static void test_seating(void)
{
    mp_relay_seats_t seats;
    uint8_t          r1[MP_RELAY_SEAT_VALUE_BYTES];
    uint8_t          r2[MP_RELAY_SEAT_VALUE_BYTES];
    uint8_t          slot = 0u;
    uint16_t         gen  = 0u;
    uint32_t         a;
    uint32_t         b;

    ut_section("players are seated and reached");
    mp_relay_seats_init(&seats);
    r_of(r1, 0x10u);
    r_of(r2, 0x40u);
    a = mp_relay_seats_open(&seats, 1u, 7u, 0u, r1, 1u, 3u);
    b = mp_relay_seats_open(&seats, 2u, 9u, 0u, r2, 1u, 4u);
    ut_check(a != 0u && b != 0u && a != b && seats.opened == 2u,
             "two MemberOpens are two endpoints");
    ut_check(mp_relay_seats_from(&seats, 1u, 7u) == a && mp_relay_seats_from(&seats, 2u, 9u) == b,
             "each player's packets arrive under their own endpoint");
    ut_check(mp_relay_seats_to(&seats, b, &slot, &gen) && slot == 2u && gen == 9u,
             "and a packet for an endpoint goes to its slot and generation");
    ut_check(mp_relay_seats_from(&seats, 1u, 8u) == 0u && seats.unknown_data == 1u,
             "a packet under a generation no seat has is dropped and counted");
    ut_check(mp_relay_seats_address(&seats, a) == 0x1716151413121110ull &&
                 mp_relay_seats_address(&seats, 9u) == 0u,
             "an endpoint's address is the first eight bytes of R, none for no seat");
    ut_check(mp_relay_seats_open_count(&seats) == 2u, "two seats are open");
}

static void test_closing(void)
{
    mp_relay_seats_t seats;
    uint8_t          r1[MP_RELAY_SEAT_VALUE_BYTES];
    uint8_t          slot = 0u;
    uint16_t         gen  = 0u;
    uint32_t         a;

    ut_section("a MemberClosed ends only the seat it names, and only after its open");
    mp_relay_seats_init(&seats);
    r_of(r1, 0x10u);
    a = mp_relay_seats_open(&seats, 1u, 7u, 0u, r1, 2u, 10u);
    ut_check(mp_relay_seats_close(&seats, 1u, 6u, 2u, 11u) == 0u,
             "one for another generation of the slot closes nothing");
    ut_check(mp_relay_seats_close(&seats, 1u, 7u, 2u, 9u) == 0u &&
                 mp_relay_seats_close(&seats, 1u, 7u, 1u, 50u) == 0u && seats.stale == 3u,
             "one from before the open, on its leg or an older one, is stale");
    mp_relay_seats_hold(&seats, a, true);
    ut_check(mp_relay_seats_close(&seats, 1u, 7u, 2u, 11u) == a && seats.closed == 1u,
             "one after the open closes it");
    ut_check(!mp_relay_seats_to(&seats, a, &slot, &gen) && seats.refused_sends == 1u &&
                 mp_relay_seats_from(&seats, 1u, 7u) == 0u,
             "nothing more is sent to it or taken from it");
    ut_check(mp_relay_seats_address(&seats, a) != 0u,
             "while the session holds the peer the endpoint keeps its address");
    mp_relay_seats_hold(&seats, a, false);
    ut_check(mp_relay_seats_address(&seats, a) == 0u, "let go, it is forgotten");

    ut_section("a notice on a new leg is newer whatever its counter");
    a = mp_relay_seats_open(&seats, 1u, 7u, 0u, r1, 2u, 10u);
    ut_check(mp_relay_seats_close(&seats, 1u, 7u, 3u, 0u) == a,
             "counter 0 on the next leg closes a seat opened under counter 10");
}

static void test_continuing(void)
{
    mp_relay_seats_t seats;
    uint8_t          r1[MP_RELAY_SEAT_VALUE_BYTES];
    uint8_t          r2[MP_RELAY_SEAT_VALUE_BYTES];
    uint8_t          slot = 0u;
    uint16_t         gen  = 0u;
    uint32_t         a;
    uint32_t         again;

    ut_section("a player who proves the seat again keeps the endpoint");
    mp_relay_seats_init(&seats);
    r_of(r1, 0x10u);
    r_of(r2, 0x40u);
    a = mp_relay_seats_open(&seats, 1u, 7u, 0u, r1, 1u, 3u);
    mp_relay_seats_hold(&seats, a, true);
    again = mp_relay_seats_open(&seats, 3u, 8u, (uint8_t)MP_RELAY_MEMBER_OPEN_CONTINUES, r1, 2u,
                                0u);
    ut_check(again == a && seats.continued == 1u && seats.opened == 1u,
             "a continuing MemberOpen with a known R is the same endpoint");
    ut_check(mp_relay_seats_to(&seats, a, &slot, &gen) && slot == 3u && gen == 8u &&
                 mp_relay_seats_from(&seats, 1u, 7u) == 0u,
             "under its new slot and generation, and no longer the old ones");
    ut_check(mp_relay_seats_open(&seats, 4u, 1u, (uint8_t)MP_RELAY_MEMBER_OPEN_CONTINUES, r2, 2u,
                                 1u) != a &&
                 seats.opened == 2u,
             "the continuing flag with an R nobody had is a new player");
    ut_check(mp_relay_seats_open(&seats, 3u, 8u, 0u, r1, 2u, 0u) == 0u && seats.stale == 1u,
             "the same MemberOpen again is not newer and changes nothing");

    ut_section("a new player on a slot means the one before has gone");
    a = mp_relay_seats_open(&seats, 5u, 1u, 0u, r1, 2u, 5u);
    mp_relay_seats_hold(&seats, a, true);
    again = mp_relay_seats_open(&seats, 5u, 2u, 0u, r2, 2u, 6u);
    ut_check(again != 0u && again != a && !mp_relay_seats_to(&seats, a, &slot, &gen),
             "the earlier seat on slot 5 is closed");
}

static void test_room(void)
{
    mp_relay_seats_t seats;
    uint8_t          r[MP_RELAY_SEAT_VALUE_BYTES];
    uint32_t         i;
    uint32_t         last = 0u;

    ut_section("sixteen endpoints, and no more");
    mp_relay_seats_init(&seats);
    for (i = 0; i < MP_RELAY_SEATS_MAX; ++i) {
        r_of(r, (uint8_t)(i * 16u));
        r[15] = (uint8_t)i;
        last  = mp_relay_seats_open(&seats, (uint8_t)i, 1u, 0u, r, 1u, i);
        mp_relay_seats_hold(&seats, last, true);
    }
    ut_check(last == MP_RELAY_SEATS_MAX, "the sixteenth is endpoint 16");
    r_of(r, 0xEEu);
    ut_check(mp_relay_seats_open(&seats, 200u, 1u, 0u, r, 1u, 99u) == 0u && seats.full == 1u,
             "a seventeenth has no endpoint, and is counted");
    memset(r, 0, sizeof r);
    mp_relay_seats_init(&seats);
    ut_check(mp_relay_seats_address(&seats, mp_relay_seats_open(&seats, 0u, 1u, 0u, r, 1u, 0u)) ==
                 1u,
             "an R whose first eight bytes are zero still has an address");
}

/* The relay gives out the lowest free slot, and a member index it reuses starts again at
 * generation 1, so after a restart the pairs repeat for whoever proves a seat first. */
static void test_a_relay_restart(void)
{
    mp_relay_seats_t seats;
    uint8_t          ra[MP_RELAY_SEAT_VALUE_BYTES];
    uint8_t          rb[MP_RELAY_SEAT_VALUE_BYTES];
    uint8_t          rc[MP_RELAY_SEAT_VALUE_BYTES];
    uint8_t          slot = 0u;
    uint16_t         gen  = 0u;
    uint32_t         a;
    uint32_t         b;
    uint32_t         c;

    ut_section("after a relay restart the same pairs come back for other players");
    mp_relay_seats_init(&seats);
    r_of(ra, 0x10u);
    r_of(rb, 0x40u);
    r_of(rc, 0x70u);
    a = mp_relay_seats_open(&seats, 0u, 1u, 0u, ra, 1u, 0u);
    b = mp_relay_seats_open(&seats, 1u, 1u, 0u, rb, 1u, 1u);
    mp_relay_seats_hold(&seats, a, true);
    mp_relay_seats_hold(&seats, b, true);
    ut_check(mp_relay_seats_open(&seats, 0u, 1u, (uint8_t)MP_RELAY_MEMBER_OPEN_CONTINUES, rb, 2u,
                                 0u) == b,
             "B proves its seat first and is given pair (0, 1): it keeps its own endpoint");
    ut_check(mp_relay_seats_open(&seats, 1u, 1u, (uint8_t)MP_RELAY_MEMBER_OPEN_CONTINUES, ra, 2u,
                                 1u) == a,
             "A comes second on pair (1, 1), and keeps its own");
    ut_check(mp_relay_seats_to(&seats, a, &slot, &gen) && slot == 1u && gen == 1u &&
                 mp_relay_seats_to(&seats, b, &slot, &gen) && slot == 0u &&
                 mp_relay_seats_from(&seats, 0u, 1u) == b &&
                 mp_relay_seats_from(&seats, 1u, 1u) == a,
             "each endpoint reaches its own player, both ways");

    ut_section("the relay registered the session afresh: a new player on an old pair");
    mp_relay_seats_close_all(&seats);
    ut_check(mp_relay_seats_open_count(&seats) == 0u && mp_relay_seats_address(&seats, a) != 0u,
             "every seat of the old session is over, and a held endpoint keeps its address");
    c = mp_relay_seats_open(&seats, 0u, 1u, 0u, rc, 3u, 0u);
    ut_check(c != 0u && c != a && c != b && mp_relay_seats_from(&seats, 0u, 1u) == c,
             "the newcomer on pair (0, 1) is an endpoint of its own, not the held one");

    ut_section("notices out of order: an old player's open arrives after the new player's");
    mp_relay_seats_init(&seats);
    c = mp_relay_seats_open(&seats, 0u, 1u, 0u, rc, 1u, 12u);
    ut_check(mp_relay_seats_open(&seats, 0u, 1u, 0u, ra, 1u, 10u) == 0u && seats.stale == 1u,
             "the open of the player before, on the same pair under an older counter, is stale");
    ut_check(mp_relay_seats_close(&seats, 0u, 1u, 1u, 11u) == 0u &&
                 mp_relay_seats_from(&seats, 0u, 1u) == c,
             "and its close closes nothing: the player who sits there now is still reached");
}

int main(void)
{
    test_seating();
    test_closing();
    test_continuing();
    test_room();
    test_a_relay_restart();
    return ut_summary("mp_relay_seats");
}
