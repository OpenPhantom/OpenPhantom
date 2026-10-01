/* What the live scoreboard decides before it draws anything, driven with no engine and no font.
 *
 * Six subjects, and each is a decision the panel would otherwise be making inside a frame where
 * nothing can be observed: whether it is on screen at all, which rows it shows and in what order,
 * how wide each column has to be, what a clock with no limit reads as, how a result reads in
 * words, and which key a configured name stands for.
 *
 * The width checks are the ones worth having. They are held against the font's own advance table
 * rather than against a number typed here, and the space is checked on purpose: a space carries
 * the pen five units and has no glyph rectangle at all, so the engine's own string measurement
 * answers one for it. A column sized that way is short by four units per space, and a header line
 * with three double spaces in it is short by twenty four.
 */
#include "unittest.h"

#include "mp_board.h"
#include "mp_menu_metrics.h"
#include "mp_roster.h"
#include "mp_rules.h"
#include "mp_score.h"
#include "mp_text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void put_line(mp_score_board_t *board, uint8_t slot, uint8_t team, int16_t points,
                     uint16_t deaths)
{
    board->line[board->count].slot   = slot;
    board->line[board->count].team   = team;
    board->line[board->count].points = points;
    board->line[board->count].deaths = deaths;
    ++board->count;
}

static void name_slot(mp_roster_t *roster, uint8_t slot, const char *name)
{
    size_t at = roster->count;

    roster->entry[at].slot = slot;
    memset(roster->entry[at].name, 0, sizeof roster->entry[at].name);
    memcpy(roster->entry[at].name, name, strlen(name));
    ++roster->count;
}

static void check_visibility(void)
{
    ut_section("when the board is on screen");

    ut_check(!mp_board_visible(false, true, true, true, true),
             "no deathmatch round means no board, whatever else is true");
    ut_check(!mp_board_visible(true, false, true, true, true),
             "no running level means no board, so it stays out of the lobby and the title screen");
    ut_check(!mp_board_visible(true, true, false, false, false),
             "a live round with nobody asking and nobody dead shows nothing");
    ut_check(mp_board_visible(true, true, true, false, false),
             "holding the key shows it");
    ut_check(mp_board_visible(true, true, false, true, false),
             "a player waiting to come back sees it without pressing anything, which is the case "
             "the pause menu cannot serve");
    ut_check(mp_board_visible(true, true, false, false, true),
             "a decided round shows its own result without anybody asking");
}

static void check_rows(void)
{
    mp_score_board_t board;
    mp_roster_t      roster;
    mp_board_row_t   row[MP_BOARD_MAX_ROWS];
    size_t           rows;

    ut_section("the rows and their order");

    memset(&board, 0, sizeof board);
    memset(&roster, 0, sizeof roster);
    put_line(&board, 0u, 1u, 3, 4u);
    put_line(&board, 1u, 2u, 9, 1u);
    put_line(&board, 2u, 1u, 3, 2u);
    put_line(&board, 3u, 2u, 3, 2u);
    name_slot(&roster, 1u, "MAUL");

    rows = mp_board_build(&board, &roster, 2u, row, MP_BOARD_MAX_ROWS);
    ut_check(rows == 4u, "every line in the table becomes a row");
    ut_check(row[0].slot == 1u, "the most points comes first");
    ut_check(row[1].slot == 2u, "a tie on points is broken by the fewer deaths");
    ut_check(row[2].slot == 3u, "a tie on both is broken by the lower world slot");
    ut_check(row[3].slot == 0u, "and the rest follow in the same order");
    ut_check(strcmp(row[0].name, "MAUL") == 0, "a slot the roster names carries that name");
    ut_check(strcmp(row[1].name, "SPIELER 2") == 0,
             "a slot the roster has not described yet gets a stand-in rather than a blank line");
    ut_check(row[1].is_self && !row[0].is_self,
             "exactly this machine's own world slot is marked");

    rows = mp_board_build(&board, &roster, 2u, row, 2u);
    ut_check(rows == 2u, "a caller with room for two rows gets two rather than an overrun");
    ut_check(mp_board_build(NULL, &roster, 0u, row, MP_BOARD_MAX_ROWS) == 0u,
             "no table means no rows");

    memset(&board, 0, sizeof board);
    put_line(&board, 7u, 0u, 0, 0u);
    rows = mp_board_build(&board, NULL, 9u, row, MP_BOARD_MAX_ROWS);
    ut_check(rows == 1u && strcmp(row[0].name, "SPIELER 7") == 0,
             "with no roster at all every row still has a name");
}

static void check_widths(void)
{
    mp_board_row_t     row[2];
    mp_board_columns_t columns;
    int32_t            expected;

    ut_section("the column widths");

    ut_check(mp_board_text_width(NULL) == 0, "no string is no width");
    ut_check(mp_board_text_width("") == 0, "an empty string is no width");
    ut_check(mp_board_text_width(" ") == (int32_t)MP_MENU_ADVANCE_SYSFONT[(unsigned char)' '],
             "a space is as wide as the font says it is, which is five and not zero");
    ut_check(mp_board_text_width("A B") ==
                 (int32_t)(MP_MENU_ADVANCE_SYSFONT[(unsigned char)'A'] +
                           MP_MENU_ADVANCE_SYSFONT[(unsigned char)' '] +
                           MP_MENU_ADVANCE_SYSFONT[(unsigned char)'B']),
             "a width is the sum of the advances, spaces included");
    ut_check(mp_board_text_width("AB\nCDEFGH") ==
                 (int32_t)(MP_MENU_ADVANCE_SYSFONT[(unsigned char)'A'] +
                           MP_MENU_ADVANCE_SYSFONT[(unsigned char)'B']),
             "a measurement stops at a newline instead of adding both lines together");

    memset(row, 0, sizeof row);
    row[0].slot = 0u;
    row[0].team = 1u;
    row[0].points = 7;
    row[0].deaths = 3;
    memcpy(row[0].name, "OBI", 3);
    row[1].slot = 1u;
    row[1].team = 2u;
    row[1].points = -123;
    row[1].deaths = 45;
    memcpy(row[1].name, "EINSEHRLANGERNAME", 17);

    mp_board_columns(row, 2u, &columns);
    ut_check(columns.name == mp_board_text_width("EINSEHRLANGERNAME"),
             "the name column is as wide as the longest name");
    ut_check(mp_board_text_width("-123") > mp_board_text_width("123"),
             "a minus sign is measured, so a negative score is wider than the same digits");
    ut_check(columns.points == mp_board_text_width(mp_board_heading(1)),
             "the heading is wider than a three digit score with a sign, so it sets the column");
    ut_check(columns.deaths == mp_board_text_width(mp_board_heading(2)),
             "a heading wider than every number under it keeps the column open");
    row[1].points = -123456;
    mp_board_columns(row, 2u, &columns);
    ut_check(columns.points == mp_board_text_width("-123456"),
             "and a number wider than the heading takes the column back");
    row[1].points = -123;
    mp_board_columns(row, 2u, &columns);
    expected = columns.name + columns.points + columns.deaths + columns.team +
               3 * MP_BOARD_COLUMN_GAP;
    ut_check(columns.total == expected, "the total is the four columns and the three gaps");

    mp_board_columns(NULL, 0u, &columns);
    ut_check(columns.name == mp_board_text_width(mp_board_heading(0)),
             "with no rows at all every column is still as wide as its own heading");
}

static void check_clock(void)
{
    mp_score_board_t board;
    mp_rules_t       rules;
    char             text[8];

    ut_section("the clock");

    mp_board_clock(0u, false, text, sizeof text);
    ut_check(strcmp(text, "--:--") == 0,
             "no time limit reads as dashes, because a zero there would read as no time left");
    mp_board_clock(32u * 95u, true, text, sizeof text);
    ut_check(strcmp(text, "01:35") == 0, "thirty two substeps are one second");
    mp_board_clock(0u, true, text, sizeof text);
    ut_check(strcmp(text, "00:00") == 0, "a limit that has run out reads as zero rather than off");
    mp_board_clock(0xFFFFFFFFu, true, text, sizeof text);
    ut_check(strcmp(text, "99:59") == 0,
             "a torn count is clamped rather than printed past the field it is written into");

    memset(&board, 0, sizeof board);
    mp_rules_default(&rules);
    board.elapsed = 32u * 60u;
    rules.time_limit_s = 120u;
    ut_check(mp_board_remaining(&board, &rules) == 32u * 60u,
             "what is left is the limit less what has run");
    board.elapsed = 32u * 300u;
    ut_check(mp_board_remaining(&board, &rules) == 0u, "a limit already past leaves nothing");
    rules.time_limit_s = 0u;
    board.elapsed = 32u;
    ut_check(mp_board_remaining(&board, &rules) == 0u, "no limit leaves nothing to count down");
    rules.time_limit_s = 120u;
    board.outcome = (uint8_t)MP_SCORE_DRAW;
    ut_check(mp_board_remaining(&board, &rules) == 0u, "a decided round has no time left in it");
}

static void check_sentences(void)
{
    mp_score_board_t board;
    mp_rules_t       rules;
    mp_board_row_t   row[2];
    char             text[MP_BOARD_LINE_MAX];

    ut_section("the two sentences");

    memset(&board, 0, sizeof board);
    memset(row, 0, sizeof row);
    mp_rules_default(&rules);

    mp_board_headline(&board, &rules, text, sizeof text);
    ut_check(strstr(text, "ZIEL 25") != NULL, "the headline names the points a round is won on");
    ut_check(strstr(text, "ZEIT 15:00") != NULL, "and how long there is left of it");
    rules.score_limit = 0u;
    rules.time_limit_s = 0u;
    mp_board_headline(&board, &rules, text, sizeof text);
    ut_check(strstr(text, "ZIEL OFFEN") != NULL && strstr(text, "--:--") != NULL,
             "a round with neither limit says so in both places");

    mp_board_result(&board, row, 0u, text, sizeof text);
    ut_check(strcmp(text, "DIE RUNDE LAEUFT") == 0, "an undecided round says it is still running");

    board.outcome = (uint8_t)MP_SCORE_WON_TEAM;
    board.winner  = 2u;
    mp_board_result(&board, row, 0u, text, sizeof text);
    ut_check(strcmp(text, "SIEGER: TEAM 2") == 0, "a team win names the team");

    row[0].slot = 4u;
    memcpy(row[0].name, "PANAKA", 6);
    board.outcome = (uint8_t)MP_SCORE_WON_PLAYER;
    board.winner  = 4u;
    mp_board_result(&board, row, 1u, text, sizeof text);
    ut_check(strcmp(text, "SIEGER: PANAKA") == 0,
             "a player win names the player rather than the world slot nobody can read");
    board.winner = 9u;
    mp_board_result(&board, row, 1u, text, sizeof text);
    ut_check(strcmp(text, "SIEGER: SPIELER 9") == 0,
             "a winning slot with no row behind it falls back to the number rather than a blank");

    board.outcome = (uint8_t)MP_SCORE_DRAW;
    mp_board_result(&board, row, 1u, text, sizeof text);
    ut_check(strcmp(text, "UNENTSCHIEDEN") == 0, "a draw is a draw, and there is no sudden death");

    ut_check(strcmp(mp_board_team_word(0u), "-") == 0,
             "a player with no team shows a dash, because no team is what a free for all is");
    ut_check(strcmp(mp_board_team_word(1u), "1") == 0 &&
                 strcmp(mp_board_team_word(2u), "2") == 0,
             "the two numbered teams show their number");
    ut_check(strcmp(mp_board_team_word(200u), "-") == 0,
             "a team number this build does not know is not a side either");
    ut_check(mp_board_heading(4)[0] == '\0', "there is no fifth column to name");
}

static void check_key(void)
{
    ut_section("the configured key");

    ut_check(mp_board_key_code("TAB") == 0x09, "TAB is the default and is spelled out");
    ut_check(mp_board_key_code("tab") == 0x09, "and case does not matter");
    ut_check(mp_board_key_code("b") == 'B', "a lower case letter is its upper case code");
    ut_check(mp_board_key_code("B") == 'B', "an upper case letter is itself");
    ut_check(mp_board_key_code("7") == '7', "a digit is itself");
    ut_check(mp_board_key_code("F1") == 0x70, "F1 is the first function key");
    ut_check(mp_board_key_code("f12") == 0x7B, "F12 is the last one this build accepts");
    ut_check(mp_board_key_code("F13") == 0, "F13 is not a key on this keyboard and is refused");
    ut_check(mp_board_key_code("F0") == 0, "there is no F0");
    ut_check(mp_board_key_code("F1X") == 0, "a function key with rubbish behind it is refused");
    ut_check(mp_board_key_code("") == 0 && mp_board_key_code(NULL) == 0,
             "an empty setting is refused rather than becoming key zero");
    ut_check(mp_board_key_code("SHIFT") == 0,
             "a name this build does not know is refused, and the caller falls back to TAB");
    ut_check(mp_board_key_code("#") == 0, "a punctuation mark is not one of the three shapes");
}

/* The same board in another language: the table is what speaks, and the numbers stay. */
static void check_another_language(void)
{
    mp_score_board_t board;
    mp_board_row_t   row[1];
    mp_rules_t       rules;
    char             text[MP_BOARD_LINE_MAX];

    ut_section("the sentences follow the chosen language");
    memset(&board, 0, sizeof board);
    memset(row, 0, sizeof row);
    mp_rules_default(&rules);
    mp_text_set_language(LANGUAGE_EN);
    mp_board_headline(&board, &rules, text, sizeof text);
    ut_check(strstr(text, "TARGET 25") != NULL && strstr(text, "TIME 15:00") != NULL,
             "in English the headline names the target and the time");
    board.outcome = (uint8_t)MP_SCORE_WON_TEAM;
    board.winner  = 2u;
    mp_board_result(&board, row, 0u, text, sizeof text);
    ut_check(strcmp(text, "WINNER: TEAM 2") == 0, "and a team win in English");
    mp_text_set_language(LANGUAGE_ES);
    mp_board_result(&board, row, 0u, text, sizeof text);
    ut_check(strcmp(text, "GANADOR: EQUIPO 2") == 0 &&
                 strcmp(mp_board_heading(1), "PTS") == 0,
             "in Spanish, headings included");
    mp_text_set_language(LANGUAGE_DE);
}

int main(void)
{
    /* The sentences below are the German table's, which is what this test was written
     * against; the section after them asks the others. */
    mp_text_set_language(LANGUAGE_DE);
    check_visibility();
    check_rows();
    check_widths();
    check_clock();
    check_sentences();
    check_key();
    check_another_language();

    return ut_summary("the live scoreboard");
}
