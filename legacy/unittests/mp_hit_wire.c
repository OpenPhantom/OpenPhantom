/* mp_hit_wire.c: the two hit messages and the enemy key they now carry.
 *
 * What would be silent if it were wrong: a key written as one byte arrives as another enemy, and
 * a copy an editor spawned past 255 would be hit, removed or named as an attacker under the index
 * of a placement that happens to share its low byte. The removal carries the same key, so it is
 * pinned here with them.
 */
#include "unittest.h"

#include "mp_events.h"
#include "mp_hit_wire.h"
#include "mp_node_map.h"
#include "mp_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void check_the_keys(void)
{
    ut_section("the key names a placement below 256 and a copy from 256, and nothing else");
    ut_check(mp_wire_key_is_placement(0u) && mp_wire_key_is_placement(254u) &&
                 mp_wire_key_is_placement(255u),
             "every index a level can hold is a placement");
    ut_check(!mp_wire_key_is_placement(256u) && mp_wire_key_is_copy(256u),
             "256 is the first copy and no placement");
    ut_check(mp_wire_key_is_copy(MP_WIRE_KEY_COUNT - 1u) &&
                 !mp_wire_key_is_copy(MP_WIRE_KEY_COUNT),
             "the copies end where the actor pool does");
    ut_check(!mp_wire_key_is_copy(255u) && !mp_wire_key_is_placement(MP_WIRE_KEY_NONE) &&
                 !mp_wire_key_is_copy(MP_WIRE_KEY_NONE),
             "the key for nobody is neither");
}

static void check_the_reported_hit(void)
{
    mp_hit_note_t in;
    mp_hit_note_t out;
    uint8_t       bytes[MP_HIT_RELAY_BYTES + 4u];
    size_t        n;

    ut_section("a reported hit carries its victim's key whole");
    memset(&in, 0, sizeof in);
    in.victim = 300u;
    in.code   = 0x29u;
    in.impact = 7u;
    in.flags  = (uint8_t)(MP_HIT_FLAG_B | MP_HIT_FLAG_ARMED);
    n = mp_hit_note_encode(&in, bytes, sizeof bytes);
    ut_checkf(n == MP_HIT_RELAY_BYTES, "a hit on a copy encodes to %u bytes", (unsigned)n);
    ut_check(mp_hit_note_is(bytes, n) && mp_hit_note_decode(bytes, n, &out) &&
                 out.victim == 300u && out.code == 0x29u && out.impact == 7u &&
                 out.flags == in.flags,
             "and decodes to the copy it was, not to placement 44");
    in.victim = 12u;
    n = mp_hit_note_encode(&in, bytes, sizeof bytes);
    ut_check(n == MP_HIT_RELAY_BYTES && mp_hit_note_decode(bytes, n, &out) && out.victim == 12u,
             "a hit on a placement travels the same way");

    in.victim = (uint16_t)MP_WIRE_KEY_COUNT;
    ut_check(mp_hit_note_encode(&in, bytes, sizeof bytes) == 0u,
             "a key past every enemy is not written");
    in.victim = 12u;
    in.flags  = 0x80u;
    ut_check(mp_hit_note_encode(&in, bytes, sizeof bytes) == 0u,
             "a flag this build does not know is not written");
    in.flags = 0u;
    n = mp_hit_note_encode(&in, bytes, sizeof bytes);
    bytes[5] = 0x40u;
    ut_check(!mp_hit_note_decode(bytes, n, &out), "nor read");
    bytes[5] = 0u;
    bytes[1] = 0xFFu;
    bytes[2] = 0xFFu;
    ut_check(!mp_hit_note_decode(bytes, n, &out), "nobody is no victim");
    ut_check(!mp_hit_note_decode(bytes, n - 1u, &out) && !mp_hit_note_is(bytes, n + 1u),
             "a length that is not its own is somebody else's message");
}

static void check_the_player_hit(void)
{
    mp_player_hit_note_t in;
    mp_player_hit_note_t out;
    uint8_t              bytes[MP_PLAYER_HIT_BYTES];
    size_t               n;

    ut_section("a player hit names its attacker by key, or nobody");
    memset(&in, 0, sizeof in);
    in.slot     = 2u;
    in.attacker = 257u;
    in.code     = 3u;
    in.impact   = 0x10u;
    in.flags    = (uint8_t)MP_HIT_FLAG_A;
    n = mp_player_hit_encode(&in, bytes, sizeof bytes);
    ut_checkf(n == MP_PLAYER_HIT_BYTES, "a hit by a copy encodes to %u bytes", (unsigned)n);
    ut_check(mp_player_hit_is(bytes, n) && mp_player_hit_decode(bytes, n, &out) &&
                 out.slot == 2u && out.attacker == 257u && out.code == 3u &&
                 out.impact == 0x10u && out.flags == (uint8_t)MP_HIT_FLAG_A,
             "and decodes to the same copy");
    in.attacker = (uint16_t)MP_PLAYER_HIT_NO_ATTACKER;
    n = mp_player_hit_encode(&in, bytes, sizeof bytes);
    ut_check(n == MP_PLAYER_HIT_BYTES && mp_player_hit_decode(bytes, n, &out) &&
                 out.attacker == (uint16_t)MP_PLAYER_HIT_NO_ATTACKER,
             "a hit with nobody behind it still travels");
    in.attacker = 255u;
    n = mp_player_hit_encode(&in, bytes, sizeof bytes);
    ut_check(n == MP_PLAYER_HIT_BYTES && mp_player_hit_decode(bytes, n, &out) &&
                 out.attacker == 255u,
             "placement 255 is a placement, not the mark for nobody");
    in.attacker = 5000u;
    ut_check(mp_player_hit_encode(&in, bytes, sizeof bytes) == 0u,
             "a key that names nothing is not written");
}

static void check_the_removal(void)
{
    mp_event_t in;
    mp_event_t out;
    uint8_t    bytes[MP_EVENT_MAX_BYTES];
    size_t     n;

    ut_section("a removal carries the key, a spawn and a pickup stay placements");
    memset(&in, 0, sizeof in);
    in.kind             = MP_EVENT_DESPAWN;
    in.tick             = 99u;
    in.level_id         = 57u;
    in.actor_index      = 300u;
    in.actor_generation = 4u;
    in.actor_reason     = (uint8_t)MP_EVENT_REMOVE_DELETED;
    n = mp_event_encode(&in, bytes, sizeof bytes);
    ut_checkf(n == MP_EVENT_DESPAWN_BYTES, "a removal of a copy encodes to %u bytes",
              (unsigned)n);
    memset(&out, 0, sizeof out);
    ut_check(mp_event_decode(bytes, n, &out) && out.kind == MP_EVENT_DESPAWN &&
                 out.actor_index == 300u && out.actor_generation == 4u &&
                 out.level_id == 57u,
             "and decodes to the copy it removed");
    in.actor_index = (uint16_t)MP_WIRE_KEY_COUNT;
    ut_check(mp_event_encode(&in, bytes, sizeof bytes) == 0u,
             "a removal of a key past every enemy is not written");

    in.kind        = MP_EVENT_PICKUP;
    in.actor_index = 300u;
    in.pickup_kind = 12u;
    ut_check(mp_event_encode(&in, bytes, sizeof bytes) == 0u,
             "a copy is never a pickup, and a claim on one is not written");
    in.kind        = MP_EVENT_SPAWN;
    ut_check(mp_event_encode(&in, bytes, sizeof bytes) == 0u,
             "nor a spawn of one: a copy is not the level's to wake");
}


/* ==============================================================================================
 * The struck node, which is why both hit messages grew a byte at wire version 28.
 * ============================================================================================ */
static void check_the_struck_node(void)
{
    static const char *const NAMES[] = { "waist", "head", "chest", "ruparm", "sabreblade1" };
    uint8_t              bytes[MP_EVENT_MAX_BYTES];
    mp_hit_note_t        in;
    mp_hit_note_t        out;
    mp_player_hit_note_t p_in;
    mp_player_hit_note_t p_out;

    ut_section("naming a node, which is a comparison and nothing else");
    ut_check(mp_node_map_match("waist", NAMES, 5u) == 0u, "the first name is id zero");
    ut_check(mp_node_map_match("ruparm", NAMES, 5u) == 3u, "and an upper arm is its own id");
    ut_check(mp_node_map_match("sabreblad", NAMES, 5u) == MP_NODE_ID_NONE,
             "a prefix is not a name: the terminator is part of the comparison");
    ut_check(mp_node_map_match("SABREBLADE1", NAMES, 5u) == MP_NODE_ID_NONE,
             "and the comparison is not case blind, because the models are not");
    ut_check(mp_node_map_match("lthigh", NAMES, 5u) == MP_NODE_ID_NONE,
             "a joint the table has no word for is no node rather than a wrong one");
    ut_check(mp_node_map_match(NULL, NAMES, 5u) == MP_NODE_ID_NONE, "no name is no node");
    ut_check(mp_node_map_match("", NAMES, 5u) == MP_NODE_ID_NONE, "and neither is an empty one");
    ut_check(mp_node_map_match("waist", NULL, 5u) == MP_NODE_ID_NONE, "and neither is no table");

    ut_section("the reported hit carries it there and back");
    memset(&in, 0, sizeof in);
    in.victim = 3u;
    in.code   = 0x21u;
    in.impact = 7u;
    in.flags  = MP_HIT_FLAG_A | MP_HIT_FLAG_ARMED;
    in.node   = 26u;
    ut_check(mp_hit_note_encode(&in, bytes, sizeof bytes) == MP_HIT_RELAY_BYTES,
             "a reported hit with a node is seven bytes");
    ut_check(mp_hit_note_decode(bytes, MP_HIT_RELAY_BYTES, &out) && out.node == 26u,
             "and the node it names comes back");
    ut_check(!mp_hit_note_decode(bytes, MP_HIT_RELAY_BYTES - 1u, &out),
             "a build on the older length is not read as a shorter note");

    in.node = (uint8_t)MP_NODE_ID_NONE;
    ut_check(mp_hit_note_encode(&in, bytes, sizeof bytes) == MP_HIT_RELAY_BYTES &&
                 mp_hit_note_decode(bytes, MP_HIT_RELAY_BYTES, &out) &&
                 out.node == (uint8_t)MP_NODE_ID_NONE,
             "and a contact that carried no node says so rather than saying nothing");

    in.node = (uint8_t)(MP_NODE_NAME_COUNT);
    ut_check(mp_hit_note_encode(&in, bytes, sizeof bytes) == MP_HIT_RELAY_BYTES &&
                 !mp_hit_note_decode(bytes, MP_HIT_RELAY_BYTES, &out),
             "an id past the table is refused rather than resolved against whatever it names here");

    ut_section("the player hit carries it too, and it is the direction the field measured");
    memset(&p_in, 0, sizeof p_in);
    p_in.slot     = 1u;
    p_in.attacker = (uint16_t)MP_PLAYER_HIT_NO_ATTACKER;
    p_in.code     = 0x21u;
    p_in.impact   = 4u;
    p_in.flags    = MP_HIT_FLAG_A;
    p_in.node     = 1u;
    ut_check(mp_player_hit_encode(&p_in, bytes, sizeof bytes) == MP_PLAYER_HIT_BYTES,
             "a player hit with a node is eight bytes");
    ut_check(mp_player_hit_decode(bytes, MP_PLAYER_HIT_BYTES, &p_out) && p_out.node == 1u,
             "and the head it names is still the head on the other side");
    p_in.node = 200u;
    ut_check(mp_player_hit_encode(&p_in, bytes, sizeof bytes) == MP_PLAYER_HIT_BYTES &&
                 mp_player_hit_decode(bytes, MP_PLAYER_HIT_BYTES, &p_out) && p_out.node == 200u,
             "an id inside the table is carried whatever it names");
}

int main(void)
{
    check_the_keys();
    check_the_reported_hit();
    check_the_player_hit();
    check_the_removal();
    check_the_struck_node();
    return ut_summary("mp_hit_wire");
}
