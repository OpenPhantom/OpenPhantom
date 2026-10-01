/* mp_hit_attacker.c: whose contact a client reports as its own hit.
 *
 * The first test keeps the earlier rule as the reference: every contact against an actor the host
 * owns was reported unless its sender was a copy of a host bolt. In a field run that rule made a
 * host kill its own actors from a client's puppet.
 */
#include "unittest.h"

#include "mp_hit_attacker.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define OWN_BODY   0x07F00010u
#define PUPPET     0x07F00020u
#define NPC_BODY   0x07F00030u
#define SHOT_A     0x07F00040u
#define SHOT_B     0x07F00050u

/* The rule before this module. */
static bool reported_before(bool npc_bolt)
{
    return !npc_bolt;
}

static void test_the_old_rule_reported_what_was_not_this_player(void)
{
    mp_hit_attacker_t npc = mp_hit_attacker_of(NPC_BODY, OWN_BODY, false, 3u, false, false, false);
    mp_hit_attacker_t puppet = mp_hit_attacker_of(PUPPET, OWN_BODY, false, 1u, false, false, true);

    ut_check(reported_before(false), "the old rule reported an actor standing against an actor");
    ut_check(!mp_hit_attacker_is_own(npc), "an actor's body is not this player's hit");
    ut_check(npc == MP_HIT_ATTACKER_OTHER, "and is named as something else");
    ut_check(!mp_hit_attacker_is_own(puppet),
             "a far player's puppet is not this player's hit, even on the player side");
}

static void test_this_players_body_and_shots_are_reported(void)
{
    ut_check(mp_hit_attacker_of(OWN_BODY, OWN_BODY, false, 1u, false, false, true) ==
                 MP_HIT_ATTACKER_OWN_BODY,
             "the player's own body with its blade armed");
    ut_check(mp_hit_attacker_of(OWN_BODY, OWN_BODY, false, 1u, false, false, false) ==
                 MP_HIT_ATTACKER_OWN_BUMP &&
                 !mp_hit_attacker_is_own(MP_HIT_ATTACKER_OWN_BUMP),
             "the same body with nothing armed is a push and is not reported");
    ut_check(mp_hit_attacker_of(SHOT_A, OWN_BODY, true, 1u, false, false, false) ==
                 MP_HIT_ATTACKER_OWN_SHOT,
             "a player side shot nobody marked as a far player's");
    ut_check(mp_hit_attacker_is_own(MP_HIT_ATTACKER_OWN_BODY) &&
                 mp_hit_attacker_is_own(MP_HIT_ATTACKER_OWN_SHOT),
             "and both are reported");
}

static void test_what_is_not_reported(void)
{
    ut_check(mp_hit_attacker_of(SHOT_A, OWN_BODY, true, 1u, false, true, false) ==
                 MP_HIT_ATTACKER_FAR_SHOT,
             "a player side shot the far player's puppet fired");
    ut_check(mp_hit_attacker_of(SHOT_A, OWN_BODY, true, 4u, true, false, false) ==
                 MP_HIT_ATTACKER_NPC_BOLT,
             "a copy of a host actor's bolt");
    ut_check(mp_hit_attacker_of(SHOT_A, OWN_BODY, true, 0u, false, false, false) ==
                 MP_HIT_ATTACKER_OTHER,
             "an effect object on side 0, a blast sphere or a chunk");
    ut_check(mp_hit_attacker_of(0u, OWN_BODY, false, 1u, false, false, true) ==
                 MP_HIT_ATTACKER_OTHER,
             "no sender at all");
    ut_check(mp_hit_attacker_of(OWN_BODY, 0u, false, 1u, false, false, true) ==
                 MP_HIT_ATTACKER_OTHER,
             "a body when this machine's own body is not known");
    ut_check(!mp_hit_attacker_is_own(MP_HIT_ATTACKER_FAR_SHOT) &&
                 !mp_hit_attacker_is_own(MP_HIT_ATTACKER_NPC_BOLT) &&
                 !mp_hit_attacker_is_own(MP_HIT_ATTACKER_OTHER),
             "none of those is reported");
}

static void test_the_owners_table(void)
{
    static mp_hit_shot_owners_t owners;
    uint32_t                    i;

    memset(&owners, 0, sizeof owners);
    ut_check(!mp_hit_shot_owners_is_far(&owners, SHOT_A), "an object never noted is not far");
    ut_check(!mp_hit_shot_owners_is_ally(&owners, SHOT_A), "nor an ally's");
    mp_hit_shot_owners_note_ally(&owners, SHOT_A);
    ut_check(mp_hit_shot_owners_is_ally(&owners, SHOT_A) &&
                 !mp_hit_shot_owners_is_far(&owners, SHOT_A),
             "an ally's shot is an ally's, and no far player's");
    mp_hit_shot_owners_note(&owners, SHOT_A, false);
    ut_check(!mp_hit_shot_owners_is_ally(&owners, SHOT_A),
             "and one reused by this player's own shot is an ally's no more");

    mp_hit_shot_owners_note(&owners, SHOT_A, false);
    mp_hit_shot_owners_note(&owners, SHOT_A, true);
    ut_check(mp_hit_shot_owners_is_far(&owners, SHOT_A),
             "the hull notes the puppet's shot as the player's, the puppet then as far: the later "
             "note stands");
    mp_hit_shot_owners_note(&owners, SHOT_A, false);
    ut_check(!mp_hit_shot_owners_is_far(&owners, SHOT_A),
             "an object reused by this player's own next shot is this player's again");

    mp_hit_shot_owners_note(&owners, SHOT_B, true);
    for (i = 0; i < MP_HIT_SHOT_OWNERS; ++i) {
        mp_hit_shot_owners_note(&owners, 0x08000000u + i, false);
    }
    ut_check(!mp_hit_shot_owners_is_far(&owners, SHOT_B),
             "a far shot pushed out by 64 newer ones is forgotten, and taken as this player's");
    mp_hit_shot_owners_note(&owners, 0u, true);
    ut_check(!mp_hit_shot_owners_is_far(&owners, 0u), "object 0 is never remembered");
    ut_check(!mp_hit_shot_owners_is_far(NULL, SHOT_A), "no table answers no");
}

static void test_the_bank_behind_a_far_shot(void)
{
    mp_hit_shot_owners_t owners;
    uint8_t              bank = 0xAA;

    memset(&owners, 0, sizeof owners);

    ut_check(!mp_hit_shot_owners_far_bank(&owners, SHOT_A, &bank),
             "an empty table names no bank");
    ut_check(bank == 0xAA, "and it leaves the caller's bank alone");

    mp_hit_shot_owners_note_far_bank(&owners, SHOT_A, 2u);
    ut_check(mp_hit_shot_owners_far_bank(&owners, SHOT_A, &bank) && bank == 2u,
             "a far shot noted with its bank names that bank");
    ut_check(mp_hit_shot_owners_is_far(&owners, SHOT_A),
             "and it is still a far player's shot to the older question");

    /* A copy of one of the host's actors' bolts is noted far and belongs to no player. Naming a
     * bank for it would charge a death to whoever happened to hold that bank. */
    mp_hit_shot_owners_note(&owners, SHOT_B, true);
    ut_check(mp_hit_shot_owners_is_far(&owners, SHOT_B) &&
                 !mp_hit_shot_owners_far_bank(&owners, SHOT_B, &bank),
             "a far shot with no player behind it names no bank");

    /* The ring is the only memory for a shot whose side the engine cleared, so a stale answer
     * here is a death charged to the wrong player. */
    mp_hit_shot_owners_note(&owners, SHOT_A, false);
    ut_check(!mp_hit_shot_owners_far_bank(&owners, SHOT_A, &bank),
             "an object reused by this player's own shot names no far bank");
    mp_hit_shot_owners_note_far_bank(&owners, SHOT_A, 3u);
    mp_hit_shot_owners_note_ally(&owners, SHOT_A);
    ut_check(!mp_hit_shot_owners_far_bank(&owners, SHOT_A, &bank),
             "and one an ally fired names none either");

    bank = 0xAA;
    mp_hit_shot_owners_note_far_bank(&owners, 0u, 1u);
    ut_check(!mp_hit_shot_owners_far_bank(&owners, 0u, &bank) && bank == 0xAA,
             "object 0 is never remembered and never answered");
    ut_check(!mp_hit_shot_owners_far_bank(NULL, SHOT_A, &bank), "no table answers no");
    mp_hit_shot_owners_note_far_bank(&owners, SHOT_B, 1u);
    ut_check(mp_hit_shot_owners_far_bank(&owners, SHOT_B, NULL),
             "a caller that only wants to know THAT a bank is named may pass none");
}

int main(void)
{
    test_the_old_rule_reported_what_was_not_this_player();
    test_this_players_body_and_shots_are_reported();
    test_what_is_not_reported();
    test_the_owners_table();
    test_the_bank_behind_a_far_shot();

    return ut_summary("mp_hit_attacker");
}
