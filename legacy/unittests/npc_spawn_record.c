/* npc_spawn_record.c: the record a spawned copy is raised from, checked without the game.
 *
 * What would be silent if it were wrong: a route the engine reads past, a copy the engine removes
 * the first time the player walks off, a copy the scan's own filter skips, a helper raised as an
 * enemy, a bazooka firing at a blaster's rate, and the live word of the level's own actor left in
 * a copy's record.
 */
#include "unittest.h"

#include "npc_census.h"
#include "npc_spawn_record.h"
#include "spawn_scripts.h"

#include <string.h>

static uint8_t s_record[NPC_SPAWN_RECORD_BYTES];

static uint32_t word_at(uint32_t offset)
{
    uint32_t value;

    memcpy(&value, s_record + offset, sizeof value);
    return value;
}

static float float_at(uint32_t offset)
{
    float value;

    memcpy(&value, s_record + offset, sizeof value);
    return value;
}

static void put_word(uint32_t offset, uint32_t value)
{
    memcpy(s_record + offset, &value, sizeof value);
}

/* A source placement as the census reads it: every byte set, so a field left alone shows. */
static void source(void)
{
    memset(s_record, 0xA5, sizeof s_record);
    put_word(PLACE_FLAGS, 0x00000040u);
    put_word(PLACE_HIT_POINTS, 25u);
    put_word(PLACE_LIVE_ACTOR, 0x00C0FFEEu);    /* the level's own actor */
    put_word(PLACE_SPAWN_STATE, 1u);
    put_word(PLACE_REVEAL_COUNT, 3u);
    put_word(PLACE_MOVE_MODE, 0u);
    memset(s_record + PLACE_WEAPON_KINDS, 0, 2);
}

static npc_spawn_desc_t desc_of(const char *file, uint8_t behaviour)
{
    npc_spawn_desc_t d;

    memset(&d, 0, sizeof d);
    d.source    = 7u;
    d.behaviour = behaviour;
    memcpy(d.file, file, strlen(file) + 1u);
    return d;
}

int main(void)
{
    static const float AT[3] = { 10.5f, -3.25f, 2.0f };
    npc_spawn_desc_t   d;
    float              f;

    ut_section("the record's shape");
    ut_check(NPC_SPAWN_RECORD_BYTES == 0xFCu && NPC_SPAWN_RECORD_ROUTE == 0xE8u &&
                 NPC_SPAWN_RECORD_ROUTE_NODE == 0xECu,
             "0xFC bytes, the smallest a level's own record is, with its route count at +0xE8 "
             "and node 0 at +0xEC");

    ut_section("a copy settled over its source");
    source();
    d = desc_of("ddroid.baf", SPAWN_BEHAVIOUR_ATTACK);
    npc_spawn_record_settle(s_record, &d, AT, 90.0f, false);
    ut_check(memcmp(s_record + PLACE_POSITION, AT, sizeof AT) == 0 &&
                 float_at(PLACE_START_YAW) == 90.0f,
             "it stands where it was asked to and faces as asked");
    ut_check(word_at(PLACE_LIVE_ACTOR) == 0u && word_at(PLACE_SPAWN_STATE) == 0u &&
                 word_at(PLACE_REVEAL_COUNT) == 0u && word_at(PLACE_START_MODE) == 0u,
             "and carries no live word of the level's actor, no spawn state, no reveals");
    ut_check(word_at(PLACE_FLAGS) == 0x41u, "the scan's own flag bit is set, the rest kept");
    ut_check(word_at(PLACE_DESPAWN_RANGE) == 0u,
             "no removal by distance: its record is in no directory the scan brings it back from");
    ut_check(word_at(PLACE_ACTIVATION_RANGE) == 0xA5A5A5A5u,
             "the activation radius is the source's: nothing scans a copy's record");
    {
        uint8_t zero[NPC_SPAWN_RECORD_ROUTE - NPC_SPAWN_RECORD_REVEALS];

        memset(zero, 0, sizeof zero);
        ut_check(memcmp(s_record + NPC_SPAWN_RECORD_REVEALS, zero, sizeof zero) == 0,
                 "the reveal list is empty");
    }
    ut_check(word_at(NPC_SPAWN_RECORD_ROUTE) == 1u &&
                 memcmp(s_record + NPC_SPAWN_RECORD_ROUTE_NODE, AT, sizeof AT) == 0 &&
                 word_at(NPC_SPAWN_RECORD_ROUTE_NODE + 12u) == 0u,
             "a route of one node, the spot it stands on, and the word nothing reads is 0");
    ut_check((int32_t)word_at(PLACE_CLASS) == 2, "an attacker is class 2, the one a hit reaches");
    ut_check(word_at(PLACE_HIT_POINTS) == 25u, "its hit points are its source's");

    ut_section("the class follows the file and the behaviour");
    source();
    d = desc_of("ddroid.baf", SPAWN_BEHAVIOUR_HELP);
    npc_spawn_record_settle(s_record, &d, AT, 0.0f, false);
    ut_check((int32_t)word_at(PLACE_CLASS) == 1, "a helper is class 1, the player's own side");
    source();
    d = desc_of("TRIPOD.BAF", SPAWN_BEHAVIOUR_HELP);
    npc_spawn_record_settle(s_record, &d, AT, 0.0f, false);
    ut_check((int32_t)word_at(PLACE_CLASS) == 4,
             "the tripod gun is class 4 whatever its behaviour, the class the Use key mounts, "
             "and the name is compared without case");
    source();
    d = desc_of("pwrhlth1.baf", SPAWN_BEHAVIOUR_ATTACK);
    npc_spawn_record_settle(s_record, &d, AT, 0.0f, false);
    ut_check((int32_t)word_at(PLACE_CLASS) == 14,
             "the large medpack is raised as class 14, its pickup code, whatever its behaviour");
    source();
    d = desc_of("PWRBACTA.BAF", SPAWN_BEHAVIOUR_HELP);
    npc_spawn_record_settle(s_record, &d, AT, 0.0f, false);
    ut_check((int32_t)word_at(PLACE_CLASS) == 13,
             "and the small one as 13, a pickup's class winning over a helper's, case aside");

    ut_section("hit points and weapons");
    source();
    put_word(PLACE_HIT_POINTS, 0u);
    d = desc_of("ddroid.baf", SPAWN_BEHAVIOUR_ATTACK);
    npc_spawn_record_settle(s_record, &d, AT, 0.0f, true);
    ut_check((int32_t)word_at(PLACE_HIT_POINTS) == PLACE_HIT_POINTS_LEAST,
             "a source with no hit points gets a few");
    {
        uint16_t weapon = 0;

        memcpy(&weapon, s_record + PLACE_WEAPON_KINDS, sizeof weapon);
        ut_check(weapon == PLACE_SHOT_KIND_BLASTER, "a shooter with no first weapon fires bolts");
    }
    source();
    {
        uint16_t rocket = 8u;
        float    fast   = 0.5f;

        memcpy(s_record + PLACE_WEAPON_KINDS, &rocket, sizeof rocket);
        memcpy(s_record + PLACE_FIRE_INTERVAL, &fast, sizeof fast);
    }
    npc_spawn_record_settle(s_record, &d, AT, 0.0f, true);
    f = float_at(PLACE_FIRE_INTERVAL);
    ut_check(f == 4.0f, "a rocket waits at least four seconds between shots");
    source();
    {
        uint16_t rocket = 8u;
        float    fast   = 0.5f;

        memcpy(s_record + PLACE_WEAPON_KINDS, &rocket, sizeof rocket);
        memcpy(s_record + PLACE_FIRE_INTERVAL, &fast, sizeof fast);
    }
    npc_spawn_record_settle(s_record, &d, AT, 0.0f, false);
    ut_check(float_at(PLACE_FIRE_INTERVAL) == 0.5f,
             "a copy that does not shoot keeps its interval");

    ut_section("which records fly");
    source();
    put_word(PLACE_MOVE_MODE, 2u);
    ut_check(npc_spawn_record_flies(s_record), "mode 2 hovers");
    put_word(PLACE_MOVE_MODE, 5u);
    ut_check(npc_spawn_record_flies(s_record), "modes 4 to 6 fly");
    put_word(PLACE_MOVE_MODE, 3u);
    ut_check(!npc_spawn_record_flies(s_record), "mode 3 walks without collision and does not fly");
    put_word(PLACE_MOVE_MODE, 0u);
    ut_check(!npc_spawn_record_flies(s_record), "and mode 0 walks");

    return ut_summary("npc_spawn_record");
}
