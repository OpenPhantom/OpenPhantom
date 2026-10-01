/* mp_bridge_seats.c: a listen host tells every peer its own world slot.
 *
 * A dedicated server always told its clients their slots and a listen host told nobody, so every
 * client of a listen host held slot 1, and two of them held it together: their deaths counted as
 * one player's, each dropped the other's appearance as its own echo, and a hit on either was
 * charged to the same seat. One host and two clients on real localhost sockets in this process,
 * the host driven through the bridge's own drain and the clients read by hand:
 *
 *   - each client is told its own slot, once, in join order;
 *   - a client that restarts from the same address is replaced in place and told again;
 *   - a client that leaves frees its index, and whoever takes it next is told that slot;
 *   - a client's drain takes the slot it is told and publishes it.
 *
 * Skips cleanly on a machine with no Winsock, like the socket tests do.
 */
#include "unittest.h"

#include "mp_bridge_drain.h"
#include "mp_bridge_seats.h"
#include "mp_session.h"
#include "mp_transport.h"
#include "mp_udp.h"

#include "common/text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CLIENTS 2
#define STEP_MS 5u

/* Static: a session is over two megabytes now that every peer carries a bulk ring. */
static mp_session_t      s_host;
static mp_session_t      s_client[CLIENTS];
static mp_udp_t          s_host_udp;
static mp_udp_t          s_client_udp[CLIENTS];
static mp_transport_t    s_host_transport;
static mp_transport_t    s_client_transport[CLIENTS];
static mp_bridge_drain_t s_drain;
static char              s_address[32];
static bool              s_live[CLIENTS];

typedef struct told {
    uint32_t notes;   /* one byte notes read, and a slot note is the only one there is */
    uint8_t  slot;    /* the last of them */
} told_t;

static told_t s_told[CLIENTS];

/* A fresh session on the client's own socket, so a restart comes from the same address. */
static void start_client(int which, uint32_t seed)
{
    mp_session_init(&s_client[which], MP_SESSION_CLIENT, &s_client_transport[which], seed);
    mp_session_connect(&s_client[which], mp_udp_resolve(&s_client_udp[which], s_address));
    memset(&s_told[which], 0, sizeof s_told[which]);
    s_live[which] = true;
}

static void read_client(int which)
{
    uint8_t note[MP_CHANNEL_MESSAGE_BYTES];
    size_t  bytes = 0;

    while (mp_session_read_reliable(&s_client[which], 0, note, sizeof note, &bytes)) {
        if (bytes == 1u) {
            ++s_told[which].notes;
            s_told[which].slot = note[0];
        }
    }
}

/* `steps` steps of the whole room: the host's session and then its drain, the order a substep
 * runs them in, then every live client. */
static void run(uint32_t *now, int steps)
{
    int step;
    int i;

    for (step = 0; step < steps; ++step) {
        *now += STEP_MS;
        mp_session_update(&s_host, *now);
        mp_bridge_drain_reliable_notes(&s_drain);
        for (i = 0; i < CLIENTS; ++i) {
            if (s_live[i]) {
                mp_session_update(&s_client[i], *now);
                read_client(i);
            }
        }
    }
}

static bool run_until_connected(uint32_t *now, int which)
{
    int step;

    for (step = 0; step < 400; ++step) {
        run(now, 1);
        if (mp_session_is_connected(&s_client[which])) {
            return true;
        }
    }
    return false;
}

static void check_each_client_is_told_its_own_slot(uint32_t *now)
{
    ut_section("a listen host tells each of two clients its own world slot, in join order");

    mp_bridge_drain_bind(&s_drain, MP_BRIDGE_UDP_HOST, &s_host, NULL);
    start_client(0, 0xC1u);
    ut_check(run_until_connected(now, 0), "the first client joins");
    start_client(1, 0xC2u);
    ut_check(run_until_connected(now, 1), "and then the second");
    run(now, 100);
    ut_check(mp_session_peer_count(&s_host) == 2u, "both are seated");
    ut_checkf(s_told[0].notes == 1u && s_told[0].slot == 1u,
              "the first was told slot 1, once (%u note(s), slot %u)",
              (unsigned)s_told[0].notes, (unsigned)s_told[0].slot);
    ut_checkf(s_told[1].notes == 1u && s_told[1].slot == 2u,
              "the second was told slot 2, once, not slot 1 beside the first "
              "(%u note(s), slot %u)",
              (unsigned)s_told[1].notes, (unsigned)s_told[1].slot);
}

static void check_a_restart_is_told_again(uint32_t *now)
{
    ut_section("a client that restarts from the same address is told its slot again");

    start_client(0, 0xE1u);
    ut_check(run_until_connected(now, 0), "the restarted client is through the handshake");
    run(now, 100);
    ut_check(mp_session_replaced(&s_host) == 1u,
             "the host replaced it in place rather than seating it a second time");
    ut_checkf(s_told[0].notes == 1u && s_told[0].slot == 1u,
              "and told the newcomer slot 1, because the connection is new although the index is "
              "not (%u note(s), slot %u)",
              (unsigned)s_told[0].notes, (unsigned)s_told[0].slot);
    ut_check(s_told[1].notes == 1u, "the other client was not told anything a second time");
}

static void check_a_free_index_is_told_to_the_next(uint32_t *now)
{
    ut_section("a client that leaves frees its index for whoever takes it next");

    mp_session_disconnect(&s_client[1]);
    run(now, 100);
    ut_check(mp_session_peer_count(&s_host) == 1u, "the host let the second client go");
    start_client(1, 0xE2u);
    ut_check(run_until_connected(now, 1), "a new player joins");
    run(now, 100);
    ut_checkf(s_told[1].notes == 1u && s_told[1].slot == 2u,
              "and is told slot 2, the index it took (%u note(s), slot %u)",
              (unsigned)s_told[1].notes, (unsigned)s_told[1].slot);
}

/* The drain holds the one side a process runs, so this client is read through it rather than by
 * hand, and the host's ledger is walked directly. Binding resets the ledger, so the host tells
 * both clients again; only the one behind the drain is looked at. */
static void check_a_client_takes_its_slot(uint32_t *now)
{
    int step;

    ut_section("a client's drain takes the slot it is told and publishes it");

    mp_bridge_drain_bind(&s_drain, MP_BRIDGE_UDP_CLIENT, NULL, &s_client[1]);
    ut_check(mp_bridge_drain_my_slot() == 1u, "a client holds slot 1 before anybody has said");
    start_client(1, 0xE3u);
    s_live[1] = false;   /* read by the drain below, not by hand */
    for (step = 0; step < 300; ++step) {
        *now += STEP_MS;
        mp_session_update(&s_host, *now);
        (void)mp_bridge_seats_update(&s_host);
        mp_session_update(&s_client[0], *now);
        read_client(0);
        mp_session_update(&s_client[1], *now);
        mp_bridge_drain_reliable_notes(&s_drain);
    }
    ut_check(mp_session_is_connected(&s_client[1]), "the client behind the drain is connected");
    ut_checkf(s_drain.my_slot == 2u && mp_bridge_drain_my_slot() == 2u,
              "and holds slot 2, in the drain and in the copy every judge of a slot reads (%u)",
              (unsigned)mp_bridge_drain_my_slot());
}

int main(void)
{
    uint32_t now = 0;
    int      i;

    if (!mp_udp_init(&s_host_udp, 0)) {
        printf("no winsock on this machine, nothing to test\n");
        return ut_summary("mp_bridge_seats");
    }
    s_host_transport = mp_udp_transport(&s_host_udp);
    mp_session_init(&s_host, MP_SESSION_HOST, &s_host_transport, 0xB5u);
    text_format(s_address, sizeof s_address, "127.0.0.1:%u",
                (unsigned)mp_udp_local_port(&s_host_udp));
    for (i = 0; i < CLIENTS; ++i) {
        ut_checkf(mp_udp_init(&s_client_udp[i], 0), "client %d gets a socket", i);
        s_client_transport[i] = mp_udp_transport(&s_client_udp[i]);
    }

    check_each_client_is_told_its_own_slot(&now);
    check_a_restart_is_told_again(&now);
    check_a_free_index_is_told_to_the_next(&now);
    check_a_client_takes_its_slot(&now);

    for (i = 0; i < CLIENTS; ++i) {
        mp_udp_shutdown(&s_client_udp[i]);
    }
    mp_udp_shutdown(&s_host_udp);
    return ut_summary("mp_bridge_seats");
}
