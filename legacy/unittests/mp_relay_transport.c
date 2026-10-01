/* mp_relay_transport.c: a host and a player through a relay on this machine's loopback.
 *
 * The relay here is the tests' own: one UDP socket that answers Hellos, opens the host's Register
 * and the player's Join with the fake relay's handshake, tells the host a player sat down, and
 * carries each side's sealed game packets to the other under the other leg. The two transports
 * are the real ones, sockets, link, seats and vtable, with the fetch and the clocks handed in.
 */
#include "unittest.h"

#include "mp_relay_fake.h"
#include "mp_relay_inner.h"
#include "mp_relay_transport.h"
#include "mp_socket.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

typedef struct test_relay {
    SOCKET             sock;
    uint16_t           port;
    fake_relay_t       host_side;
    fake_relay_t       member_side;
    struct sockaddr_in host_at;
    struct sockaddr_in member_at;
    uint32_t           forwarded;
    uint32_t           refreshes;
    uint32_t           closes;
    uint32_t           leaves;
} test_relay_t;

static test_relay_t         relay;
static mp_relay_transport_t host;
static mp_relay_transport_t member;

/* ---- the services ---------------------------------------------------------------------------- */

static uint32_t       clock_ms = 500000u;
static bool           fetch_out;
static bool           fetch_wanted_key;
static uint16_t       fetch_port;
static uint32_t       saves;

static bool t_fetch_begin(bool want_key)
{
    if (fetch_out) {
        return false;
    }
    fetch_out        = true;
    fetch_wanted_key = want_key;
    return true;
}

/* A fetch not yet taken counts as running, so one transport never takes the other's. */
static bool t_fetch_running(void)
{
    return fetch_out;
}

static bool t_fetch_take(mp_relay_fetch_result_t *out)
{
    if (!fetch_out) {
        return false;
    }
    memset(out, 0, sizeof *out);
    out->wanted_key          = fetch_wanted_key;
    out->key_fetched         = fetch_wanted_key;
    out->key                 = fake_relay_key(&relay.host_side);
    out->addresses           = 1u;
    out->address[0].bytes[0] = 127u;
    out->address[0].bytes[3] = 1u;
    out->address[0].port     = fetch_port;
    out->elapsed_ms          = 7u;
    fetch_out                = false;
    return true;
}

static bool t_state_load(mp_relay_key_t *key, int64_t *at)
{
    (void)key;
    (void)at;
    return false;
}

static bool t_state_save(const mp_relay_key_t *key, int64_t at)
{
    (void)key;
    (void)at;
    ++saves;
    return true;
}

static int64_t t_unix_now(void)
{
    return 1800000000;
}

static uint32_t t_now_ms(void)
{
    return clock_ms;
}

static const mp_relay_services_t SERVICES = {
    t_fetch_begin, t_fetch_running, t_fetch_take, t_state_load,
    t_state_save,  t_unix_now,      t_now_ms,     fake_random,
};

/* ---- the relay ------------------------------------------------------------------------------- */

static bool relay_open(void)
{
    struct sockaddr_in me;
    int                len = (int)sizeof me;
    u_long             nonblocking = 1u;

    relay.sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    memset(&me, 0, sizeof me);
    me.sin_family      = AF_INET;
    me.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (relay.sock == INVALID_SOCKET || bind(relay.sock, (struct sockaddr *)&me, sizeof me) != 0 ||
        getsockname(relay.sock, (struct sockaddr *)&me, &len) != 0 ||
        ioctlsocket(relay.sock, FIONBIO, &nonblocking) != 0) {
        return false;
    }
    relay.port = ntohs(me.sin_port);
    return true;
}

static void relay_to(const struct sockaddr_in *to, const uint8_t *data, size_t bytes)
{
    if (bytes != 0u) {
        (void)sendto(relay.sock, (const char *)data, (int)bytes, 0, (struct sockaddr *)to,
                     sizeof *to);
    }
}

static void relay_sealed(const uint8_t *in, size_t got, const struct sockaddr_in *from)
{
    mp_relay_sealed_header_t header;
    uint8_t                  plain[MP_RELAY_DATAGRAM_MAX];
    uint8_t                  out[MP_RELAY_DATAGRAM_MAX];
    size_t                   n = 0u;
    bool                     from_host = in[0] == MP_RELAY_TYPE_HOST_TO_RELAY ||
                                         in[0] == MP_RELAY_TYPE_HOST_TO_RELAY_CONTROL;
    fake_relay_t            *side = from_host ? &relay.host_side : &relay.member_side;

    (void)from;
    if (!fake_open(side, in, got, &header, plain, sizeof plain, &n)) {
        return;
    }
    switch (in[0]) {
    case MP_RELAY_TYPE_HOST_TO_RELAY:
        n = fake_seal(&relay.member_side, (uint8_t)MP_RELAY_TYPE_RELAY_TO_MEMBER, 0u,
                      relay.member_side.joined.gen, relay.member_side.joined.member_index, plain,
                      n, out, sizeof out);
        relay_to(&relay.member_at, out, n);
        ++relay.forwarded;
        return;
    case MP_RELAY_TYPE_MEMBER_TO_RELAY:
        n = fake_seal(&relay.host_side, (uint8_t)MP_RELAY_TYPE_RELAY_TO_HOST, 1u,
                      relay.member_side.joined.gen, relay.host_side.registered.session_index,
                      plain, n, out, sizeof out);
        relay_to(&relay.host_at, out, n);
        ++relay.forwarded;
        return;
    default:
        relay.refreshes += plain[0] == MP_RELAY_INNER_REFRESH ||
                                   plain[0] == MP_RELAY_INNER_MEMBER_REFRESH
                               ? 1u
                               : 0u;
        relay.closes += plain[0] == MP_RELAY_INNER_CLOSE ? 1u : 0u;
        relay.leaves += plain[0] == MP_RELAY_INNER_LEAVE ? 1u : 0u;
        return;
    }
}

static void relay_step(void)
{
    uint8_t            in[MP_RELAY_DATAGRAM_MAX + 64u];
    uint8_t            out[MP_RELAY_DATAGRAM_MAX];
    uint8_t            inner[64];
    struct sockaddr_in from;
    int                from_len;
    int                got;
    size_t             n;
    uint8_t            role;
    uint64_t           nonce;

    for (;;) {
        from_len = (int)sizeof from;
        got = recvfrom(relay.sock, (char *)in, (int)sizeof in, 0, (struct sockaddr *)&from,
                       &from_len);
        if (got <= 0) {
            return;
        }
        switch (in[0]) {
        case MP_RELAY_TYPE_HELLO:
            if (fake_hello_read(in, (size_t)got, &role, &nonce)) {
                relay_to(&from, out, fake_cookie(&relay.host_side, role, nonce, out));
            }
            break;
        case MP_RELAY_TYPE_REGISTER:
            relay.host_at = from;
            relay_to(&from, out, fake_answer(&relay.host_side, in, (size_t)got, out, sizeof out));
            break;
        case MP_RELAY_TYPE_JOIN:
            relay.member_at = from;
            relay_to(&from, out, fake_answer(&relay.member_side, in, (size_t)got, out,
                                             sizeof out));
            n = fake_member_open(1u, 0u, relay.member_side.joined.gen, relay.member_side.joined.r,
                                 inner);
            relay_to(&relay.host_at, out, fake_to_host(&relay.host_side, inner, n, out));
            break;
        default:
            relay_sealed(in, (size_t)got, &from);
            break;
        }
    }
}

/* ---- driving --------------------------------------------------------------------------------- */

/* Every read a pump makes, until the transport says nothing more waits. Game packets that arrive
 * are counted and dropped: the tests that want them read them themselves. */
static void drain(mp_relay_transport_t *t)
{
    mp_transport_t face = mp_relay_transport_face(t);
    uint8_t        buffer[MP_RELAY_GAME_PACKET_MAX];
    uint32_t       from;

    if (t->up) {
        while (face.recv(face.context, &from, buffer, sizeof buffer) != 0u) {
        }
    }
}

/* Ten milliseconds of both sides and the relay, `rounds` times or until `done` holds. */
static bool run(int rounds, bool (*done)(void))
{
    int i;

    for (i = 0; i < rounds; ++i) {
        clock_ms += 10u;
        drain(&host);
        drain(&member);
        Sleep(1);
        relay_step();
        if (done != NULL && done()) {
            return true;
        }
    }
    return done == NULL;
}

static bool host_ready(void)
{
    return mp_relay_transport_state(&host) == MP_RELAY_STATE_READY;
}

static bool both_ready(void)
{
    return host_ready() && mp_relay_transport_state(&member) == MP_RELAY_STATE_READY &&
           mp_relay_seats_open_count(&host.seats) == 1u;
}

/* The next game packet `t` hands its session, within a second. */
static size_t await_packet(mp_relay_transport_t *t, uint32_t *from, uint8_t *buffer,
                           size_t capacity)
{
    mp_transport_t face = mp_relay_transport_face(t);
    int            i;

    for (i = 0; i < 100; ++i) {
        size_t got;

        clock_ms += 10u;
        Sleep(1);
        relay_step();
        got = face.recv(face.context, from, buffer, capacity);
        if (got != 0u) {
            return got;
        }
    }
    return 0u;
}

static void test_a_session_through_the_relay(void)
{
    mp_transport_t host_face;
    mp_transport_t member_face;
    uint8_t        code[MP_RELAY_CODE_BYTES];
    uint8_t        buffer[MP_RELAY_GAME_PACKET_MAX];
    uint32_t       from = 0u;
    uint64_t       address = 0u;
    size_t         got;
    size_t         i;

    ut_section("a host registers through the relay");
    ut_check(mp_relay_transport_state(&host) == MP_RELAY_STATE_OFF,
             "a transport not started is off");
    ut_check(mp_relay_transport_start_host(&host, &SERVICES, 3u), "the host's transport starts");
    ut_check(mp_relay_transport_state(&host) == MP_RELAY_STATE_LOOKING_UP,
             "and looks the relay up first");
    ut_check(run(200, host_ready), "within two seconds it is registered");
    ut_check(mp_relay_transport_code(&host, code) &&
                 memcmp(code, relay.host_side.registered.code, sizeof code) == 0,
             "with the relay's code");
    ut_check(saves == 1u && host.leg_family == MP_RELAY_IPV4,
             "the fetched key was kept, and the leg is IPv4, the only family the relay has here");

    ut_section("a player joins by the code");
    ut_check(mp_relay_transport_start_join(&member, &SERVICES, code),
             "the player's transport starts");
    ut_check(run(300, both_ready), "the player is seated, and the host has a seat for them");

    ut_section("game packets both ways");
    host_face   = mp_relay_transport_face(&host);
    member_face = mp_relay_transport_face(&member);
    ut_check(member_face.send(member_face.context, 1u, "PING", 4u), "the player sends to its host");
    got = await_packet(&host, &from, buffer, sizeof buffer);
    ut_check(got == 4u && memcmp(buffer, "PING", 4u) == 0 && from == 1u,
             "the host's session reads it from the player's endpoint");
    for (i = 0; i < 8u; ++i) {
        address |= (uint64_t)relay.member_side.joined.r[i] << (8u * i);
    }
    ut_check(host_face.address(host_face.context, from) == address &&
                 member_face.address(member_face.context, 1u) == 1u,
             "the endpoint's address is the seat's R; the player's host is address 1");
    ut_check(host_face.send(host_face.context, from, "PONG", 4u), "the host answers the endpoint");
    got = await_packet(&member, &from, buffer, sizeof buffer);
    ut_check(got == 4u && memcmp(buffer, "PONG", 4u) == 0 && from == 1u,
             "and the player's session reads it from its host");
    ut_check(!host_face.send(host_face.context, 9u, "LOST", 4u) &&
                 !member_face.send(member_face.context, 2u, "LOST", 4u),
             "a send to an endpoint nobody sits on is refused");
    ut_check(relay.forwarded == 2u && host.game_in == 1u && member.game_in == 1u,
             "the relay carried exactly the two");

    ut_section("the goodbye");
    mp_relay_transport_farewell(&member);
    mp_relay_transport_farewell(&host);
    (void)run(5, NULL);
    ut_check(relay.leaves == 1u && relay.closes == 1u,
             "a Leave from the player, a Close from the host");
    mp_relay_transport_report(&host);
    mp_relay_transport_report(&member);
    mp_relay_transport_close(&host);
    mp_relay_transport_close(&member);
    mp_relay_transport_close(&member);
    ut_check(mp_relay_transport_state(&host) == MP_RELAY_STATE_OFF &&
                 !host_face.send(host_face.context, 1u, "LATE", 4u) &&
                 host_face.recv(host_face.context, &from, buffer, sizeof buffer) == 0u,
             "closed, twice over, it is off and neither sends nor reads");
}

static void test_a_relay_that_does_not_answer(void)
{
    SOCKET             silent = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in me;
    int                len = (int)sizeof me;
    int                i;

    ut_section("a relay that does not answer: the transport waits and tries again");
    memset(&me, 0, sizeof me);
    me.sin_family      = AF_INET;
    me.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ut_check(silent != INVALID_SOCKET && bind(silent, (struct sockaddr *)&me, sizeof me) == 0 &&
                 getsockname(silent, (struct sockaddr *)&me, &len) == 0,
             "a socket that never reads");
    fetch_port = ntohs(me.sin_port);
    ut_check(mp_relay_transport_start_host(&host, &SERVICES, 2u), "a host starts against it");
    for (i = 0; i < 60; ++i) {
        clock_ms += 100u;
        drain(&host);
    }
    ut_check(mp_relay_transport_state(&host) == MP_RELAY_STATE_WAITING &&
                 strcmp(mp_relay_transport_failure(&host), "the relay did not answer") == 0 &&
                 host.attempts_failed == 1u,
             "after four Hellos a second apart: waiting, because the relay did not answer");
    mp_relay_transport_close(&host);
    (void)closesocket(silent);
}

int main(void)
{
    ut_check(mp_socket_startup() && relay_open(), "the test relay's socket opens");
    fake_relay_init(&relay.host_side);
    fake_relay_init(&relay.member_side);
    fetch_port = relay.port;
    test_a_session_through_the_relay();
    test_a_relay_that_does_not_answer();
    (void)closesocket(relay.sock);
    mp_socket_shutdown();
    return ut_summary("mp_relay_transport");
}
