/* The bridge's command mapping, and its refusals with no game.
 *
 * The mapping is the one piece here a test can pin exactly: the wire's action bits and the
 * engine's action ids grew separately, and a wrong pairing would not look wrong, it would jump
 * when the far player meant to fire. The pump itself is the network layer's proven end-to-end
 * composition and is checked live by the bridge's own state verification in game.
 *
 * SIZE NOTE: over 600 lines, because the appearance checks need the connected pair this file
 * already builds: the host's table is only observable through a session with a peer in it, and a
 * second program would have to build the same pair again.
 */
#include "unittest.h"

#include "mp_actions.h"
#include "mp_bridge.h"
#include "mp_bridge_command.h"
#include "mp_bridge_appearance.h"
#include "mp_bridge_drain.h"
#include "mp_bridge_far.h"
#include "mp_bridge_roster.h"
#include "mp_bridge_world.h"
#include "mp_command.h"
#include "mp_events.h"
#include "mp_interp.h"
#include "mp_loopback.h"
#include "mp_payload_prefix.h"
#include "mp_session.h"
#include "mp_snapshot.h"
#include "mp_transport.h"
#include "mp_wire.h"
#include "mp_input.h"
#include "mp_roster.h"

#include <string.h>

/* Static: a session is over two megabytes and the loopback ring another three hundred
 * kilobytes, far past the stack. */
static mp_loopback_t  s_net;
static mp_session_t   s_host;
static mp_session_t   s_client;
static mp_transport_t s_host_transport;
static mp_transport_t s_client_transport;
static mp_bridge_drain_t s_drain;

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

/* Two torn payloads, one per direction: five bytes where a world's header alone takes eight,
 * and an acknowledgement prefix with three bytes behind it. Both are consumed and refused.
 *
 * The host one now also has to survive being read as a length plus a snapshot. Its first two bytes
 * say the enemy block is 0xADDE long, far past the five bytes there are, which is exactly the case
 * the length check exists for: a torn packet must not have its tail read as a snapshot. */
static void send_torn_host_world(void)
{
    static const uint8_t torn[5] = { 0xDEu, 0xADu, 0xBEu, 0xEFu, 0x01u };

    (void)mp_session_set_payload(&s_host, 0, torn, sizeof torn);
}

static void send_torn_client_state(void)
{
    uint8_t payload[MP_PAYLOAD_ACK_BYTES + 3u];

    if (mp_bridge_world_put_ack(payload, sizeof payload)) {
        payload[MP_PAYLOAD_ACK_BYTES]      = 0xDEu;
        payload[MP_PAYLOAD_ACK_BYTES + 1u] = 0xADu;
        payload[MP_PAYLOAD_ACK_BYTES + 2u] = 0xBEu;
        (void)mp_session_set_payload(&s_client, 0, payload, sizeof payload);
    }
}

/* A host payload carries the enemy block in front of the snapshot, behind a two byte length. This
 * builds one with an EMPTY block, which is what a level with no live actor produces, so the tests
 * below exercise the snapshot path with nothing in front of it. */
#define TEST_ENEMY_LENGTH_BYTES 2u

static void send_host_world(uint32_t tick, float x)
{
    uint8_t        payload[MP_SESSION_PAYLOAD_BYTES];
    mp_snapshot_t  world;
    mp_wire_body_t body;
    size_t         bytes = 0;

    memset(&body, 0, sizeof body);
    body.position[0] = x;
    body.alive       = true;
    mp_snapshot_clear(&world);
    world.tick = tick;
    mp_snapshot_set_body(&world, 0, &body);
    payload[0] = 0u;
    payload[1] = 0u;
    if (mp_snapshot_encode(&world, NULL, payload + TEST_ENEMY_LENGTH_BYTES,
                           sizeof payload - TEST_ENEMY_LENGTH_BYTES, &bytes)) {
        (void)mp_session_set_payload(&s_host, 0, payload, bytes + TEST_ENEMY_LENGTH_BYTES);
    }
}

/* A hero block shaped range with every field the builder reads placed at its offset, and a
 * second reader that stops short of the position, so the two answers of the builder are pinned:
 * a whole block becomes a body, a block the position cannot be read from becomes nothing. */
#define TEST_BLOCK_BYTES 0x3ACu

static uint8_t test_block[TEST_BLOCK_BYTES];

static void put_u32(size_t offset, uint32_t value) { memcpy(&test_block[offset], &value, 4u); }
static void put_f32(size_t offset, float value)    { memcpy(&test_block[offset], &value, 4u); }

static bool read_test_block(size_t offset, void *out, size_t size)
{
    if (offset + size > TEST_BLOCK_BYTES) {
        return false;
    }
    memcpy(out, &test_block[offset], size);
    return true;
}

static bool read_short_block(size_t offset, void *out, size_t size)
{
    return offset + size <= 0x118u && read_test_block(offset, out, size);
}

/* One reliable message from the client to the host, so the host's drain can be driven with a real
 * arrival rather than with a hand placed struct. */
static void send_reliable_event(const mp_event_t *event)
{
    uint8_t bytes[MP_EVENT_MAX_BYTES];
    size_t  count = mp_event_encode(event, bytes, sizeof bytes);

    ut_check(count != 0u, "the event encodes for the channel");
    ut_check(mp_session_send_reliable(&s_client, 0, bytes, count), "and the client sends it");
}

/* A reliable message needs more than one pump to cross, so the drain is run after each of them
 * rather than once: one round is not the claim being made, and asserting after a single one only
 * measures the channel's latency. */
static void pump_and_drain(mp_bridge_drain_t *drain, uint32_t *now)
{
    int round;

    for (round = 0; round < 4; ++round) {
        pump_pair(now);
        mp_bridge_drain_reliable_notes(drain);
    }
}

/* The two roles must not hold the same world slot.
 *
 * Every message that names a slot is judged against this side's own: a death whose victim is this
 * slot is this machine's own report coming back and is dropped, and an appearance for this slot is
 * this machine's own echo. A host that held the client's slot swallowed exactly the messages it
 * was supposed to act on, and nothing about that is visible from either end. */
static void check_the_world_slots(mp_bridge_drain_t *drain)
{
    uint32_t host_slot;
    uint32_t host_far;

    ut_section("which world slot each role holds");

    mp_bridge_drain_bind(drain, MP_BRIDGE_UDP_HOST, &s_host, &s_client);
    host_slot = drain->my_slot;
    host_far  = drain->far_slot;
    ut_check(host_slot == MP_BRIDGE_HOST_SLOT, "a host holds world slot 0");
    ut_check(host_far == MP_BRIDGE_LISTEN_HOST_CLIENT_SLOT,
             "and the one far body it shows stands for its client at slot 1");

    mp_bridge_drain_bind(drain, MP_BRIDGE_UDP_CLIENT, &s_host, &s_client);
    ut_check(drain->my_slot == MP_BRIDGE_LISTEN_HOST_CLIENT_SLOT,
             "a client holds slot 1 until its host says otherwise");
    ut_check(drain->far_slot == MP_BRIDGE_HOST_SLOT,
             "and the body it shows is the listen host's own at slot 0");
    ut_check(drain->my_slot != host_slot,
             "the two roles never hold the same slot, so neither can read the other's report as "
             "its own");
    ut_check(drain->far_slot == host_slot && drain->my_slot == host_far,
             "and each side's far slot is the other side's own");
}

/* An appearance is a rebuild of a body and must not reach the puppet's queue, which performs
 * moments inside a bank window. The fork is checked against the one message that shares the
 * channel and does belong there. */
static void check_the_appearance_fork(mp_bridge_drain_t *drain, uint32_t *now)
{
    mp_event_t event;
    uint8_t    hero = 0;

    ut_section("an appearance is answered by the body, a push is queued for the puppet");

    ut_check(!mp_actions_note_appearance(0u, NULL, 0u, NULL),
             "with no engine to sample there is no appearance of this player to send");
    ut_check(mp_actions_appearances() == 0u && mp_actions_appearances_refused() == 0u,
             "and nothing is counted as sent or as a name the codec refused");

    mp_bridge_drain_bind(drain, MP_BRIDGE_UDP_HOST, &s_host, &s_client);

    memset(&event, 0, sizeof event);
    event.kind      = MP_EVENT_SKIN;
    event.tick      = 1u;
    event.skin_kind = MP_SKIN_CHARACTER;
    event.skin_hero = 3u;
    event.skin_slot = (uint8_t)MP_BRIDGE_LISTEN_HOST_CLIENT_SLOT;
    memcpy(event.skin_asset, "mace.baf", sizeof "mace.baf");
    send_reliable_event(&event);
    pump_and_drain(drain, now);
    ut_check(drain->events_in == 0u, "the appearance was not queued for the puppet");
    ut_check(mp_bridge_far_hero(1u, &hero) && hero == 3u,
             "and the hero it named is kept, so a repeated roster cannot dress the body as the "
             "lobby's choice instead");

    memset(&event, 0, sizeof event);
    event.kind   = MP_EVENT_PUSH;
    event.tick   = 2u;
    event.charge = 1.0f;
    send_reliable_event(&event);
    pump_and_drain(drain, now);
    ut_check(drain->events_in == 1u, "a force push on the same channel still is");

    /* An appearance for a slot this build shows no body for is dropped rather than painted onto
     * the one body there is, which is what would happen with three players in a session. */
    memset(&event, 0, sizeof event);
    event.kind      = MP_EVENT_SKIN;
    event.tick      = 3u;
    event.skin_kind = MP_SKIN_MODEL;
    event.skin_hero = 0u;
    event.skin_slot = 5u;
    memcpy(event.skin_asset, "queen.baf", sizeof "queen.baf");
    send_reliable_event(&event);
    pump_and_drain(drain, now);
    ut_check(drain->events_in == 1u, "a stranger's appearance is not queued either");
    ut_check(mp_bridge_far_hero(1u, &hero) && hero == 3u && !mp_bridge_far_hero(2u, &hero),
             "and it does not overwrite the hero of a player a bank here does show");

    /* A second client's appearance names slot 2, which a listen host shows in bank 2. It used to
     * be dropped, because the one far body stood for slot 1. Sent by the first client it is that
     * client dressing somebody else, and the drain refuses it; from the second, peer 1, the
     * appearance module dresses bank 2. */
    memset(&event, 0, sizeof event);
    event.kind      = MP_EVENT_SKIN;
    event.tick      = 4u;
    event.skin_kind = MP_SKIN_CHARACTER;
    event.skin_hero = 2u;
    event.skin_slot = 2u;
    memcpy(event.skin_asset, "quigon.baf", sizeof "quigon.baf");
    send_reliable_event(&event);
    pump_and_drain(drain, now);
    ut_check(drain->notes_refused == 2u && !mp_bridge_far_hero(2u, &hero),
             "the first client can dress neither the second client's body nor a stranger's");
    mp_bridge_appearance_take_event((uint8_t)drain->my_slot, 1u, &event);
    ut_check(mp_bridge_far_hero(2u, &hero) && hero == 2u,
             "an appearance for slot 2 from the second client dresses bank 2, its body");
    ut_check(mp_bridge_far_hero(1u, &hero) && hero == 3u, "and leaves bank 1's as it was");

    mp_bridge_far_reset();
    ut_check(!mp_bridge_far_hero(1u, &hero) && !mp_bridge_far_hero(2u, &hero),
             "a reset of the session forgets the heroes the last players named");
    mp_bridge_appearance_report();
}

/* Where the host's table has client slot 1, true when it carries `kind` and `asset` there. */
static bool host_line_says(uint8_t kind, const char *asset)
{
    mp_roster_t table;
    size_t      i;

    (void)mp_bridge_roster_host_tick(&s_host, 1u);
    if (!mp_bridge_roster_current(&table)) {
        return false;
    }
    for (i = 0; i < table.count; ++i) {
        if (table.entry[i].slot == (uint8_t)MP_BRIDGE_LISTEN_HOST_CLIENT_SLOT) {
            return table.entry[i].asset_kind == kind &&
                   strcmp(table.entry[i].asset, asset) == 0;
        }
    }
    return false;
}

/* A model is worn over the hero's own body. The first field run with a model swap
 * built the far body out of the model's own file, sixteen clips, and the far player's next
 * weapon change ended the host in the engine's assert. The body module needs the game, so what
 * is checked is what the bank was told to build, which is exactly what wear() asks the body. */
static void check_the_model_over_the_hero(uint32_t *now)
{
    mp_event_t  event;
    mp_roster_t table;
    uint8_t     note[MP_ROSTER_BYTES];
    size_t      bytes;
    char        model[MP_EVENT_ASSET_MAX];

    ut_section("a model is worn over the body there is, and only a character names an actor");
    ut_check(strcmp(mp_skin_body_actor(MP_SKIN_MODEL, "anakin.baf", 2u, NULL, 0u), "") == 0,
             "a model with nothing kept builds the hero's own asset, not the model's file");
    ut_check(strcmp(mp_skin_body_actor(MP_SKIN_MODEL, "anakin.baf", 2u, "panaka.baf", 2u),
                    "panaka.baf") == 0,
             "a model over the actor the bank stands on keeps it, so nothing is rebuilt");
    ut_check(strcmp(mp_skin_body_actor(MP_SKIN_MODEL, "anakin.baf", 1u, "panaka.baf", 2u),
                    "") == 0,
             "but not an actor kept for another hero");
    ut_check(strcmp(mp_skin_body_actor(MP_SKIN_CHARACTER, "mace.baf", 3u, "panaka.baf", 3u),
                    "mace.baf") == 0,
             "a character is the actor it names");
    ut_check(strcmp(mp_skin_body_actor(MP_SKIN_KIND_MAX + 1u, "anakin.baf", 2u, NULL, 0u),
                    "") == 0 &&
                 strcmp(mp_skin_body_actor(MP_SKIN_CHARACTER, NULL, 2u, NULL, 0u), "") == 0,
             "and neither an unknown kind nor no name builds anything foreign");

    ut_section("the appearance event: the bank keeps the body and the model over it");
    mp_bridge_far_reset();
    memset(&event, 0, sizeof event);
    event.kind      = MP_EVENT_SKIN;
    event.tick      = 10u;
    event.skin_kind = MP_SKIN_CHARACTER;
    event.skin_hero = 2u;
    event.skin_slot = (uint8_t)MP_BRIDGE_LISTEN_HOST_CLIENT_SLOT;
    memcpy(event.skin_asset, "panaka.baf", sizeof "panaka.baf");
    mp_bridge_appearance_take_event(0u, 0u, &event);
    ut_check(strcmp(mp_bridge_far_actor(1u), "panaka.baf") == 0 &&
                 !mp_bridge_far_model(1u, NULL, 0u),
             "a character change builds bank 1 as the actor it names, with no model over it");
    event.skin_kind = MP_SKIN_MODEL;
    memset(event.skin_asset, 0, sizeof event.skin_asset);
    memcpy(event.skin_asset, "anakin.baf", sizeof "anakin.baf");
    mp_bridge_appearance_take_event(0u, 0u, &event);
    ut_check(mp_bridge_far_model(1u, model, sizeof model) && strcmp(model, "anakin.baf") == 0,
             "a model change is kept as the model bank 1 wears");
    ut_check(strcmp(mp_bridge_far_actor(1u), "panaka.baf") == 0,
             "and the body under it stays the actor it was, not the model's file");
    ut_check(host_line_says(MP_SKIN_MODEL, "anakin.baf"),
             "the host's table carries it AS a model, for a late joiner and the other clients");

    ut_section("the repeated table: a late joiner reads a model as a model");
    mp_bridge_far_reset();
    memset(&table, 0, sizeof table);
    table.count = 2u;
    memcpy(table.entry[0].name, "Host", 5u);
    table.entry[1].slot       = (uint8_t)MP_BRIDGE_LISTEN_HOST_CLIENT_SLOT;
    table.entry[1].hero       = 2u;
    table.entry[1].asset_kind = MP_SKIN_MODEL;
    memcpy(table.entry[1].name, "Guest", 6u);
    memcpy(table.entry[1].asset, "anakin.baf", sizeof "anakin.baf");
    bytes = mp_roster_encode(&table, note, sizeof note);
    ut_check(bytes != 0u && mp_bridge_roster_take(note, bytes), "a table with a model is taken");
    mp_bridge_appearance_take_roster();
    ut_check(mp_bridge_far_model(1u, model, sizeof model) && strcmp(model, "anakin.baf") == 0 &&
                 mp_bridge_far_actor(1u)[0] == '\0',
             "and bank 1 is the hero's own body with the model over it, not the model's file");

    ut_section("the look goes with its player");
    mp_bridge_far_unseat(1u);
    ut_check(!mp_bridge_far_model(1u, NULL, 0u) && mp_bridge_far_actor(1u)[0] == '\0',
             "a bank given up forgets the look it showed");
    mp_bridge_far_seat(1u, (uint8_t)MP_BRIDGE_LISTEN_HOST_CLIENT_SLOT);
    mp_bridge_appearance_take_event(0u, 0u, &event);
    mp_bridge_far_start_over(1u, 0x1234u);
    ut_check(!mp_bridge_far_model(1u, NULL, 0u), "and so does one a new connection starts over");
    mp_bridge_appearance_take_event(0u, 0u, &event);
    mp_bridge_far_reset();
    ut_check(!mp_bridge_far_model(1u, NULL, 0u), "and a reset of the session");

    /* The host's table: a player who left leaves nothing behind for the next one on the index,
     * although this one never sent a lobby note, and neither does a session that was left. */
    mp_bridge_appearance_take_event(0u, 0u, &event);
    mp_session_disconnect(&s_client);
    pump_pair(now);
    (void)mp_bridge_roster_host_tick(&s_host, 2u);
    ut_check(connect_pair(now), "the client comes back on the same index");
    ut_check(host_line_says(MP_SKIN_CHARACTER, ""),
             "and its line carries nothing of the look the one before it wore");
    mp_bridge_appearance_take_event(0u, 0u, &event);
    mp_bridge_roster_forget();
    ut_check(host_line_says(MP_SKIN_CHARACTER, ""),
             "a session left forgets the look of every player in it");
}

static void check_the_command_mapping(void)
{
    mp_command_t       command;
    mp_input_command_t input;

    memset(&command, 0, sizeof command);

    ut_section("the axes cross unchanged");
    command.turn = 0.5f;
    command.move = -1.0f;
    mp_bridge_map_command(&command, &input);
    ut_near(input.turn_axis, 0.5, 0.0, "the wire's turn lands on the turn axis");
    ut_near(input.move_axis, -1.0, 0.0, "the wire's move lands on the move axis");
    ut_near(input.mouse_turn, 0.0, 0.0, "nothing on the wire feeds the relative axis");

    ut_section("every action bit lands on its engine id");
    {
        static const struct { uint32_t wire; uint32_t action; const char *name; } cases[] = {
            { MP_CMD_ATTACK,      2u,    "attack"      },
            { MP_CMD_JUMP,        3u,    "jump"        },
            { MP_CMD_FORCE,       4u,    "force push"  },
            { MP_CMD_SIDLE,       5u,    "sidle"       },
            { MP_CMD_USE,         6u,    "use"         },
            { MP_CMD_RUN,         7u,    "run"         },
            { MP_CMD_WEAPON1,     0x0Bu, "weapon 1"    },
            { MP_CMD_WEAPON2,     0x0Cu, "weapon 2"    },
            { MP_CMD_WEAPON3,     0x0Du, "weapon 3"    },
            { MP_CMD_WEAPON4,     0x0Eu, "weapon 4"    },
            { MP_CMD_WEAPON5,     0x0Fu, "weapon 5"    },
            { MP_CMD_WEAPON6,     0x10u, "weapon 6"    },
            { MP_CMD_WEAPON_PREV, 0x14u, "weapon prev" },
            { MP_CMD_WEAPON_NEXT, 0x15u, "weapon next" }
        };
        size_t i;

        for (i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
            memset(&command, 0, sizeof command);
            command.buttons = cases[i].wire;
            mp_bridge_map_command(&command, &input);
            ut_checkf(input.buttons == (1u << cases[i].action),
                      "%s maps to action id 0x%02X and nothing else", cases[i].name,
                      (unsigned)cases[i].action);
        }

        /* The objectives key is carried and lands nowhere: on the host the input phase answers
         * it with a quick save, which is the local machine's to make. */
        memset(&command, 0, sizeof command);
        command.buttons = MP_CMD_OBJECTIVES;
        mp_bridge_map_command(&command, &input);
        ut_check(input.buttons == 0u, "objectives maps to no action at all");
    }

    ut_section("edges");
    command.buttons = ~0u;
    mp_bridge_map_command(&command, &input);
    ut_check((input.buttons & (1u << 8)) == 0u && (input.buttons & (1u << 0)) == 0u,
             "wire bits with no engine action set no stray engine bit");
    mp_bridge_map_command(NULL, &input);
    ut_check(input.buttons == 0u && input.turn_axis == 0.0f,
             "no command maps to stillness, never to leftovers");
}

static void check_the_body_read(void)
{
    ut_section("a body out of a hero block shaped range");
    {
        mp_wire_body_t body;

        memset(test_block, 0, sizeof test_block);
        put_f32(0x118u, 1.5f);
        put_f32(0x11Cu, -2.25f);
        put_f32(0x120u, 3.0f);
        put_f32(0x2A0u, 270.0f);
        put_u32(0x6Cu, 1u);
        put_u32(0x394u, 0u);
        put_u32(0x36Cu, 0x47u);
        put_u32(0x370u, 0x20u);
        put_u32(0x84u, 1u);
        ut_check(mp_bridge_world_build_body(&body, &read_test_block),
                 "a block every field reads out of becomes a body");
        ut_near(body.position[0], 1.5, 0.0, "the position's x is the block's +0x118");
        ut_near(body.position[1], -2.25, 0.0, "the position's y is the block's +0x11C");
        ut_near(body.position[2], 3.0, 0.0, "the position's z is the block's +0x120");
        ut_near(body.orientation[1], 270.0, 0.0, "the heading rides the second angle");
        ut_check(body.hero == 1u, "the hero index is the block's +0x6C");
        ut_check(body.alive && !body.dead, "a zero dead word is a living body");
        ut_check(body.anim.clip[0] == 0x47u && body.anim.clip[1] == 0x20u,
                 "the base and overlay clips are the block's two mirrors");
        ut_check(body.weapon == 1u, "the weapon slot is the block's +0x84");
        ut_check(body.twist_count == 0u, "a block with no actor handle carries no twist");

        put_u32(0x394u, 1u);
        ut_check(mp_bridge_world_build_body(&body, &read_test_block) && body.dead && !body.alive,
                 "a set dead word is a dead body, not a living one");
        ut_check(!mp_bridge_world_build_body(&body, &read_short_block),
                 "a block the position cannot be read from builds nothing");
    }

    ut_section("the object read with no object");
    {
        mp_wire_body_t body;

        memset(&body, 0, sizeof body);
        body.anim.clip[0]      = 0x47u;
        body.anim.track[0]     = 500u;
        body.anim.track[1]     = 600u;
        body.anim.channel_mask = 0x7u;
        ut_check(!mp_bridge_world_read_object(&body, 0u),
                 "a handle of zero reads nothing and says so");
        ut_check(body.anim.track[0] == 0u && body.anim.track[1] == 0u,
                 "and leaves both playheads at zero");
        ut_check(body.anim.channel_mask == 0u, "with no live bit and no fade bit");
        ut_check(body.anim.clip[0] == 0x47u, "while the block's clip mirror stands");
    }
}

static void check_the_acknowledgements(void)
{
    ut_section("the acknowledgement prefix is the module's, out of its own history");
    {
        uint8_t          prefix[MP_PAYLOAD_ACK_BYTES];
        mp_payload_ack_t acked;

        mp_bridge_world_reset();
        ut_check(mp_bridge_world_put_ack(prefix, sizeof prefix), "it writes");
        ut_check(mp_payload_get_ack(prefix, sizeof prefix, &acked) && acked.newest == 0u &&
                     acked.bits == 0u,
                 "and with nothing received it says nought and names nothing");
        ut_check(!mp_bridge_world_put_ack(prefix, MP_PAYLOAD_ACK_BYTES - 1u),
                 "seven bytes do not take one");
    }

    ut_section("the acknowledgement round trip through a session pair");
    {
        uint32_t         now = 0;
        mp_snapshot_t    decoded;
        mp_payload_ack_t acked;
        uint8_t          prefix[MP_PAYLOAD_ACK_BYTES];

        mp_bridge_world_reset();
        ut_check(connect_pair(&now), "a host and a client connect over the loopback");

        send_host_world(300u, 5.0f);
        pump_pair(&now);
        ut_check(mp_bridge_world_receive(&s_client, &decoded) == MP_BRIDGE_WORLD_DECODED &&
                     decoded.tick == 300u,
                 "the client decodes the host's world at tick 300");
        ut_check(mp_bridge_world_put_ack(prefix, sizeof prefix) &&
                     mp_payload_get_ack(prefix, sizeof prefix, &acked) && acked.newest == 300u,
                 "and now holds 300 as its newest tick");
        ut_check(mp_bridge_world_receive(&s_client, &decoded) == MP_BRIDGE_WORLD_NOTHING,
                 "a second read finds the ring empty");

        send_client_state(7u, 42.0f);
        pump_pair(&now);
        ut_check(mp_bridge_world_receive_full(&s_host, 0, 1, &decoded, &acked) ==
                     MP_BRIDGE_WORLD_DECODED,
                 "the host decodes the client's own state in slot 1");
        ut_check(acked.newest == 300u,
                 "with the client's acknowledgement of tick 300 in front of it");
        ut_check(decoded.tick == 7u && mp_snapshot_has_body(&decoded, 1) &&
                 decoded.body[1].health == 97u,
                 "and the body behind the prefix, health included");
        ut_check(mp_bridge_world_receive_full(&s_host, 0, 1, &decoded, &acked) ==
                     MP_BRIDGE_WORLD_NOTHING,
                 "a second read finds nothing");

        send_client_state(8u, 43.0f);
        pump_pair(&now);
        ut_check(mp_bridge_world_receive_full(&s_host, 0, 0, &decoded, &acked) ==
                     MP_BRIDGE_WORLD_REFUSED,
                 "a state carrying no body in the asked slot is consumed and refused");
        mp_session_disconnect(&s_client);
        mp_session_disconnect(&s_host);
    }
}

/* Two payloads in the ring, the first torn: a drain that stopped at the refusal would leave the
 * second waiting a substep and, with more arriving, pushed out of the ring unread. */
static void check_a_torn_payload(void)
{
    ut_section("a torn payload does not end the drain");
    {
        uint32_t         now = 0;
        mp_snapshot_t    decoded;
        mp_payload_ack_t acked;

        mp_bridge_world_reset();
        ut_check(connect_pair(&now), "a fresh host and client connect over the loopback");

        send_torn_host_world();
        pump_pair(&now);
        send_host_world(301u, 6.0f);
        pump_pair(&now);
        ut_check(mp_bridge_world_receive(&s_client, &decoded) == MP_BRIDGE_WORLD_REFUSED,
                 "the torn world at the head of the client's ring is consumed and refused");
        ut_check(mp_bridge_world_receive(&s_client, &decoded) == MP_BRIDGE_WORLD_DECODED &&
                     decoded.tick == 301u,
                 "and the world behind it is decoded in the same drain");
        ut_check(mp_bridge_world_receive(&s_client, &decoded) == MP_BRIDGE_WORLD_NOTHING,
                 "which then finds the ring empty");

        send_torn_client_state();
        pump_pair(&now);
        send_client_state(9u, 44.0f);
        pump_pair(&now);
        ut_check(mp_bridge_world_receive_full(&s_host, 0, 1, &decoded, &acked) ==
                     MP_BRIDGE_WORLD_REFUSED,
                 "the torn state at the head of the host's ring is consumed and refused");
        ut_check(mp_bridge_world_receive_full(&s_host, 0, 1, &decoded, &acked) ==
                     MP_BRIDGE_WORLD_DECODED &&
                     decoded.tick == 9u && acked.newest == 301u,
                 "and the state behind it lands in the same drain, acknowledgement and all");
        ut_check(mp_bridge_world_receive_full(&s_host, 0, 1, &decoded, &acked) ==
                     MP_BRIDGE_WORLD_NOTHING,
                 "which then finds the ring empty");
        mp_session_disconnect(&s_client);
        mp_session_disconnect(&s_host);
    }
}

static void check_the_connected_pair(void)
{
    uint32_t now = 0;

    mp_bridge_world_reset();
    ut_check(connect_pair(&now), "a host and a client connect for the appearance checks");
    check_the_world_slots(&s_drain);
    check_the_appearance_fork(&s_drain, &now);
    check_the_model_over_the_hero(&now);
    mp_session_disconnect(&s_client);
    mp_session_disconnect(&s_host);
}

static void check_the_module_with_no_game(void)
{
    ut_section("the module with no game");
    ut_check(!mp_bridge_installed(), "nothing is installed before the installer ran");
    ut_check(!mp_bridge_install(10u),
             "an install with no hero block cell refuses before building sessions");
    mp_bridge_world_reset();
    mp_bridge_tick_pre();
    mp_bridge_tick_post();
    mp_bridge_substep_end();
    mp_bridge_report("a test process");
    ut_check(!mp_bridge_installed(), "the ticks, the substep end and the report on an uninstalled "
                                     "bridge touch nothing");

    /* The two halves hang off different things now, so the substep end has to survive running
     * without a task half in front of it. Whether it then declines to send is not observable from
     * here, because an uninstalled bridge sends nothing either way; what is checked is that the
     * unpaired call is not itself a fault. */
    mp_bridge_substep_end();
    mp_bridge_substep_end();
    ut_check(!mp_bridge_installed(), "repeated substep ends with no task half between them are "
                                     "harmless on an uninstalled bridge");
}

int main(void)
{
    check_the_command_mapping();
    check_the_body_read();
    check_the_acknowledgements();
    check_a_torn_payload();
    check_the_connected_pair();
    check_the_module_with_no_game();

    return ut_summary("mp_bridge");
}
