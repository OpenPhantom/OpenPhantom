/* mp_bridge_client_far.c: a client shows every other player the host's world carries, each in
 * the far bank its slot is seated in.
 *
 * The client took the first foreign body of every world into its one far body and dropped the
 * rest, so with three players each client showed one of the two others. One host and one client
 * over the loopback, the host's worlds built by hand:
 *
 *   - which bank a slot is seated in: the slot below this side's own one bank up, the ones above
 *     it in the bank of their own number, the lowest free bank for one that does not fit, and
 *     none once the banks run out;
 *   - a client at slot 2 seats the host in bank 1, slot 1 in bank 2 and slot 3 in bank 3, and each
 *     bank takes its own player's states;
 *   - a moment goes to the bank that shows the slot it names, and to none for this side's own;
 *   - a player missing from the world for a second gives its bank up, and the host never does;
 *   - the report names each far player by its world slot here too, and a bank given up is
 *     described before its history goes.
 */
#include "unittest.h"

#include "mp_bank.h"
#include "mp_bridge_drain.h"
#include "mp_bridge_far.h"
#include "mp_events.h"
#include "mp_interp.h"
#include "mp_loopback.h"
#include "mp_session.h"
#include "mp_snapshot.h"
#include "mp_snapshot_history.h"
#include "mp_timeline.h"
#include "mp_transport.h"
#include "mp_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Static: a session is over two megabytes now that every peer carries a bulk ring. */
static mp_loopback_t     s_net;
static mp_session_t      s_host;
static mp_session_t      s_client;
static mp_transport_t    s_host_transport;
static mp_transport_t    s_client_transport;
static mp_bridge_drain_t s_drain;
static uint32_t          s_now;

static void pump(void)
{
    s_now += 16u;
    mp_loopback_pump(&s_net, s_now);
    mp_session_update(&s_host, s_now);
    mp_session_update(&s_client, s_now);
}

static bool connect_pair(void)
{
    int tick;

    mp_loopback_init(&s_net, NULL, 0x31u);
    s_host_transport   = mp_loopback_transport(&s_net, MP_LOOPBACK_ENDPOINT_A);
    s_client_transport = mp_loopback_transport(&s_net, MP_LOOPBACK_ENDPOINT_B);
    mp_session_init(&s_host, MP_SESSION_HOST, &s_host_transport, 0x13u);
    mp_session_init(&s_client, MP_SESSION_CLIENT, &s_client_transport, 0x31u);
    mp_session_connect(&s_client, MP_LOOPBACK_ENDPOINT_A);
    for (tick = 0; tick < 200; ++tick) {
        pump();
        if (mp_session_is_connected(&s_client) && mp_session_peer_count(&s_host) == 1u) {
            return true;
        }
    }
    return false;
}

/* A host's world with a body in each slot `mask` names, each standing at ten times its slot plus
 * ten along x, behind an empty enemy block, and taken through the client's drain. */
static void world(uint32_t tick, uint16_t mask)
{
    uint8_t        payload[MP_SESSION_PAYLOAD_BYTES];
    mp_snapshot_t  snapshot;
    mp_wire_body_t body;
    size_t         bytes = 0;
    size_t         slot;

    mp_snapshot_clear(&snapshot);
    snapshot.tick = tick;
    for (slot = 0; slot < MP_SNAPSHOT_MAX_BODIES; ++slot) {
        if ((mask & (1u << slot)) == 0u) {
            continue;
        }
        memset(&body, 0, sizeof body);
        body.position[0] = 10.0f + 10.0f * (float)slot;
        body.alive       = true;
        mp_snapshot_set_body(&snapshot, slot, &body);
    }
    payload[0] = 0u;
    payload[1] = 0u;
    if (mp_snapshot_encode(&snapshot, NULL, payload + 2u, sizeof payload - 2u, &bytes)) {
        (void)mp_session_set_payload(&s_host, 0, payload, bytes + 2u);
    }
    pump();
    pump();
    (void)mp_bridge_drain_host_world(&s_drain);
}

static bool newest_x(size_t bank, float want)
{
    const mp_snapshot_t *newest = mp_snapshot_history_newest(&mp_bridge_far_interp(bank)->history);

    return newest != NULL && mp_snapshot_has_body(newest, 1u) &&
           newest->body[1].position[0] > want - 0.1f && newest->body[1].position[0] < want + 0.1f;
}

/* One of a player's moments as the host passes it on: a push naming world slot `source`, sent to
 * this client and taken through its drain. */
static void moment(uint8_t source)
{
    uint8_t    bytes[MP_EVENT_MAX_BYTES];
    mp_event_t push;
    size_t     count;

    memset(&push, 0, sizeof push);
    push.kind        = MP_EVENT_PUSH;
    push.tick        = 50u;
    push.source_slot = source;
    push.charge      = 1.0f;
    count = mp_event_encode(&push, bytes, sizeof bytes);
    ut_checkf(count != 0u && mp_session_send_reliable(&s_host, 0, bytes, count),
              "the host passes on a push from slot %u", (unsigned)source);
    pump();
    pump();
    mp_bridge_drain_reliable_notes(&s_drain);
}

static void check_the_rule(void)
{
    ut_section("which far bank a slot is seated in");

    mp_bridge_far_unseat_all();
    ut_check(mp_bridge_far_seat_slot(0u, 2u) == 1u, "a client at slot 2 seats the host in bank 1");
    ut_check(mp_bridge_far_seat_slot(1u, 2u) == 2u, "slot 1, below its own, one bank up");
    ut_check(mp_bridge_far_seat_slot(3u, 2u) == 3u, "slot 3, above it, in the bank of its number");
    ut_check(mp_bridge_far_seat_slot(1u, 2u) == 2u, "a slot already seated keeps its bank");
    ut_check(mp_bridge_far_seat_slot(2u, 2u) == 0u, "this side's own slot is never seated");
    ut_check(mp_bridge_far_seat_slot(4u, 2u) == 0u,
             "and a fourth other player finds no bank: this build shows three");

    mp_bridge_far_unseat_all();
    ut_check(mp_bridge_far_seat_slot(2u, 1u) == 2u && mp_bridge_far_seat_slot(3u, 1u) == 3u,
             "a dedicated server's client at slot 1 seats slots 2 and 3 in their own banks");
    ut_check(mp_bridge_far_seat_slot(4u, 1u) == 1u,
             "and slot 4, which has no bank of its number, in the lowest free one");
    mp_bridge_far_unseat(2u);
    ut_check(mp_bridge_far_bank_of_slot(2u) == 0u && mp_bridge_far_seat_slot(5u, 1u) == 2u,
             "a bank given up is free for the next player");
}

int main(void)
{
    uint32_t tick;
    int      i;

    check_the_rule();

    ut_section("a client shows every other player the host's world carries");
    ut_check(connect_pair(), "a host and a client connect over the loopback");
    mp_bridge_far_reset();
    mp_bridge_drain_bind(&s_drain, MP_BRIDGE_UDP_CLIENT, &s_host, &s_client);
    s_drain.my_slot = 2u;   /* what a host's slot note would have said */
    for (tick = 1u; tick <= 10u; ++tick) {
        world(tick, (uint16_t)((1u << 0) | (1u << 1) | (1u << 2) | (1u << 3)));
    }
    ut_check(mp_bridge_far_bank_of_slot(0u) == 1u && newest_x(1u, 10.0f),
             "the host is in bank 1 with its own states");
    ut_check(mp_bridge_far_bank_of_slot(1u) == 2u && newest_x(2u, 20.0f),
             "slot 1 is in bank 2 with its own");
    ut_check(mp_bridge_far_bank_of_slot(3u) == 3u && newest_x(3u, 40.0f),
             "slot 3 is in bank 3 with its own");
    ut_check(mp_bridge_far_bank_of_slot(2u) == 0u, "and this side's own slot is in none");

    ut_section("the report names them by the world slot, not by the bank, on a client too");
    {
        char text[320];

        (void)mp_bridge_far_states_text(text, sizeof text);
        /* How often each history started depends on the seating drill at the top of this test,
         * which seats and unseats the same banks by hand; the states and the slot do not. */
        ut_checkf(strstr(text, "slot 0 10 (history started ") != NULL,
                  "the host is named as slot 0 with its ten states (%s)", text);
        ut_checkf(strstr(text, "slot 1 10 ") != NULL && strstr(text, "slot 3 10 ") != NULL,
                  "and the two other clients as slots 1 and 3 (%s)", text);
    }

    ut_section("a moment goes to the bank that shows the slot it names");
    moment(0u);
    moment(1u);
    moment(3u);
    moment(2u);
    moment(5u);
    ut_check(mp_bridge_far_moments(1u) == 1u, "the host's own push, slot 0, is bank 1's");
    ut_check(mp_bridge_far_moments(2u) == 1u,
             "slot 1's, which the host passed on, is bank 2's");
    ut_check(mp_bridge_far_moments(3u) == 1u, "slot 3's is bank 3's");
    ut_checkf(s_drain.events_in == 3u && s_drain.events_unplaced == 2u,
              "and one naming this side's own slot or a slot nobody here shows is queued nowhere "
              "(%u queued, %u not)", (unsigned)s_drain.events_in,
              (unsigned)s_drain.events_unplaced);

    ut_section("a player missing from the world gives its bank up, and the host never does");
    for (i = 0; i < 40; ++i) {
        world(100u + (uint32_t)i, (uint16_t)((1u << 2) | (1u << 3)));
    }
    ut_check(mp_bridge_far_bank_of_slot(1u) == 0u &&
                 mp_snapshot_history_newest(&mp_bridge_far_interp(2u)->history) == NULL,
             "slot 1, gone for longer than a second, has no bank and bank 2 no history left");
    ut_check(mp_bridge_far_bank_of_slot(0u) == 1u,
             "while the host, missing just as long, keeps bank 1: a world without the host's "
             "body is a host between two levels");
    ut_check(mp_bridge_far_bank_of_slot(3u) == 3u, "and slot 3, still there, keeps bank 3");
    ut_checkf(mp_bridge_far_departures() == 1u,
              "and bank 2 was described before its history went, so the report does not show "
              "the nought it holds now for a player who played (%u)",
              (unsigned)mp_bridge_far_departures());

    ut_section("the host's world stands still only while this side is connected to that host");
    /* Worlds have arrived; five seconds of wall clock are simulated by moving the stamp back. */
    s_drain.last_world_ms -= 5000u;
    ut_checkf(mp_bridge_drain_world_still_ms() >= 5000u,
              "connected and five seconds without a world: the notice has its number (%u ms)",
              (unsigned)mp_bridge_drain_world_still_ms());
    mp_session_disconnect(&s_client);
    pump();
    ut_checkf(mp_bridge_drain_world_still_ms() == 0u,
              "once the session is gone there is no host whose world could stand (%u ms)",
              (unsigned)mp_bridge_drain_world_still_ms());
    ut_check(connect_pair(), "a new connection comes up");
    ut_checkf(mp_bridge_drain_world_still_ms() == 0u,
              "and before its first world the last session's clock says nothing (%u ms)",
              (unsigned)mp_bridge_drain_world_still_ms());
    world(200u, (uint16_t)(1u << 0));
    s_drain.last_world_ms -= 2000u;
    ut_check(mp_bridge_drain_world_still_ms() >= 2000u,
             "a world on the new connection starts the clock again");

    mp_session_disconnect(&s_client);
    mp_session_disconnect(&s_host);
    return ut_summary("mp_bridge_client_far");
}
