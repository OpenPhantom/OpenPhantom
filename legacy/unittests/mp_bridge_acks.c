/* mp_bridge_acks.c: the client's acknowledgement with its bits, and a dedicated server's world,
 * both through the real receiving path on both ends.
 *
 * The host stamps payloads whose enemy block names placement 7, laid out as its send lays them
 * out and in the room a payload of the session's size leaves; the client takes them with its own
 * receive, which is what keeps a tick in its history, and answers with the prefix the world module
 * builds out of that history rather than one this test writes. What is checked is what the host's
 * enemies then believe. A payload the client took and only answered with a newer one's tick used
 * to open every key it carried; its bit now keeps them.
 *
 * The dedicated server runs over the same loopback, and its world goes through the client's own
 * receive. It was sent without the enemy length every client reads first, and every one of its
 * worlds was refused.
 */
#include "unittest.h"

#include "mp_bridge_world.h"
#include "mp_budget_rule.h"
#include "mp_channel.h"
#include "mp_enemy_sync.h"
#include "mp_enemy_sync_internal.h"
#include "mp_enemy_wire.h"
#include "mp_loopback.h"
#include "mp_payload_prefix.h"
#include "mp_server.h"
#include "mp_session.h"
#include "mp_snapshot.h"
#include "mp_transport.h"
#include "mp_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Static: a session is over two megabytes and the loopback ring another three hundred
 * kilobytes, far past the stack. */
static mp_loopback_t  s_net;
static mp_session_t   s_host;
static mp_session_t   s_client;
static mp_transport_t s_host_transport;
static mp_transport_t s_client_transport;

static bool connect_pair(uint32_t *now)
{
    int tick;

    mp_loopback_init(&s_net, NULL, 0x77u);
    s_host_transport   = mp_loopback_transport(&s_net, MP_LOOPBACK_ENDPOINT_A);
    s_client_transport = mp_loopback_transport(&s_net, MP_LOOPBACK_ENDPOINT_B);
    mp_session_init(&s_host, MP_SESSION_HOST, &s_host_transport, 0xA5u);
    mp_session_init(&s_client, MP_SESSION_CLIENT, &s_client_transport, 0x5Au);
    mp_session_connect(&s_client, MP_LOOPBACK_ENDPOINT_A);
    for (tick = 0; tick < 200; ++tick) {
        *now += 16u;
        mp_loopback_pump(&s_net, *now);
        mp_session_update(&s_host, *now);
        mp_session_update(&s_client, *now);
        if (mp_session_is_connected(&s_client) && mp_session_peer_count(&s_host) == 1u) {
            return true;
        }
    }
    return false;
}

static void pump_pair(uint32_t *now)
{
    int tick;

    for (tick = 0; tick < 4; ++tick) {
        *now += 16u;
        mp_loopback_pump(&s_net, *now);
        mp_session_update(&s_host, *now);
        mp_session_update(&s_client, *now);
    }
}

/* The client's own state as the world module lays it out: the acknowledgement, then a full
 * one-body snapshot. The body is built here, because the module's own builder needs the game; the
 * acknowledgement is the module's, out of the history of the host's worlds this process received,
 * so what the host is told is what the client's receive really kept. */
static void send_client_state(uint32_t tick, float x)
{
    uint8_t        payload[MP_SESSION_PAYLOAD_BYTES];
    mp_snapshot_t  own;
    mp_wire_body_t body;
    size_t         bytes = 0;

    memset(&body, 0, sizeof body);
    body.position[0] = x;
    body.alive       = true;
    body.health      = 97u;
    mp_snapshot_clear(&own);
    own.tick = tick;
    mp_snapshot_set_body(&own, 1, &body);
    if (mp_bridge_world_put_ack(payload, sizeof payload) &&
        mp_snapshot_encode(&own, NULL, payload + MP_PAYLOAD_ACK_BYTES,
                           sizeof payload - MP_PAYLOAD_ACK_BYTES, &bytes)) {
        (void)mp_session_set_payload(&s_client, 0, payload, bytes + MP_PAYLOAD_ACK_BYTES);
    }
}

/* ==============================================================================================
 * The acknowledgement's bits, through the real receiving path on both ends.
 * ============================================================================================ */

#define ENEMY_LEVEL 57u
#define ENEMY_KEY   7u

static mp_server_t s_server;

/* Placement 7 standing at x as the host's census reads it, put where the encoder reads it. */
static void enemy_at(float x)
{
    enemy_sync_state_t *s      = mp_enemy_sync_state();
    mp_enemy_record_t  *record = &s->placement[ENEMY_KEY].current;
    uint32_t            packed = 0;

    memset(record, 0, sizeof *record);
    record->value[MP_ENEMY_F_INDEX]      = ENEMY_KEY;
    record->value[MP_ENEMY_F_GENERATION] = 1u;
    (void)mp_enemy_wire_put_position(x, &packed);
    record->value[MP_ENEMY_F_POS_X] = packed;
    (void)mp_enemy_wire_put_position(5.0f, &packed);
    record->value[MP_ENEMY_F_POS_Y] = packed;
    (void)mp_enemy_wire_put_position(7.0f, &packed);
    record->value[MP_ENEMY_F_POS_Z]  = packed;
    record->value[MP_ENEMY_F_HEALTH] = mp_enemy_wire_put_health(60);
    s->placement[ENEMY_KEY].current_ok = true;
    s->placement[ENEMY_KEY].generation = 1u;
}

/* One host payload of `tick` for peer 0, laid out as the host's send lays it out: the enemy block
 * in the room a payload of the session's size leaves beside one body, its length, then the world;
 * stamped as sent once the session took it. */
static void host_stamps(uint32_t tick, float x)
{
    uint8_t        payload[MP_SESSION_PAYLOAD_BYTES];
    mp_snapshot_t  world;
    mp_wire_body_t body;
    size_t         room = mp_budget_enemy_room(sizeof payload, mp_payload_world_reserve(1u), 0u);
    size_t         enemy_bytes = 0;
    size_t         bytes = 0;

    enemy_at(x);
    memset(&body, 0, sizeof body);
    body.alive = true;
    mp_snapshot_clear(&world);
    world.tick = tick;
    mp_snapshot_set_body(&world, 0, &body);
    if (!mp_enemy_sync_encode_for(0u, payload + MP_PAYLOAD_ENEMY_LENGTH_BYTES, room,
                                  &enemy_bytes)) {
        enemy_bytes = 0;
    }
    if (mp_payload_put_enemy_length(payload, sizeof payload, enemy_bytes) &&
        mp_snapshot_encode(&world, NULL, payload + MP_PAYLOAD_ENEMY_LENGTH_BYTES + enemy_bytes,
                           sizeof payload - MP_PAYLOAD_ENEMY_LENGTH_BYTES - enemy_bytes, &bytes) &&
        mp_session_set_payload(&s_host, 0, payload,
                               MP_PAYLOAD_ENEMY_LENGTH_BYTES + enemy_bytes + bytes)) {
        mp_enemy_sync_sent_for(0u, tick);
    } else {
        mp_enemy_sync_abandon_for(0u);
    }
}

/* Every world in the client's ring through the client's receive, then its own state back with
 * the prefix the module builds, through the host's receive and into its acknowledgement. */
static uint32_t client_takes_and_answers(uint32_t *now)
{
    mp_snapshot_t    decoded;
    mp_payload_ack_t ack;
    uint32_t         taken = 0;

    while (mp_bridge_world_receive(&s_client, &decoded) != MP_BRIDGE_WORLD_NOTHING) {
        ++taken;
    }
    send_client_state(*now, 42.0f);
    pump_pair(now);
    while (mp_bridge_world_receive_full(&s_host, 0, 1, &decoded, &ack) ==
           MP_BRIDGE_WORLD_DECODED) {
        mp_bridge_world_acknowledged(0u, &ack);
    }
    return taken;
}

/* The host's view of peer 0 confirmed by payload 100, which carried placement 7 whole. */
static bool start_the_view(uint32_t *now)
{
    mp_bridge_world_reset();
    mp_enemy_sync_reset();
    mp_enemy_sync_set_enabled(true);
    mp_enemy_sync_set_level(true, ENEMY_LEVEL);
    (void)mp_enemy_sync_begin_send();
    if (!connect_pair(now)) {
        return false;
    }
    host_stamps(100u, 40.0f);
    pump_pair(now);
    return client_takes_and_answers(now) == 1u && mp_enemy_sync_state()->view[0].confirmed &&
           mp_enemy_sync_state()->view[0].known[ENEMY_KEY];
}

static void check_the_bits_keep_what_the_client_took(uint32_t *now)
{
    enemy_sync_state_t *s = mp_enemy_sync_state();
    uint8_t             block[MP_SESSION_PAYLOAD_BYTES];
    uint8_t             whole[128];
    size_t              whole_bytes = 0;
    size_t              bytes       = 0;
    uint32_t            reopened;

    ut_section("two payloads in one substep of the client: the one stepped over was taken");
    ut_check(start_the_view(now), "payload 100 carried placement 7 whole and confirmed the view");
    host_stamps(101u, 41.0f);
    pump_pair(now);
    host_stamps(102u, 42.0f);
    pump_pair(now);
    reopened = s->reopened;
    ut_check(client_takes_and_answers(now) == 2u,
             "101 and 102, each carrying 7 as a delta, are both taken before the client answers");
    ut_checkf(s->reopened == reopened && s->view[0].known[ENEMY_KEY],
              "the answer names 102 and its bits 101, so 7 stays known (%u key(s) opened)",
              (unsigned)(s->reopened - reopened));
    enemy_at(43.0f);
    ut_check(mp_enemy_wire_encode_whole(&s->placement[ENEMY_KEY].current, whole, sizeof whole,
                                        &whole_bytes) &&
                 mp_enemy_sync_encode_for(0u, block, sizeof block, &bytes) &&
                 bytes < MP_ENEMY_SYNC_HEADER_BYTES + MP_ENEMY_SYNC_IDENTITY_BYTES + whole_bytes,
             "and the next block describes 7 as a delta, not whole");
    mp_enemy_sync_abandon_for(0u);
    mp_session_disconnect(&s_client);
    mp_session_disconnect(&s_host);

    ut_section("the same, with 101 refused by the client: its bit stays clear and 7 is opened");
    ut_check(start_the_view(now), "a fresh view, confirmed by 100");
    host_stamps(101u, 41.0f);
    pump_pair(now);
    host_stamps(102u, 42.0f);
    pump_pair(now);
    reopened = s->reopened;
    {
        mp_snapshot_t decoded;

        mp_enemy_sync_set_level(false, 0u);   /* the client's level closes under 101 */
        ut_check(mp_bridge_world_receive(&s_client, &decoded) == MP_BRIDGE_WORLD_REFUSED,
                 "101 arrives with no level open and is refused, so it is not kept");
        mp_enemy_sync_set_level(true, ENEMY_LEVEL);
    }
    (void)client_takes_and_answers(now);
    ut_checkf(s->reopened == reopened + 1u && !s->view[0].known[ENEMY_KEY],
              "the answer names 102 with 101's bit clear, and the key 101 carried is opened (%u)",
              (unsigned)(s->reopened - reopened));
    mp_session_disconnect(&s_client);
    mp_session_disconnect(&s_host);
}

/* The dedicated server's world, through the one receive every client of a listen host uses. */
static void check_the_dedicated_servers_world(void)
{
    uint8_t       note[MP_CHANNEL_MESSAGE_BYTES];
    mp_snapshot_t decoded;
    size_t        bytes    = 0;
    uint32_t      now      = 0;
    uint32_t      worlds   = 0;
    uint32_t      refused  = 0;
    int           step;

    ut_section("a dedicated server's world is read by a client's own receive");
    mp_bridge_world_reset();
    mp_loopback_init(&s_net, NULL, 0x78u);
    s_host_transport   = mp_loopback_transport(&s_net, MP_LOOPBACK_ENDPOINT_A);
    s_client_transport = mp_loopback_transport(&s_net, MP_LOOPBACK_ENDPOINT_B);
    mp_server_init(&s_server, &s_host_transport, 0xD7u);
    mp_session_init(&s_client, MP_SESSION_CLIENT, &s_client_transport, 0x5Bu);
    mp_session_connect(&s_client, MP_LOOPBACK_ENDPOINT_A);
    for (step = 0; step < 400; ++step) {
        mp_bridge_world_read_t read;

        now += 5u;
        mp_loopback_pump(&s_net, now);
        mp_server_tick(&s_server, now);
        mp_session_update(&s_client, now);
        while (mp_session_read_reliable(&s_client, 0, note, sizeof note, &bytes)) {
        }
        if (mp_session_is_connected(&s_client)) {
            send_client_state(now, 42.0f);
        }
        while ((read = mp_bridge_world_receive(&s_client, &decoded)) != MP_BRIDGE_WORLD_NOTHING) {
            if (read == MP_BRIDGE_WORLD_DECODED && mp_snapshot_has_body(&decoded, 1u) &&
                decoded.body[1].health == 97u) {
                ++worlds;
            } else {
                ++refused;
            }
        }
    }
    ut_checkf(worlds > 0u && refused == 0u,
              "every world the server sent decodes, the client's own body in slot 1 (%u taken, %u "
              "refused)", (unsigned)worlds, (unsigned)refused);
    mp_session_disconnect(&s_client);
}

int main(void)
{
    uint32_t now = 0;

    check_the_bits_keep_what_the_client_took(&now);
    mp_enemy_sync_reset();
    check_the_dedicated_servers_world();
    return ut_summary("mp_bridge_acks");
}
