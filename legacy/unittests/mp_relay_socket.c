/* mp_relay_socket.c: the relay's two sockets, on this machine's own loopback: two of them play
 * PC and relay to each other, and a third is the stranger whose datagrams must not get through.
 */
#include "unittest.h"

#include "mp_relay_socket.h"
#include "mp_relay_wire.h"

#include <windows.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* The loopback address of a family, and the port of the socket it points at. */
static mp_relay_address_t loopback(mp_relay_family_t family, uint16_t port)
{
    mp_relay_address_t address;

    memset(&address, 0, sizeof address);
    address.ipv6 = family == MP_RELAY_IPV6;
    if (address.ipv6) {
        address.bytes[15] = 1u;
    } else {
        address.bytes[0] = 127u;
        address.bytes[3] = 1u;
    }
    address.port = port;
    return address;
}

/* Waits up to a second for a datagram to arrive. */
static size_t recv_soon(mp_relay_socket_t *s, mp_relay_family_t family, uint8_t *buffer,
                        size_t capacity)
{
    int i;

    for (i = 0; i < 100; ++i) {
        size_t got = mp_relay_socket_recv(s, family, buffer, capacity);

        if (got != 0u) {
            return got;
        }
        Sleep(10);
    }
    return 0u;
}

static void test_family(mp_relay_family_t family)
{
    static mp_relay_socket_t pc;
    static mp_relay_socket_t relay;
    static mp_relay_socket_t stranger;
    mp_relay_address_t       address;
    uint8_t                  buffer[MP_RELAY_DATAGRAM_MAX + 8u];
    uint8_t                  big[MP_RELAY_DATAGRAM_MAX + 1u];
    size_t                   got;

    ut_check(mp_relay_socket_open(&pc) && mp_relay_socket_open(&relay) &&
                 mp_relay_socket_open(&stranger),
             "three sockets open");
    if (!pc.family[family].open) {
        ut_check(true, "this machine has no socket in the family; nothing more to try");
        mp_relay_socket_close(&pc);
        mp_relay_socket_close(&relay);
        mp_relay_socket_close(&stranger);
        return;
    }
    ut_check(!mp_relay_socket_usable(&pc, family) &&
                 !mp_relay_socket_send(&pc, family, (const uint8_t *)"x", 1u),
             "before the relay's address is known nothing is sent");
    address = loopback(family, relay.family[family].local_port);
    mp_relay_socket_set_relay(&pc, &address, 1u);
    address = loopback(family, pc.family[family].local_port);
    mp_relay_socket_set_relay(&relay, &address, 1u);
    mp_relay_socket_set_relay(&stranger, &address, 1u);
    ut_check(mp_relay_socket_usable(&pc, family), "with it the family is usable");

    ut_check(mp_relay_socket_send(&pc, family, (const uint8_t *)"MPRL-HELLO", 10u),
             "a datagram to the relay is handed to the stack");
    got = recv_soon(&relay, family, buffer, sizeof buffer);
    ut_check(got == 10u && memcmp(buffer, "MPRL-HELLO", 10u) == 0, "and arrives whole");

    ut_check(mp_relay_socket_send(&stranger, family, (const uint8_t *)"FORGED", 6u),
             "a stranger writes to the PC's port");
    ut_check(mp_relay_socket_send(&relay, family, (const uint8_t *)"COOKIE", 6u),
             "and so does the relay");
    got = recv_soon(&pc, family, buffer, sizeof buffer);
    ut_check(got == 6u && memcmp(buffer, "COOKIE", 6u) == 0 && pc.family[family].foreign == 1u,
             "the PC hears only the relay, and counts the stranger");

    memset(big, 0x5A, sizeof big);
    ut_check(!mp_relay_socket_send(&pc, family, big, sizeof big),
             "a datagram over the protocol's 1232 bytes is refused");
    mp_relay_socket_close(&pc);
    mp_relay_socket_close(&relay);
    mp_relay_socket_close(&stranger);
    mp_relay_socket_close(&stranger);
    ut_check(!pc.family[family].open && !mp_relay_socket_usable(&pc, family),
             "closed, twice over, nothing is left open");
}

int main(void)
{
    ut_section("IPv4");
    test_family(MP_RELAY_IPV4);
    ut_section("IPv6");
    test_family(MP_RELAY_IPV6);
    ut_section("the addresses of a lookup");
    {
        mp_relay_socket_t  s;
        mp_relay_address_t list[3];

        memset(&s, 0, sizeof s);
        list[0] = loopback(MP_RELAY_IPV4, 1000u);
        list[1] = loopback(MP_RELAY_IPV4, 2000u);
        list[2] = loopback(MP_RELAY_IPV6, 3000u);
        mp_relay_socket_set_relay(&s, list, 3u);
        ut_check(s.family[MP_RELAY_IPV4].relay_port == 1000u &&
                     s.family[MP_RELAY_IPV6].relay_port == 3000u,
                 "the first address of each family is the one kept");
        list[0] = loopback(MP_RELAY_IPV4, 4000u);
        mp_relay_socket_set_relay(&s, list, 1u);
        ut_check(s.family[MP_RELAY_IPV4].relay_port == 4000u &&
                     s.family[MP_RELAY_IPV6].have_relay &&
                     s.family[MP_RELAY_IPV6].relay_port == 3000u,
                 "a later lookup without an IPv6 address keeps the one IPv6 had: a leg may use it");
        ut_check(strcmp(mp_relay_family_name(MP_RELAY_IPV6), "IPv6") == 0, "and named");
    }
    return ut_summary("mp_relay_socket");
}
