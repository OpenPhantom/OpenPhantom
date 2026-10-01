/* mp_bridge_far.c: a listen host takes every client's own state into the bank of its slot, and a
 * newcomer starts only its own bank over.
 *
 * The host drained its first peer alone, in slot 1, so a second client's every state stayed in its
 * ring and its body never moved on the host; and every arrival of anybody started the one far
 * history over, so a second client's join cost the first one's body its timeline. One host and two
 * clients on real localhost sockets, the host read through the bridge's own drain:
 *
 *   - both clients' states are taken, each into the bank of its own slot;
 *   - a second client's arrival leaves the first one's history as it was;
 *   - a client that restarts from the same address starts its own bank over and nobody else's;
 *   - a client that leaves has its bank emptied;
 *   - a client's moment reaches the other client, naming its slot and on the host's clock;
 *   - the report names each far player by its world slot, and a bank that loses its player is
 *     described before its history goes.
 *
 * Skips cleanly on a machine with no Winsock, like the socket tests do.
 */
#include "unittest.h"

#include "mp_bridge_drain.h"
#include "mp_bridge_far.h"
#include "mp_bridge_relay.h"
#include "mp_bridge_world.h"
#include "mp_events.h"
#include "mp_interp.h"
#include "mp_payload_prefix.h"
#include "mp_session.h"
#include "mp_snapshot.h"
#include "mp_snapshot_history.h"
#include "mp_timeline.h"
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
static mp_udp_t          s_host_udp;
static mp_udp_t          s_client_udp[CLIENTS];
static mp_transport_t    s_host_transport;
static mp_transport_t    s_client_transport[CLIENTS];
static mp_bridge_drain_t s_drain;
static char              s_address[32];
static bool              s_live[CLIENTS];

/* A fresh session on the client's own socket, so a restart comes from the same address. */
static void start_client(int which, uint32_t seed)
{
    mp_session_init(&s_client[which], MP_SESSION_CLIENT, &s_client_transport[which], seed);
    mp_session_connect(&s_client[which], mp_udp_resolve(&s_client_udp[which], s_address));
    s_live[which] = true;
}

/* The world every body of this test was sent from, unless a section says otherwise. */
static uint8_t s_world = 3u;

/* The client's own state as the world module lays it out: the acknowledgement, then a full one
 * body snapshot in the slot the client holds, which in join order is its index plus one. */
static void send_state(int which, uint32_t tick)
{
    uint8_t          payload[MP_SESSION_PAYLOAD_BYTES];
    mp_snapshot_t    own;
    mp_wire_body_t   body;
    mp_payload_ack_t ack;
    size_t           bytes = 0;

    memset(&body, 0, sizeof body);
    body.position[0] = (float)(which + 1) * 100.0f;
    body.alive       = true;
    body.world       = s_world;
    mp_snapshot_clear(&own);
    own.tick = tick;
    mp_snapshot_set_body(&own, (size_t)which + 1u, &body);
    memset(&ack, 0, sizeof ack);
    if (mp_payload_put_ack(payload, sizeof payload, &ack) &&
        mp_snapshot_encode(&own, NULL, payload + MP_PAYLOAD_ACK_BYTES,
                           sizeof payload - MP_PAYLOAD_ACK_BYTES, &bytes)) {
        (void)mp_session_set_payload(&s_client[which], 0, payload, bytes + MP_PAYLOAD_ACK_BYTES);
    }
}

/* One step of the room: the host's session and then its drain, the order a substep runs them in,
 * then every live client, which sends a state first when it is given a tick. */
static void step(uint32_t *now, uint32_t tick0, uint32_t tick1)
{
    uint8_t note[MP_CHANNEL_MESSAGE_BYTES];
    size_t  bytes = 0;
    int     i;

    *now += STEP_MS;
    mp_session_update(&s_host, *now);
    (void)mp_bridge_drain_client_state(&s_drain);
    for (i = 0; i < CLIENTS; ++i) {
        uint32_t tick = i == 0 ? tick0 : tick1;

        if (!s_live[i]) {
            continue;
        }
        if (tick != 0u && mp_session_is_connected(&s_client[i])) {
            send_state(i, tick);
        }
        mp_session_update(&s_client[i], *now);
        while (mp_session_read_reliable(&s_client[i], 0, note, sizeof note, &bytes)) {
        }
    }
}

static void quiet(uint32_t *now, int steps)
{
    int i;

    for (i = 0; i < steps; ++i) {
        step(now, 0u, 0u);
    }
}

static bool until_connected(uint32_t *now, int which)
{
    int i;

    for (i = 0; i < 400; ++i) {
        step(now, 0u, 0u);
        if (mp_session_is_connected(&s_client[which])) {
            return true;
        }
    }
    return false;
}

/* What one client made of the worlds it was sent: how many decoded, which slots they carried and
 * where each body stood. */
typedef struct seen {
    uint32_t worlds;
    bool     had[MP_SNAPSHOT_MAX_BODIES];
    float    x[MP_SNAPSHOT_MAX_BODIES];
} seen_t;

/* The host's payload as a client reads it: the enemy block behind its two byte length, then a
 * world, full here because these clients acknowledge nothing. */
static void read_world(int which, seen_t *seen)
{
    uint8_t       payload[MP_SESSION_PAYLOAD_BYTES];
    size_t        bytes = 0;
    mp_snapshot_t world;
    size_t        slot;

    while (mp_session_read_payload(&s_client[which], 0, payload, sizeof payload, &bytes)) {
        size_t   enemy = bytes >= 2u ? (size_t)payload[0] | ((size_t)payload[1] << 8) : 0u;
        uint32_t baseline = 1u;

        if (bytes < 2u + enemy ||
            !mp_snapshot_baseline_tick(payload + 2u + enemy, bytes - 2u - enemy, &baseline) ||
            baseline != 0u ||
            !mp_snapshot_decode(payload + 2u + enemy, bytes - 2u - enemy, NULL, &world)) {
            continue;
        }
        ++seen->worlds;
        for (slot = 0; slot < MP_SNAPSHOT_MAX_BODIES; ++slot) {
            if (mp_snapshot_has_body(&world, slot)) {
                seen->had[slot] = true;
                seen->x[slot]   = world.body[slot].position[0];
            }
        }
    }
}

static uint32_t newest_in(size_t bank)
{
    return mp_timeline_newest_tick(mp_interp_timeline(mp_bridge_far_interp(bank)));
}

static bool holds(size_t bank, uint32_t tick)
{
    return mp_snapshot_history_get(&mp_bridge_far_interp(bank)->history, tick) != NULL;
}

/* One of a client's moments on its way to the host, as the bridge puts it out. */
static void send_moment(int which, const mp_event_t *event)
{
    uint8_t bytes[MP_EVENT_MAX_BYTES];
    size_t  count = mp_event_encode(event, bytes, sizeof bytes);

    ut_checkf(count != 0u && mp_session_send_reliable(&s_client[which], 0, bytes, count),
              "client %d sends its moment", which);
}

/* The players' moments that reached one client on its reliable channel, the last one kept. */
typedef struct heard {
    uint32_t   moments;
    mp_event_t last;
} heard_t;

static void hear(int which, heard_t *heard)
{
    uint8_t    note[MP_CHANNEL_MESSAGE_BYTES];
    size_t     bytes = 0;
    mp_event_t event;

    while (mp_session_read_reliable(&s_client[which], 0, note, sizeof note, &bytes)) {
        if (mp_event_decode(note, bytes, &event)) {
            ++heard->moments;
            heard->last = event;
        }
    }
}

static void check_every_client_is_taken(uint32_t *now)
{
    uint32_t tick;

    ut_section("a listen host takes every client's own state, each into the bank of its slot");
    start_client(0, 0xC1u);
    ut_check(until_connected(now, 0), "the first client joins");
    for (tick = 101u; tick <= 110u; ++tick) {
        step(now, tick, 0u);
    }
    quiet(now, QUIET);
    ut_checkf(newest_in(1u) == 110u && holds(1u, 101u) && holds(1u, 110u),
              "the first client's ten states are in bank 1, newest %u", (unsigned)newest_in(1u));

    start_client(1, 0xC2u);
    ut_check(until_connected(now, 1), "the second client joins");
    for (tick = 501u; tick <= 510u; ++tick) {
        step(now, 0u, tick);
    }
    quiet(now, QUIET);
    ut_checkf(s_drain.states_in == 20u,
              "both clients' states were taken, ten each (%u taken)", (unsigned)s_drain.states_in);
    ut_checkf(newest_in(2u) == 510u && holds(2u, 501u),
              "the second client's are in bank 2, newest %u", (unsigned)newest_in(2u));
    ut_check(holds(1u, 101u) && holds(1u, 110u) && newest_in(1u) == 110u,
             "and the first client's history is as it was: the second one's arrival did not "
             "start it over");
}

static void check_every_far_body_is_placed(void)
{
    mp_bridge_far_pose_t pose;
    int                  i;

    ut_section("a listen host places every far body it has a history for");
    for (i = 0; i < 40; ++i) {
        mp_bridge_drain_place_puppet(&s_drain);
    }
    ut_check(mp_bridge_far_pose(1u, MP_FAR_READER_OTHER, &pose) && pose.slot == 1u &&
                 pose.position[0] > 99.9f && pose.position[0] < 100.1f,
             "bank 1's pose is the first client's, at its own place and in its own slot");
    ut_check(mp_bridge_far_pose(2u, MP_FAR_READER_OTHER, &pose) && pose.slot == 2u &&
                 pose.position[0] > 199.9f && pose.position[0] < 200.1f,
             "and bank 2's is the second client's, at its own place and in its own slot");
    ut_check(mp_bridge_far_occupied(1u) && mp_bridge_far_occupied(2u) &&
                 !mp_bridge_far_occupied(3u),
             "so two far bodies have a player behind them and the third bank has none");
    ut_check(mp_bridge_far_bank_of_slot(2u) == 2u && mp_bridge_far_bank_of_slot(0u) == 0u,
             "slot 2 is shown in bank 2, and the host's own slot in none");
}

static void check_the_world_goes_to_every_peer(uint32_t *now)
{
    int i;

    ut_section("the world goes to every peer, each without its own body");
    {
        seen_t seen[CLIENTS];
        int    c;

        memset(seen, 0, sizeof seen);
        for (i = 0; i < 20; ++i) {
            *now += STEP_MS;
            mp_bridge_world_send(&s_host, 900u + (uint32_t)i);
            mp_session_update(&s_host, *now);
            for (c = 0; c < CLIENTS; ++c) {
                mp_session_update(&s_client[c], *now);
                read_world(c, &seen[c]);
            }
        }
        ut_checkf(seen[1].worlds > 0u && seen[1].x[1] > 99.9f && seen[1].x[1] < 100.1f,
                  "the second client decodes the host's world, with the first client's body in "
                  "slot 1 (%u world(s))", (unsigned)seen[1].worlds);
        ut_checkf(seen[0].worlds > 0u && seen[0].x[2] > 199.9f && seen[0].x[2] < 200.1f,
                  "and the first client decodes it too, with the second client's in slot 2 "
                  "(%u world(s))", (unsigned)seen[0].worlds);
        ut_check(!seen[0].had[1] && !seen[1].had[2],
                 "and neither is sent its own body back");
    }
}

static void check_a_moment_reaches_the_other_client(uint32_t *now)
{
    int i;

    ut_section("a client's moment reaches the other client, in its slot and on the host's clock");
    {
        heard_t    heard[CLIENTS];
        mp_event_t shot;
        mp_event_t door;
        uint32_t   passed_before = 0;
        uint32_t   passed = 0;
        uint32_t   refused = 0;
        int        c;

        mp_bridge_relay_counts(&passed_before, NULL);
        memset(heard, 0, sizeof heard);
        memset(&shot, 0, sizeof shot);
        shot.kind        = MP_EVENT_SHOT;
        shot.tick        = 4321u;   /* the first client's own count */
        shot.source_slot = 3u;      /* and a slot it has no business naming */
        shot.shot_kind   = 11u;
        shot.origin[1]   = 0.5f;
        send_moment(0, &shot);
        memset(&door, 0, sizeof door);
        door.kind       = MP_EVENT_MOVER;
        door.tick       = 77u;
        door.mover_id   = 258u;
        door.mover_mode = MP_EVENT_MOVER_OPEN;
        send_moment(1, &door);
        s_drain.tick = 900u;   /* the host's substep, which the bridge writes */
        for (i = 0; i < QUIET; ++i) {
            *now += STEP_MS;
            mp_session_update(&s_host, *now);
            mp_bridge_drain_reliable_notes(&s_drain);
            for (c = 0; c < CLIENTS; ++c) {
                mp_session_update(&s_client[c], *now);
                hear(c, &heard[c]);
            }
        }
        ut_checkf(heard[1].moments == 1u && heard[1].last.kind == MP_EVENT_SHOT,
                  "the second client hears the first one's shot, which the listen host passes "
                  "on (%u moment(s))", (unsigned)heard[1].moments);
        ut_checkf(heard[1].last.source_slot == 1u,
                  "naming slot 1, the first client's, not the %u it wrote",
                  (unsigned)shot.source_slot);
        ut_checkf(heard[1].last.tick == 900u,
                  "on the host's substep, which the second client replays the first one on, not "
                  "on the first one's own count (tick %u)", (unsigned)heard[1].last.tick);
        ut_check(heard[1].last.shot_kind == 11u && heard[1].last.origin[1] > 0.49f &&
                     heard[1].last.origin[1] < 0.51f,
                 "and otherwise as it was fired");
        ut_checkf(heard[0].moments == 1u && heard[0].last.kind == MP_EVENT_MOVER &&
                      heard[0].last.tick == 900u && heard[0].last.mover_id == 258u,
                  "the first client hears the second one's door, on the host's clock too "
                  "(%u moment(s))", (unsigned)heard[0].moments);
        mp_bridge_relay_counts(&passed, &refused);
        ut_checkf(passed - passed_before == 2u && refused == 0u,
                  "two copies passed on and none refused, so neither moment went back to its "
                  "sender (%u, %u)", (unsigned)(passed - passed_before), (unsigned)refused);
        ut_check(mp_bridge_far_moments(1u) == 1u && mp_bridge_far_moments(2u) == 0u,
                 "the host queued the shot for bank 1, the first client's body, itself; the "
                 "door went to the map");
        {
            uint32_t last1 = 0u;
            uint32_t last2 = 0u;

            ut_check(mp_bridge_far_passed_on(2u, &last2) == 1u && last2 == 900u &&
                         mp_bridge_far_passed_on(1u, &last1) == 1u && last1 == 900u,
                     "and counted one copy for each player, the shot to peer 1 and the door to "
                     "peer 0, each on substep 900: the report says whom it passed nothing to");
        }
    }
}

static void check_a_restart_and_a_departure(uint32_t *now)
{
    mp_bridge_far_pose_t pose;
    uint32_t             tick;

    ut_section("a restart starts its own bank over and nobody else's");
    start_client(1, 0xE2u);
    ut_check(until_connected(now, 1), "the second client restarts from the same address");
    for (tick = 1u; tick <= 5u; ++tick) {
        step(now, 0u, tick);
    }
    quiet(now, QUIET);
    ut_checkf(newest_in(2u) == 5u && !holds(2u, 510u),
              "bank 2 holds the new connection's five states and nothing of the old one's "
              "(newest %u)", (unsigned)newest_in(2u));
    ut_check(mp_bridge_far_passed_on(2u, NULL) == 0u,
             "and the copies passed on to the old connection are not counted for the new one");
    ut_check(holds(1u, 110u), "bank 1 still holds the first client's");

    ut_section("a client that leaves has its bank emptied");
    mp_session_disconnect(&s_client[1]);
    s_live[1] = false;
    quiet(now, 100);
    ut_check(mp_session_peer_count(&s_host) == 1u, "the host let the second client go");
    ut_check(mp_snapshot_history_newest(&mp_bridge_far_interp(2u)->history) == NULL,
             "and its bank holds nothing a later reader could take for a player");
    ut_check(!mp_bridge_far_occupied(2u) && !mp_bridge_far_pose(2u, MP_FAR_READER_OTHER, &pose),
             "nobody stands behind bank 2 any more, so its body is taken down");
    ut_check(holds(1u, 110u), "while the first client's bank is untouched");
}

static void check_the_report(void)
{
    ut_section("the report names every far player by the world slot it holds");
    {
        char text[320];

        (void)mp_bridge_far_states_text(text, sizeof text);
        ut_checkf(strstr(text, "slot 1 10 (history started 1 time(s)") != NULL &&
                      strstr(text, "slot 2 15 (history started 2 time(s)") != NULL,
                  "the first client's ten states in slot 1, the second's fifteen over two "
                  "connections in slot 2 (%s)", text);
        ut_checkf(mp_bridge_far_departures() == 2u,
                  "and both times bank 2 lost its player, by the restart and by the departure, "
                  "its history was described before it went (%u)",
                  (unsigned)mp_bridge_far_departures());
        mp_bridge_far_describe_banks();
        ut_check(true, "the banks a player stands behind describe themselves at a report");
    }
}

static void check_the_next_world(uint32_t *now)
{
    mp_bridge_far_pose_t pose;
    uint32_t             tick;
    int                  i;

    ut_section("the host enters the next world: the pose of the world before is nobody's place");
    {
        static const mp_bridge_far_reader_t readers[] = {
            MP_FAR_READER_ARRIVAL, MP_FAR_READER_REENTRY, MP_FAR_READER_RANGE_GATE,
            MP_FAR_READER_SCENE, MP_FAR_READER_OTHER
        };
        uint8_t slot    = 0u;
        bool    refused = true;
        size_t  r;

        mp_bridge_far_enter_world(4u);
        for (i = 0; i < 4; ++i) {
            mp_bridge_drain_place_puppet(&s_drain);
        }
        for (r = 0; r < sizeof readers / sizeof readers[0]; ++r) {
            refused = refused && !mp_bridge_far_pose(1u, readers[r], &pose);
        }
        ut_check(refused, "bank 1 still resolves the first client's pose of world 3, and every "
                          "reader is refused it");
        ut_check(!mp_bridge_far_occupied(1u), "so nobody stands behind bank 1 in this world");
        ut_check(mp_bridge_far_in_another_world(1u, MP_FAR_READER_SCENE, &slot) && slot == 1u,
                 "and its player, slot 1, is one on its way here");
        s_world = 4u;
        for (tick = 111u; tick <= 120u; ++tick) {
            step(now, tick, 0u);
        }
        quiet(now, QUIET);
        for (i = 0; i < 40; ++i) {
            mp_bridge_drain_place_puppet(&s_drain);
        }
        ut_check(mp_bridge_far_pose(1u, MP_FAR_READER_ARRIVAL, &pose) && pose.world == 4u &&
                     mp_bridge_far_occupied(1u) &&
                     !mp_bridge_far_in_another_world(1u, MP_FAR_READER_SCENE, NULL),
                 "once its poses are of world 4, they are a place here again");
        mp_bridge_far_report_worlds();
    }
}

int main(void)
{
    uint32_t now = 0;
    int      i;

    if (!mp_udp_init(&s_host_udp, 0)) {
        printf("no winsock on this machine, nothing to test\n");
        return ut_summary("mp_bridge_far");
    }
    s_host_transport = mp_udp_transport(&s_host_udp);
    mp_session_init(&s_host, MP_SESSION_HOST, &s_host_transport, 0xF5u);
    text_format(s_address, sizeof s_address, "127.0.0.1:%u",
                (unsigned)mp_udp_local_port(&s_host_udp));
    for (i = 0; i < CLIENTS; ++i) {
        ut_checkf(mp_udp_init(&s_client_udp[i], 0), "client %d gets a socket", i);
        s_client_transport[i] = mp_udp_transport(&s_client_udp[i]);
    }
    mp_bridge_far_reset();
    mp_bridge_far_enter_world(s_world);   /* the level the host began under generation 3 */
    mp_bridge_drain_bind(&s_drain, MP_BRIDGE_UDP_HOST, &s_host, NULL);

    check_every_client_is_taken(&now);
    check_every_far_body_is_placed();
    check_the_world_goes_to_every_peer(&now);
    check_a_moment_reaches_the_other_client(&now);
    check_a_restart_and_a_departure(&now);
    check_the_report();
    check_the_next_world(&now);

    for (i = 0; i < CLIENTS; ++i) {
        mp_udp_shutdown(&s_client_udp[i]);
    }
    mp_udp_shutdown(&s_host_udp);
    return ut_summary("mp_bridge_far");
}
