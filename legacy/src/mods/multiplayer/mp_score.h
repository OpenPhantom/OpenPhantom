/* mp_score.h: what a round is worth so far, as arithmetic with no wire, no clock and no engine.
 *
 * Layer 1, pure. It is fed deaths and substeps and it answers with a table of points and with
 * whether the round is over. Nothing in it reads a cell, opens a socket or asks the operating
 * system what time it is, which is what lets every rule below be driven in a test: a betrayal, a
 * suicide, a death with nobody to blame, a round that ends on points, a round that ends on time,
 * and a round that ends level.
 *
 * The clock is the host's substep counter, not the wall clock. Two machines have two starting
 * points for their wall clocks, and the engine clamps the frame delta feeding its simulation, so
 * under load simulated time falls behind the wall clock and never ahead. A round timed against
 * the wall clock would therefore end at a different point in the simulation on each machine and
 * at a different point again from the one the players saw. The host counts its own substeps and
 * that count is the round's clock.
 *
 * The host's table is the truth and a client does not argue with it. A client keeps its own table
 * so that its display does not have to wait a round trip for a kill it just made, and when the
 * host's table arrives it TAKES it, whole, without comparing it against its own and without
 * merging anything. That is what mp_score_adopt does and it is the reason it has no return value
 * to check: there is no case in which a client's own arithmetic wins.
 *
 * The table travels as a note of its own, repeated, and not inside the host's setup note. Two
 * reasons, and the first one is the decisive one. The setup note is what decides whether a level
 * LOADS, and its decoder refuses the entire message when any byte in it is out of range; folding
 * a table that changes on every death into it would mean that one torn score line also costs the
 * level choice and the start. The second is that the two change at different times and for
 * different reasons: a setup changes when a host decides something, a table changes when somebody
 * dies, and a note that suppresses a repeat of an unchanged setup would have to stop doing that.
 * It is repeated rather than sent once for the same reason the setup is: a player who joins in
 * the middle of a round has missed every earlier message, and the only thing that reaches them is
 * one that comes round again.
 *
 * NO SUDDEN DEATH. A round that runs out of time with two players level ends level. There is no
 * overtime, no tie break and no first-to-the-next-point.
 */
#ifndef MULTIPLAYER_MP_SCORE_H
#define MULTIPLAYER_MP_SCORE_H

#include "mp_hit_relay.h"
#include "mp_lobby.h"
#include "mp_rules.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* As many players as the roster carries. */
#define MP_SCORE_MAX_PLAYERS 16u

/* What a player's points may reach in either direction. The ceiling is above the largest score
 * limit a rule set allows, so a round can always be won; the floor exists because the penalties
 * are the only thing that drives a score down and a table with no floor would eventually need a
 * wider field on the wire for a number nobody can read. */
#define MP_SCORE_POINTS_MAX 999
#define MP_SCORE_POINTS_MIN (-999)

/* The tag on the reliable channel. The event tags run 0x81 to 0x8E, the roster is 0x8F, the
 * player hit 0x90, the three lobby notes 0x91 to 0x93 and the death 0x94; this is the next free
 * one. */
#define MP_SCORE_TAG 0x95u

/* tag, generation, count, outcome, winner, elapsed as a long. */
#define MP_SCORE_HEADER_BYTES 9u

/* slot, team, points, deaths. */
#define MP_SCORE_ENTRY_BYTES 6u

/* What a table of `n` players weighs, and what the largest one weighs. The second is what a
 * buffer is sized for; the first is what actually goes out.
 *
 * An odd header and an even entry make every length on this ladder odd, and every roster is an
 * even number of bytes, so the two variable length families on this channel cannot be confused at
 * any population. That is arithmetic rather than luck, and the test walks both ladders. Counted
 * rather than remembered: the ladder 9, 15, 21 to 105 meets none of the fixed lengths on the
 * channel (1, 4, 5, 6, 7, 8, 10, 11, 22, 40, 53 and the setup's 110); it does meet the mover
 * digest family, eight bytes plus seven per mover, at 15, 57 and 99, the digests of 1, 7 and
 * 13 movers, which is allowed because that family's tag differs, and the test says so rather
 * than leaving it to be noticed. */
#define MP_SCORE_BYTES_FOR(n) (MP_SCORE_HEADER_BYTES + (size_t)(n) * MP_SCORE_ENTRY_BYTES)
#define MP_SCORE_BYTES        MP_SCORE_BYTES_FOR(MP_SCORE_MAX_PLAYERS)

/* How the round ended, or that it has not. */
#define MP_SCORE_RUNNING    0u
#define MP_SCORE_WON_PLAYER 1u   /* the winner byte is a world slot */
#define MP_SCORE_WON_TEAM   2u   /* the winner byte is a team number, 1..MP_LOBBY_TEAM_MAX */
#define MP_SCORE_DRAW       3u   /* time ran out with nobody ahead */
#define MP_SCORE_OUTCOME_MAX MP_SCORE_DRAW

/* The winner byte when there is nobody to name: while the round runs, and on a draw. */
#define MP_SCORE_NOBODY 0xFFu

typedef struct mp_score_line {
    uint8_t  slot;     /* the world slot this player rides: 0 the listen host, 1.. the peers */
    uint8_t  team;     /* what the lobby gave them, MP_LOBBY_TEAM_NONE in a free for all */
    int16_t  points;   /* MP_SCORE_POINTS_MIN..MP_SCORE_POINTS_MAX */
    uint16_t deaths;   /* saturates rather than wraps */
} mp_score_line_t;

/* What crosses the wire: the table and the state of the round, and nothing that only the host
 * needs. The rules are not in here because they ride in the host's setup note already, and one
 * value in two messages is one value that can disagree with itself. */
typedef struct mp_score_board {
    uint8_t         generation;   /* which round this table belongs to (the setup's generation) */
    uint8_t         count;
    uint8_t         outcome;      /* MP_SCORE_RUNNING and the three ways a round ends */
    uint8_t         winner;       /* a slot, a team, or MP_SCORE_NOBODY */
    uint32_t        elapsed;      /* substeps since the round began, as the HOST counted them */
    mp_score_line_t line[MP_SCORE_MAX_PLAYERS];
} mp_score_board_t;

typedef struct mp_score {
    mp_rules_t       rules;
    mp_score_board_t board;
    uint32_t         started_tick;   /* the host substep the round began on */
    bool             started;

    /* Every path this module can take, counted, so a report can say what it did rather than that
     * it was installed. A death that arrives after the round was decided is counted apart from a
     * death that was refused as malformed, because the two say different things about a session:
     * the first is ordinary and the second is not.
     *
     * They count the current round. A caller that wants the session's totals reads them before it
     * starts the next round, because starting one clears them along with everything else. */
    uint32_t deaths_taken;
    uint32_t deaths_refused;
    uint32_t deaths_after_end;
    uint32_t boards_adopted;
} mp_score_t;

/* Starts a round: the rules it is played under, the generation it belongs to, and the host
 * substep it begins on. Everything else is cleared, players included, because a round that kept
 * the last one's table would be scoring two rounds at once. A rule set that is out of range is
 * clamped rather than refused, so a round can always start.
 *
 * This is also the call that turns an uninitialised structure into a usable one, which is why it
 * clears the counters too rather than carrying them over: carrying them would mean reading fields
 * the caller may never have written. */
void mp_score_reset(mp_score_t *score, const mp_rules_t *rules, uint8_t generation,
                    uint32_t host_tick);

/* Puts a player in the table, or moves the team of one already there. False when the table is
 * full or the slot is not one. */
bool mp_score_add_player(mp_score_t *score, uint8_t slot, uint8_t team);

/* Moves a player onto another team, keeping their points. False when the slot is not in the
 * table. */
bool mp_score_set_team(mp_score_t *score, uint8_t slot, uint8_t team);

/* Takes a player out, closing the gap behind them. False when the slot is not in the table. */
bool mp_score_remove_player(mp_score_t *score, uint8_t slot);

/* One player's line, or NULL when they are not in the table. */
const mp_score_line_t *mp_score_line_of(const mp_score_t *score, uint8_t slot);

/* Everything one team has between them. A team of MP_LOBBY_TEAM_NONE answers zero: a player with
 * no team plays for nobody, which is what a free for all means. */
int32_t mp_score_team_points(const mp_score_t *score, uint8_t team);

/* A death, and the whole of the scoring:
 *
 *   a killer on the other side gets a point;
 *   a killer on the victim's own side gets the rule set's betrayal penalty;
 *   a victim who killed themselves, or who died with nobody to name, gets the suicide penalty;
 *   and the victim's death count goes up either way.
 *
 * Two sides are the SAME side only when both are on a numbered team and it is the same number.
 * A player with no team is on nobody's side, which is the same rule the damage gate uses, so a
 * free for all needs no second case here either.
 *
 * The reason is carried, checked and reported, but it does not decide who scores: the killer slot
 * does. Only the victim's machine can tell a fall from a hit, and a player pushed off a ledge is
 * a kill whichever of the two the victim's machine called it.
 *
 * False, and counted as refused, for a victim who is not in the table, a named killer who is not,
 * and a reason this build does not know. False as well, and counted apart, for a death that
 * arrives after the round was decided: bodies go on falling until the world is reloaded, and
 * scoring them would change a final table that players are already reading. */
bool mp_score_death(mp_score_t *score, const mp_death_note_t *note);

/* One call per host substep, or one call with whatever the host's counter now reads; both work,
 * because the elapsed time is the difference from the substep the round began on and not a count
 * kept here. This is where a round ends on time. */
void mp_score_host_tick(mp_score_t *score, uint32_t host_tick);

/* How the round stands. `winner` may be NULL; when it is not it is written on every call. */
uint8_t mp_score_outcome(const mp_score_t *score, uint8_t *winner);

/* Substeps since the round began, and substeps left of the time limit. The second answers zero
 * when there is no time limit and zero once the round is over, so a caller showing a countdown
 * has to ask the rule set whether there is one at all. */
uint32_t mp_score_elapsed(const mp_score_t *score);
uint32_t mp_score_remaining(const mp_score_t *score);

/* Exactly MP_SCORE_BYTES_FOR(count), or 0 when the buffer is too small or a line is out of range.
 * It takes the board rather than the whole score, because the board is what travels and because
 * the encoder and the decoder then work on the same type in both directions. */
size_t mp_score_encode(const mp_score_board_t *board, uint8_t *buffer, size_t capacity);

/* True for a buffer that is a table by tag and length together. The count is read out of the
 * message before anything that depends on it, so this is one exact length rather than a range: a
 * message one byte short of what its own count promises is refused as flatly as one with the
 * wrong tag. */
bool mp_score_is_score(const uint8_t *buffer, size_t bytes);

/* Decodes, refusing a count past the table, an outcome this build does not know, a winner that
 * does not fit the outcome that names it, a slot past the table, a team that is not one and a
 * points value outside the range the encoder promises. On false nothing of `out` is to be
 * trusted. */
bool mp_score_decode(const uint8_t *buffer, size_t bytes, mp_score_board_t *out);

/* The client's side: take the host's table, whole. There is no comparison against what this
 * machine had worked out and no merge of the two, because the host's figure is the truth and this
 * machine's was only there so the display did not have to wait for it. The generation comes
 * across with the rest, so a client whose own idea of the round is stale follows the host into
 * the new one rather than dropping its table. */
void mp_score_adopt(mp_score_t *score, const mp_score_board_t *board);

/* What this module has done, for the report. Any of the four may be NULL. */
void mp_score_counters(const mp_score_t *score, uint32_t *deaths_taken, uint32_t *deaths_refused,
                       uint32_t *deaths_after_end, uint32_t *boards_adopted);

#endif /* MULTIPLAYER_MP_SCORE_H */
