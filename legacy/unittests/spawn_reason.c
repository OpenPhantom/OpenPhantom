/* spawn_reason.c: why the entity spawner cannot be used, the rule alone.
 *
 * The case this exists for looked like a broken panel in a field run: in a co-op session, in the
 * running level, on the HOST and on the CLIENT alike, the entity spawner's row read n/a and the
 * placement mode printed "the session runs no copies" nine times, while the multiplayer on both
 * machines ran them. Two spellings of one state: the group's rows asked whether the multiplayer
 * ran the copies, the mode asked only whether a session was running at all. The checks below hold
 * the two together.
 *
 * What would be silent if it were wrong: a session that runs the copies reading as one that does
 * not, which greys a working panel; and the other way round, a session that runs none reading as
 * usable, which would let a click build a copy the session hands out no key for.
 */
#include "unittest.h"

#include "spawn_reason.h"

#include <string.h>

/* A level with a player in it, a kind picked, a camera and a builder: everything ready. */
static void ready(spawn_reason_facts_t *facts)
{
    memset(facts, 0, sizeof *facts);
    facts->level   = true;
    facts->builder = true;
    facts->camera  = true;
    facts->chosen  = true;
    facts->kinds   = 204u;
}

/* The multiplayer's record as each side of a co-op session publishes it. The host names a cap, a
 * client its own world slot, and either one means the copies run; the group's lock is what turns
 * that into "held" or not, and it is the fact this rule reads. */
static void the_session(void)
{
    spawn_reason_facts_t facts;

    ut_section("a session that runs the copies");

    ready(&facts);
    facts.session       = true;    /* the host: cap 16 from NpcCopiesMax */
    facts.session_holds = false;
    ut_check(spawn_reason_why(&facts, SPAWN_REASON_GROUP) == NULL,
             "on the host of a session that runs the copies the group is usable");
    ut_check(spawn_reason_why(&facts, SPAWN_REASON_PLACE) == NULL,
             "and the placement mode starts");

    ready(&facts);
    facts.session       = true;    /* a client: cap 0, world slot 1 */
    facts.session_holds = false;
    facts.builder       = false;   /* the host raises it, so this machine need not be able to */
    ut_check(spawn_reason_why(&facts, SPAWN_REASON_PLACE) == NULL,
             "on a client of that session the placement mode starts too");

    ready(&facts);
    facts.session       = false;
    facts.session_holds = true;    /* a session runs and the multiplayer runs no copies in it */
    ut_check(spawn_reason_why(&facts, SPAWN_REASON_GROUP) != NULL &&
                 strcmp(spawn_reason_why(&facts, SPAWN_REASON_GROUP),
                        "the session runs no copies") == 0,
             "a session that runs no copies takes the group, and says exactly that");
    ut_check(spawn_reason_why(&facts, SPAWN_REASON_PLACE) ==
                 spawn_reason_why(&facts, SPAWN_REASON_GROUP),
             "and the mode gives the rows' own sentence, not one of its own");

    ready(&facts);
    ut_check(spawn_reason_why(&facts, SPAWN_REASON_PLACE) == NULL,
             "outside a session everything is as it was");
}

/* The order matters: the first answer is the one the panel writes, so the reason a player can do
 * nothing about has to come before the ones they can. */
static void the_order(void)
{
    spawn_reason_facts_t facts;

    ut_section("which reason is given first");

    ready(&facts);
    facts.session_holds = true;
    facts.level         = false;
    facts.chosen        = false;
    ut_check(strcmp(spawn_reason_why(&facts, SPAWN_REASON_PLACE),
                    "the session runs no copies") == 0,
             "the session comes before the level and the choice");

    ready(&facts);
    facts.level  = false;
    facts.chosen = false;
    ut_check(strcmp(spawn_reason_why(&facts, SPAWN_REASON_PLACE),
                    "no level, or no player in it") == 0,
             "the level comes before the choice");

    ready(&facts);
    facts.kinds = 0u;
    ut_check(strcmp(spawn_reason_why(&facts, SPAWN_REASON_GROUP),
                    "this level offers nothing to spawn") == 0,
             "a level offering nothing takes the whole group, not only the mode");
}

/* What the mode needs beyond the group, and nothing of it leaks the other way. */
static void the_mode(void)
{
    spawn_reason_facts_t facts;

    ut_section("what the placement mode needs on top");

    ready(&facts);
    facts.chosen = false;
    ut_check(spawn_reason_why(&facts, SPAWN_REASON_GROUP) == NULL,
             "with nothing chosen the group is still usable: the list is how one is chosen");
    ut_check(strcmp(spawn_reason_why(&facts, SPAWN_REASON_PLACE),
                    "nothing is chosen to place") == 0,
             "but the mode has nothing to place");

    ready(&facts);
    facts.camera = false;
    ut_check(spawn_reason_why(&facts, SPAWN_REASON_GROUP) == NULL &&
                 strcmp(spawn_reason_why(&facts, SPAWN_REASON_PLACE),
                        "the camera cells did not resolve") == 0,
             "an unread camera stops the mode alone");

    ready(&facts);
    facts.builder = false;
    ut_check(strcmp(spawn_reason_why(&facts, SPAWN_REASON_PLACE),
                    "the spawner cannot raise a copy here") == 0,
             "outside a session a machine that cannot raise one says so");

    ready(&facts);
    facts.builder = false;
    facts.session = true;
    ut_check(spawn_reason_why(&facts, SPAWN_REASON_PLACE) == NULL,
             "in a session it is the host that raises it, so this machine need not");

    ut_check(spawn_reason_why(NULL, SPAWN_REASON_PLACE) != NULL,
             "no facts at all is a reason, never a silent yes");
}

/* The group's rows read this rule with SPAWN_REASON_GROUP and the mode with SPAWN_REASON_PLACE.
 * That only holds together while PLACE carries everything GROUP refuses, word for word: a row that
 * is open while the mode says the group itself is shut, or a row greyed for a reason the mode
 * never mentions, is the same drift this rule was written to end. Every combination of the six
 * facts is walked, so the property is proved rather than sampled. */
static void group_is_carried_by_place(void)
{
    spawn_reason_facts_t facts;
    uint32_t             bits;
    uint32_t             carried = 0;
    uint32_t             shut    = 0;

    ut_section("what shuts the group shuts the mode, in the same words");

    for (bits = 0; bits < 64u; ++bits) {
        const char *group;
        const char *place;

        memset(&facts, 0, sizeof facts);
        facts.session_holds = (bits & 1u) != 0u;
        facts.session       = (bits & 2u) != 0u;
        facts.level         = (bits & 4u) != 0u;
        facts.builder       = (bits & 8u) != 0u;
        facts.camera        = (bits & 16u) != 0u;
        facts.chosen        = (bits & 32u) != 0u;
        facts.kinds         = 12u;
        group = spawn_reason_why(&facts, SPAWN_REASON_GROUP);
        place = spawn_reason_why(&facts, SPAWN_REASON_PLACE);
        if (group == NULL) {
            continue;
        }
        ++shut;
        carried += (place == group) ? 1u : 0u;
    }
    ut_check(shut > 0u && carried == shut,
             "every reason the group gives is the reason the mode gives, over all 64 cases");

    memset(&facts, 0, sizeof facts);
    facts.level  = true;
    facts.kinds  = 0u;
    ut_check(spawn_reason_why(&facts, SPAWN_REASON_GROUP) ==
                 spawn_reason_why(&facts, SPAWN_REASON_PLACE),
             "an empty level too, which only the group's own half can answer");
}

int main(void)
{
    the_session();
    the_order();
    the_mode();
    group_is_carried_by_place();
    return ut_summary("spawn reason");
}
