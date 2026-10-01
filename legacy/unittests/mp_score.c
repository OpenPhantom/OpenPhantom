/* What a round is worth so far, driven with no wire, no clock and no game.
 *
 * SIZE NOTE: over 600 lines. Nine sections, one per rule a round can be lost by, and the count
 * is what the module's own contract is worth rather than repetition: every ending is a case that
 * cannot be taken back, and every refusal on the wire is a case where a stranger wrote the bytes.
 * The seam, if it grows again, is the channel length census, which is about the
 * reliable channel rather than about the score.
 *
 * The scoring is four sentences long and every one of them is a rule somebody can lose a round
 * by, so each gets its own case: a kill, a betrayal, a suicide, and a death with nobody to blame.
 * The endings get more than that, because an ending cannot be taken back: on points, on time, and
 * level, with the last one pinning that there is no sudden death.
 *
 * The section before the last is the channel's length census. The recogniser for every message
 * kind on the reliable channel tests its length together with its first byte, so a new kind that
 * lands on somebody else's length is only safe while the tags differ, and a new kind whose length
 * is a FAMILY has to be walked rather than argued about. This one is a family, and so are two of
 * the kinds it has to stay clear of.
 */
#include "unittest.h"

#include "mp_channel.h"
#include "mp_events.h"
#include "mp_hit_relay.h"
#include "mp_lobby.h"
#include "mp_roster.h"
#include "mp_rules.h"
#include "mp_scratch_wire.h"
#include "mp_score.h"
#include "mp_world.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void start_round(mp_score_t *score, uint16_t score_limit, uint16_t time_limit_s,
                        bool teams)
{
    mp_rules_t rules;

    mp_rules_default(&rules);
    rules.score_limit  = score_limit;
    rules.time_limit_s = time_limit_s;
    rules.flags        = teams ? (uint8_t)MP_RULES_F_TEAMS : 0u;
    mp_score_reset(score, &rules, 1u, 1000u);
}

static void kill(mp_score_t *score, uint8_t victim, uint8_t killer, uint8_t reason)
{
    mp_death_note_t note;

    note.victim_slot = victim;
    note.killer_slot = killer;
    note.reason      = reason;
    (void)mp_score_death(score, &note);
}

static int32_t points_of(const mp_score_t *score, uint8_t slot)
{
    const mp_score_line_t *line = mp_score_line_of(score, slot);

    return line == NULL ? -32768 : line->points;
}

static uint32_t deaths_of(const mp_score_t *score, uint8_t slot)
{
    const mp_score_line_t *line = mp_score_line_of(score, slot);

    return line == NULL ? 0xFFFFFFFFu : line->deaths;
}

static void check_the_table(void)
{
    mp_score_t score;

    ut_section("who is in the table");

    start_round(&score, 25u, 900u, true);
    ut_check(score.board.count == 0u, "a fresh round has nobody in it");
    ut_check(mp_score_add_player(&score, 0u, 1u), "the listen host takes a slot");
    ut_check(mp_score_add_player(&score, 1u, 2u), "and so does a peer");
    ut_check(score.board.count == 2u, "two players");
    ut_check(mp_score_add_player(&score, 0u, 2u) && score.board.count == 2u,
             "adding one who is already there moves their team and adds no line");
    ut_check(mp_score_line_of(&score, 0u)->team == 2u, "and the team is the one that moved");
    ut_check(mp_score_set_team(&score, 0u, 1u), "the team can be set on its own");
    ut_check(!mp_score_set_team(&score, 9u, 1u), "but not for a slot nobody holds");
    ut_check(!mp_score_add_player(&score, MP_SCORE_MAX_PLAYERS, 1u),
             "a slot past the table is not a slot");
    ut_check(!mp_score_add_player(&score, 2u, (uint8_t)(MP_LOBBY_TEAM_MAX + 1u)),
             "and a team past the last is not a team");
    ut_check(mp_score_line_of(&score, 7u) == NULL, "a slot nobody holds has no line");
    ut_check(mp_score_remove_player(&score, 0u) && score.board.count == 1u &&
                 mp_score_line_of(&score, 1u) != NULL,
             "removing one closes the gap behind it and leaves the other where it was");
    ut_check(!mp_score_remove_player(&score, 0u), "and removing it twice is refused");
}

static void check_the_scoring(void)
{
    mp_score_t score;

    ut_section("a kill, a betrayal, a suicide, and a death with nobody to blame");

    start_round(&score, 0u, 0u, true);
    (void)mp_score_add_player(&score, 0u, 1u);
    (void)mp_score_add_player(&score, 1u, 2u);
    (void)mp_score_add_player(&score, 2u, 1u);

    kill(&score, 1u, 0u, MP_DEATH_BY_HIT);
    ut_check(points_of(&score, 0u) == 1, "a killer on the other side gets a point");
    ut_check(deaths_of(&score, 1u) == 1u, "and the victim's death count goes up");

    kill(&score, 2u, 0u, MP_DEATH_BY_HIT);
    ut_check(points_of(&score, 0u) == 0,
             "a killer on the victim's own side gets the betrayal penalty instead");
    ut_check(deaths_of(&score, 2u) == 1u, "a betrayed player is still dead");

    kill(&score, 0u, 0u, MP_DEATH_BY_SUICIDE);
    ut_check(points_of(&score, 0u) == -1, "killing yourself costs the suicide penalty");
    kill(&score, 0u, MP_DEATH_NO_KILLER, MP_DEATH_BY_FALL);
    ut_check(points_of(&score, 0u) == -2, "and so does a fall nobody can be blamed for");
    ut_check(deaths_of(&score, 0u) == 2u, "both of those are deaths");

    ut_section("the reason is carried and checked, but the killer decides who scores");

    kill(&score, 1u, 0u, MP_DEATH_BY_FALL);
    ut_check(points_of(&score, 0u) == -1,
             "a player pushed off a ledge is a kill, whatever the victim's machine called it");

    ut_section("with teams off nobody is on anybody's side");

    start_round(&score, 0u, 0u, false);
    (void)mp_score_add_player(&score, 0u, 1u);
    (void)mp_score_add_player(&score, 1u, 1u);
    kill(&score, 1u, 0u, MP_DEATH_BY_HIT);
    ut_check(points_of(&score, 0u) == 1,
             "two players who both picked team one score off each other in a free for all");
}

static void check_what_a_death_refuses(void)
{
    mp_score_t      score;
    mp_death_note_t note;
    uint32_t        taken = 0;
    uint32_t        refused = 0;
    uint32_t        after_end = 0;
    uint32_t        adopted = 0;

    ut_section("what a death refuses, and every refusal is counted");

    start_round(&score, 0u, 0u, true);
    (void)mp_score_add_player(&score, 0u, 1u);
    (void)mp_score_add_player(&score, 1u, 2u);

    note.victim_slot = 5u;
    note.killer_slot = 0u;
    note.reason      = MP_DEATH_BY_HIT;
    ut_check(!mp_score_death(&score, &note), "a victim who is not in the table");

    note.victim_slot = 1u;
    note.killer_slot = 5u;
    ut_check(!mp_score_death(&score, &note),
             "a named killer who is not in it either, which is a torn or a stale note");

    note.killer_slot = 0u;
    note.reason      = (uint8_t)(MP_DEATH_REASON_MAX + 1u);
    ut_check(!mp_score_death(&score, &note), "a reason this build does not know");

    ut_check(!mp_score_death(&score, NULL) && !mp_score_death(NULL, &note), "and nothing at all");
    ut_check(points_of(&score, 0u) == 0 && deaths_of(&score, 1u) == 0u,
             "none of those moved a number");

    mp_score_counters(&score, &taken, &refused, &after_end, &adopted);
    ut_check(taken == 0u && refused == 3u && after_end == 0u,
             "three refusals counted, and no death taken");

    ut_section("a death after the round was decided is counted apart from a malformed one");

    start_round(&score, 1u, 0u, false);
    (void)mp_score_add_player(&score, 0u, 0u);
    (void)mp_score_add_player(&score, 1u, 0u);
    kill(&score, 1u, 0u, MP_DEATH_BY_HIT);
    ut_check(mp_score_outcome(&score, NULL) == MP_SCORE_WON_PLAYER, "one point ends this round");
    kill(&score, 0u, 1u, MP_DEATH_BY_HIT);
    mp_score_counters(&score, &taken, &refused, &after_end, &adopted);
    ut_check(after_end == 1u, "the death that came after it is counted on its own line");
    ut_check(refused == 0u,
             "and not as a malformed one, because the two say different things about a session");
    ut_check(taken == 1u, "one death was taken, the one that decided the round");
}

static void check_the_endings(void)
{
    mp_score_t score;
    uint8_t    winner = 0;

    ut_section("a round ends on points");

    start_round(&score, 2u, 0u, false);
    (void)mp_score_add_player(&score, 0u, 0u);
    (void)mp_score_add_player(&score, 1u, 0u);
    kill(&score, 1u, 0u, MP_DEATH_BY_HIT);
    ut_check(mp_score_outcome(&score, &winner) == MP_SCORE_RUNNING, "one point is not two");
    kill(&score, 1u, 0u, MP_DEATH_BY_HIT);
    ut_check(mp_score_outcome(&score, &winner) == MP_SCORE_WON_PLAYER && winner == 0u,
             "the second point ends it, and the winner is the slot that scored it");

    kill(&score, 0u, 1u, MP_DEATH_BY_HIT);
    ut_check(mp_score_outcome(&score, &winner) == MP_SCORE_WON_PLAYER && winner == 0u,
             "a death after the decision does not move the winner");
    ut_check(points_of(&score, 1u) == 0, "and it does not score either");

    ut_section("a team round ends on the team's points, not on one player's");

    start_round(&score, 3u, 0u, true);
    (void)mp_score_add_player(&score, 0u, 1u);
    (void)mp_score_add_player(&score, 1u, 1u);
    (void)mp_score_add_player(&score, 2u, 2u);
    kill(&score, 2u, 0u, MP_DEATH_BY_HIT);
    kill(&score, 2u, 1u, MP_DEATH_BY_HIT);
    ut_check(mp_score_team_points(&score, 1u) == 2, "two players of a team have two between them");
    ut_check(mp_score_outcome(&score, &winner) == MP_SCORE_RUNNING, "which is not three");
    kill(&score, 2u, 0u, MP_DEATH_BY_HIT);
    ut_check(mp_score_outcome(&score, &winner) == MP_SCORE_WON_TEAM && winner == 1u,
             "the third ends it and the winner is the team");

    ut_section("a round ends on time, and the clock is the host's substeps");

    start_round(&score, 0u, 10u, false);
    (void)mp_score_add_player(&score, 0u, 0u);
    (void)mp_score_add_player(&score, 1u, 0u);
    kill(&score, 1u, 0u, MP_DEATH_BY_HIT);
    mp_score_host_tick(&score, 1000u + 10u * 32u - 1u);
    ut_check(mp_score_outcome(&score, &winner) == MP_SCORE_RUNNING,
             "one substep short of the limit is short of it");
    ut_check(mp_score_remaining(&score) == 1u, "with one substep left to run");
    mp_score_host_tick(&score, 1000u + 10u * 32u);
    ut_check(mp_score_outcome(&score, &winner) == MP_SCORE_WON_PLAYER && winner == 0u,
             "and the limit itself ends it, in favour of whoever is ahead");
    ut_check(mp_score_remaining(&score) == 0u, "a round that is over has no time left");
    ut_check(mp_score_elapsed(&score) == 10u * 32u,
             "the elapsed time is measured from the substep the round began on");

    ut_section("level at the end is a draw, and there is no sudden death");

    start_round(&score, 0u, 10u, false);
    (void)mp_score_add_player(&score, 0u, 0u);
    (void)mp_score_add_player(&score, 1u, 0u);
    kill(&score, 1u, 0u, MP_DEATH_BY_HIT);
    kill(&score, 0u, 1u, MP_DEATH_BY_HIT);
    mp_score_host_tick(&score, 1000u + 10u * 32u);
    ut_check(mp_score_outcome(&score, &winner) == MP_SCORE_DRAW && winner == MP_SCORE_NOBODY,
             "two players level when the time runs out end level");

    start_round(&score, 0u, 10u, true);
    (void)mp_score_add_player(&score, 0u, 1u);
    (void)mp_score_add_player(&score, 1u, 2u);
    mp_score_host_tick(&score, 1000u + 10u * 32u);
    ut_check(mp_score_outcome(&score, &winner) == MP_SCORE_DRAW,
             "and so do two teams that never scored");

    ut_section("no limit means no ending");

    start_round(&score, 0u, 0u, false);
    (void)mp_score_add_player(&score, 0u, 0u);
    kill(&score, 0u, MP_DEATH_NO_KILLER, MP_DEATH_BY_FALL);
    mp_score_host_tick(&score, 1000u + 100000u);
    ut_check(mp_score_outcome(&score, &winner) == MP_SCORE_RUNNING,
             "a round with neither limit runs until somebody stops it");
    ut_check(mp_score_remaining(&score) == 0u,
             "and it has no time left to show, because it has no time limit");
}

static void check_the_round_trip(void)
{
    mp_score_t       score;
    mp_score_board_t back;
    uint8_t          note[MP_SCORE_BYTES];
    size_t           bytes;

    ut_section("the table on the wire");

    start_round(&score, 5u, 60u, true);
    (void)mp_score_add_player(&score, 0u, 1u);
    (void)mp_score_add_player(&score, 3u, 2u);
    kill(&score, 3u, 0u, MP_DEATH_BY_HIT);
    kill(&score, 0u, 0u, MP_DEATH_BY_SUICIDE);
    mp_score_host_tick(&score, 1000u + 77u);

    bytes = mp_score_encode(&score.board, note, sizeof note);
    ut_check(bytes == MP_SCORE_BYTES_FOR(2u), "two players weigh what the header promises");
    ut_check(note[0] == MP_SCORE_TAG, "the tag leads");
    ut_check(mp_score_is_score(note, bytes), "recognised by tag and length together");
    ut_check(mp_score_decode(note, bytes, &back), "decoded");
    ut_check(back.count == 2u && back.generation == 1u && back.elapsed == 77u,
             "the count, the round it belongs to and the host's own clock survive");
    ut_check(back.line[0].slot == 0u && back.line[0].team == 1u && back.line[0].points == 0 &&
                 back.line[0].deaths == 1u,
             "and so does a line whose points went up and back down again");
    ut_check(back.line[1].slot == 3u && back.line[1].points == 0 && back.line[1].deaths == 1u,
             "and the second line with it");

    ut_section("an empty table is a table");

    start_round(&score, 0u, 0u, false);
    bytes = mp_score_encode(&score.board, note, sizeof note);
    ut_check(bytes == MP_SCORE_BYTES_FOR(0u) && mp_score_is_score(note, bytes) &&
                 mp_score_decode(note, bytes, &back) && back.count == 0u,
             "a round nobody has joined yet crosses as a header and nothing else");

    ut_section("a negative score survives, because the penalties are the only thing that "
               "produces one");

    start_round(&score, 0u, 0u, false);
    (void)mp_score_add_player(&score, 2u, 0u);
    kill(&score, 2u, MP_DEATH_NO_KILLER, MP_DEATH_BY_FALL);
    kill(&score, 2u, MP_DEATH_NO_KILLER, MP_DEATH_BY_FALL);
    bytes = mp_score_encode(&score.board, note, sizeof note);
    ut_check(bytes != 0u && mp_score_decode(note, bytes, &back) && back.line[0].points == -2,
             "minus two goes out and comes back as minus two");
}

static void check_what_the_wire_refuses(void)
{
    mp_score_t       score;
    mp_score_board_t back;
    uint8_t          note[MP_SCORE_BYTES];
    size_t           bytes;

    ut_section("what the table refuses, in both directions");

    start_round(&score, 0u, 0u, false);
    (void)mp_score_add_player(&score, 0u, 0u);
    bytes = mp_score_encode(&score.board, note, sizeof note);
    ut_check(bytes == MP_SCORE_BYTES_FOR(1u), "a sound table exists to forge from");
    ut_check(mp_score_encode(&score.board, note, bytes - 1u) == 0u,
             "a buffer one byte short of it");

    score.board.count = (uint8_t)(MP_SCORE_MAX_PLAYERS + 1u);
    ut_check(mp_score_encode(&score.board, note, sizeof note) == 0u, "a count past the table");
    score.board.count = 1u;
    score.board.outcome = (uint8_t)(MP_SCORE_OUTCOME_MAX + 1u);
    ut_check(mp_score_encode(&score.board, note, sizeof note) == 0u,
             "an ending this build does not know");
    score.board.outcome = (uint8_t)MP_SCORE_WON_TEAM;
    score.board.winner  = (uint8_t)MP_LOBBY_TEAM_NONE;
    ut_check(mp_score_encode(&score.board, note, sizeof note) == 0u,
             "a team win whose winner is not a team");
    score.board.outcome = (uint8_t)MP_SCORE_RUNNING;
    score.board.winner  = 0u;
    ut_check(mp_score_encode(&score.board, note, sizeof note) == 0u,
             "and a running round that names a winner: running and drawn both name nobody, in "
             "the one spelling, so a receiver never has to guess which no-winner it is reading");

    ut_section("and it refuses the same things when a stranger sent them");

    start_round(&score, 0u, 0u, false);
    (void)mp_score_add_player(&score, 0u, 0u);
    bytes = mp_score_encode(&score.board, note, sizeof note);
    ut_check(bytes == MP_SCORE_BYTES_FOR(1u), "another sound one");
    ut_check(!mp_score_is_score(note, bytes - 1u), "a length that is not exact");
    ut_check(!mp_score_is_score(note, bytes + 1u), "in either direction");
    note[0] = (uint8_t)MP_ROSTER_TAG;
    ut_check(!mp_score_is_score(note, bytes), "a foreign tag at the right length");
    note[0] = (uint8_t)MP_SCORE_TAG;
    note[2] = (uint8_t)MP_SCORE_MAX_PLAYERS;
    ut_check(!mp_score_is_score(note, bytes),
             "a count that promises more than the message carries, which is what makes this one "
             "exact length rather than a range");
    note[2] = 1u;
    note[3] = (uint8_t)(MP_SCORE_OUTCOME_MAX + 1u);
    ut_check(!mp_score_decode(note, bytes, &back), "an ending nobody knows");
    note[3] = (uint8_t)MP_SCORE_RUNNING;
    note[9] = (uint8_t)MP_SCORE_MAX_PLAYERS;   /* the first line's slot */
    ut_check(!mp_score_decode(note, bytes, &back), "a slot past the table");
    note[9] = 0u;
    note[10] = (uint8_t)(MP_LOBBY_TEAM_MAX + 1u);
    ut_check(!mp_score_decode(note, bytes, &back), "a team that is not one");
    note[10] = 0u;
    note[11] = 0xFFu;   /* the first line's biased points */
    note[12] = 0xFFu;
    ut_check(!mp_score_decode(note, bytes, &back),
             "and a points value outside the span the encoder promises");
}

/* The client takes the host's figures. It does not compare them against its own, because its own
 * were only ever there so the display did not have to wait a round trip for a kill it had already
 * seen. */
static void check_the_adoption(void)
{
    mp_score_t       host;
    mp_score_t       client;
    mp_score_board_t board;
    uint8_t          note[MP_SCORE_BYTES];
    size_t           bytes;
    uint32_t         adopted = 0;

    ut_section("the host's table is the truth and the client does not argue with it");

    start_round(&host, 0u, 0u, false);
    (void)mp_score_add_player(&host, 0u, 0u);
    (void)mp_score_add_player(&host, 1u, 0u);
    kill(&host, 1u, 0u, MP_DEATH_BY_HIT);
    kill(&host, 1u, 0u, MP_DEATH_BY_HIT);

    /* The client guessed differently, and higher, which is the case that matters: a merge or a
     * highest-wins rule would keep the wrong number. */
    start_round(&client, 0u, 0u, false);
    (void)mp_score_add_player(&client, 0u, 0u);
    (void)mp_score_add_player(&client, 1u, 0u);
    kill(&client, 1u, 0u, MP_DEATH_BY_HIT);
    kill(&client, 1u, 0u, MP_DEATH_BY_HIT);
    kill(&client, 1u, 0u, MP_DEATH_BY_HIT);
    ut_check(points_of(&client, 0u) == 3, "the client had counted three");

    bytes = mp_score_encode(&host.board, note, sizeof note);
    ut_check(bytes != 0u && mp_score_decode(note, bytes, &board), "the host's table crossed");
    mp_score_adopt(&client, &board);
    ut_check(points_of(&client, 0u) == 2, "and two is what the client has afterwards");
    ut_check(deaths_of(&client, 1u) == 2u, "the death counts come across with the points");

    mp_score_counters(&client, NULL, NULL, NULL, &adopted);
    ut_check(adopted == 1u, "and the adoption is counted");

    ut_section("a table from another round comes across whole, generation and all");

    board.generation = 9u;
    board.outcome    = (uint8_t)MP_SCORE_RUNNING;
    board.winner     = (uint8_t)MP_SCORE_NOBODY;
    mp_score_adopt(&client, &board);
    ut_check(client.board.generation == 9u,
             "a client whose own idea of the round is stale follows the host into the new one "
             "rather than dropping its table");
}

/* Every length the reliable channel carries, counted rather than remembered.
 *
 * Two of the kinds on this channel are FAMILIES rather than single lengths, and this is now a
 * third. A family cannot be excluded by naming one number, so all three ladders are walked.
 */
static void check_the_channel_lengths(void)
{
    static const struct { const char *what; unsigned bytes; } FIXED[] = {
        { "the world slot note",     MP_EVENT_FOREIGN_SLOT_NOTE_BYTES },
        { "the acknowledgement",     MP_EVENT_FOREIGN_ACK_BYTES },
        { "the lobby line",          MP_LOBBY_BYTES },
        { "a death",                 MP_EVENT_DEATH_BYTES },
        { "a reported hit",          MP_HIT_RELAY_BYTES },
        { "the content fingerprint", MP_LOBBY_CONTENT_BYTES },
        { "a push",                  MP_EVENT_PUSH_BYTES },
        { "a weapon change",         MP_EVENT_WEAPON_BYTES },
        { "a player hit",            MP_PLAYER_HIT_BYTES },
        { "a sabre action",          MP_EVENT_SABRE_BYTES },
        { "a mover",                 MP_EVENT_MOVER_BYTES },
        { "an empty digest",         MP_WORLD_DIGEST_HEADER_BYTES },
        { "a despawn",               MP_EVENT_DESPAWN_BYTES },
        { "a pickup claim",          MP_EVENT_PICKUP_BYTES },
        { "a spawn",                 MP_EVENT_SPAWN_BYTES },
        { "a shot",                  MP_EVENT_SHOT_BYTES },
        { "an appearance change",    MP_EVENT_SKIN_BYTES },
        { "the blackboard",          MP_SCRATCH_WIRE_AI_BYTES },
        { "the lobby setup",         MP_LOBBY_SETUP_BYTES },
    };
    unsigned players;
    unsigned entries;
    size_t   i;

    ut_section("no table on the score ladder has a length any fixed message on the channel has");

    for (players = 0; players <= MP_SCORE_MAX_PLAYERS; ++players) {
        unsigned table = (unsigned)MP_SCORE_BYTES_FOR(players);

        for (i = 0; i < sizeof FIXED / sizeof FIXED[0]; ++i) {
            ut_checkf(table != FIXED[i].bytes, "a table of %u player(s) is %u bytes, and %s is %u",
                      players, table, FIXED[i].what, FIXED[i].bytes);
        }
    }

    ut_section("the setup note collides with nothing fixed");

    /* The setup is the last row of the table and is skipped BY POSITION, not by value. Skipping
     * whatever has the setup's length would skip a real collision along with the
     * self-comparison, which is the one thing this section exists to catch. */
    for (i = 0; i + 1u < sizeof FIXED / sizeof FIXED[0]; ++i) {
        ut_checkf(FIXED[i].bytes != MP_LOBBY_SETUP_BYTES, "%s is %u bytes, not %u",
                  FIXED[i].what, FIXED[i].bytes, (unsigned)MP_LOBBY_SETUP_BYTES);
    }
    ut_check(FIXED[sizeof FIXED / sizeof FIXED[0] - 1u].bytes == MP_LOBBY_SETUP_BYTES,
             "and the row that was skipped is the setup itself, which is what makes the loop "
             "above a census rather than a tautology");
    /* Written as the SUM rather than as the number it comes to, because the number went stale the
     * moment the savegame's name and size joined the note and this check then failed for a change
     * that was correct. What is worth asserting here is the composition: every part of the note is
     * named, so a part that is added without a thought about this file fails with the reason in
     * front of the reader rather than as two numbers that differ. */
    ut_check(MP_LOBBY_SETUP_BYTES == 92u + MP_RULES_BYTES + 1u + 8u + 1u,
             "ninety two as it shipped, plus nine of rules, one of generation, eight of the "
             "savegame's name and size and one of the host's difficulty");
    ut_check(MP_RULES_BYTES == 9u, "and the rule set is the nine bytes it says it is");

    ut_section("the roster and the score table cannot be confused at any population");

    /* This used to be argued by parity, an odd score table against an even roster, and the
     * roster entry has an odd width since it carries the kind of its asset. So the two ladders
     * are walked against each other, which holds whatever the widths become; the tags differ
     * as well. */
    for (players = 0; players <= MP_ROSTER_MAX_ENTRIES; ++players) {
        for (entries = 0; entries <= MP_SCORE_MAX_PLAYERS; ++entries) {
            ut_checkf(MP_ROSTER_BYTES_FOR(players) != MP_SCORE_BYTES_FOR(entries),
                      "a roster of %u player(s) is %u bytes, a table of %u is %u", players,
                      (unsigned)MP_ROSTER_BYTES_FOR(players), entries,
                      (unsigned)MP_SCORE_BYTES_FOR(entries));
        }
    }
    ut_check(MP_ROSTER_TAG != MP_SCORE_TAG, "and the two tags differ");

    ut_section("the two families that DO meet the score ladder are separated by their tags");

    /* The map's digest is eight bytes plus seven per mover, so its odd lengths land on the score
     * ladder now and again. That is allowed, because every recogniser on this channel tests its
     * tag as well as its length, and this is the check that says the tags differ. */
    for (entries = 0; entries <= MP_WORLD_DIGEST_MAX_ENTRIES; ++entries) {
        unsigned digest = MP_WORLD_DIGEST_HEADER_BYTES + entries * MP_WORLD_DIGEST_ENTRY_BYTES;
        unsigned found = 0;

        for (players = 0; players <= MP_SCORE_MAX_PLAYERS; ++players) {
            if ((unsigned)MP_SCORE_BYTES_FOR(players) == digest) {
                found = 1u;
            }
        }
        if (found != 0u) {
            ut_checkf(MP_WORLD_DIGEST_TAG != MP_SCORE_TAG,
                      "a digest of %u mover(s) is %u bytes, which a table also reaches, so the "
                      "tags have to differ", entries, digest);
        }
    }
    ut_check(MP_SCRATCH_TAG_BANK != MP_SCORE_TAG && MP_SCRATCH_TAG_AI != MP_SCORE_TAG,
             "the campaign bank's length is genuinely free, so its tags carry the whole "
             "separation");
    ut_check(MP_SCORE_TAG != MP_ROSTER_TAG && MP_SCORE_TAG != MP_LOBBY_SETUP_TAG &&
                 MP_SCORE_TAG != MP_EVENT_DEATH && MP_SCORE_TAG != MP_LOBBY_CONTENT_TAG,
             "and the score tag is none of the four nearest to it");

    ut_section("the largest table still fits one reliable message");

    ut_check(MP_SCORE_BYTES <= MP_CHANNEL_MESSAGE_BYTES,
             "a full table of sixteen fits, which the channel refuses outright otherwise");
    ut_check(MP_LOBBY_SETUP_BYTES <= MP_CHANNEL_MESSAGE_BYTES, "and so does the setup note");
    ut_check(MP_SCORE_MAX_PLAYERS == MP_ROSTER_MAX_ENTRIES,
             "and the table holds exactly as many players as the roster names");
}

/* The sequence the bridge actually performs, end to end.
 *
 * Every rule above is driven on its own, and each of them passed while nothing in the game called
 * any of it. This runs the calls in the order and with the arguments the wiring uses them in: the
 * round is started from the rule set and the generation that came out of the host's setup note;
 * the players are seeded from a roster; deaths arrive through the one listener both machines feed;
 * the clock is a substep count that only ever moves forward; the host encodes its table and the
 * client decodes and adopts it; and the next world change starts a fresh round on the same
 * structure.
 *
 * What it can catch that the sections above cannot is an ordering fault: a table that survives a
 * round it should not have, a clock read before the round it belongs to, an adoption that leaves a
 * client's own arithmetic standing beside the host's. */
static void check_the_round_the_bridge_drives(void)
{
    mp_rules_t       rules;
    mp_score_t       host;
    mp_score_t       client;
    mp_score_board_t board;
    uint8_t          note[MP_SCORE_BYTES];
    uint32_t         substep = 4000u;   /* the host's counter, wherever it happens to stand */
    size_t           bytes;
    uint8_t          winner = 0;

    ut_section("the round, driven the way the bridge drives it");

    /* What the lobby put in the setup note, and what both sides read back out of it. */
    mp_rules_default(&rules);
    rules.score_limit  = 2u;
    rules.time_limit_s = 0u;
    mp_score_reset(&host, &rules, 9u, substep);
    mp_score_reset(&client, &rules, 9u, substep);

    /* Seeded from the roster, on both sides, and twice on the host because it reseeds before
     * every repeat: a late joiner and a team change both arrive that way. */
    ut_check(mp_score_add_player(&host, 0u, 1u), "the host takes its own slot");
    ut_check(mp_score_add_player(&host, 1u, 2u), "and the peer's");
    ut_check(mp_score_add_player(&host, 0u, 1u), "and seeding again is not a second line");
    ut_check(mp_score_add_player(&client, 0u, 1u) && mp_score_add_player(&client, 1u, 2u),
             "the client seeds the same table from the same roster");

    /* Two deaths, each through the shape the death listener is handed. */
    kill(&host, 0u, 1u, (uint8_t)MP_DEATH_BY_HIT);
    kill(&client, 0u, 1u, (uint8_t)MP_DEATH_BY_HIT);
    substep += 64u;
    mp_score_host_tick(&host, substep);
    ut_check(mp_score_elapsed(&host) == 64u,
             "the elapsed time is the difference from the substep the round began on");
    ut_check(mp_score_outcome(&host, &winner) == (uint8_t)MP_SCORE_RUNNING,
             "one kill of two does not end it");

    /* The host publishes, the client adopts, and the client's own arithmetic does not survive. */
    kill(&host, 0u, 1u, (uint8_t)MP_DEATH_BY_HIT);
    substep += 32u;
    mp_score_host_tick(&host, substep);
    ut_check(mp_score_outcome(&host, &winner) != (uint8_t)MP_SCORE_RUNNING,
             "the second one reaches the limit and the round is decided");
    bytes = mp_score_encode(&host.board, note, sizeof note);
    ut_checkf(bytes == MP_SCORE_BYTES_FOR(2u), "the table of two encodes to its own length (%u)",
              (unsigned)bytes);
    ut_check(mp_score_is_score(note, bytes), "and is recognised by tag and length together");
    ut_check(mp_score_decode(note, bytes, &board), "and decodes");
    mp_score_adopt(&client, &board);
    ut_check(points_of(&client, 1u) == points_of(&host, 1u),
             "the client's table is the host's, point for point");
    ut_check(deaths_of(&client, 0u) == deaths_of(&host, 0u), "deaths and all");
    ut_check(mp_score_elapsed(&client) == mp_score_elapsed(&host),
             "and it carries the host's clock rather than the client's, which has none");
    ut_check(mp_score_outcome(&client, &winner) == mp_score_outcome(&host, &winner),
             "so both sides say the same round is over");

    /* The next world change. The bridge starts a round on a generation it has not scored yet, and
     * nothing of the last one may be left standing. */
    substep += 500u;
    mp_score_reset(&host, &rules, 10u, substep);
    ut_check(host.board.count == 0u, "a new round starts with an empty table");
    ut_check(mp_score_outcome(&host, &winner) == (uint8_t)MP_SCORE_RUNNING,
             "and running, whatever the last one ended as");
    ut_check(host.board.generation == 10u, "under the generation it was started for");
    mp_score_host_tick(&host, substep);
    ut_check(mp_score_elapsed(&host) == 0u, "with the clock starting where this round did");
}

int main(void)
{
    check_the_table();
    check_the_scoring();
    check_what_a_death_refuses();
    check_the_endings();
    check_the_round_trip();
    check_what_the_wire_refuses();
    check_the_adoption();
    check_the_channel_lengths();
    check_the_round_the_bridge_drives();
    return ut_summary("mp_score");
}
