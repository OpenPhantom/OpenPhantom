/* mp_relay_list.c: the public list over its sockets, against a relay of the test's own on the
 * loopback that hands out one page of two sessions.
 */
#include "unittest.h"

#include "mp_relay_fake.h"
#include "mp_relay_list.h"
#include "mp_socket.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static SOCKET   relay_sock = INVALID_SOCKET;
static uint16_t relay_port;
static uint32_t clock_ms = 700000u;
static bool     fetch_out;
static uint32_t queries;

static bool t_fetch_begin(bool want_key)
{
    (void)want_key;
    if (fetch_out) {
        return false;
    }
    fetch_out = true;
    return true;
}

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
    out->addresses           = 1u;
    out->address[0].bytes[0] = 127u;
    out->address[0].bytes[3] = 1u;
    out->address[0].port     = relay_port;
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

static size_t two_sessions(uint8_t *out)
{
    size_t i;

    memset(out, 0, MP_RELAY_LIST_PAGE_HEADER_BYTES + 2u * MP_RELAY_LIST_ENTRY_BYTES);
    out[0] = (uint8_t)MP_RELAY_TYPE_LIST_PAGE;
    memcpy(out + 1, "MPRL", 4u);
    out[5]  = (uint8_t)MP_RELAY_VERSION;
    out[10] = 1u;   /* one page */
    out[12] = 2u;
    for (i = 0; i < 2u; ++i) {
        uint8_t      *entry = out + MP_RELAY_LIST_PAGE_HEADER_BYTES + i * MP_RELAY_LIST_ENTRY_BYTES;
        mp_announce_t announce;

        memset(&announce, 0, sizeof announce);
        announce.wire      = 1u;
        announce.game_port = 27970u;
        announce.players   = (uint8_t)(1u + i);
        announce.slots     = 4u;
        memcpy(announce.name, i == 0u ? "Naboo" : "Tatooine", i == 0u ? 6u : 9u);
        memset(entry, (int)(0x11u * (i + 1u)), MP_RELAY_CODE_BYTES);
        (void)mp_announce_encode(&announce, entry + MP_RELAY_CODE_BYTES, MP_RELAY_ANNOUNCE_BYTES);
    }
    return MP_RELAY_LIST_PAGE_HEADER_BYTES + 2u * MP_RELAY_LIST_ENTRY_BYTES;
}

static void relay_step(fake_relay_t *relay)
{
    uint8_t            in[MP_RELAY_DATAGRAM_MAX + 64u];
    uint8_t            out[MP_RELAY_DATAGRAM_MAX];
    struct sockaddr_in from;
    int                from_len = (int)sizeof from;
    int                got;
    uint8_t            role  = 0u;
    uint64_t           nonce = 0u;

    while ((got = recvfrom(relay_sock, (char *)in, (int)sizeof in, 0, (struct sockaddr *)&from,
                           &from_len)) > 0) {
        size_t n = 0u;

        if (fake_hello_read(in, (size_t)got, &role, &nonce)) {
            n = fake_cookie(relay, role, nonce, out);
        } else if (in[0] == MP_RELAY_TYPE_LIST_QUERY && got == (int)MP_RELAY_LIST_QUERY_BYTES) {
            ++queries;
            n = two_sessions(out);
        }
        if (n != 0u) {
            (void)sendto(relay_sock, (const char *)out, (int)n, 0, (struct sockaddr *)&from,
                         sizeof from);
        }
        from_len = (int)sizeof from;
    }
}

int main(void)
{
    static mp_relay_list_t list;
    fake_relay_t           relay;
    struct sockaddr_in     me;
    int                    len = (int)sizeof me;
    u_long                 nonblocking = 1u;
    int                    i;

    ut_section("the public list from a relay on the loopback");
    fake_relay_init(&relay);
    relay_sock = mp_socket_startup() ? socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP) : INVALID_SOCKET;
    memset(&me, 0, sizeof me);
    me.sin_family      = AF_INET;
    me.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ut_check(relay_sock != INVALID_SOCKET &&
                 bind(relay_sock, (struct sockaddr *)&me, sizeof me) == 0 &&
                 getsockname(relay_sock, (struct sockaddr *)&me, &len) == 0 &&
                 ioctlsocket(relay_sock, FIONBIO, &nonblocking) == 0,
             "the test relay's socket opens");
    relay_port = ntohs(me.sin_port);

    ut_check(mp_relay_list_count(&list) == 0u && mp_relay_list_row(&list, 0u) == NULL &&
                 mp_relay_list_state(&list) == MP_RELAY_LISTING_SILENT,
             "a list not open has no rows and hears nothing");
    ut_check(mp_relay_list_open(&list, &SERVICES), "the list opens its sockets");
    ut_check(mp_relay_list_state(&list) == MP_RELAY_LISTING_ASKING, "and asks");
    for (i = 0; i < 100 && mp_relay_list_count(&list) == 0u; ++i) {
        clock_ms += 20u;
        mp_relay_list_pump(&list);
        Sleep(1);
        relay_step(&relay);
    }
    ut_check(mp_relay_list_count(&list) == 2u, "the page arrives: two sessions");
    ut_check(strcmp(mp_relay_list_row(&list, 1u)->announce.name, "Tatooine") == 0 &&
                 mp_relay_list_row(&list, 1u)->announce.players == 2u &&
                 mp_relay_list_row(&list, 0u)->code[0] == 0x11u,
             "with their names, their numbers and their codes");
    ut_check(mp_relay_list_state(&list) == MP_RELAY_LISTING_ANSWERING && list.family == 1,
             "the relay answers, over IPv4, the family its cookie came in");
    mp_relay_list_close(&list);
    mp_relay_list_close(&list);
    ut_check(mp_relay_list_count(&list) == 0u && queries >= 1u,
             "closed, twice over, it shows nothing");

    (void)closesocket(relay_sock);
    mp_socket_shutdown();
    return ut_summary("mp_relay_list");
}
