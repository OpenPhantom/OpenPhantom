/* character_profile.c: the half of the character layer that can be checked without the game.
 *
 * What is under test is the fallback chain, and every way it can be wrong is quiet. Let the walk
 * and run pair fall back to each other and an actor with neither loops for ever. Let a missing jump
 * answer with the idle and a body snaps to a stand in mid air instead of holding its pose. Let a
 * missing flinch answer with the death and a character dies from a graze. None of those crash and
 * none of them log.
 *
 * The parser is deliberately not driven here: it reads a file beside the game, and a test process
 * has no game beside it, so the table is empty and every lookup takes the no profile path. That is
 * exactly the state a player gets on an installation whose data file did not ship, and it is worth
 * pinning down that it stays playable.
 */
#include "unittest.h"

#include "common/character_profile.h"

#include <string.h>

/* A profile built by hand rather than parsed, so the chain is tested and not the file format. */
static character_profile_t make(int idle, int walk, int run, int back, int attack, int melee)
{
    character_profile_t p;
    int i;

    memset(&p, 0, sizeof p);
    for (i = 0; i < (int)CHARACTER_ROLE_COUNT; ++i) {
        p.clip[i] = CHARACTER_PROFILE_NO_CLIP;
    }
    memcpy(p.asset, "test.baf", sizeof "test.baf");
    p.clip[CHARACTER_ROLE_IDLE] = idle;
    p.clip[CHARACTER_ROLE_WALK] = walk;
    p.clip[CHARACTER_ROLE_RUN] = run;
    p.clip[CHARACTER_ROLE_BACK] = back;
    p.clip[CHARACTER_ROLE_ATTACK] = attack;
    p.clip[CHARACTER_ROLE_MELEE] = melee;
    return p;
}

static const int NONE = CHARACTER_PROFILE_NO_CLIP;

/* The locomotion chain, from a complete profile down to one with nothing at all. */
static void check_the_gaits(void)
{
    ut_section("a complete profile answers with itself");
    {
        character_profile_t p = make(0, 1, 2, 3, 4, 5);

        ut_check(character_profile_clip(&p, CHARACTER_ROLE_IDLE) == 0, "the idle is the idle");
        ut_check(character_profile_clip(&p, CHARACTER_ROLE_WALK) == 1, "the walk is the walk");
        ut_check(character_profile_clip(&p, CHARACTER_ROLE_RUN) == 2, "the run is the run");
        ut_check(character_profile_clip(&p, CHARACTER_ROLE_BACK) == 3, "and so is walking back");
    }

    ut_section("an actor that only runs, which is what the Tusken and the sabre lord are");
    {
        character_profile_t p = make(0, NONE, 1, NONE, NONE, NONE);

        ut_check(character_profile_clip(&p, CHARACTER_ROLE_WALK) == 1,
                 "asking for a walk gives the run, because one gait serves both");
        ut_check(character_profile_clip(&p, CHARACTER_ROLE_BACK) == 1,
                 "and walking backwards uses it too rather than standing still");
    }

    ut_section("an actor that only walks");
    {
        character_profile_t p = make(0, 5, NONE, NONE, NONE, NONE);

        ut_check(character_profile_clip(&p, CHARACTER_ROLE_RUN) == 5, "running gives the walk");
    }

    ut_section("an actor with nothing but an idle, which is the case the chain exists for");
    {
        character_profile_t p = make(7, NONE, NONE, NONE, NONE, NONE);

        ut_check(character_profile_clip(&p, CHARACTER_ROLE_WALK) == 7,
                 "walking falls all the way through to the idle");
        ut_check(character_profile_clip(&p, CHARACTER_ROLE_RUN) == 7, "and so does running");
        ut_check(character_profile_clip(&p, CHARACTER_ROLE_BACK) == 7, "and so does backwards");
        ut_check(character_profile_clip(&p, CHARACTER_ROLE_JUMP) == NONE,
                 "but a jump answers nothing, so the refusal holds the pose the body was in");
        ut_check(character_profile_clip(&p, CHARACTER_ROLE_HIT) == NONE,
                 "and a flinch answers nothing rather than borrowing the death");
        ut_check(character_profile_clip(&p, CHARACTER_ROLE_DEATH) == NONE,
                 "and a death that was never authored stays absent");
    }

    ut_section("an actor with nothing at all cannot loop the chain");
    {
        character_profile_t p = make(NONE, NONE, NONE, NONE, NONE, NONE);

        ut_check(character_profile_clip(&p, CHARACTER_ROLE_WALK) == NONE,
                 "walking runs out rather than bouncing between walk and run");
        ut_check(character_profile_clip(&p, CHARACTER_ROLE_RUN) == NONE, "and so does running");
    }
}

/* The contract the generator spends.
 *
 * The mined locomotion ordinals are ranked by the move speed a level script set, and a move
 * speed of zero is standing still while a clip plays, so the slowest candidate was regularly
 * the rest pose. Forty seven Walk and Run ordinals in an earlier data file were a stand, a spin
 * or a droid's arm reaching out. The shipped file omits such a row instead of writing it, on
 * the strength of what this section checks: an absent locomotion role is answered by the next
 * gait and finally by the idle, so omitting a lie costs nothing on screen and gains an actor
 * that runs with its walk instead of standing still while it sprints.
 *
 * Thirty seven rows are absent on that basis. If the chain below ever stops answering, those
 * thirty seven characters lose their locomotion silently, which is why it is pinned here and
 * not left to the two sections above to imply. */
static void check_an_emptied_locomotion_slot(void)
{
    ut_section("an emptied locomotion slot, which is how the data file says it has nothing "
               "better");
    {
        character_profile_t p = make(0, 1, NONE, NONE, NONE, NONE);

        ut_check(character_profile_clip(&p, CHARACTER_ROLE_RUN) == 1,
                 "a character whose run was a standing pose runs with its walk");

        p.clip[CHARACTER_ROLE_WALK] = NONE;
        ut_check(character_profile_clip(&p, CHARACTER_ROLE_WALK) == 0,
                 "and one with no gait at all keeps its rest pose, the ordinal a walk row naming "
                 "a standing pose would give");
        ut_check(character_profile_clip(&p, CHARACTER_ROLE_RUN) == 0, "for the run as well");
        ut_check(character_profile_clip(&p, CHARACTER_ROLE_IDLE) == 0,
                 "so removing both rows changes the ordinal the engine is handed by nothing");
    }
}

static void check_the_attacks(void)
{
    ut_section("the two attacks stand in for each other");
    {
        character_profile_t p = make(0, 1, 2, NONE, 9, NONE);

        ut_check(character_profile_clip(&p, CHARACTER_ROLE_MELEE) == 9,
                 "an actor with one attack uses it for both kinds");
        p.clip[CHARACTER_ROLE_ATTACK] = NONE;
        p.clip[CHARACTER_ROLE_MELEE] = 4;
        ut_check(character_profile_clip(&p, CHARACTER_ROLE_ATTACK) == 4, "in either direction");
    }

    /* The fire clip is the one the weapon row plays when a shot leaves, and it is a separate
     * question from the attack: the battle droid captain swings with one clip and throws with
     * another. 40 of the 164 shipped rows name one, so the interesting case is the other 124. */
    ut_section("the fire clip, which most actors do not have");
    {
        character_profile_t p = make(0, 1, 2, NONE, 6, NONE);

        ut_check(character_profile_clip(&p, CHARACTER_ROLE_FIRE) == 6,
                 "an actor with no fire clip of its own shoots with its attack");

        p.clip[CHARACTER_ROLE_FIRE] = 10;
        ut_check(character_profile_clip(&p, CHARACTER_ROLE_FIRE) == 10,
                 "and an actor that has one uses it rather than the swing");
        ut_check(character_profile_clip(&p, CHARACTER_ROLE_ATTACK) == 6,
                 "without the fire clip displacing the attack, which is a different question");

        p.clip[CHARACTER_ROLE_ATTACK] = NONE;
        ut_check(character_profile_clip(&p, CHARACTER_ROLE_ATTACK) == 10,
                 "a hand written row that names only a fire clip still answers for the attack");

        p.clip[CHARACTER_ROLE_FIRE] = NONE;
        p.clip[CHARACTER_ROLE_MELEE] = 4;
        ut_check(character_profile_clip(&p, CHARACTER_ROLE_FIRE) == 4,
                 "and an actor with nothing but a swing fires with the swing");
    }
}

static void check_no_profile(void)
{
    ut_section("no profile at all falls back to the convention");
    ut_check(character_profile_clip(NULL, CHARACTER_ROLE_IDLE) == 0,
             "ordinal 0 is the rest pose, which holds for 152 of the 164 shipped rows");
    ut_check(character_profile_clip(NULL, CHARACTER_ROLE_WALK) == 1, "1 is the first gait");
    ut_check(character_profile_clip(NULL, CHARACTER_ROLE_RUN) == 2, "2 is the second");
    ut_check(character_profile_clip(NULL, CHARACTER_ROLE_ATTACK) == NONE,
             "and nothing else is guessed, because a guessed ordinal gets believed later");
    ut_check(character_profile_clip(NULL, CHARACTER_ROLE_FIRE) == NONE,
             "which includes the fire clip: an actor nobody profiled does not get armed");

    ut_section("indices and names that do not exist");
    ut_check(character_profile_clip(NULL, (character_role_t)99) == NONE, "a role past the end");
    ut_check(strcmp(character_role_name((character_role_t)99), "?") == 0, "has no name either");
    ut_check(strcmp(character_role_name(CHARACTER_ROLE_STRAFE), "Strafe") == 0,
             "and a real one is named as the data file spells it");
}

/* The parser matches a key by walking this same table, so a role whose word is missing or
 * misspelled here is a line of characters.ini that is read and silently dropped. That is
 * exactly what happened to Fire for as long as the generator wrote it. */
static void check_the_role_names(void)
{
    static const char *const KEY[] = {
        "Idle", "Walk", "Run", "Back", "Jump", "Attack", "Fire", "Melee", "Block",
        "Death", "Hit", "GetUp", "Strafe", "Talk"
    };
    int role;

    ut_section("every role is spelled the way the data file spells it");
    ut_check((int)(sizeof KEY / sizeof KEY[0]) == (int)CHARACTER_ROLE_COUNT,
             "the list of keys is as long as the list of roles");
    for (role = 0; role < (int)CHARACTER_ROLE_COUNT; ++role) {
        ut_check(strcmp(character_role_name((character_role_t)role), KEY[role]) == 0,
                 KEY[role]);
    }
}

/* The set and the pick are two different answers, and the reason the set exists at all is the
 * actor with more than one death: imposing the pick on a body that is already playing one of
 * the others is exactly the fault this list prevents. */
static void check_the_deaths(void)
{
    character_profile_t p = make(0, 1, 2, NONE, NONE, NONE);

    ut_section("every death the actor carries, not only the one to play");
    p.death_clip[0] = 5;
    p.death_clip[1] = 6;
    p.death_clip_count = 2u;

    ut_check(character_profile_is_death_clip(&p, 5), "the first is a death");
    ut_check(character_profile_is_death_clip(&p, 6), "and so is the second");
    ut_check(!character_profile_is_death_clip(&p, 7), "the one after it is not");
    ut_check(!character_profile_is_death_clip(&p, 0), "and neither is the idle");
    ut_check(!character_profile_is_death_clip(&p, -1),
             "a negative ordinal is refused rather than indexed with");
    ut_check(!character_profile_is_death_clip(NULL, 5),
             "and an actor nobody profiled claims no clip at all");

    ut_check(character_profile_death_clip(&p) == 5,
             "with no Death role the lowest of the set is the one to impose");
    p.clip[CHARACTER_ROLE_DEATH] = 6;
    ut_check(character_profile_death_clip(&p) == 6,
             "and the arbitrated pick outranks it once there is one");
    ut_check(character_profile_death_clip(NULL) == NONE,
             "an unprofiled actor is left alone, which is the whole of the fallback");
}

static void check_the_empty_table(void)
{
    ut_section("with no game beside it the table is empty and lookups are honest");
    ut_check(!character_profile_load(),
             "with no characters.ini beside this program the load answers false rather than "
             "inventing a table");
    ut_check(character_profile_count() == 0u, "and nothing is loaded");
    ut_check(character_profile_find("baron.baf") == NULL, "so no actor is claimed");
    ut_check(character_profile_find(NULL) == NULL, "and a missing name is refused, not crashed on");
}

int main(void)
{
    check_the_gaits();
    check_an_emptied_locomotion_slot();
    check_the_attacks();
    check_no_profile();
    check_the_role_names();
    check_the_deaths();
    check_the_empty_table();

    return ut_summary("the character profile");
}
