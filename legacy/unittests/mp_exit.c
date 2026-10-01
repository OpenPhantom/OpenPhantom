/* mp_exit.c: the one way out of a session.
 *
 * The rules pinned with no session and no game in the process: when the transport comes down,
 * when the title screen means the session has nothing left to be, and what a player is told at the
 * title.
 * Without this module a host that goes back to its title screen keeps admitting players, and a
 * client whose host ended the session sees the reason for half a second of a fade, never again.
 */
#include "unittest.h"

#include "mp_exit.h"

#include <stdbool.h>
#include <stdint.h>

static mp_exit_title_facts_t at_title_after_a_level(void)
{
    mp_exit_title_facts_t facts;

    facts.title_shown    = true;
    facts.menu_armed     = true;
    facts.closing        = false;
    facts.start_pending  = false;
    facts.following      = false;
    facts.host_connected = false;
    facts.ended          = false;
    facts.level_seen     = true;
    return facts;
}

static void test_the_transport_waits_for_the_level_and_the_last_word(void)
{
    ut_section("the transport comes down once no level stands and the last word is out");

    ut_check(mp_exit_step(false, false, 0u, 5000u) == MP_EXIT_STEP_NONE,
             "no exit under way: nothing to do, whatever the clock says");
    ut_check(mp_exit_step(true, true, 0u, 0u) == MP_EXIT_STEP_WAIT,
             "a level still runs: wait for it to let go");
    ut_check(mp_exit_step(true, true, 0u, 60000u) == MP_EXIT_STEP_WAIT,
             "and with no deadline, because the transport must not go under a standing world");
    ut_check(mp_exit_step(true, false, 0u, 0u) == MP_EXIT_STEP_FINISH,
             "no level and nothing unacknowledged: down at once");
    ut_check(mp_exit_step(true, false, 2u, 10u) == MP_EXIT_STEP_WAIT,
             "the host's last word still unacknowledged: wait a little");
    ut_check(mp_exit_step(true, false, 2u, MP_EXIT_LINGER_MS - 1u) == MP_EXIT_STEP_WAIT,
             "up to the last millisecond of the wait");
    ut_check(mp_exit_step(true, false, 2u, MP_EXIT_LINGER_MS) == MP_EXIT_STEP_FINISH,
             "and no longer: a peer that has not answered in a second has gone");
}

static void test_the_title_door(void)
{
    mp_exit_title_facts_t facts;

    ut_section("the title screen with a session up and no level left is an exit");

    facts = at_title_after_a_level();
    ut_check(mp_exit_title_door(&facts),
             "a session whose level was left without the campaign saying so ends at the title");
    ut_check(!mp_exit_title_door(NULL), "no facts, no door");

    facts = at_title_after_a_level();
    facts.title_shown = false;
    ut_check(!mp_exit_title_door(&facts), "another screen is not the title");

    facts = at_title_after_a_level();
    facts.menu_armed = false;
    ut_check(!mp_exit_title_door(&facts),
             "a transport the ini or the environment put up is not the menu's to take down");

    facts = at_title_after_a_level();
    facts.closing = true;
    ut_check(!mp_exit_title_door(&facts), "an exit already under way is not asked for twice");

    facts = at_title_after_a_level();
    facts.start_pending = true;
    ut_check(!mp_exit_title_door(&facts),
             "a start driven through the title menu passes the title on purpose");

    facts = at_title_after_a_level();
    facts.level_seen = false;
    ut_check(!mp_exit_title_door(&facts),
             "a session that has had no level is a lobby on its way to one");

    ut_section("a follower keeps its session at the title only while its host is still in it");
    facts = at_title_after_a_level();
    facts.following      = true;
    facts.host_connected = true;
    ut_check(!mp_exit_title_door(&facts),
             "on its way to the host's next world, with the host there: it stays");
    facts.ended = true;
    ut_check(mp_exit_title_door(&facts),
             "the host ended the session while this side waited: nothing is followed");
    facts.ended          = false;
    facts.host_connected = false;
    ut_check(mp_exit_title_door(&facts),
             "the host went away while this side waited: nothing is followed");
}

static void test_what_a_player_is_told(void)
{
    ut_section("what a player is told at the title");

    ut_check(mp_exit_notice(false, MP_LOBBY_OVER_ALL_LEFT, false, false, false, false) ==
                 MP_LOBBY_OVER_NO,
             "a host is told nothing: its session ends when it leaves");
    ut_check(mp_exit_notice(true, MP_LOBBY_OVER_NO, false, false, false, false) == MP_LOBBY_OVER_NO,
             "a client that left on its own is told nothing");
    ut_check(mp_exit_notice(true, MP_LOBBY_OVER_HOST_ENDED, false, false, false, false) ==
                 MP_LOBBY_OVER_HOST_ENDED,
             "the level's verdict that the host ended it is said again at the title");
    ut_check(mp_exit_notice(true, MP_LOBBY_OVER_NO, true, false, false, false) ==
                 MP_LOBBY_OVER_HOST_ENDED,
             "the host's word heard without a level is the same sentence");
    ut_check(mp_exit_notice(true, MP_LOBBY_OVER_NO, false, true, false, false) ==
                 MP_LOBBY_OVER_HOST_ENDED,
             "and so is a goodbye: a host that says goodbye has left on purpose");
    ut_check(mp_exit_notice(true, MP_LOBBY_OVER_NO, false, false, true, false) ==
                 MP_LOBBY_OVER_HOST_LOST,
             "a host that only went quiet is a lost connection");
    ut_check(mp_exit_notice(true, MP_LOBBY_OVER_CONTENT, true, true, true, false) ==
                 MP_LOBBY_OVER_CONTENT,
             "a client that cut itself off for different game data is told that, first");
    ut_check(mp_exit_notice(true, MP_LOBBY_OVER_BEHIND, false, false, false, false) ==
                 MP_LOBBY_OVER_BEHIND,
             "the level's verdict that the host sent this side away is said again at the title");
    ut_check(mp_exit_notice(true, MP_LOBBY_OVER_NO, false, false, true, true) ==
                 MP_LOBBY_OVER_BEHIND,
             "and so is the host's notice heard outside a level, rather than a lost connection");
    ut_check(mp_exit_notice(false, MP_LOBBY_OVER_NO, false, false, false, true) ==
                 MP_LOBBY_OVER_NO,
             "a host is told nothing, whatever a flag says");
}

static void test_every_reason_has_a_name(void)
{
    int why;

    ut_section("every reason has a name in the log");
    for (why = 0; why < (int)MP_EXIT_WHY_COUNT; ++why) {
        const char *name = mp_exit_why_name((mp_exit_why_t)why);

        ut_check(name != NULL && name[0] != '\0' &&
                     name != mp_exit_why_name(MP_EXIT_WHY_COUNT),
                 "a reason the exit can be asked for is named, not left to the fallback");
    }
}

int main(void)
{
    test_the_transport_waits_for_the_level_and_the_last_word();
    test_the_title_door();
    test_what_a_player_is_told();
    test_every_reason_has_a_name();
    return ut_summary("mp_exit");
}
