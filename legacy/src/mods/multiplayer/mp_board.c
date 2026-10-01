/* mp_board.c: the scoreboard as a decision rather than a picture.
 *
 * Nothing here draws. Every function takes what it needs and answers, so the whole of it can be
 * driven in a test: the order two players level on points end up in, the width a column needs for
 * the longest name in it, what a clock with no limit reads as, and which of five conditions puts
 * the panel on screen.
 */
#include "mp_board.h"

#include "mp_menu_metrics.h"
#include "mp_text.h"

#include "common/text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* The simulation's fixed ladder. The rule set is held against the same number at compile time in
 * its own file; this one is here because a clock printed from substeps needs it too. A round
 * timed against the wall clock instead would end at a different point in the simulation on each
 * machine, because the engine clamps the frame delta feeding its simulation, so simulated time
 * falls behind the wall clock under load and never ahead. */
#define BOARD_SUBSTEPS_PER_SECOND 32u

/* What a clock is allowed to read. An hour is the longest limit a rule set accepts, so two digits
 * of minutes are always enough; the clamp exists so that a torn number off a wire cannot produce
 * a string the field was not sized for. */
#define BOARD_CLOCK_MAX_MINUTES 99u

static const mp_text_id_t HEADING[4] = {
    MP_TEXT_BOARD_COL_NAME, MP_TEXT_BOARD_COL_POINTS, MP_TEXT_BOARD_COL_DEATHS,
    MP_TEXT_BOARD_COL_TEAM
};

/* The engine's own measure was refused for this. Its character measure at 0x0046B2FC reaches
 * 0x00479A60, which takes the glyph's rectangle in the atlas and adds one:
 *
 *   00479A80  fld [glyph+8] / fld [glyph+0xC] / fsub [glyph+4] / fsub [glyph]
 *   00479A93  fmul qword [004A8BC8]      256.0, the atlas width
 *   00479B0E  fsub dword [004A8BD8]      -1.0, so this adds one
 *
 * while the drawing pass steps the pen by a different field, the advance at glyph+0x14:
 *
 *   0047949B  fild dword [ecx+eax*8+0x14]
 *   004794A3  fmul qword [esp+0x40]      the glyph scale in pixels
 *   004794A7  faddp
 *
 * Read out of the font file, a space has a rectangle 0 wide and an advance of 5, and a capital A
 * a rectangle 15 wide and an advance of 13; two independent readings of the field gave the same
 * table. So a string measured the engine's way is short by four units for every space in it, and
 * the headline this board draws has six. The engine's string measure at 0x0046B37A also loops to
 * the NUL byte rather than stopping at a newline, which this one does. */
int32_t mp_board_text_width(const char *text)
{
    int32_t width = 0;
    size_t  i;

    if (text == NULL) {
        return 0;
    }
    for (i = 0; text[i] != '\0' && text[i] != '\n'; ++i) {
        const unsigned char glyph = (unsigned char)text[i];

        if (glyph < 128u) {
            width += (int32_t)MP_MENU_ADVANCE_SYSFONT[glyph];
        }
    }
    return width;
}

const char *mp_board_heading(size_t column)
{
    return (column < sizeof HEADING / sizeof HEADING[0]) ? mp_text(HEADING[column]) : "";
}

const char *mp_board_team_word(uint8_t team)
{
    switch (team) {
    case 1u:  return "1";
    case 2u:  return "2";
    default:  return "-";
    }
}

bool mp_board_visible(bool deathmatch_running, bool level_running, bool key_held,
                      bool waiting_to_return, bool round_decided)
{
    if (!deathmatch_running || !level_running) {
        return false;
    }
    return key_held || waiting_to_return || round_decided;
}

/* ==============================================================================================
 * The rows.
 * ============================================================================================ */

/* A name for a slot the roster has not described yet. A player who joined between two roster
 * repeats would otherwise be a blank line in the middle of the table. */
static void stand_in_name(uint8_t slot, char *out, size_t size)
{
    text_format(out, size, mp_text(MP_TEXT_BOARD_STAND_IN), (unsigned)slot);
}

static void copy_name(const mp_roster_t *roster, uint8_t slot, char *out, size_t size)
{
    size_t i;

    if (roster != NULL) {
        for (i = 0; i < roster->count && i < MP_ROSTER_MAX_ENTRIES; ++i) {
            const char *from = roster->entry[i].name;
            size_t      at;

            if (roster->entry[i].slot != slot || from[0] == '\0') {
                continue;
            }
            /* Bounded by the SOURCE field as well as by the destination. The decoder refuses a
             * roster whose name has no terminator inside its sixteen bytes, but a copy that reads
             * until it finds one is a copy that depends on somebody else's check still being
             * there. */
            memset(out, 0, size);
            for (at = 0; at < MP_ROSTER_NAME_MAX && at + 1u < size && from[at] != '\0'; ++at) {
                out[at] = from[at];
            }
            return;
        }
    }
    stand_in_name(slot, out, size);
}

/* Most points first; a tie is broken by fewer deaths and then by the lower world slot, so the
 * order is total and two rows never trade places between one frame and the next. */
static bool row_precedes(const mp_board_row_t *a, const mp_board_row_t *b)
{
    if (a->points != b->points) {
        return a->points > b->points;
    }
    if (a->deaths != b->deaths) {
        return a->deaths < b->deaths;
    }
    return a->slot < b->slot;
}

size_t mp_board_build(const mp_score_board_t *board, const mp_roster_t *roster, uint8_t self_slot,
                      mp_board_row_t *out, size_t capacity)
{
    size_t written = 0;
    size_t i;

    if (board == NULL || out == NULL || capacity == 0u) {
        return 0;
    }
    for (i = 0; i < board->count && i < MP_SCORE_MAX_PLAYERS && written < capacity; ++i) {
        mp_board_row_t row;
        size_t         at;

        memset(&row, 0, sizeof row);
        row.slot    = board->line[i].slot;
        row.team    = board->line[i].team;
        row.points  = (int32_t)board->line[i].points;
        row.deaths  = (int32_t)board->line[i].deaths;
        row.is_self = (row.slot == self_slot);
        copy_name(roster, row.slot, row.name, sizeof row.name);

        /* Inserted in place rather than sorted afterwards: sixteen rows at most, and the order is
         * then true at every step instead of only at the end. */
        for (at = written; at > 0u && row_precedes(&row, &out[at - 1u]); --at) {
            out[at] = out[at - 1u];
        }
        out[at] = row;
        ++written;
    }
    return written;
}

/* ==============================================================================================
 * The widths.
 * ============================================================================================ */

static int32_t wider(int32_t a, int32_t b)
{
    return (a > b) ? a : b;
}

void mp_board_columns(const mp_board_row_t *rows, size_t count, mp_board_columns_t *out)
{
    size_t i;

    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof *out);
    out->name   = mp_board_text_width(mp_board_heading(0));
    out->points = mp_board_text_width(mp_board_heading(1));
    out->deaths = mp_board_text_width(mp_board_heading(2));
    out->team   = mp_board_text_width(mp_board_heading(3));

    for (i = 0; rows != NULL && i < count; ++i) {
        char number[12];

        out->name = wider(out->name, mp_board_text_width(rows[i].name));

        text_format(number, sizeof number, "%d", (int)rows[i].points);
        out->points = wider(out->points, mp_board_text_width(number));

        text_format(number, sizeof number, "%d", (int)rows[i].deaths);
        out->deaths = wider(out->deaths, mp_board_text_width(number));

        out->team = wider(out->team, mp_board_text_width(mp_board_team_word(rows[i].team)));
    }
    out->total = out->name + out->points + out->deaths + out->team + 3 * MP_BOARD_COLUMN_GAP;
}

/* ==============================================================================================
 * The clock and the two sentences.
 * ============================================================================================ */

uint32_t mp_board_remaining(const mp_score_board_t *board, const mp_rules_t *rules)
{
    uint32_t limit;

    if (board == NULL || rules == NULL || board->outcome != (uint8_t)MP_SCORE_RUNNING) {
        return 0u;
    }
    limit = mp_rules_time_limit_substeps(rules);
    if (limit == 0u || board->elapsed >= limit) {
        return 0u;
    }
    return limit - board->elapsed;
}

void mp_board_clock(uint32_t substeps, bool has_limit, char *out, size_t size)
{
    uint32_t seconds;
    uint32_t minutes;

    if (out == NULL || size == 0u) {
        return;
    }
    if (!has_limit) {
        text_format(out, size, "--:--");
        return;
    }
    seconds = substeps / BOARD_SUBSTEPS_PER_SECOND;
    minutes = seconds / 60u;
    if (minutes > BOARD_CLOCK_MAX_MINUTES) {
        minutes = BOARD_CLOCK_MAX_MINUTES;
        seconds = minutes * 60u + 59u;
    }
    text_format(out, size, "%02u:%02u", (unsigned)minutes, (unsigned)(seconds % 60u));
}

void mp_board_headline(const mp_score_board_t *board, const mp_rules_t *rules, char *out,
                       size_t size)
{
    char clock[8];

    if (out == NULL || size == 0u) {
        return;
    }
    if (board == NULL || rules == NULL) {
        text_format(out, size, "%s", mp_text(MP_TEXT_BOARD_TITLE));
        return;
    }
    mp_board_clock(mp_board_remaining(board, rules), rules->time_limit_s != 0u, clock,
                   sizeof clock);
    if (rules->score_limit == 0u) {
        text_format(out, size, mp_text(MP_TEXT_BOARD_HEAD_OPEN), clock);
    } else {
        text_format(out, size, mp_text(MP_TEXT_BOARD_HEAD_TARGET), clock,
                    (unsigned)rules->score_limit);
    }
}

/* The winner as a name where there is one. A world slot is a number this feature uses internally
 * and means nothing to the person reading the panel, so a slot with no row behind it falls back
 * to the number rather than printing an empty name. */
static const char *winner_name(const mp_board_row_t *rows, size_t count, uint8_t slot)
{
    size_t i;

    for (i = 0; rows != NULL && i < count; ++i) {
        if (rows[i].slot == slot) {
            return rows[i].name;
        }
    }
    return NULL;
}

void mp_board_result(const mp_score_board_t *board, const mp_board_row_t *rows, size_t count,
                     char *out, size_t size)
{
    const char *name;

    if (out == NULL || size == 0u) {
        return;
    }
    if (board == NULL || board->outcome == (uint8_t)MP_SCORE_RUNNING) {
        text_format(out, size, "%s", mp_text(MP_TEXT_BOARD_RUNNING));
        return;
    }
    switch (board->outcome) {
    case (uint8_t)MP_SCORE_WON_TEAM:
        text_format(out, size, mp_text(MP_TEXT_BOARD_WON_TEAM), mp_board_team_word(board->winner));
        break;
    case (uint8_t)MP_SCORE_WON_PLAYER:
        name = winner_name(rows, count, board->winner);
        if (name != NULL) {
            text_format(out, size, mp_text(MP_TEXT_BOARD_WON_NAME), name);
        } else {
            text_format(out, size, mp_text(MP_TEXT_BOARD_WON_SLOT), (unsigned)board->winner);
        }
        break;
    default:
        text_format(out, size, "%s", mp_text(MP_TEXT_BOARD_DRAW));
        break;
    }
}

/* ==============================================================================================
 * The key.
 * ============================================================================================ */

/* The platform's virtual key codes for the three shapes a configured name may have. They are
 * written as numbers so that this file needs no platform header and the rule can be driven in a
 * test that runs anywhere. */
#define BOARD_VK_TAB 0x09
#define BOARD_VK_F1  0x70
#define BOARD_VK_F12 0x7B

static bool equal_ignoring_case(const char *a, const char *b)
{
    size_t i;

    for (i = 0; a[i] != '\0' && b[i] != '\0'; ++i) {
        char left  = a[i];
        char right = b[i];

        if (left >= 'a' && left <= 'z') {
            left = (char)(left - 'a' + 'A');
        }
        if (right >= 'a' && right <= 'z') {
            right = (char)(right - 'a' + 'A');
        }
        if (left != right) {
            return false;
        }
    }
    return a[i] == b[i];
}

int32_t mp_board_key_code(const char *name)
{
    if (name == NULL || name[0] == '\0') {
        return 0;
    }
    if (equal_ignoring_case(name, "TAB")) {
        return BOARD_VK_TAB;
    }
    if (name[1] == '\0') {
        /* A letter or a digit is its own code on this platform, upper case for a letter. */
        if (name[0] >= 'a' && name[0] <= 'z') {
            return (int32_t)(name[0] - 'a' + 'A');
        }
        if ((name[0] >= 'A' && name[0] <= 'Z') || (name[0] >= '0' && name[0] <= '9')) {
            return (int32_t)name[0];
        }
        return 0;
    }
    if ((name[0] == 'F' || name[0] == 'f') && name[1] >= '0' && name[1] <= '9') {
        int32_t number = name[1] - '0';

        if (name[2] >= '0' && name[2] <= '9' && name[3] == '\0') {
            number = number * 10 + (name[2] - '0');
        } else if (name[2] != '\0') {
            return 0;
        }
        if (number >= 1 && number <= (BOARD_VK_F12 - BOARD_VK_F1 + 1)) {
            return BOARD_VK_F1 + number - 1;
        }
    }
    return 0;
}
