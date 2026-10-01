/* mp_director_rule.c: the script director's twenty commands by class.
 *
 * The class table replaced two lists that each answered one question about a command number: the
 * level door's "does it end the level" and the level state's "is it fog". A refactor of that kind
 * can drop a number out of a gate without a word, so the two old lists are written out here as
 * they stood and every number, inside the table and past both ends of it, is asked both ways.
 */
#include "unittest.h"

#include "mp_director_rule.h"
#include "mp_level_state_rule.h"

#include <stdbool.h>
#include <stdint.h>

/* The two answers as they were before the table, verbatim. */
static bool old_ends_level(int32_t command)
{
    return command == 1 || command == 14;
}

static bool old_is_fog(int32_t command)
{
    return command == 6 || command == 7 || command == 11 || command == 12;
}

static void check_the_old_lists(void)
{
    int32_t command;
    bool    doors_agree = true;
    bool    fog_agrees  = true;

    ut_section("the class table answers what the two old lists answered, for every number");
    for (command = -3; command < MP_DIRECTOR_COMMANDS + 4; ++command) {
        bool door = mp_director_class_of(command) == MP_DIRECTOR_LEVEL_DOOR;

        doors_agree = doors_agree && door == old_ends_level(command);
        fog_agrees  = fog_agrees && mp_level_state_fog_command(command) == old_is_fog(command);
    }
    ut_check(doors_agree, "the level doors are 1 and 14 and nothing else, as before");
    ut_check(fog_agrees, "and the level state's fog is 6, 7, 11 and 12, as before");
}

static void check_the_counts(void)
{
    unsigned count[MP_DIRECTOR_CLASSES];
    int32_t  command;
    unsigned total = 0;
    size_t   cls;

    ut_section("every command has exactly one class, and the classes add up to twenty");
    for (cls = 0; cls < MP_DIRECTOR_CLASSES; ++cls) {
        count[cls] = 0u;
    }
    for (command = 0; command < MP_DIRECTOR_COMMANDS; ++command) {
        ++count[mp_director_class_of(command)];
    }
    for (cls = 0; cls < MP_DIRECTOR_CLASSES; ++cls) {
        total += count[cls];
    }
    ut_check(total == 20u, "twenty commands in the jump table, twenty classes handed out");
    ut_check(count[MP_DIRECTOR_NONE] == 2u, "0 and 13 have no arm worth the name");
    ut_check(count[MP_DIRECTOR_LEVEL_DOOR] == 2u, "two doors");
    ut_check(count[MP_DIRECTOR_SHIELD] == 4u, "four for the shield, 2 to 5");
    ut_check(count[MP_DIRECTOR_FOG] == 4u, "four for the fog");
    ut_check(count[MP_DIRECTOR_NODES] == 2u, "two for the weapon on the model, 8 and 9");
    ut_check(count[MP_DIRECTOR_BODY_FLAGS] == 2u, "two bits of the body's flags, 10 and 16");
    ut_check(count[MP_DIRECTOR_ESCORT] == 1u && count[MP_DIRECTOR_SOUND_STOP] == 1u &&
                 count[MP_DIRECTOR_CRAWL] == 1u && count[MP_DIRECTOR_BLAST] == 1u,
             "and one each for the escort, the sound stop, the crawl and the blast");
    ut_check(mp_director_class_of(-1) == MP_DIRECTOR_NONE &&
                 mp_director_class_of(MP_DIRECTOR_COMMANDS) == MP_DIRECTOR_NONE,
             "a number past either end of the table is none");
}

static void check_the_replays(void)
{
    ut_section("how a client may replay each class");
    ut_check(mp_director_replay_of(mp_director_class_of(17)) == MP_DIRECTOR_REPLAY_REPLICA,
             "17 frees the channel on its actor, so only the replica: a stand-in frees channel 0");
    ut_check(mp_director_replay_of(mp_director_class_of(15)) == MP_DIRECTOR_REPLAY_NEVER,
             "15 divides by the placement's hit points, so never: the bar travels as state");
    ut_check(mp_director_replay_of(mp_director_class_of(16)) == MP_DIRECTOR_REPLAY_NEVER,
             "16 is the body's shadow bit, which has one writer in the body's state");
    ut_check(mp_director_replay_of(mp_director_class_of(8)) == MP_DIRECTOR_REPLAY_NEVER &&
                 mp_director_replay_of(mp_director_class_of(9)) == MP_DIRECTOR_REPLAY_NEVER,
             "8 and 9 never: a stand-in would hide the children of the root");
    ut_check(mp_director_replay_of(mp_director_class_of(19)) == MP_DIRECTOR_REPLAY_NEVER,
             "19 never through the director: its explosion is replayed at its own function");
    ut_check(mp_director_replay_of(mp_director_class_of(6)) == MP_DIRECTOR_REPLAY_STAND_IN &&
                 mp_director_replay_of(mp_director_class_of(18)) == MP_DIRECTOR_REPLAY_STAND_IN,
             "the fog and the crawl read nothing of their actor, so a stand-in will do");
    ut_check(mp_director_replay_of(mp_director_class_of(2)) == MP_DIRECTOR_REPLAY_REPLICA,
             "the shield reads the actor's body, so only the replica");
    ut_check(mp_director_replay_of(mp_director_class_of(1)) == MP_DIRECTOR_REPLAY_NEVER &&
                 mp_director_replay_of(MP_DIRECTOR_CLASSES) == MP_DIRECTOR_REPLAY_NEVER,
             "a level door never, and a class past the table never");
}

int main(void)
{
    check_the_old_lists();
    check_the_counts();
    check_the_replays();
    return ut_summary("mp_director_rule");
}
