/* A listen host greets a client when it enters the host's world, not when it connects; through
 * the bridge's own drain, one host and two clients on real localhost sockets.
 *
 * The host greeted a client the moment its connection appeared: the appearance, the movers and
 * the push blocks were told again then. A client that joins a running session sits in its lobby
 * for as long as its player takes to say ready, and everything told to it there was gone by the
 * time it had a level. And a world change greeted nobody. So:
 *
 *   - a connection is greeted as a connection: its bank starts over, and nothing it is owed goes
 *     out while it sends no state of this world;
 *   - its first state of this world is its entry, greeted exactly once;
 *   - a state of another world is no entry;
 *   - a world change is an entry for every player, with no bank started over;
 *   - an entry noticed between two substeps is greeted in the next one;
 *   - a client that joined while the level ran and went before it entered is counted;
 *   - and no greeting ever went to a bank whose newest state is of another world.
 *
 * Skips cleanly on a machine with no Winsock, like the socket tests do.
 */
#include "unittest.h"

#include "mp_bridge_drain.h"
#include "mp_bridge_drain_host.h"
#include "mp_bridge_far.h"
#include "mp_bridge_lobby.h"
#include "mp_lobby.h"
#include "mp_payload_prefix.h"
#include "mp_session.h"
#include "mp_snapshot.h"
#include "mp_transport.h"
#include "mp_udp.h"
#include "mp_wire.h"

#include "common/text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CLIENTS 2
#define STEP_MS 5u
#define QUIET   20

/* Static: a session is over two megabytes now that every peer carries a bulk ring. */
static mp_session_t      s_host;
static mp_session_t      s_client[CLIENTS];
static mp_session_t      s_unused;
static mp_udp_t          s_host_udp;
static mp_udp_t          s_client_udp[CLIENTS];
static mp_transport_t    s_host_transport;
static mp_transport_t    s_client_transport[CLIENTS];
static mp_bridge_drain_t s_drain;
static char              s_address[32];
static bool              s_live[CLIENTS];
static uint8_t           s_world[CLIENTS];   /* the world each client sends its body from */
static uint32_t          s_tick[CLIENTS];

static void start_client(int which, uint32_t seed)
{
    mp_session_init(&s_client[which], MP_SESSION_CLIENT, &s_client_transport[which], seed);
    mp_session_connect(&s_client[which], mp_udp_resolve(&s_client_udp[which], s_address));
    s_live[which] = true;
}

/* The client's own state as the world module lays it out, from the world it stands in. */
static void send_state(int which)
{
    uint8_t          payload[MP_SESSION_PAYLOAD_BYTES];
    mp_snapshot_t    own;
    mp_wire_body_t   body;
    mp_payload_ack_t ack;
    size_t           bytes = 0;

    memset(&body, 0, sizeof body);
    body.position[0] = (float)(which + 1) * 100.0f;
    body.alive       = true;
    body.world       = s_world[which];
    mp_snapshot_clear(&own);
    own.tick = ++s_tick[which];
    mp_snapshot_set_body(&own, (size_t)which + 1u, &body);
    memset(&ack, 0, sizeof ack);
    if (mp_payload_put_ack(payload, sizeof payload, &ack) &&
        mp_snapshot_encode(&own, NULL, payload + MP_PAYLOAD_ACK_BYTES,
                           sizeof payload - MP_PAYLOAD_ACK_BYTES, &bytes)) {
        (void)mp_session_set_payload(&s_client[which], 0, payload, bytes + MP_PAYLOAD_ACK_BYTES);
    }
}

/* One step of the room: the host's session and then its drain, inside a substep or between two,
 * then every live client, which sends a state when it is told to. */
static void step(uint32_t *now, bool in_substep, bool send0, bool send1)
{
    uint8_t note[MP_CHANNEL_MESSAGE_BYTES];
    size_t  bytes = 0;
    int     i;

    *now += STEP_MS;
    mp_session_update(&s_host, *now);
    if (in_substep) {
        (void)mp_bridge_drain_client_state(&s_drain);
    } else {
        (void)mp_bridge_drain_host_between_substeps(&s_drain);
    }
    for (i = 0; i < CLIENTS; ++i) {
        bool send = i == 0 ? send0 : send1;

        if (!s_live[i]) {
            continue;
        }
        if (send && mp_session_is_connected(&s_client[i])) {
            send_state(i);
        }
        mp_session_update(&s_client[i], *now);
        while (mp_session_read_reliable(&s_client[i], 0, note, sizeof note, &bytes)) {
        }
    }
}

static void steps(uint32_t *now, int count, bool send0, bool send1)
{
    int i;

    for (i = 0; i < count; ++i) {
        step(now, true, send0, send1);
    }
}

static bool until_connected(uint32_t *now, int which)
{
    int i;

    for (i = 0; i < 400; ++i) {
        step(now, true, false, false);
        if (mp_session_is_connected(&s_client[which])) {
            return true;
        }
    }
    return false;
}

static mp_bridge_drain_joins_t joins(void)
{
    mp_bridge_drain_joins_t counts;

    memset(&counts, 0, sizeof counts);
    mp_bridge_drain_host_joins(&counts);
    return counts;
}

/* The host's lobby: its setup, and the start that makes a later connection a late one. */
static void the_host_starts(void)
{
    mp_lobby_setup_t setup;

    memset(&setup, 0, sizeof setup);
    setup.mode = MP_LOBBY_MODE_COOP;
    memcpy(setup.level, "level\\fedship.b3d", 18u);
    memcpy(setup.title, "Fedship", 8u);
    mp_bridge_lobby_set_setup(&setup);
    mp_bridge_lobby_start();
}

int main(void)
{
    uint32_t now = 0;
    uint32_t starts1;
    uint32_t starts2;
    int      i;

    if (!mp_udp_init(&s_host_udp, 0)) {
        printf("no winsock on this machine, nothing to test\n");
        return ut_summary("mp_bridge_entry");
    }
    s_host_transport = mp_udp_transport(&s_host_udp);
    mp_session_init(&s_host, MP_SESSION_HOST, &s_host_transport, 0xE5u);
    text_format(s_address, sizeof s_address, "127.0.0.1:%u",
                (unsigned)mp_udp_local_port(&s_host_udp));
    for (i = 0; i < CLIENTS; ++i) {
        ut_checkf(mp_udp_init(&s_client_udp[i], 0), "client %d gets a socket", i);
        s_client_transport[i] = mp_udp_transport(&s_client_udp[i]);
        s_world[i] = 3u;
    }
    mp_bridge_far_reset();
    mp_bridge_far_enter_world(3u);   /* the level the host stands in, generation 3 */
    mp_bridge_drain_bind(&s_drain, MP_BRIDGE_UDP_HOST, &s_host, NULL);
    mp_bridge_lobby_bind(&s_host, &s_unused, NULL, false, &s_drain);
    mp_bridge_lobby_reset();

    ut_section("a client that connects is greeted as a connection, not as an arrival");
    start_client(0, 0xC1u);
    ut_check(until_connected(&now, 0), "the first client joins, before the host has started");
    steps(&now, QUIET, false, false);
    ut_checkf(mp_bridge_far_starts(1u) == 1u && joins().greeted_at_entry == 0u,
              "its bank started over once, and nothing it is owed went out while it sent no state "
              "(%u greeting(s))", (unsigned)joins().greeted_at_entry);
    ut_check(joins().joined_running == 0u, "a connection before the start is no late join");

    ut_section("its first state of this world is its entry, greeted once");
    steps(&now, QUIET, true, false);
    ut_checkf(joins().greeted_at_entry == 1u,
              "one greeting after twenty states of world 3 (%u)",
              (unsigned)joins().greeted_at_entry);

    ut_section("a client that connects while the level runs waits in its lobby");
    the_host_starts();
    start_client(1, 0xC2u);
    ut_check(until_connected(&now, 1), "the second client joins the running session");
    steps(&now, 4 * QUIET, true, false);
    ut_checkf(joins().joined_running == 1u && joins().greeted_at_entry == 1u,
              "counted as a late join, and not greeted while it sends nothing (%u, %u)",
              (unsigned)joins().joined_running, (unsigned)joins().greeted_at_entry);
    s_world[1] = 2u;
    steps(&now, QUIET, true, true);
    ut_checkf(joins().greeted_at_entry == 1u,
              "a state of another world is no entry (%u)", (unsigned)joins().greeted_at_entry);
    s_world[1] = 3u;
    steps(&now, QUIET, true, true);
    ut_checkf(joins().greeted_at_entry == 2u && joins().entered_late == 1u,
              "its first state of world 3 is its entry: greeted once, as a late one (%u, %u)",
              (unsigned)joins().greeted_at_entry, (unsigned)joins().entered_late);
    steps(&now, QUIET, true, true);
    ut_check(joins().greeted_at_entry == 2u, "and the states after it greet nobody");

    ut_section("a world change is an entry for every player, and starts no bank over");
    starts1 = mp_bridge_far_starts(1u);
    starts2 = mp_bridge_far_starts(2u);
    mp_bridge_far_enter_world(4u);
    steps(&now, QUIET, true, true);
    ut_checkf(joins().greeted_at_entry == 2u,
              "while both still send world 3, nobody enters (%u)",
              (unsigned)joins().greeted_at_entry);
    s_world[0] = 4u;
    s_world[1] = 4u;
    steps(&now, QUIET, true, true);
    ut_checkf(joins().greeted_at_entry == 4u,
              "both follow into world 4, and each is greeted once (%u)",
              (unsigned)joins().greeted_at_entry);
    ut_check(mp_bridge_far_starts(1u) == starts1 && mp_bridge_far_starts(2u) == starts2,
             "with no connection changed, no bank started over");

    ut_section("an entry noticed between two substeps is greeted in the next one");
    mp_bridge_far_enter_world(5u);
    s_world[0] = 5u;
    for (i = 0; i < QUIET; ++i) {
        step(&now, false, true, false);
    }
    ut_checkf(joins().greeted_at_entry == 4u,
              "between substeps the entry is noticed and not greeted (%u)",
              (unsigned)joins().greeted_at_entry);
    step(&now, true, false, false);
    ut_checkf(joins().greeted_at_entry == 5u,
              "and the next substep greets it (%u)", (unsigned)joins().greeted_at_entry);

    ut_section("a late client that goes before it enters");
    start_client(1, 0xE2u);
    ut_check(until_connected(&now, 1), "the second client comes back on a new connection");
    steps(&now, QUIET, true, false);
    mp_session_disconnect(&s_client[1]);
    s_live[1] = false;
    steps(&now, 100, true, false);
    ut_checkf(joins().joined_running == 2u && joins().left_before_entry == 1u,
              "joined during the level and left before entering, counted (%u, %u)",
              (unsigned)joins().joined_running, (unsigned)joins().left_before_entry);

    ut_section("no greeting went to a bank with no state of this world");
    ut_checkf(joins().greeted_unentered == 0u, "the must-be-0 count is 0 (%u)",
              (unsigned)joins().greeted_unentered);
    mp_bridge_drain_host_report();
    mp_bridge_far_describe_banks();

    for (i = 0; i < CLIENTS; ++i) {
        mp_udp_shutdown(&s_client_udp[i]);
    }
    mp_udp_shutdown(&s_host_udp);
    return ut_summary("mp_bridge_entry");
}
