/* The announce socket, over the machine's own loopback, with no game.
 *
 * What a broadcast does on a given machine depends on its interfaces, so the test does not rely on
 * one: a listener on a test port and a sender that addresses 127.0.0.1 are the whole path a real
 * announce takes minus the routing, and everything the module owns is exercised, the shared bind,
 * the non-blocking receive, the sender's address coming back as text, and a datagram too large for
 * the buffer being counted rather than handed on.
 */
#include "unittest.h"

#include "mp_announce.h"
#include "mp_discovery.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Not the real announce port, so a game running on this machine cannot leak into the test. */
#define TEST_PORT 27977u

static size_t receive_within(mp_discovery_t *d, void *buffer, size_t capacity, char *from)
{
    int i;

    for (i = 0; i < 200; ++i) {
        size_t got = mp_discovery_receive(d, buffer, capacity, from);

        if (got != 0u) {
            return got;
        }
        Sleep(1);
    }
    return 0u;
}

static void check_an_announce_crosses(void)
{
    mp_discovery_t sender;
    mp_discovery_t listener;
    mp_announce_t  announce;
    mp_announce_t  back;
    uint8_t        out[MP_ANNOUNCE_BYTES];
    uint8_t        in[MP_ANNOUNCE_BYTES];
    char           from[MP_DISCOVERY_ADDRESS_MAX];

    ut_section("a host's announce reaches a browser on the same machine");
    ut_check(mp_discovery_open_listener(&listener, TEST_PORT),
             "the listener binds the announce port");
    ut_check(mp_discovery_open_sender(&sender, TEST_PORT), "the sender comes up on any port");

    memset(&announce, 0, sizeof announce);
    announce.wire        = 15u;
    announce.fingerprint = 0x1234u;
    announce.game_port   = 27960u;
    announce.players     = 1u;
    announce.slots       = 4u;
    mp_announce_name_clean("Naboo Hangar", announce.name);
    ut_check(mp_announce_encode(&announce, out, sizeof out) == MP_ANNOUNCE_BYTES, "encoded");

    ut_check(mp_discovery_send_to(&sender, "127.0.0.1", out, sizeof out),
             "sent to the machine itself");
    ut_check(receive_within(&listener, in, sizeof in, from) == MP_ANNOUNCE_BYTES,
             "and it arrives whole");
    ut_check(strcmp(from, "127.0.0.1") == 0, "with the sender's address as text");
    ut_check(mp_announce_decode(in, sizeof in, &back) && strcmp(back.name, "Naboo Hangar") == 0,
             "and decodes to what was sent");

    ut_check(mp_discovery_receive(&listener, in, sizeof in, from) == 0u,
             "an empty socket answers at once with nothing");

    mp_discovery_close(&sender);
    mp_discovery_close(&listener);
}

static void check_two_listeners_share_the_port(void)
{
    mp_discovery_t a;
    mp_discovery_t b;

    ut_section("two instances on one machine both listen on the announce port");
    ut_check(mp_discovery_open_listener(&a, TEST_PORT), "the first binds");
    ut_check(mp_discovery_open_listener(&b, TEST_PORT), "and so does the second, on the same port");
    mp_discovery_close(&a);
    mp_discovery_close(&b);
}

static void check_a_broadcast_is_at_least_sent(void)
{
    mp_discovery_t sender;
    uint8_t        out[MP_ANNOUNCE_BYTES];
    mp_announce_t  announce;

    ut_section("a broadcast leaves the socket");
    ut_check(mp_discovery_open_sender(&sender, TEST_PORT), "up");
    memset(&announce, 0, sizeof announce);
    announce.wire      = 15u;
    announce.game_port = 27960u;
    announce.players   = 1u;
    announce.slots     = 2u;
    mp_announce_name_clean("x", announce.name);
    (void)mp_announce_encode(&announce, out, sizeof out);
    ut_check(mp_discovery_broadcast(&sender, out, sizeof out),
             "the stack accepts a broadcast datagram; where it goes is the machine's business");
    ut_check(sender.sent == 1u, "and it is counted");
    mp_discovery_close(&sender);
}

static void check_refusals(void)
{
    mp_discovery_t d;
    uint8_t        buffer[8];
    char           from[MP_DISCOVERY_ADDRESS_MAX];

    ut_section("what a closed socket refuses");
    memset(&d, 0, sizeof d);
    ut_check(!mp_discovery_broadcast(&d, buffer, sizeof buffer),
             "a send on a socket that is not up");
    ut_check(mp_discovery_receive(&d, buffer, sizeof buffer, from) == 0u, "a receive on one");
    ut_check(mp_discovery_open_sender(&d, TEST_PORT), "up");
    ut_check(!mp_discovery_send_to(&d, "not an address", buffer, sizeof buffer),
             "a send to nonsense");
    ut_check(!mp_discovery_broadcast(&d, buffer, 0u), "and a send of nothing");
    mp_discovery_close(&d);
    mp_discovery_close(&d);   /* twice is harmless */
}

int main(void)
{
    check_an_announce_crosses();
    check_two_listeners_share_the_port();
    check_a_broadcast_is_at_least_sent();
    check_refusals();

    return ut_summary("the announce socket");
}
