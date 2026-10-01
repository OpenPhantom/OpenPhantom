/* mp_udp.c: two real UDP sockets on localhost, in one process.
 *
 * The socket is the loopback's real counterpart, so the test that matters is the same shape: a
 * packet crosses and comes back with its bytes and its sender intact. Around it are the answers no
 * loopback needed: a recv on an empty socket returns at once rather than blocking, an address is
 * parsed strictly, and a datagram larger than the reader's buffer is dropped rather than torn.
 *
 * A machine without Winsock, or one whose loopback is firewalled, is not a broken build: init
 * failing is a skip, the same way mp_socket's test treats a machine with no library.
 */
#include "unittest.h"

#include "mp_transport.h"
#include "mp_udp.h"

#include "common/text.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Non-blocking recv, so a lost packet becomes a clean failure at the deadline rather than a hang.
 * Localhost delivery is immediate, so the budget only has to cover scheduling, not the network. */
static size_t recv_within(const mp_transport_t *t, uint32_t *from, void *buffer, size_t capacity)
{
    int i;

    for (i = 0; i < 200; ++i) {
        size_t got = mp_transport_recv(t, from, buffer, capacity);

        if (got != 0u) {
            return got;
        }
        Sleep(1);
    }
    return 0u;
}

static void check_resolve(void)
{
    static mp_udp_t udp;

    ut_section("address parsing is strict");

    if (!mp_udp_init(&udp, 0u)) {
        ut_check(true, "no udp socket on this machine, so parsing is skipped");
        return;
    }

    ut_check(mp_udp_resolve(&udp, "127.0.0.1:6000") != 0u, "a valid address resolves");
    ut_check(mp_udp_resolve(&udp, "1.2.3.4:1") != 0u, "so does the low port");
    ut_check(mp_udp_resolve(&udp, "255.255.255.255:65535") != 0u, "and the high extremes");

    ut_check(mp_udp_resolve(&udp, "127.0.0.1") == 0u, "an address with no port is refused");
    ut_check(mp_udp_resolve(&udp, "1.2.3:6000") == 0u, "so is one with three octets");
    ut_check(mp_udp_resolve(&udp, "1.2.3.256:6000") == 0u, "and an octet past 255");
    ut_check(mp_udp_resolve(&udp, "1.2.3.4:0") == 0u, "and port zero");
    ut_check(mp_udp_resolve(&udp, "1.2.3.4:99999") == 0u, "and a port past 65535");
    ut_check(mp_udp_resolve(&udp, "1.2.3.4:60x") == 0u, "and trailing junk on the port");
    ut_check(mp_udp_resolve(&udp, "x:6000") == 0u, "and a non-numeric host");

    /* The same address twice is one endpoint, which is what lets the session compare on
     * equality. */
    ut_check(mp_udp_resolve(&udp, "9.9.9.9:9") == mp_udp_resolve(&udp, "9.9.9.9:9"),
             "the same address interns to the same endpoint");

    mp_udp_shutdown(&udp);
}

static void check_round_trip(void)
{
    static mp_udp_t host;
    static mp_udp_t client;
    mp_transport_t  ht;
    mp_transport_t  ct;
    char            address[32];
    uint32_t        host_ep;
    uint32_t        from = 0;
    uint8_t         buffer[64];
    size_t          got;

    ut_section("a packet crosses localhost and is answered");

    if (!mp_udp_init(&host, 0u)) {
        ut_check(true, "no udp socket on this machine, so the round trip is skipped");
        return;
    }
    if (!mp_udp_init(&client, 0u)) {
        ut_check(true, "no second udp socket, skipped");
        mp_udp_shutdown(&host);
        return;
    }

    ut_check(mp_udp_local_port(&host) != 0u, "the host's ephemeral port resolved");

    ht = mp_udp_transport(&host);
    ct = mp_udp_transport(&client);

    /* A recv on a socket nothing has sent to must return at once, which is the whole point of a
     * non-blocking socket and the one thing a blocking one would get wrong invisibly. */
    ut_check(mp_transport_recv(&ht, &from, buffer, sizeof buffer) == 0u,
             "a recv with nothing waiting returns immediately");

    text_format(address, sizeof address, "127.0.0.1:%u", (unsigned)mp_udp_local_port(&host));
    host_ep = mp_udp_resolve(&client, address);
    ut_check(host_ep != 0u, "the client resolves the host's address");

    ut_check(mp_transport_send(&ct, host_ep, "hello", 5u), "the client sends");
    got = recv_within(&ht, &from, buffer, sizeof buffer);
    ut_check(got == 5u && memcmp(buffer, "hello", 5u) == 0, "the host receives it whole");
    ut_check(from != 0u, "and learns the client's endpoint from the datagram");

    /* The host answers the endpoint it learned, without having been told the client's address. */
    ut_check(mp_transport_send(&ht, from, "world", 5u), "the host replies to that endpoint");
    got = recv_within(&ct, &from, buffer, sizeof buffer);
    ut_check(got == 5u && memcmp(buffer, "world", 5u) == 0, "the client receives the reply");

    /* Oversized: a datagram past the reader's buffer is dropped, not torn. */
    {
        uint8_t big[64];
        uint8_t small[8];

        memset(big, 0xAB, sizeof big);
        mp_transport_send(&ct, host_ep, big, sizeof big);
        got = recv_within(&ht, &from, small, sizeof small);
        ut_check(got == 0u, "a datagram too big for the buffer is dropped, not truncated");
    }

    mp_udp_shutdown(&client);
    mp_udp_shutdown(&host);
}

/* Sixteen strangers, each sending one datagram inside one tick, are all fresher than a peer that
 * last spoke a tick ago. Without the hold the recency rule would recycle the peer's slot and the
 * host's next packet to that number, connection id and all, would go to a stranger. */
static void check_held_endpoint_survives_a_storm(void)
{
    static mp_udp_t host;
    static mp_udp_t peer;
    static mp_udp_t stranger[MP_UDP_MAX_ENDPOINTS];
    mp_transport_t  ht;
    mp_transport_t  pt;
    char            address[32];
    uint32_t        host_ep;
    uint32_t        peer_ep = 0;
    uint32_t        from = 0;
    uint8_t         buffer[64];
    size_t          i;
    size_t          numbered = 0;

    ut_section("a held endpoint keeps its number through a storm of strangers");

    if (!mp_udp_init(&host, 0u) || !mp_udp_init(&peer, 0u)) {
        ut_check(true, "no udp sockets on this machine, skipped");
        return;
    }
    ht = mp_udp_transport(&host);
    pt = mp_udp_transport(&peer);
    text_format(address, sizeof address, "127.0.0.1:%u", (unsigned)mp_udp_local_port(&host));
    host_ep = mp_udp_resolve(&peer, address);

    mp_transport_send(&pt, host_ep, "peer", 4u);
    ut_check(recv_within(&ht, &peer_ep, buffer, sizeof buffer) == 4u && peer_ep != 0u,
             "the host numbers the peer");
    mp_transport_hold(&ht, peer_ep, true);

    for (i = 0; i < MP_UDP_MAX_ENDPOINTS; ++i) {
        mp_transport_t st;
        uint32_t       ep;

        if (!mp_udp_init(&stranger[i], 0u)) {
            continue;
        }
        st = mp_udp_transport(&stranger[i]);
        ep = mp_udp_resolve(&stranger[i], address);
        mp_transport_send(&st, ep, "x", 1u);
    }
    for (i = 0; i < MP_UDP_MAX_ENDPOINTS; ++i) {
        if (recv_within(&ht, &from, buffer, sizeof buffer) == 1u && from != 0u) {
            ++numbered;
        }
    }
    /* Fifteen free slots and then one recycled from among the strangers themselves: the table
     * stays open to newcomers, it only refuses them the held slot. */
    ut_check(numbered == MP_UDP_MAX_ENDPOINTS, "every stranger was numbered");

    mp_transport_send(&pt, host_ep, "peer", 4u);
    ut_check(recv_within(&ht, &from, buffer, sizeof buffer) == 4u && from == peer_ep,
             "and the peer still arrives under its own number");

    mp_transport_hold(&ht, peer_ep, false);
    {
        mp_transport_t st = mp_udp_transport(&stranger[0]);

        mp_transport_send(&st, mp_udp_resolve(&stranger[0], address), "y", 1u);
    }
    ut_check(recv_within(&ht, &from, buffer, sizeof buffer) == 1u && from != 0u,
             "a newcomer after the release is still numbered");

    for (i = 0; i < MP_UDP_MAX_ENDPOINTS; ++i) {
        mp_udp_shutdown(&stranger[i]);
    }
    mp_udp_shutdown(&peer);
    mp_udp_shutdown(&host);
}

/* Refuse ahead of a packet does not hide it. Every caller loops until the transport answers
 * zero, and a zero for a datagram it had to throw away ended that loop at the first one: a
 * stranger could hold the real traffic back with a trickle of oversized or empty datagrams. */
static void check_refuse_does_not_hide_a_packet(void)
{
    static mp_udp_t host;
    static mp_udp_t client;
    mp_transport_t  ht;
    mp_transport_t  ct;
    char            address[32];
    uint32_t        host_ep;
    uint32_t        from = 0;
    uint8_t         big[64];
    uint8_t         small[8];
    size_t          got;

    ut_section("a datagram thrown away does not hide the packet behind it");

    if (!mp_udp_init(&host, 0u) || !mp_udp_init(&client, 0u)) {
        ut_check(true, "no udp sockets on this machine, skipped");
        return;
    }
    ht = mp_udp_transport(&host);
    ct = mp_udp_transport(&client);
    text_format(address, sizeof address, "127.0.0.1:%u", (unsigned)mp_udp_local_port(&host));
    host_ep = mp_udp_resolve(&client, address);

    memset(big, 0xAB, sizeof big);
    mp_transport_send(&ct, host_ep, big, sizeof big);
    mp_transport_send(&ct, host_ep, "ok", 2u);
    Sleep(20);   /* both are queued before the one call below */
    got = mp_transport_recv(&ht, &from, small, sizeof small);
    ut_check(got == 2u && memcmp(small, "ok", 2u) == 0,
             "one call reads past the datagram too large for the buffer to the packet behind it");
    ut_check(mp_udp_recv_dropped(&host) == 1u, "and counts the one it threw away");
    ut_check(mp_transport_recv(&ht, &from, small, sizeof small) == 0u,
             "and then answers zero, because nothing more waits");

    mp_udp_shutdown(&client);
    mp_udp_shutdown(&host);
}

int main(void)
{
    check_resolve();
    check_round_trip();
    check_held_endpoint_survives_a_storm();
    check_refuse_does_not_hide_a_packet();

    return ut_summary("mp_udp");
}
