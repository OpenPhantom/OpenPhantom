/* mp_body.c: the body module in a process with no game.
 *
 * Almost all of this module is engine choreography: the dispatcher runs on the scheduler's thread,
 * the shot hull rewrites an object field. What a test without a game can pin is the same claim
 * every engine-facing module here makes, which is that with nothing resolved every entry point is
 * a clean refusal, nothing is dereferenced, and the co-op class constants are the values the pair
 * filter reasoning depends on.
 */
#include "unittest.h"

#include "mp_bank.h"
#include "mp_body.h"
#include "mp_body_death.h"
#include "mp_body_internal.h"
#include "mp_contact_rule.h"
#include "mp_lobby.h"
#include "mp_rules.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static void check_class_band(void)
{
    ut_section("the co-op classes are the values the pair filter needs");

    ut_check(MP_BODY_CLASS_PLAYER0 == 1, "player 0 keeps the retail class");
    ut_check(MP_BODY_CLASS_PLAYER1 >= 1 && MP_BODY_CLASS_PLAYER1 < 10,
             "player 1's class is inside the solid body band");
    ut_check(MP_BODY_CLASS_PLAYER2 >= 1 && MP_BODY_CLASS_PLAYER2 < 10, "so is player 2's");
    ut_check(MP_BODY_CLASS_PLAYER3 >= 1 && MP_BODY_CLASS_PLAYER3 < 10, "so is player 3's");

    ut_check(MP_BODY_CLASS_PLAYER1 != 2 && MP_BODY_CLASS_PLAYER2 != 2 &&
             MP_BODY_CLASS_PLAYER3 != 2, "none is the enemy class 2");
    ut_check(MP_BODY_CLASS_PLAYER1 != 9 && MP_BODY_CLASS_PLAYER2 != 9 &&
             MP_BODY_CLASS_PLAYER3 != 9, "none is the ally class 9");

    ut_check(MP_BODY_CLASS_PLAYER0 != MP_BODY_CLASS_PLAYER1 &&
             MP_BODY_CLASS_PLAYER1 != MP_BODY_CLASS_PLAYER2 &&
             MP_BODY_CLASS_PLAYER2 != MP_BODY_CLASS_PLAYER3 &&
             MP_BODY_CLASS_PLAYER0 != MP_BODY_CLASS_PLAYER2 &&
             MP_BODY_CLASS_PLAYER0 != MP_BODY_CLASS_PLAYER3 &&
             MP_BODY_CLASS_PLAYER1 != MP_BODY_CLASS_PLAYER3,
             "all four classes are distinct, so every player pair passes the filter");
}

static void check_without_a_game(void)
{
    ut_section("in a process with no game");

    ut_check(!mp_body_install(), "with no cells resolved the install refuses");
    ut_check(!mp_body_installed(), "and the module does not claim readiness");

    mp_body_arm_dispatcher();
    ut_check(mp_body_contacts_total() == 0, "arming without an install counts nothing");
    ut_check(mp_body_contacts_other() == 0, "and touches nothing");

    mp_body_set_second_is_puppet(true);
    ut_check(mp_body_contacts_suppressed() == 0,
             "declaring the second body a puppet suppresses nothing by itself");
    ut_check(!mp_body_collision_restore_at(1u),
             "restoring the collision of a body that was never spawned is refused");
    mp_body_set_second_is_puppet(false);
}

/* The body is a field of three, one per far bank. The classes the header names are the
 * bank's own rule, and every index-addressed door refuses cleanly without a game. */
static void check_the_field_of_bodies(void)
{
    size_t index;

    ut_section("three far bodies, addressed by their bank");

    ut_check(mp_bank_class_of(1u) == MP_BODY_CLASS_PLAYER1 &&
                 mp_bank_class_of(2u) == MP_BODY_CLASS_PLAYER2 &&
                 mp_bank_class_of(3u) == MP_BODY_CLASS_PLAYER3,
             "the three co-op classes are what the bank's rule answers for banks 1, 2 and 3");
    for (index = 0u; index <= 4u; ++index) {
        ut_checkf(!mp_body_exists_at(index), "no body exists at index %u without a game",
                  (unsigned)index);
        ut_checkf(mp_body_hero_at(index) == -1, "and index %u wears no hero", (unsigned)index);
        ut_checkf(!mp_body_collision_restore_at(index),
                  "and index %u has no collision to restore", (unsigned)index);
    }
    mp_body_spawn_at(2u);
    mp_body_tick_at(3u);
    mp_body_note_revived_at(1u);
    ut_check(!mp_body_exists_at(2u) && !mp_body_exists_at(3u),
             "a spawn or a tick without an install changes nothing");
    (void)mp_body_teardown_at(2u);
    ut_check(mp_body_far_at(2u) != NULL && mp_body_far_at(2u)->serial == 0u,
             "and neither a refused spawn nor a refused take down counts a body serial: the "
             "serial tells the overlay which body it is, and only one that stood or went is one");
    ut_check(!mp_body_second_exists() && mp_body_hero_at(1u) == -1,
             "and the names that mean bank 1 answer as bank 1 does");
}

/* The attribution's doors. What the dispatcher does with them needs the engine, but two claims a
 * test can hold are the ones the whole report rests on: the class a bank's body carries is unique
 * to that bank, which is what lets a reader turn an attacker's class back into a bank, and the
 * doors that carry the slots and the listener are safe before anything is installed. */
static void check_the_attribution(void)
{
    size_t index;

    ut_section("who died and who did it");

    ut_check(mp_bank_class_of(0u) == MP_BODY_CLASS_PLAYER0,
             "bank 0's body carries the retail class, which is what names the local player as the "
             "attacker behind his own weapon");
    for (index = 1u; index <= 3u; ++index) {
        ut_checkf(mp_bank_class_of(index) == (int32_t)(4u + index),
                  "bank %u's body carries class %u, so its class names it and no other",
                  (unsigned)index, (unsigned)(4u + index));
    }

    mp_body_set_death_listener(NULL);
    mp_body_set_bank_slot(0u, 0u);
    mp_body_set_bank_slot(1u, 1u);
    mp_body_set_bank_slot(9999u, 7u);   /* no bank: written nowhere rather than past the array */
    {
        uint8_t slot = 0xFFu;

        ut_check(mp_body_bank_slot(1u, &slot) && slot == 1u,
                 "bank 1 reads back the slot it was told");
        mp_body_set_bank_slot(2u, 3u);
        ut_check(mp_body_bank_slot(2u, &slot) && slot == 3u,
                 "and bank 2 the slot IT was told, which is not its own number on a client");
        ut_check(!mp_body_bank_slot(3u, &slot) && !mp_body_bank_slot(9999u, &slot) &&
                     !mp_body_bank_slot(1u, NULL),
                 "a bank nobody set, one that does not exist and nowhere to put it answer "
                 "false, so a hit for a far body is never addressed to a slot made up here");
    }
    ut_check(mp_body_deaths_seen() == 0u && mp_body_deaths_reported() == 0u,
             "no contact has been delivered, so no death has been seen and none reported");
    mp_body_report("a test process");
    ut_check(!mp_body_installed(),
             "the slots, the listener and the report on an uninstalled module touch nothing");
}

/* ==============================================================================================
 * The friendly fire gate, asked from both sides.
 *
 * The gate reads the sender of a contact off a cell the engine writes, which a test process does
 * not have, so the ordering is driven through the door that takes the sender's bank as an
 * argument. The gate this stands in for is the bridge's: it hands the rule THIS side's team and
 * the team of the slot it was given, whichever of the two swung.
 * ============================================================================================ */

#define OWN_SLOT  3u
#define PEER_SLOT 5u
#define FAR_BANK  1u

static mp_rules_t rules_under_test;
static uint8_t    mode_under_test;
static uint8_t    own_team;
static uint8_t    peer_team;
static uint8_t    gate_asked_about;
static unsigned   gate_calls;
static bool       everything_is_an_ally;

static bool test_gate(uint8_t peer_slot)
{
    gate_asked_about = peer_slot;
    ++gate_calls;
    return mp_rules_may_damage(&rules_under_test, mode_under_test, own_team,
                               peer_slot == (uint8_t)PEER_SLOT ? peer_team : own_team);
}

static bool test_ally(void)
{
    return everything_is_an_ally;
}

static void check_the_gates_arms(void)
{
    ut_section("every arm of the gate, one at a time");

    mp_body_set_bank_slot(0u, (uint8_t)OWN_SLOT);
    mp_body_set_bank_slot(FAR_BANK, (uint8_t)PEER_SLOT);
    mp_body_set_bank_slot(2u, (uint8_t)PEER_SLOT + 1u);
    mp_body_set_damage_gate(&test_gate);
    mp_body_death_set_ally_test(&test_ally);
    everything_is_an_ally = false;
    mp_rules_default(&rules_under_test);
    mode_under_test = (uint8_t)MP_LOBBY_MODE_COOP;
    own_team        = (uint8_t)MP_LOBBY_TEAM_NONE;
    peer_team       = (uint8_t)MP_LOBBY_TEAM_NONE;

    gate_calls = 0u;
    ut_check(mp_body_death_judge_contact(-1, 0u) == MP_CONTACT_ALLOWED &&
                 mp_body_death_judge_contact(-1, FAR_BANK) == MP_CONTACT_ALLOWED,
             "a sender behind no bank is an enemy, a mover or the world, and is never refused");
    ut_check(mp_body_death_judge_contact(0, 0u) == MP_CONTACT_ALLOWED &&
                 mp_body_death_judge_contact(2, 2u) == MP_CONTACT_ALLOWED,
             "a body touched by its own weapon is not a pair of players");
    ut_check(mp_body_death_judge_contact(0, 99u) == MP_CONTACT_ALLOWED,
             "a receiver in no bank at all is not judged either");
    ut_check(gate_calls == 0u, "none of those three arms asks the rule set anything");

    /* Two far bodies, one client's player touching another's on the host. This check once read
     * `allowed` here, and the puppet branch took that as a hit to report: the victim ran the
     * report with no gate, and two clients hurt each other with friendly fire off. */
    ut_check(mp_body_death_judge_contact(2, FAR_BANK) == MP_CONTACT_NOT_OURS,
             "two far bodies belong to the machines they stand for, not to this one");
    ut_check(!mp_contact_rule_puppet_reports(mp_body_death_judge_contact(2, FAR_BANK)),
             "so the puppet branch reports no hit and shows no hurt for them");
    ut_check(mp_contact_rule_carries_out(mp_body_death_judge_contact(2, FAR_BANK)),
             "and a routed delivery, which reports nothing to anybody, runs as it always did");
    ut_check(gate_calls == 0u, "and the rule set is not asked about them either");

    everything_is_an_ally = true;
    ut_check(mp_body_death_judge_contact(0, FAR_BANK) == MP_CONTACT_REFUSED &&
                 mp_body_death_judge_contact(FAR_BANK, 0u) == MP_CONTACT_REFUSED,
             "an ally's contact is refused before the rule set is asked, in both directions");
    everything_is_an_ally = false;

    mp_body_set_damage_gate(NULL);
    ut_check(mp_body_death_judge_contact(0, FAR_BANK) == MP_CONTACT_ALLOWED &&
                 mp_body_death_judge_contact(FAR_BANK, 0u) == MP_CONTACT_ALLOWED,
             "with no gate wired nothing is refused, because a refusal nobody chose is not one");
    mp_body_set_damage_gate(&test_gate);
    ut_check(mp_body_death_judge_contact(0, 3u) == MP_CONTACT_ALLOWED &&
                 mp_body_death_judge_contact(3, 0u) == MP_CONTACT_ALLOWED,
             "and neither is a contact whose far body nobody has given a world slot");

    gate_calls = 0u;
    ut_check(mp_body_death_judge_contact(0, FAR_BANK) == MP_CONTACT_REFUSED,
             "co-op with friendly fire off refuses this player's own contact on a far one");
    ut_check(gate_asked_about == (uint8_t)PEER_SLOT && gate_calls == 1u,
             "and the gate is asked once, about the FAR player's slot");
    gate_calls = 0u;
    ut_check(mp_body_death_judge_contact(FAR_BANK, 0u) == MP_CONTACT_REFUSED,
             "and the same contact arriving on this player from the far one");
    ut_check(gate_asked_about == (uint8_t)PEER_SLOT && gate_calls == 1u,
             "asked about the same far slot, which is what makes one question serve both sides");
}

/* The test the whole design stands on. A build that gates only the direction in which this machine
 * is the attacker passes every other check here and leaves a client hurt by its host, so the two
 * directions are asked side by side for every combination of game, teams and the switch. */
static void check_the_gate_is_symmetric(void)
{
    const uint8_t MODES[] = { (uint8_t)MP_LOBBY_MODE_COOP, (uint8_t)MP_LOBBY_MODE_TDM };
    const uint8_t TEAMS[] = { (uint8_t)MP_LOBBY_TEAM_NONE, 1u, 2u };
    unsigned      differ = 0u;
    unsigned      pairs = 0u;
    unsigned      refused = 0u;
    size_t        m;
    size_t        a;
    size_t        v;
    size_t        f;

    ut_section("the gate answers the same from either side of the same pair");

    mp_body_set_bank_slot(0u, (uint8_t)OWN_SLOT);
    mp_body_set_bank_slot(FAR_BANK, (uint8_t)PEER_SLOT);
    mp_body_set_damage_gate(&test_gate);
    everything_is_an_ally = false;
    for (m = 0; m < sizeof MODES / sizeof MODES[0]; ++m) {
        for (a = 0; a < sizeof TEAMS / sizeof TEAMS[0]; ++a) {
            for (v = 0; v < sizeof TEAMS / sizeof TEAMS[0]; ++v) {
                for (f = 0; f < 2u; ++f) {
                    bool attacking;
                    bool attacked;

                    mp_rules_default(&rules_under_test);
                    rules_under_test.flags = (uint8_t)(MP_RULES_F_TEAMS |
                                                       (f != 0u ? MP_RULES_F_FRIENDLY_FIRE : 0u));
                    mode_under_test = MODES[m];
                    own_team        = TEAMS[a];
                    peer_team       = TEAMS[v];

                    attacking = mp_body_death_judge_contact(0, FAR_BANK) == MP_CONTACT_ALLOWED;
                    attacked  = mp_body_death_judge_contact(FAR_BANK, 0u) == MP_CONTACT_ALLOWED;
                    ++pairs;
                    if (attacking != attacked) {
                        ++differ;
                    }
                    if (!attacking) {
                        ++refused;
                    }
                }
            }
        }
    }
    ut_checkf(differ == 0u, "%u of %u combinations answer one way for the attacker and another "
              "for the victim", differ, pairs);
    ut_checkf(refused != 0u && refused != pairs,
              "and the switch still decides something: %u of %u combinations are refused",
              refused, pairs);
    mp_body_set_damage_gate(NULL);
    mp_body_death_set_ally_test(NULL);
}

int main(void)
{
    check_class_band();
    check_without_a_game();
    check_the_field_of_bodies();
    check_the_attribution();
    check_the_gates_arms();
    check_the_gate_is_symmetric();

    return ut_summary("mp_body");
}
