/* mp_board.h: what the live scoreboard says and how wide each column of it is, as arithmetic with
 * no engine, no wire and no clock in it.
 *
 * Layer 1, pure. Everything a panel has to decide before it can draw one pixel is here: whether it
 * should be on screen at all, which rows it shows and in what order, what each column is called
 * and how wide it has to be, and how a remaining time and a result read in words. The drawing
 * itself is somewhere else and knows none of this.
 *
 * Widths come from the font's advance table, not from the engine's own measure. The engine will
 * measure a string, but what it answers is the size of the glyph's rectangle in the texture plus
 * one, and that is not how far the pen moves: a space has no rectangle at all and still advances
 * five units. A column sized from that measurement is too narrow by one space width per space.
 * The advance table is the font file's own field and is what the drawing pass itself steps by.
 *
 * Widths are in the font's authored units, which is the 640 by 480 screen the fonts were drawn
 * for. A caller that sets the glyph scale so that text comes out at its authored size gets these
 * numbers back as pixels; one that scales differently has to scale these to match.
 *
 * The numbers are not measured for alignment. A right hand column is right aligned by asking the
 * engine to end the string at the column edge (its alignment value 2), which works the offset
 * out from the same advance the drawing pass steps by and is therefore exact by
 * construction. What the widths here are for is deciding how wide the panel
 * has to be, which is a different question and tolerates a unit either way.
 */
#ifndef MULTIPLAYER_MP_BOARD_H
#define MULTIPLAYER_MP_BOARD_H

#include "mp_roster.h"
#include "mp_rules.h"
#include "mp_score.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* As many rows as the table can hold players. */
#define MP_BOARD_MAX_ROWS MP_SCORE_MAX_PLAYERS

/* A name on the board is a roster name, or a stand-in built from the slot when the roster has not
 * arrived yet. The stand-in is the longer of the two, so the field is sized for it. */
#define MP_BOARD_NAME_MAX 20u

/* The header and the result line. Long enough for the widest sentence either builds. */
#define MP_BOARD_LINE_MAX 64u

typedef struct mp_board_row {
    char     name[MP_BOARD_NAME_MAX];
    int32_t  points;
    int32_t  deaths;
    uint8_t  team;
    uint8_t  slot;
    bool     is_self;   /* this machine's own player, so the panel can mark the row */
} mp_board_row_t;

/* The four columns, in authored units, plus what the whole row takes with the gaps between them.
 * The name column is what is left after the three narrow ones, so it is the only one that grows
 * with a long name. */
typedef struct mp_board_columns {
    int32_t name;
    int32_t points;
    int32_t deaths;
    int32_t team;
    int32_t total;
} mp_board_columns_t;

/* The gap between two columns, in authored units. One space is five, and two of them is the
 * smallest gap at which a right aligned number stops reading as part of the name beside it. */
#define MP_BOARD_COLUMN_GAP 10

/* How far a string carries the pen in the built in font, in authored units. It stops at a newline
 * rather than running through it, because a two line string measured whole is as wide as both of
 * its lines together and nothing that wide is ever drawn. */
int32_t mp_board_text_width(const char *text);

/* Whether the board belongs on screen at all.
 *
 * A deathmatch that is actually being played is the precondition and the other three are reasons:
 * the player is holding the key, the player is dead and waiting to come back, or the round has
 * been decided and the last thing anybody wants is to have to ask what the score was. The dead
 * player is the case the pause menu cannot serve: the engine refuses to open it for a corpse, and
 * a corpse is exactly what somebody is during the seconds they want to read the table. */
bool mp_board_visible(bool deathmatch_running, bool level_running, bool key_held,
                      bool waiting_to_return, bool round_decided);

/* Fills `out` with one row per player in the table, ordered: most points first, then fewest
 * deaths, then lowest world slot, so two players level on both never swap places between frames.
 * Returns how many rows were written. A slot the roster does not name gets a stand-in built from
 * the slot number rather than an empty line. */
size_t mp_board_build(const mp_score_board_t *board, const mp_roster_t *roster, uint8_t self_slot,
                      mp_board_row_t *out, size_t capacity);

/* The width of each column over the rows given, headings included, so a heading is never wider
 * than the column under it. */
void mp_board_columns(const mp_board_row_t *rows, size_t count, mp_board_columns_t *out);

/* The heading of each of the four columns, so the panel and the widths cannot disagree about
 * what they say. Index 0 name, 1 points, 2 deaths, 3 team. */
const char *mp_board_heading(size_t column);

/* What a team number reads as. A player with no team is a player with no team, which is what a
 * free for all is, so it is a dash rather than a zero. */
const char *mp_board_team_word(uint8_t team);

/* Substeps left of the time limit, worked out from the copies a display holds rather than from
 * the round's own state. Zero when there is no time limit, when the round is over and when the
 * limit has already run out. */
uint32_t mp_board_remaining(const mp_score_board_t *board, const mp_rules_t *rules);

/* Substeps as minutes and seconds. The simulation runs a fixed ladder of thirty two substeps a
 * second and that is the divisor. A round with no time limit is written as dashes, because a
 * zero there would read as "no time left". */
void mp_board_clock(uint32_t substeps, bool has_limit, char *out, size_t size);

/* The line above the table: what is being played for and how long there is left of it. */
void mp_board_headline(const mp_score_board_t *board, const mp_rules_t *rules, char *out,
                       size_t size);

/* The line under the table: who won, or that the round is still running. The winner is looked up
 * in the rows rather than printed as a number, because a world slot means nothing to a player. */
void mp_board_result(const mp_score_board_t *board, const mp_board_row_t *rows, size_t count,
                     char *out, size_t size);

/* The Windows virtual key code a configured key name stands for, or 0 for a name this build does
 * not know. Accepted: TAB, one letter or digit, and F1 to F12. The codes are the platform's and
 * are written as numbers here so that the rule can be driven without a platform. */
int32_t mp_board_key_code(const char *name);

#endif /* MULTIPLAYER_MP_BOARD_H */
