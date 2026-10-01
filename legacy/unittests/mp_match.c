/* unittests/mp_match.c: the dedicated server's match authority.
 *
 * Everything here is what a listen host gets from a lobby screen and a running game, and what a
 * server with neither has to decide for itself: which side a joiner goes on, whether somebody may
 * swap, when a round is over, and what happens next.
 */
#include "unittest.h"

#include "mp_match.h"

#include <string.h>

static void a_default_match(mp_match_t *match)
{
    mp_lobby_setup_t setup;

    memset(&setup, 0, sizeof setup);
    setup.level_index = 0u;
    /* The encoder refuses a setup with no level, and rightly: a note that names no map tells a
     * client nothing it can act on. */
    memcpy(setup.level, "ASSAULT", 8u);
    memcpy(setup.title, "Assault", 8u);
    mp_rules_default(&setup.rules);
    mp_match_start(match, &setup);
}

static void check_the_balance(void)
{
    ut_section("a joiner goes on the emptier side");

    ut_check(mp_match_balanced_team(0u, 0u) == MP_MATCH_TEAM_A,
             "the first player opens team A, so a filling server alternates rather than stacks");
    ut_check(mp_match_balanced_team(1u, 0u) == MP_MATCH_TEAM_B, "the second balances it");
    ut_check(mp_match_balanced_team(1u, 1u) == MP_MATCH_TEAM_A, "the third goes back to A");
    ut_check(mp_match_balanced_team(2u, 1u) == MP_MATCH_TEAM_B, "and the fourth evens it again");
    ut_check(mp_match_balanced_team(1u, 5u) == MP_MATCH_TEAM_A,
             "a lopsided pair sends the joiner to the short side whichever one it is");
}

static void check_the_swap_rule(void)
{
    ut_section("nobody may leave a balanced game to unbalance it");

    /* Two on each side. Leaving A for B makes it 1v3, two apart, so it is refused. This is the
     * case the rule exists for: without it a server empties into one team. */
    ut_check(!mp_match_swap_allowed(2u, 2u), "2v2 is not a game anybody may make 1v3 of");
    ut_check(!mp_match_swap_allowed(3u, 3u), "and 3v3 is not one to make 2v4 of");

    ut_section("but anybody may leave a lopsided one to fix it");

    ut_check(mp_match_swap_allowed(3u, 1u), "3v1 may become 2v2");
    ut_check(mp_match_swap_allowed(4u, 2u), "4v2 may become 3v3");
    ut_check(mp_match_swap_allowed(2u, 1u), "2v1 may become 1v2, which is no worse");

    ut_section("and the empty side is never left");

    ut_check(!mp_match_swap_allowed(0u, 3u), "nobody is on that side to be leaving it");
}

static void check_a_join_and_a_leave(void)
{
    mp_match_t match;

    a_default_match(&match);

    ut_section("four players fill two sides evenly");

    mp_match_player_joined(&match, 1u);
    mp_match_player_joined(&match, 2u);
    mp_match_player_joined(&match, 3u);
    mp_match_player_joined(&match, 4u);
    ut_check(mp_match_team_of(&match, 1u) == MP_MATCH_TEAM_A, "slot 1 on A");
    ut_check(mp_match_team_of(&match, 2u) == MP_MATCH_TEAM_B, "slot 2 on B");
    ut_check(mp_match_team_of(&match, 3u) == MP_MATCH_TEAM_A, "slot 3 on A");
    ut_check(mp_match_team_of(&match, 4u) == MP_MATCH_TEAM_B, "slot 4 on B");

    ut_section("a swap that would stack is refused, one that repairs is taken");

    ut_check(!mp_match_request_team(&match, 1u, MP_MATCH_TEAM_B), "2v2 stays 2v2");
    ut_check(mp_match_team_of(&match, 1u) == MP_MATCH_TEAM_A, "and the player did not move");
    ut_check(mp_match_request_team(&match, 1u, MP_MATCH_TEAM_A),
             "asking for the side you are already on is not a refusal");

    mp_match_player_left(&match, 2u);
    mp_match_player_left(&match, 4u);
    ut_check(mp_match_request_team(&match, 1u, MP_MATCH_TEAM_B), "2v0 may become 1v1");
    ut_check(mp_match_team_of(&match, 1u) == MP_MATCH_TEAM_B, "and the player moved");

    ut_section("a player who left is gone, not remembered");

    ut_check(mp_match_team_of(&match, 2u) == MP_LOBBY_TEAM_NONE, "a free slot has no side");
    mp_match_player_joined(&match, 2u);
    ut_check(mp_match_team_of(&match, 2u) != MP_LOBBY_TEAM_NONE,
             "and somebody arriving in it is placed fresh");
}

static void check_teams_off(void)
{
    mp_match_t       match;
    mp_lobby_setup_t setup;

    memset(&setup, 0, sizeof setup);
    memcpy(setup.level, "ASSAULT", 8u);
    mp_rules_default(&setup.rules);
    setup.rules.flags = 0u;   /* no teams, no friendly fire */
    mp_match_start(&match, &setup);

    ut_section("a free for all puts nobody on a side");

    mp_match_player_joined(&match, 1u);
    mp_match_player_joined(&match, 2u);
    ut_check(mp_match_team_of(&match, 1u) == MP_LOBBY_TEAM_NONE, "slot 1 has no side");
    ut_check(mp_match_team_of(&match, 2u) == MP_LOBBY_TEAM_NONE, "and neither has slot 2");
    ut_check(!mp_match_request_team(&match, 1u, MP_MATCH_TEAM_A),
             "and there is no side to ask for");
}

static void check_a_death(void)
{
    mp_match_t      match;
    mp_death_note_t note;

    a_default_match(&match);
    mp_match_player_joined(&match, 1u);
    mp_match_player_joined(&match, 2u);

    ut_section("a kill counts, a death for nobody does not");

    note.victim_slot = 2u;
    note.killer_slot = 1u;
    note.reason      = MP_DEATH_BY_HIT;
    ut_check(mp_match_take_death(&match, &note), "a kill between two seated players is taken");

    note.victim_slot = 9u;   /* nobody is on slot 9 */
    ut_check(!mp_match_take_death(&match, &note),
             "a death for a slot nobody is on is a decoding fault rather than a kill");
    ut_check(match.deaths_refused == 1u, "and it is counted as refused");
    ut_check(!mp_match_take_death(&match, NULL), "and no note at all is no death");
}

static void check_the_round_ends_and_the_next_begins(void)
{
    mp_match_t       match;
    mp_lobby_setup_t setup;
    mp_death_note_t  note;
    uint8_t          winner = 0;
    uint8_t          first_generation;
    unsigned         i;

    memset(&setup, 0, sizeof setup);
    memcpy(setup.level, "ASSAULT", 8u);
    mp_rules_default(&setup.rules);
    setup.rules.score_limit = 2u;   /* two points ends it, so the test is short */
    mp_match_start(&match, &setup);
    first_generation = match.setup.generation;
    mp_match_player_joined(&match, 1u);
    mp_match_player_joined(&match, 2u);

    ut_check(first_generation != 0u,
             "the first generation is not zero: a fresh client holds zero and compares for "
             "inequality, so a first note of zero is one it has already acted on");

    ut_section("the score limit decides the round");

    note.victim_slot = 2u;
    note.killer_slot = 1u;
    note.reason      = MP_DEATH_BY_HIT;
    ut_check(mp_match_take_death(&match, &note), "one kill");
    mp_match_tick(&match);
    ut_check(mp_match_outcome(&match, &winner) == MP_SCORE_RUNNING, "one is not enough");
    ut_check(mp_match_take_death(&match, &note), "two kills");
    mp_match_tick(&match);
    ut_check(mp_match_outcome(&match, &winner) != MP_SCORE_RUNNING, "two is");

    ut_section("and the decided table is HELD UP before the next round starts");

    ut_check(!mp_match_take_death(&match, &note),
             "a death after the round was decided does not move the final table");
    for (i = 0; i < MP_MATCH_INTERMISSION_SUBSTEPS - 2u; ++i) {
        mp_match_tick(&match);
        ut_checkf(match.setup.generation == first_generation,
                  "substep %u of the intermission still shows the same round", i);
        if (match.setup.generation != first_generation) {
            break;   /* one failing line is the report; the rest would be noise */
        }
    }

    ut_section("then a new round on a generation nobody has acted on");

    mp_match_tick(&match);
    mp_match_tick(&match);
    ut_check(match.setup.generation != first_generation, "the generation moved");
    ut_check(match.setup.generation != 0u, "and it skipped zero, which means 'not acted on yet'");
    ut_check(mp_match_outcome(&match, &winner) == MP_SCORE_RUNNING, "the new round is running");
    ut_check(match.rounds_begun == 2u, "two rounds have begun");

    ut_section("and everybody still here is in the new table on the side they were on");

    ut_check(mp_match_team_of(&match, 1u) == MP_MATCH_TEAM_A, "slot 1 kept its side");
    ut_check(mp_match_team_of(&match, 2u) == MP_MATCH_TEAM_B, "and so did slot 2");
}

static void check_the_notes(void)
{
    mp_match_t match;
    uint8_t    buffer[256];

    a_default_match(&match);

    ut_section("both notes come out, and both say the match is on");

    ut_check(mp_match_setup_note(&match, buffer, sizeof buffer) == MP_LOBBY_SETUP_BYTES,
             "the setup note is its whole length");
    ut_check(mp_lobby_is_setup(buffer, MP_LOBBY_SETUP_BYTES), "and it recognises itself");
    ut_check(mp_match_score_note(&match, buffer, sizeof buffer) != 0u, "the score note comes out");

    ut_section("a buffer too small gets nothing rather than a truncation");

    ut_check(mp_match_setup_note(&match, buffer, 4u) == 0u, "the setup refuses");
    ut_check(mp_match_score_note(&match, buffer, 2u) == 0u, "and so does the score");
}

int main(void)
{
    check_the_balance();
    check_the_swap_rule();
    check_a_join_and_a_leave();
    check_teams_off();
    check_a_death();
    check_the_round_ends_and_the_next_begins();
    check_the_notes();
    return ut_summary("mp_match");
}
