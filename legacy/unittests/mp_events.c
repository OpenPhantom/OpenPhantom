/* mp_events.c: the event codec, the ring and the body frame, driven over their edges.
 *
 * SIZE NOTE: over 600 lines, one check function per event kind plus the two properties
 * that belong to the codec as a whole, the recogniser and the channel's length census. The seam,
 * if this needs one, is the census: it is the only part that reads other modules' headers, and it
 * asks a question about the channel rather than about any one event.
 *
 * An event that is mistaken for a slot note, or one of those mistaken for an event, would move a
 * player to a wrong world slot or fire a shot nobody fired, so the length-and-tag test is the case
 * with the most checks. The ring's drop rule is checked because a ring that blocks would stall
 * the bridge's substep on a burst of shots. The body frame is pinned to the engine's convention,
 * forward is (-sin yaw, cos yaw, 0), because a sign wrong there puts every far bolt behind the
 * puppet instead of in front of it, and the round trip is pinned because the sender and the puppet
 * run the two halves. A body's moment names its player and a host restamps a client's before it
 * passes it on; both are pinned, because a slot or a tick that is wrong puts one player's shot on
 * another's body or at another moment.
 */
#include "unittest.h"

#include "mp_events.h"
#include "mp_channel.h"
#include "mp_hit_relay.h"
#include "mp_lobby.h"
#include "mp_npc_shot.h"
#include "mp_roster.h"
#include "mp_scratch_wire.h"
#include "mp_snapshot.h"
#include "mp_wire.h"
#include "mp_world.h"
#include "mp_world_state.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>

static void check_shot_round_trip(void)
{
    mp_event_t sent;
    mp_event_t got;
    uint8_t    buffer[MP_EVENT_MAX_BYTES];
    size_t     bytes;

    ut_section("a shot goes out and comes back, with its tick and its body relative muzzle");

    memset(&sent, 0, sizeof sent);
    sent.kind        = MP_EVENT_SHOT;
    sent.tick        = 0xDEADBEEFu;
    sent.source_slot = 2u;
    sent.shot_kind   = 11;
    sent.origin[0]   = 0.25f;
    sent.origin[1]   = -0.5f;
    sent.origin[2]   = 1.125f;
    sent.pitch       = -30.0f;
    sent.yaw         = 200.0f;

    bytes = mp_event_encode(&sent, buffer, sizeof buffer);
    ut_check(bytes == MP_EVENT_SHOT_BYTES, "a shot is exactly the size the header promises");
    ut_check(bytes == 23u, "which is twenty-three bytes");
    ut_check(buffer[0] == MP_EVENT_SHOT && buffer[1] == 0xEFu && buffer[2] == 0xBEu &&
             buffer[3] == 0xADu && buffer[4] == 0xDEu,
             "the tag first, then the tick, least significant byte first");
    ut_check(buffer[5] == 2u, "then the world slot of the player who fired it");
    ut_check(mp_event_is_event(buffer, bytes), "and is recognised as an event");
    ut_check(mp_event_decode(buffer, bytes, &got), "and decodes");
    ut_check(got.kind == MP_EVENT_SHOT && got.shot_kind == 11, "kind and projectile survive");
    ut_check(got.tick == 0xDEADBEEFu, "the sender's tick survives whole");
    ut_check(got.source_slot == 2u, "and whose shot it is");
    ut_near(got.origin[0], 0.25f, MP_WIRE_POSITION_ERROR, "the offset's x survives");
    ut_near(got.origin[1], -0.5f, MP_WIRE_POSITION_ERROR, "the offset's y survives");
    ut_near(got.origin[2], 1.125f, MP_WIRE_POSITION_ERROR, "the offset's z survives");
    ut_near(got.pitch, -30.0f, MP_WIRE_ANGLE_ERROR, "a negative pitch comes back negative");
    ut_near(got.yaw, 200.0f, MP_WIRE_ANGLE_ERROR, "the relative yaw survives");
}

static void check_push_round_trip(void)
{
    mp_event_t sent;
    mp_event_t got;
    uint8_t    buffer[MP_EVENT_MAX_BYTES];
    size_t     bytes;

    ut_section("a push carries its tick and its charge");

    memset(&sent, 0, sizeof sent);
    sent.kind   = MP_EVENT_PUSH;
    sent.tick   = 41u;
    sent.charge = 0.6f;
    bytes = mp_event_encode(&sent, buffer, sizeof buffer);
    ut_check(bytes == MP_EVENT_PUSH_BYTES && bytes == 7u, "a push is seven bytes");
    ut_check(mp_event_decode(buffer, bytes, &got), "and decodes");
    ut_check(got.tick == 41u, "with its tick");
    ut_near(got.charge, 0.6f, 1.0f / 255.0f, "the charge survives to a byte's precision");

    sent.charge = 7.0f;
    bytes = mp_event_encode(&sent, buffer, sizeof buffer);
    (void)mp_event_decode(buffer, bytes, &got);
    ut_check(got.charge == 1.0f, "a charge past one is clamped to a full push");

    sent.charge = -1.0f;
    bytes = mp_event_encode(&sent, buffer, sizeof buffer);
    (void)mp_event_decode(buffer, bytes, &got);
    ut_check(got.charge == 0.0f, "and a negative one to nothing");
}

static void check_sabre_round_trip(void)
{
    mp_event_t sent;
    mp_event_t got;
    uint8_t    buffer[MP_EVENT_MAX_BYTES];
    size_t     bytes;

    ut_section("a sabre action carries its tick, its action and its operand");

    memset(&sent, 0, sizeof sent);
    sent.kind    = MP_EVENT_SABRE;
    sent.tick    = 1000u;
    sent.action  = MP_SABRE_SWING;
    sent.operand = 24;
    bytes = mp_event_encode(&sent, buffer, sizeof buffer);
    ut_check(bytes == MP_EVENT_SABRE_BYTES && bytes == 8u, "a sabre action is eight bytes");
    ut_check(mp_event_is_event(buffer, bytes), "and is recognised");
    ut_check(mp_event_decode(buffer, bytes, &got), "and decodes");
    ut_check(got.kind == MP_EVENT_SABRE && got.tick == 1000u, "kind and tick survive");
    ut_check(got.action == MP_SABRE_SWING && got.operand == 24, "a swing of row 24 survives");

    sent.action  = MP_SABRE_DISARM;
    sent.operand = 0;
    bytes = mp_event_encode(&sent, buffer, sizeof buffer);
    ut_check(mp_event_decode(buffer, bytes, &got) && got.action == MP_SABRE_DISARM,
             "a disarm survives");

    sent.action = MP_SABRE_ACTION_MAX + 1u;
    ut_check(mp_event_encode(&sent, buffer, sizeof buffer) == 0u,
             "an action past the last one does not encode");

    sent.action = MP_SABRE_PARRY;
    bytes = mp_event_encode(&sent, buffer, sizeof buffer);
    buffer[6] = 9u;   /* the action byte, forged past the range */
    ut_check(mp_event_is_event(buffer, bytes), "a forged action still looks like an event");
    ut_check(!mp_event_decode(buffer, bytes, &got),
             "but does not decode, because the far side has no starter for it");
}

static void check_weapon_round_trip(void)
{
    mp_event_t sent;
    mp_event_t got;
    uint8_t    buffer[MP_EVENT_MAX_BYTES];
    size_t     bytes;

    ut_section("a weapon change carries its tick and the slot it is heading for");

    memset(&sent, 0, sizeof sent);
    sent.kind        = MP_EVENT_WEAPON;
    sent.tick        = 77u;
    sent.weapon_slot = 3u;
    bytes = mp_event_encode(&sent, buffer, sizeof buffer);
    ut_check(bytes == MP_EVENT_WEAPON_BYTES && bytes == MP_EVENT_PUSH_BYTES,
             "a weapon change is as long as a push");
    ut_check(mp_event_is_event(buffer, bytes), "and is recognised by its tag");
    ut_check(mp_event_decode(buffer, bytes, &got), "and decodes");
    ut_check(got.kind == MP_EVENT_WEAPON && got.tick == 77u && got.weapon_slot == 3u,
             "kind, tick and slot survive");
    ut_check(got.charge == 0.0f, "and it is not read as the push it has the length of");

    sent.weapon_slot = 0u;
    bytes = mp_event_encode(&sent, buffer, sizeof buffer);
    ut_check(mp_event_decode(buffer, bytes, &got) && got.weapon_slot == 0u,
             "the holster, slot zero, survives");

    sent.weapon_slot = (uint8_t)MP_EVENT_WEAPON_SLOTS;
    ut_check(mp_event_encode(&sent, buffer, sizeof buffer) == 0u,
             "a slot past the last one does not encode");

    sent.weapon_slot = 2u;
    bytes = mp_event_encode(&sent, buffer, sizeof buffer);
    buffer[6] = 200u;   /* the slot byte, forged past the ammo table */
    ut_check(mp_event_is_event(buffer, bytes), "a forged slot still looks like an event");
    ut_check(!mp_event_decode(buffer, bytes, &got),
             "but does not decode, because the setter would index an ammo table with it");
}

/* The map's own moment. It names no player, which leaves it the length of a sabre action; the
 * tag is what tells the two apart, and this is where that is proven. */
static void check_mover_round_trip(void)
{
    mp_event_t sent;
    mp_event_t got;
    uint8_t    buffer[MP_EVENT_MAX_BYTES];
    size_t     bytes;

    ut_section("a mover trigger carries its tick, the mover it names and which way it went");

    memset(&sent, 0, sizeof sent);
    sent.kind       = MP_EVENT_MOVER;
    sent.tick       = 9u;
    sent.mover_id   = 258u;
    sent.mover_mode = MP_EVENT_MOVER_OPEN;
    bytes = mp_event_encode(&sent, buffer, sizeof buffer);
    ut_check(bytes == MP_EVENT_MOVER_BYTES && bytes == 8u, "a mover event is eight bytes");
    ut_check(bytes == MP_EVENT_SABRE_BYTES && bytes != MP_EVENT_SHOT_BYTES &&
             bytes != MP_EVENT_PUSH_BYTES && bytes != MP_EVENT_WEAPON_BYTES,
             "the length of a sabre action and of no other event, and one the slot note and the "
             "acknowledgement have not");
    ut_check(mp_event_is_event(buffer, bytes), "and it is recognised");
    ut_check(mp_event_decode(buffer, bytes, &got), "and decodes");
    ut_check(got.kind == MP_EVENT_MOVER && got.tick == 9u,
             "kind and tick survive, and it is not read as the sabre action it has the length of");
    ut_check(got.mover_id == 258u && got.mover_mode == MP_EVENT_MOVER_OPEN,
             "and an id past a byte survives, which is why the id is two bytes");

    sent.mover_mode = MP_EVENT_MOVER_CLOSE;
    bytes = mp_event_encode(&sent, buffer, sizeof buffer);
    ut_check(mp_event_decode(buffer, bytes, &got) && got.mover_mode == MP_EVENT_MOVER_CLOSE,
             "a close survives");

    sent.mover_mode = MP_EVENT_MOVER_OPEN + 1u;
    ut_check(mp_event_encode(&sent, buffer, sizeof buffer) == 0u,
             "a mode the engine has no function for does not encode");

    sent.mover_mode = MP_EVENT_MOVER_OPEN;
    bytes = mp_event_encode(&sent, buffer, sizeof buffer);
    buffer[7] = 7u;   /* the mode byte, forged */
    ut_check(mp_event_is_event(buffer, bytes), "a forged mode still looks like an event");
    ut_check(!mp_event_decode(buffer, bytes, &got),
             "but does not decode, because the engine has two openers and no third");
}

static void check_telling_events_apart(void)
{
    uint8_t slot_note[1] = { 0x81u };
    uint8_t ack[4] = { 0x81u, 0x00u, 0x00u, 0x00u };
    uint8_t short_shot[MP_EVENT_SHOT_BYTES - 1u];
    uint8_t wrong_tag[MP_EVENT_PUSH_BYTES] = { 0x8Au, 0, 0, 0, 0, 0 };
    uint8_t push_length_shot_tag[MP_EVENT_PUSH_BYTES] = { 0x81u, 0, 0, 0, 0, 0 };
    uint8_t sabre_length_push_tag[MP_EVENT_SABRE_BYTES] = { 0x82u, 0, 0, 0, 0, 0, 0 };
    uint8_t shot_length_mover_tag[MP_EVENT_SHOT_BYTES] = { 0x85u };
    mp_event_t got;
    mp_event_t bad;
    uint8_t    buffer[MP_EVENT_MAX_BYTES];

    ut_section("events, slot notes and the old acknowledgement are told apart");

    ut_check(!mp_event_is_event(slot_note, sizeof slot_note),
             "a one byte message is a slot note whatever its value");
    ut_check(!mp_event_is_event(ack, sizeof ack),
             "a four byte message is the old acknowledgement whatever its first byte");
    memset(short_shot, 0, sizeof short_shot);
    short_shot[0] = MP_EVENT_SHOT;
    ut_check(!mp_event_is_event(short_shot, sizeof short_shot), "a short shot is not an event");
    ut_check(!mp_event_is_event(wrong_tag, sizeof wrong_tag), "an unknown tag is not an event");
    ut_check(!mp_event_is_event(push_length_shot_tag, sizeof push_length_shot_tag),
             "a push sized message with the shot tag is neither");
    ut_check(!mp_event_is_event(sabre_length_push_tag, sizeof sabre_length_push_tag),
             "nor a sabre sized message with the push tag");
    ut_check(!mp_event_is_event(shot_length_mover_tag, sizeof shot_length_mover_tag),
             "nor a shot sized message with the mover tag");
    ut_check(!mp_event_decode(short_shot, sizeof short_shot, &got), "and none of them decodes");
    ut_check(!mp_event_is_event(NULL, 0), "nor does nothing");

    memset(&bad, 0, sizeof bad);
    bad.kind = 0x7Fu;
    ut_check(mp_event_encode(&bad, buffer, sizeof buffer) == 0u, "an unknown kind does not encode");
    bad.kind = MP_EVENT_PUSH;
    ut_check(mp_event_encode(&bad, buffer, MP_EVENT_PUSH_BYTES - 1u) == 0u,
             "nor does a push into a byte less than it needs");
}

static void check_the_ring(void)
{
    mp_event_queue_t queue;
    mp_event_t       event;
    mp_event_t       out;
    size_t           i;

    ut_section("the ring keeps order and drops the oldest when full");

    mp_event_queue_init(&queue);
    ut_check(!mp_event_queue_pop(&queue, &out), "an empty ring gives nothing");

    memset(&event, 0, sizeof event);
    event.kind = MP_EVENT_SHOT;
    for (i = 0; i < MP_EVENT_QUEUE_SLOTS + 3u; ++i) {
        event.shot_kind = (uint8_t)i;
        mp_event_queue_push(&queue, &event);
    }
    ut_check(queue.count == MP_EVENT_QUEUE_SLOTS, "the ring holds its capacity");
    ut_check(queue.dropped == 3u, "and counted the three it dropped");
    ut_check(mp_event_queue_peek(&queue, &out) && out.shot_kind == 3u,
             "the oldest kept is the fourth pushed");
    for (i = 3; i < MP_EVENT_QUEUE_SLOTS + 3u; ++i) {
        ut_checkf(mp_event_queue_pop(&queue, &out) && out.shot_kind == (uint8_t)i,
                  "event %u pops in order", (unsigned)i);
    }
    ut_check(!mp_event_queue_pop(&queue, &out), "and then the ring is empty again");
}

static void check_body_frame(void)
{
    static const float FORWARD[3] = { 0.0f, 1.0f, 0.0f };
    float matrix[9];
    float world[3];
    float local[3];
    float back[3];
    static const float OFFSET[3] = { 0.3f, 0.5f, 1.2f };

    ut_section("forward is minus sine yaw, cosine yaw, and up is up");

    mp_event_body_matrix(0.0f, 0.0f, 0.0f, matrix);
    mp_event_to_world(matrix, FORWARD, world);
    ut_near(world[0], 0.0, 1e-6, "at yaw zero forward has no x");
    ut_near(world[1], 1.0, 1e-6, "and points along positive y");
    ut_near(world[2], 0.0, 1e-6, "and is level");

    mp_event_body_matrix(0.0f, 90.0f, 0.0f, matrix);
    mp_event_to_world(matrix, FORWARD, world);
    ut_near(world[0], -1.0, 1e-6, "at yaw ninety forward is minus x");
    ut_near(world[1], 0.0, 1e-6, "with no y");

    mp_event_body_matrix(0.0f, 30.0f, 0.0f, matrix);
    mp_event_to_world(matrix, FORWARD, world);
    ut_near(world[0], -0.5, 1e-6, "at yaw thirty forward's x is minus sine thirty");
    ut_near(world[1], sqrt(3.0) / 2.0, 1e-6, "and its y is cosine thirty");

    mp_event_body_matrix(20.0f, 0.0f, 0.0f, matrix);
    mp_event_to_world(matrix, FORWARD, world);
    ut_check(world[2] > 0.3f && world[1] > 0.9f, "a positive pitch raises the forward vector");

    ut_section("the sender's local offset and the puppet's world offset are inverses");

    mp_event_body_matrix(10.0f, 200.0f, -5.0f, matrix);
    mp_event_to_local(matrix, OFFSET, local);
    mp_event_to_world(matrix, local, back);
    ut_near(back[0], OFFSET[0], 1e-5, "x returns through the round trip");
    ut_near(back[1], OFFSET[1], 1e-5, "y returns");
    ut_near(back[2], OFFSET[2], 1e-5, "z returns");
    ut_near(local[0] * local[0] + local[1] * local[1] + local[2] * local[2],
            OFFSET[0] * OFFSET[0] + OFFSET[1] * OFFSET[1] + OFFSET[2] * OFFSET[2], 1e-5,
            "and the length is preserved, the frame is a rotation");

    ut_section("the wrap");

    ut_near(mp_event_wrap360(370.0f), 10.0, 1e-5, "370 is 10");
    ut_near(mp_event_wrap360(-90.0f), 270.0, 1e-5, "minus 90 is 270");
    ut_near(mp_event_wrap360(0.0f), 0.0, 1e-5, "zero stays zero");
}

static void check_the_skin_names(mp_event_t sent);

/* The appearance change, and the one field on this channel whose validation is not tidiness.
 *
 * A receiver hands the name to the engine's resource loader. That loader answers a miss with a
 * silent zero, the bind that follows asserts, and this build's assert handler is a message box and
 * then exit. An unknown name is therefore a closed game rather than a failed swap, so a stranger
 * who can put bytes on this channel must not be able to choose one.
 */
static void check_the_skin_event(void)
{
    mp_event_t sent;
    mp_event_t got;
    uint8_t    buffer[MP_EVENT_MAX_BYTES];
    size_t     bytes;

    ut_section("an appearance change carries a NAME, because no index means the same twice");

    memset(&sent, 0, sizeof sent);
    sent.kind = MP_EVENT_SKIN;
    sent.tick = 4242u;
    sent.skin_kind = MP_SKIN_CHARACTER;
    sent.skin_hero = 3u;
    sent.skin_slot = 2u;
    memcpy(sent.skin_asset, "mace.baf", 8u);

    bytes = mp_event_encode(&sent, buffer, sizeof buffer);
    ut_checkf(bytes == MP_EVENT_SKIN_BYTES, "it encodes to %u bytes", (unsigned)bytes);
    ut_check(bytes == 42u,
             "which is forty two: a tag, a tick, a kind, a hero, a slot, the scale and the "
             "name");
    ut_check(mp_event_decode(buffer, bytes, &got), "and decodes");
    ut_check(got.kind == MP_EVENT_SKIN && got.tick == 4242u, "the tag and the tick came back");
    ut_check(got.skin_kind == MP_SKIN_CHARACTER && got.skin_hero == 3u,
             "and so did the kind and the hero slot");
    ut_check(strcmp(got.skin_asset, "mace.baf") == 0, "and the name is the name");

    ut_section("it names the WORLD SLOT it is for, because a session holds more than two");

    ut_check(got.skin_slot == 2u, "the slot came back");
    sent.skin_slot = (uint8_t)(MP_SNAPSHOT_MAX_BODIES - 1u);
    bytes = mp_event_encode(&sent, buffer, sizeof buffer);
    ut_check(bytes == MP_EVENT_SKIN_BYTES && mp_event_decode(buffer, bytes, &got) &&
             got.skin_slot == MP_SNAPSHOT_MAX_BODIES - 1u,
             "the last slot the snapshot has is taken");
    sent.skin_slot = (uint8_t)MP_SNAPSHOT_MAX_BODIES;
    ut_check(mp_event_encode(&sent, buffer, sizeof buffer) == 0u,
             "and one past it does not encode");
    sent.skin_slot = 0u;
    bytes = mp_event_encode(&sent, buffer, sizeof buffer);
    ut_check(bytes == MP_EVENT_SKIN_BYTES, "slot zero, the listen host's own body, is a slot");
    buffer[7] = (uint8_t)MP_SNAPSHOT_MAX_BODIES;   /* the slot byte, forged past the table */
    ut_check(mp_event_is_event(buffer, bytes), "a forged slot still looks like an event");
    ut_check(!mp_event_decode(buffer, bytes, &got),
             "but does not decode: it would select a body the table does not have");
    sent.skin_slot = 2u;
    bytes = mp_event_encode(&sent, buffer, sizeof buffer);
    (void)bytes;

    ut_section("a model change is not a character change, and the wire keeps them apart");

    sent.skin_kind = MP_SKIN_MODEL;
    bytes = mp_event_encode(&sent, buffer, sizeof buffer);
    ut_check(mp_event_decode(buffer, bytes, &got) && got.skin_kind == MP_SKIN_MODEL,
             "the model change survives as one");
    ut_check(MP_SKIN_CHARACTER != MP_SKIN_MODEL,
             "and the two are different values, which is the whole distinction");
    check_the_skin_names(sent);
}

static void check_the_skin_names(mp_event_t sent)
{
    mp_event_t got;
    uint8_t    buffer[MP_EVENT_MAX_BYTES];
    size_t     bytes;

    ut_section("the name rule is the ENGINE'S 8.3 gate, character for character");

    {
        /* The engine gates every resource request on an 8.3 test before it opens anything: at most
         * one dot, at most eight characters before it, at most three after it, and every other
         * character out of a 63 character alphabet of the letters, the digits and the underscore.
         * A name outside that can never name a resource, so it is refused here rather than carried
         * to a layer where the refusal would come too late to tell anybody.
         *
         * The hyphen is the trap and it is deliberate: the alphabet has none. Hyphenated names do
         * exist in this game's data, but they are clip names inside a .baf and a clip name never
         * reaches the gate. This codec used to allow the hyphen, which made it LOOSER than the
         * engine it protects.
         *
         * The rows the engine ACCEPTS that a stricter rule would have refused are in here on
         * purpose: a dotless name at any length, and a name whose only dot is the first character.
         * Copying the gate means copying what it takes as well as what it throws away. */
        static const struct { const char *name; bool sound; } SHAPES[] = {
            { "obiwan.baf",     true  },   /* the ordinary case */
            { "MACE.BAF",       true  },   /* upper case is in the alphabet */
            { "mace_2.baf",     true  },   /* and so is the underscore */
            { "abcdefgh.baf",   true  },   /* a stem of exactly eight */
            { "abcdefghi.baf",  false },   /* nine, which the gate refuses */
            { "mace.ba",        true  },   /* an extension of two */
            { "mace.",          true  },   /* a dot with nothing after it, which the gate takes */
            { "mace.abcd",      false },   /* an extension of four */
            { "ma.ce.baf",      false },   /* two dots */
            { "stnd-wp1.baf",   false },   /* a hyphen, which is NOT in the alphabet */
            { "mace baf",       false },   /* a space */
            { ".baf",           true  },   /* a leading dot: an empty stem, which the gate takes */
            { "obiwan",         true  },   /* no dot at all, which the gate never length checks */
            { "mace\nbaf",      false },   /* a control character */
            { "..\\..\\windows", false },  /* a path, which is what an attacker reaches for */
            { "",               false },   /* empty: the loader asserts on it like any other miss */
        };
        size_t i;

        for (i = 0; i < sizeof SHAPES / sizeof SHAPES[0]; ++i) {
            memset(sent.skin_asset, 0, sizeof sent.skin_asset);
            memcpy(sent.skin_asset, SHAPES[i].name, strlen(SHAPES[i].name));
            bytes = mp_event_encode(&sent, buffer, sizeof buffer);
            ut_checkf((bytes != 0u) == SHAPES[i].sound, "the encoder %s \"%s\"",
                      SHAPES[i].sound ? "takes" : "refuses", SHAPES[i].name);
            if (SHAPES[i].sound) {
                ut_checkf(mp_event_decode(buffer, bytes, &got) &&
                          strcmp(got.skin_asset, SHAPES[i].name) == 0,
                          "and it survives the round trip: \"%s\"", SHAPES[i].name);
            }
        }
    }

    ut_section("a name this build could not have written is refused on BOTH sides");

    {
        /* The decoder is fed by a stranger, so every shape the encoder throws away has to be
         * thrown away again on the way in. The whole table above is replayed through the decoder
         * by forging the name into a message that is otherwise valid. */
        static const struct { const char *name; bool sound; } WIRE[] = {
            { "obiwan.baf",    true  },
            { "abcdefghi.baf", false },
            { "mace.abcd",     false },
            { "ma.ce.baf",     false },
            { "stnd-wp1.baf",  false },
            { "mace baf",      false },
        };
        uint8_t spoiled[MP_EVENT_MAX_BYTES];
        size_t  name_at = MP_EVENT_SKIN_BYTES - MP_EVENT_ASSET_MAX;
        size_t  i;

        memset(sent.skin_asset, 0, sizeof sent.skin_asset);
        memcpy(sent.skin_asset, "obiwan.baf", 10u);
        bytes = mp_event_encode(&sent, buffer, sizeof buffer);
        ut_check(bytes == MP_EVENT_SKIN_BYTES, "a good message exists to forge from");

        for (i = 0; i < sizeof WIRE / sizeof WIRE[0]; ++i) {
            memcpy(spoiled, buffer, bytes);
            memset(spoiled + name_at, 0, MP_EVENT_ASSET_MAX);
            memcpy(spoiled + name_at, WIRE[i].name, strlen(WIRE[i].name));
            ut_checkf(mp_event_decode(spoiled, bytes, &got) == WIRE[i].sound,
                      "off the wire the decoder %s \"%s\"",
                      WIRE[i].sound ? "takes" : "refuses", WIRE[i].name);
        }
    }

    /* And the decode side, which is the one that matters, because it is fed by a stranger. A valid
     * message is built and then spoiled in the ways a hostile one would be. */
    memset(sent.skin_asset, 0, sizeof sent.skin_asset);
    memcpy(sent.skin_asset, "obiwan.baf", 10u);
    bytes = mp_event_encode(&sent, buffer, sizeof buffer);
    ut_check(bytes == MP_EVENT_SKIN_BYTES, "a good message exists to spoil");

    {
        uint8_t spoiled[MP_EVENT_MAX_BYTES];
        size_t  name_at = MP_EVENT_SKIN_BYTES - MP_EVENT_ASSET_MAX;
        size_t  i;

        memcpy(spoiled, buffer, bytes);
        for (i = 0; i < MP_EVENT_ASSET_MAX; ++i) {
            spoiled[name_at + i] = 'a';         /* no terminator anywhere in the field */
        }
        ut_check(!mp_event_decode(spoiled, bytes, &got),
                 "a name with no terminator is refused rather than read off the end");

        memcpy(spoiled, buffer, bytes);
        spoiled[name_at] = '\0';
        ut_check(!mp_event_decode(spoiled, bytes, &got), "an empty name is refused");

        memcpy(spoiled, buffer, bytes);
        spoiled[name_at + 2u] = '/';
        ut_check(!mp_event_decode(spoiled, bytes, &got),
                 "a path separator inside the name is refused");

        memcpy(spoiled, buffer, bytes);
        spoiled[MP_EVENT_SKIN_BYTES - 1u] = 'x';
        ut_check(!mp_event_decode(spoiled, bytes, &got),
                 "and padding that carries something is refused, so the encoding is canonical");

        memcpy(spoiled, buffer, bytes);
        spoiled[5] = (uint8_t)(MP_SKIN_KIND_MAX + 1u);
        ut_check(!mp_event_decode(spoiled, bytes, &got),
                 "a kind the engine has no arm for is refused");
    }
}


/* Every length the reliable channel carries, counted rather than remembered.
 *
 * The recogniser for each message kind on that channel tests its length together with its first
 * byte, so two kinds may share a length only while their tags differ. The appearance event is
 * forty two bytes, and a message that grows is exactly where a collision gets introduced
 * silently: nothing fails, one kind simply starts being read as another.
 *
 * So the census is here, as a test, and it names its sources rather than repeating numbers. The
 * two shapes that are not a single constant are the ones worth spelling out: the map's digest is
 * eight bytes plus seven per mover, and the campaign bank is five bytes plus whatever delta it
 * carries, so neither can be excluded by length alone. The digest is excluded arithmetically; the
 * bank is excluded by its tag, which is what the section below pins.
 */
static void check_the_channel_lengths(void)
{
    static const struct { const char *what; unsigned bytes; } OTHERS[] = {
        { "the world slot note",       1u },
        { "the lobby note",            MP_LOBBY_BYTES },
        { "a reported hit",            MP_HIT_RELAY_BYTES },
        { "the content fingerprint",   MP_LOBBY_CONTENT_BYTES },
        { "a push",                    MP_EVENT_PUSH_BYTES },
        { "a weapon change",           MP_EVENT_WEAPON_BYTES },
        { "a player hit",              MP_PLAYER_HIT_BYTES },
        { "a sabre action",            MP_EVENT_SABRE_BYTES },
        { "a mover",                   MP_EVENT_MOVER_BYTES },
        { "an empty digest",           MP_WORLD_DIGEST_HEADER_BYTES },
        { "a despawn",                 MP_EVENT_DESPAWN_BYTES },
        { "a pickup claim",            MP_EVENT_PICKUP_BYTES },
        { "a spawn",                   MP_EVENT_SPAWN_BYTES },
        { "a shot",                    MP_EVENT_SHOT_BYTES },
        { "the blackboard",            MP_SCRATCH_WIRE_AI_BYTES },
        { "the lobby setup",           MP_LOBBY_SETUP_BYTES },
        { "an NPC's bolt",             MP_NPC_SHOT_BYTES },
    };
    size_t   i;
    unsigned entries;
    unsigned players;

    ut_section("the appearance event's length collides with nothing else on the channel");

    for (i = 0; i < sizeof OTHERS / sizeof OTHERS[0]; ++i) {
        ut_checkf(OTHERS[i].bytes != MP_EVENT_SKIN_BYTES, "%s is %u bytes, not forty two",
                  OTHERS[i].what, OTHERS[i].bytes);
    }

    /* The digest is a family rather than a length: eight bytes plus seven per mover, so it reaches
     * 8, 15, 22 and so on. Forty two is not on that ladder, and the whole ladder is walked rather
     * than argued about. */
    for (entries = 0; entries <= MP_WORLD_DIGEST_MAX_ENTRIES; ++entries) {
        unsigned digest = MP_WORLD_DIGEST_HEADER_BYTES + entries * MP_WORLD_DIGEST_ENTRY_BYTES;

        ut_checkf(digest != MP_EVENT_SKIN_BYTES,
                  "a digest of %u mover(s) is %u bytes, not forty two", entries, digest);
    }

    /* The roster is a family too: two bytes plus fifty seven per player. It is walked rather than
     * argued about for the same reason as the digest, and for one more: the ladder moves whenever
     * an entry gains a field. */
    for (players = 0; players <= MP_ROSTER_MAX_ENTRIES; ++players) {
        ut_checkf(MP_ROSTER_BYTES_FOR(players) != MP_EVENT_SKIN_BYTES,
                  "a roster of %u player(s) is %u bytes, not forty two", players,
                  (unsigned)MP_ROSTER_BYTES_FOR(players));
    }

    /* The map's state note is twelve bytes plus seven a mover, 12, 19, 26, 33, 40, 47 and so on,
     * which never lands on forty two. Should a later size land on the appearance event's length,
     * the tag is the whole of what separates them, and walking the ladder is what finds it. */
    for (entries = 0; entries <= MP_WORLD_STATE_MAX_ENTRIES; ++entries) {
        unsigned note = MP_WORLD_STATE_HEADER_BYTES + entries * MP_WORLD_STATE_ENTRY_BYTES;

        ut_checkf(note != MP_EVENT_SKIN_BYTES || MP_WORLD_STATE_TAG != MP_EVENT_SKIN,
                  "a map state note of %u mover(s) is %u bytes; were that forty two the tag "
                  "would tell it from an appearance", entries, note);
    }

    /* The campaign bank is the one message on this channel whose length is genuinely free, so it
     * can be forty two and is told apart by its tag alone. That is only safe while the tags
     * differ, and this is the check that says so. */
    ut_check(MP_SCRATCH_TAG_BANK != MP_EVENT_SKIN && MP_SCRATCH_TAG_AI != MP_EVENT_SKIN,
             "the campaign's two tags are not the appearance tag, which is what excludes a bank "
             "delta that happens to be forty two bytes long");

    ut_section("the appearance event is still the largest, and the channel still holds it");

    ut_check(MP_EVENT_MAX_BYTES == MP_EVENT_SKIN_BYTES,
             "the buffer every caller sizes from is the appearance event's own length");
    ut_check(MP_EVENT_MAX_BYTES <= MP_CHANNEL_MESSAGE_BYTES,
             "and it fits in one reliable message, which the channel refuses outright otherwise");
}


/* The pickup claim, and the two values that must never cross.
 *
 * A pickup is an ENMY placement whose shooter class falls in a band the player's contact handler
 * dispatches on. A census of all 2250 placements in the eleven shipped levels finds 177 pickups
 * and none of them in 0x0b or 0x0c, and the retail handler's arm for those two reaches a stack
 * defect. The shipped game can therefore never arrive there. A message off the wire could.
 */
static void check_the_pickup_claim(void)
{
    mp_event_t sent;
    mp_event_t got;
    uint8_t    buffer[MP_EVENT_MAX_BYTES];
    size_t     bytes;
    unsigned   kind;

    ut_section("a pickup claim names the placement and the kind");

    memset(&sent, 0, sizeof sent);
    sent.kind = MP_EVENT_PICKUP;
    sent.tick = 900u;
    sent.source_slot = 3u;
    sent.level_id = 528u;
    sent.actor_index = 77u;
    sent.actor_generation = 1u;
    sent.pickup_kind = 0x0Au;

    bytes = mp_event_encode(&sent, buffer, sizeof buffer);
    ut_checkf(bytes == MP_EVENT_PICKUP_BYTES, "it encodes to %u bytes", (unsigned)bytes);
    ut_check(mp_event_decode(buffer, bytes, &got), "and decodes");
    ut_check(got.kind == MP_EVENT_PICKUP && got.level_id == 528u && got.actor_index == 77u &&
             got.actor_generation == 1u && got.pickup_kind == 0x0Au,
             "with every field back");
    ut_check(got.source_slot == 3u && buffer[5] == 3u,
             "and the claimant's world slot behind the tick, which is whom the grant is for");
    ut_check(MP_EVENT_PICKUP_BYTES == MP_EVENT_SPAWN_BYTES,
             "it shares its length with the spawn, which the tag is what tells apart");
    ut_check(!mp_event_restamp(buffer, bytes, 1u, 5u),
             "and it is no moment a host passes on: the host answers a claim itself");
    buffer[5] = (uint8_t)MP_SNAPSHOT_MAX_BODIES;
    ut_check(!mp_event_decode(buffer, bytes, &got),
             "a claimant past the snapshot's table does not decode");
    buffer[5] = 3u;

    ut_section("the two unplaced kinds are refused on both sides");

    for (kind = 0; kind < 0x30u; ++kind) {
        bool placed = kind >= MP_PICKUP_KIND_MIN && kind <= MP_PICKUP_KIND_MAX &&
                      kind != MP_PICKUP_KIND_UNPLACED_LO && kind != MP_PICKUP_KIND_UNPLACED_HI;

        sent.pickup_kind = (uint8_t)kind;
        bytes = mp_event_encode(&sent, buffer, sizeof buffer);
        ut_checkf((bytes != 0u) == placed, "kind %02X: the encoder %s it", kind,
                  placed ? "takes" : "refuses");
    }

    /* And the decode side, which is the one a stranger feeds. A valid message is spoiled in the
     * one byte that matters. */
    sent.pickup_kind = 0x0Au;
    bytes = mp_event_encode(&sent, buffer, sizeof buffer);
    ut_check(bytes == MP_EVENT_PICKUP_BYTES, "a good message exists to spoil");
    buffer[MP_EVENT_PICKUP_BYTES - 1u] = (uint8_t)MP_PICKUP_KIND_UNPLACED_LO;
    ut_check(!mp_event_decode(buffer, bytes, &got),
             "an unplaced kind off the wire is refused rather than passed to the handler");
    buffer[MP_EVENT_PICKUP_BYTES - 1u] = 0xFFu;
    ut_check(!mp_event_decode(buffer, bytes, &got), "and so is one outside the band");
}

/* The moments of a body name the player who did them, one byte behind the tick, so a client of
 * three others can tell whose shot it is looking at. Four of them are checked here, a player's own
 * sound in mp_events_moments.c. A mover names nobody. */
static void check_the_source_slot(void)
{
    static const uint8_t KINDS[] = { MP_EVENT_SHOT, MP_EVENT_PUSH, MP_EVENT_SABRE,
                                     MP_EVENT_WEAPON };
    mp_event_t sent;
    mp_event_t got;
    uint8_t    buffer[MP_EVENT_MAX_BYTES];
    size_t     bytes;
    size_t     i;

    ut_section("a body's moment names the player who did it, and a mover names nobody");

    for (i = 0; i < sizeof KINDS; ++i) {
        memset(&sent, 0, sizeof sent);
        sent.kind        = KINDS[i];
        sent.tick        = 12u;
        sent.source_slot = (uint8_t)(MP_SNAPSHOT_MAX_BODIES - 1u);
        bytes = mp_event_encode(&sent, buffer, sizeof buffer);
        ut_checkf(bytes != 0u && buffer[5] == sent.source_slot &&
                      mp_event_decode(buffer, bytes, &got) && got.source_slot == sent.source_slot,
                  "kind %02X: the last slot rides behind the tick and comes back",
                  (unsigned)KINDS[i]);
        buffer[5] = (uint8_t)MP_SNAPSHOT_MAX_BODIES;   /* forged one past the table */
        ut_checkf(mp_event_is_event(buffer, bytes) && !mp_event_decode(buffer, bytes, &got),
                  "kind %02X: one past the table looks like an event and does not decode",
                  (unsigned)KINDS[i]);
        sent.source_slot = (uint8_t)MP_SNAPSHOT_MAX_BODIES;
        ut_checkf(mp_event_encode(&sent, buffer, sizeof buffer) == 0u,
                  "kind %02X: and does not encode either", (unsigned)KINDS[i]);
    }

    memset(&sent, 0, sizeof sent);
    sent.kind        = MP_EVENT_MOVER;
    sent.tick        = 12u;
    sent.mover_id    = 3u;
    sent.source_slot = 9u;
    bytes = mp_event_encode(&sent, buffer, sizeof buffer);
    ut_check(bytes == MP_EVENT_MOVER_BYTES && mp_event_decode(buffer, bytes, &got) &&
                 got.source_slot == 0u && got.mover_id == 3u,
             "a mover carries no slot, whatever the struct holds: a door is not a body");
}

/* What a host does to a client's moment before it passes it on: the slot of the peer it came from
 * and the host's own tick written in, nothing else touched, and nothing written into a message
 * that is not a player's moment. */
static void check_the_restamp(void)
{
    mp_event_t sent;
    mp_event_t got;
    uint8_t    buffer[MP_EVENT_MAX_BYTES];
    uint8_t    before[MP_EVENT_MAX_BYTES];
    size_t     bytes;

    ut_section("a host restamps a client's moment with the client's slot and its own tick");

    memset(&sent, 0, sizeof sent);
    sent.kind        = MP_EVENT_SHOT;
    sent.tick        = 100u;
    sent.source_slot = 3u;
    sent.shot_kind   = 11u;
    sent.origin[1]   = 0.5f;
    sent.pitch       = -30.0f;
    sent.yaw         = 200.0f;
    bytes = mp_event_encode(&sent, buffer, sizeof buffer);
    ut_check(mp_event_restamp(buffer, bytes, 1u, 5000u), "a shot is restamped");
    ut_check(mp_event_decode(buffer, bytes, &got) && got.tick == 5000u && got.source_slot == 1u,
             "with the host's tick and the slot of the peer it came from, not the one it named");
    ut_check(got.shot_kind == 11u && got.origin[1] > 0.49f && got.origin[1] < 0.51f &&
                 got.pitch < -29.9f && got.pitch > -30.1f,
             "and the rest as it was sent");

    sent.kind   = MP_EVENT_PUSH;
    sent.charge = 1.0f;
    bytes = mp_event_encode(&sent, buffer, sizeof buffer);
    ut_check(mp_event_restamp(buffer, bytes, 2u, 7u) && mp_event_decode(buffer, bytes, &got) &&
                 got.tick == 7u && got.source_slot == 2u && got.charge == 1.0f,
             "a push the same");

    memset(&sent, 0, sizeof sent);
    sent.kind       = MP_EVENT_MOVER;
    sent.tick       = 100u;
    sent.mover_id   = 258u;
    sent.mover_mode = MP_EVENT_MOVER_OPEN;
    bytes = mp_event_encode(&sent, buffer, sizeof buffer);
    ut_check(mp_event_restamp(buffer, bytes, 2u, 5000u) && mp_event_decode(buffer, bytes, &got) &&
                 got.tick == 5000u && got.mover_id == 258u &&
                 got.mover_mode == MP_EVENT_MOVER_OPEN,
             "a mover takes the host's tick and keeps its id: it has no slot to write");

    memset(&sent, 0, sizeof sent);
    sent.kind      = MP_EVENT_SKIN;
    sent.skin_kind = MP_SKIN_CHARACTER;
    sent.skin_hero = 3u;
    sent.skin_slot = 1u;
    memcpy(sent.skin_asset, "mace.baf", sizeof "mace.baf");
    bytes = mp_event_encode(&sent, buffer, sizeof buffer);
    memcpy(before, buffer, bytes);
    ut_check(bytes == MP_EVENT_SKIN_BYTES && !mp_event_restamp(buffer, bytes, 2u, 5000u) &&
                 memcmp(before, buffer, bytes) == 0,
             "an appearance is not a moment the host passes on, and is left as it was");

    memset(&sent, 0, sizeof sent);
    sent.kind = MP_EVENT_SABRE;
    sent.tick = 100u;
    bytes = mp_event_encode(&sent, buffer, sizeof buffer);
    memcpy(before, buffer, bytes);
    ut_check(!mp_event_restamp(buffer, bytes, (uint8_t)MP_SNAPSHOT_MAX_BODIES, 5000u) &&
                 memcmp(before, buffer, bytes) == 0,
             "a slot past the table is refused and nothing is written");
    buffer[6] = 9u;   /* the action byte, forged past the range */
    memcpy(before, buffer, bytes);
    ut_check(!mp_event_restamp(buffer, bytes, 1u, 5000u) && memcmp(before, buffer, bytes) == 0,
             "and so is a torn moment, which is then not passed on at all");
    ut_check(!mp_event_restamp(NULL, 0u, 1u, 5000u), "and nothing");
}


/* The size a player is drawn at.
 *
 * It rides the appearance event because that event already answers this question, what does this
 * player look like, and because the two developer overlay cheats that set it are a
 * factor composed into the player's own draw matrix every frame, which nothing on a second
 * machine can read. Hundredths of the body's own size, and ZERO means nobody said.
 */
static void check_the_scale_travels(void)
{
    uint8_t    buffer[MP_EVENT_MAX_BYTES];
    mp_event_t sent;
    mp_event_t got;
    size_t     bytes;

    ut_section("an appearance carries how big the body is drawn");

    memset(&sent, 0, sizeof sent);
    sent.kind       = MP_EVENT_SKIN;
    sent.tick       = 77u;
    sent.skin_kind  = MP_SKIN_MODEL;
    sent.skin_hero  = 1u;
    sent.skin_slot  = 1u;
    sent.skin_scale = 300u;   /* the giant, three times its own size */
    memcpy(sent.skin_asset, "obiwan.baf", 10u);

    bytes = mp_event_encode(&sent, buffer, sizeof buffer);
    ut_checkf(bytes == MP_EVENT_SKIN_BYTES, "it encodes to %u bytes", (unsigned)bytes);
    ut_check(mp_event_decode(buffer, bytes, &got), "and decodes");
    ut_checkf(got.skin_scale == 300u, "with the scale it was given (%u)", (unsigned)got.skin_scale);

    sent.skin_scale = 0u;
    bytes = mp_event_encode(&sent, buffer, sizeof buffer);
    ut_check(bytes == MP_EVENT_SKIN_BYTES && mp_event_decode(buffer, bytes, &got) &&
                 got.skin_scale == 0u,
             "nobody said is a value of its own, and the shortest thing a sender can say");

    ut_section("and a factor nobody could have meant is refused rather than drawn");

    sent.skin_scale = (uint16_t)(MP_WIRE_SCALE_MAX + 1u);
    ut_check(mp_event_encode(&sent, buffer, sizeof buffer) == 0u,
             "a body past six and a half times its size is not a thing anybody asked for");
    sent.skin_scale = 1u;
    ut_check(mp_event_encode(&sent, buffer, sizeof buffer) == 0u,
             "and neither is one a hundredth of it");
    sent.skin_scale = MP_WIRE_SCALE_MAX;
    ut_check(mp_event_encode(&sent, buffer, sizeof buffer) == MP_EVENT_SKIN_BYTES,
             "the bounds themselves are inside");
    sent.skin_scale = MP_WIRE_SCALE_MIN;
    ut_check(mp_event_encode(&sent, buffer, sizeof buffer) == MP_EVENT_SKIN_BYTES,
             "at both ends");
}

int main(void)
{
    check_shot_round_trip();
    check_push_round_trip();
    check_sabre_round_trip();
    check_weapon_round_trip();
    check_mover_round_trip();
    check_the_source_slot();
    check_the_restamp();
    check_telling_events_apart();
    check_the_ring();
    check_body_frame();
    check_the_skin_event();
    check_the_channel_lengths();
    check_the_pickup_claim();
    check_the_scale_travels();

    return ut_summary("mp_events");
}
