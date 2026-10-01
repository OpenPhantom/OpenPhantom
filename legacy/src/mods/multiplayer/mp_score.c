/* mp_score.c: what a round is worth so far. See mp_score.h. */
#include "mp_score.h"

#include "mp_lobby.h"
#include "mp_rules.h"
#include "mp_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Points travel as their distance above the floor, in an unsigned short, so nothing on the wire
 * depends on how a compiler converts a large unsigned value into a signed one, and the range
 * check is the same comparison in both directions. */
#define POINTS_BIAS  ((uint16_t)(-MP_SCORE_POINTS_MIN))
#define POINTS_SPAN  ((uint16_t)(MP_SCORE_POINTS_MAX - MP_SCORE_POINTS_MIN))

/* A death count saturates rather than wraps: a player who died 65536 times has not died none. */
#define DEATHS_MAX 0xFFFFu

static mp_score_line_t *find_line(mp_score_t *score, uint8_t slot)
{
    uint8_t i;

    for (i = 0; i < score->board.count; ++i) {
        if (score->board.line[i].slot == slot) {
            return &score->board.line[i];
        }
    }
    return NULL;
}

/* Two players are on the same side only when both are on a NUMBERED team and it is the same
 * number. A player with no team plays for nobody, which is the same rule the damage gate uses,
 * so a free for all needs no second case here. */
static bool same_side(uint8_t a, uint8_t b)
{
    return a != (uint8_t)MP_LOBBY_TEAM_NONE && a == b;
}

static void award(mp_score_line_t *line, int32_t delta)
{
    int32_t points = (int32_t)line->points + delta;

    if (points > MP_SCORE_POINTS_MAX) {
        points = MP_SCORE_POINTS_MAX;
    }
    if (points < MP_SCORE_POINTS_MIN) {
        points = MP_SCORE_POINTS_MIN;
    }
    line->points = (int16_t)points;
}

/* The side that is ahead, and whether it is ahead on its own. With teams the sides are the
 * numbered teams that have at least one player in them; without teams they are the players. False
 * when there is no side at all, which is a table with nobody in it. */
static bool leader(const mp_score_t *score, uint8_t *who, int32_t *best, bool *unique)
{
    bool    found = false;
    uint8_t i;

    *who    = (uint8_t)MP_SCORE_NOBODY;
    *best   = 0;
    *unique = false;

    if (mp_rules_teams(&score->rules)) {
        uint8_t team;

        for (team = 1u; team <= MP_LOBBY_TEAM_MAX; ++team) {
            int32_t total = 0;
            bool    manned = false;

            for (i = 0; i < score->board.count; ++i) {
                if (score->board.line[i].team == team) {
                    total += score->board.line[i].points;
                    manned = true;
                }
            }
            if (!manned) {
                continue;
            }
            if (!found || total > *best) {
                found   = true;
                *best   = total;
                *who    = team;
                *unique = true;
            } else if (total == *best) {
                *unique = false;
            }
        }
        return found;
    }

    for (i = 0; i < score->board.count; ++i) {
        int32_t points = score->board.line[i].points;

        if (!found || points > *best) {
            found   = true;
            *best   = points;
            *who    = score->board.line[i].slot;
            *unique = true;
        } else if (points == *best) {
            *unique = false;
        }
    }
    return found;
}

static void finish(mp_score_t *score, bool decided, uint8_t who)
{
    if (!decided) {
        score->board.outcome = (uint8_t)MP_SCORE_DRAW;
        score->board.winner  = (uint8_t)MP_SCORE_NOBODY;
        return;
    }
    score->board.outcome = mp_rules_teams(&score->rules) ? (uint8_t)MP_SCORE_WON_TEAM
                                                         : (uint8_t)MP_SCORE_WON_PLAYER;
    score->board.winner = who;
}

/* Whether the round has ended, on points or on time. Once it has ended it stays ended: the table
 * goes on moving while bodies are still falling, but the winner that was announced is the winner.
 * There is no sudden death, so a round that runs out of time level ends level. */
static void evaluate(mp_score_t *score)
{
    uint8_t  who = (uint8_t)MP_SCORE_NOBODY;
    int32_t  best = 0;
    bool     unique = false;
    bool     anybody;
    uint32_t time_limit;

    if (score->board.outcome != (uint8_t)MP_SCORE_RUNNING) {
        return;
    }
    anybody    = leader(score, &who, &best, &unique);
    time_limit = mp_rules_time_limit_substeps(&score->rules);

    if (anybody && score->rules.score_limit != 0u &&
        best >= (int32_t)score->rules.score_limit) {
        finish(score, unique, who);
        return;
    }
    if (time_limit != 0u && score->board.elapsed >= time_limit) {
        finish(score, anybody && unique, who);
    }
}

void mp_score_reset(mp_score_t *score, const mp_rules_t *rules, uint8_t generation,
                    uint32_t host_tick)
{
    if (score == NULL) {
        return;
    }
    /* Everything goes, the counters included. Carrying them over would mean reading fields of the
     * caller's structure before this call has written any, and this is the call that turns an
     * uninitialised one into a usable one. A caller that wants session totals rather than round
     * totals reads the counters before starting the next round. */
    memset(score, 0, sizeof *score);

    if (rules != NULL) {
        score->rules = *rules;
    } else {
        mp_rules_default(&score->rules);
    }
    /* Clamped rather than refused: a round has to be able to start, and a stale ini or a torn
     * note must not be able to leave the session with a rule nobody can play. */
    (void)mp_rules_clamp(&score->rules);

    score->board.generation = generation;
    score->board.outcome    = (uint8_t)MP_SCORE_RUNNING;
    score->board.winner     = (uint8_t)MP_SCORE_NOBODY;
    score->started_tick     = host_tick;
    score->started          = true;
}

bool mp_score_add_player(mp_score_t *score, uint8_t slot, uint8_t team)
{
    mp_score_line_t *line;

    if (score == NULL || slot >= MP_SCORE_MAX_PLAYERS || team > MP_LOBBY_TEAM_MAX) {
        return false;
    }
    line = find_line(score, slot);
    if (line != NULL) {
        line->team = team;   /* already here; the team is the only thing an add may move */
        return true;
    }
    if (score->board.count >= MP_SCORE_MAX_PLAYERS) {
        return false;
    }
    line = &score->board.line[score->board.count];
    memset(line, 0, sizeof *line);
    line->slot = slot;
    line->team = team;
    ++score->board.count;
    return true;
}

bool mp_score_set_team(mp_score_t *score, uint8_t slot, uint8_t team)
{
    mp_score_line_t *line;

    if (score == NULL || team > MP_LOBBY_TEAM_MAX) {
        return false;
    }
    line = find_line(score, slot);
    if (line == NULL) {
        return false;
    }
    line->team = team;
    return true;
}

bool mp_score_remove_player(mp_score_t *score, uint8_t slot)
{
    uint8_t i;

    if (score == NULL) {
        return false;
    }
    for (i = 0; i < score->board.count; ++i) {
        if (score->board.line[i].slot != slot) {
            continue;
        }
        for (; i + 1u < score->board.count; ++i) {
            score->board.line[i] = score->board.line[i + 1u];
        }
        --score->board.count;
        memset(&score->board.line[score->board.count], 0, sizeof score->board.line[0]);
        return true;
    }
    return false;
}

const mp_score_line_t *mp_score_line_of(const mp_score_t *score, uint8_t slot)
{
    uint8_t i;

    if (score == NULL) {
        return NULL;
    }
    for (i = 0; i < score->board.count; ++i) {
        if (score->board.line[i].slot == slot) {
            return &score->board.line[i];
        }
    }
    return NULL;
}

int32_t mp_score_team_points(const mp_score_t *score, uint8_t team)
{
    int32_t total = 0;
    uint8_t i;

    if (score == NULL || team == (uint8_t)MP_LOBBY_TEAM_NONE || team > MP_LOBBY_TEAM_MAX) {
        return 0;
    }
    for (i = 0; i < score->board.count; ++i) {
        if (score->board.line[i].team == team) {
            total += score->board.line[i].points;
        }
    }
    return total;
}

bool mp_score_death(mp_score_t *score, const mp_death_note_t *note)
{
    mp_score_line_t *victim;
    mp_score_line_t *killer = NULL;

    if (score == NULL || note == NULL) {
        return false;
    }
    if (score->board.outcome != (uint8_t)MP_SCORE_RUNNING) {
        /* The round is decided. Bodies go on falling until the world is reloaded, and scoring
         * them would change a final table that players are already reading. */
        ++score->deaths_after_end;
        return false;
    }
    if (note->reason > MP_DEATH_REASON_MAX) {
        ++score->deaths_refused;
        return false;
    }
    victim = find_line(score, note->victim_slot);
    if (victim == NULL) {
        ++score->deaths_refused;
        return false;
    }
    if (note->killer_slot != (uint8_t)MP_DEATH_NO_KILLER &&
        note->killer_slot != note->victim_slot) {
        killer = find_line(score, note->killer_slot);
        if (killer == NULL) {
            ++score->deaths_refused;   /* a killer nobody is playing is a torn or a stale note */
            return false;
        }
    }

    ++score->deaths_taken;
    if (victim->deaths < DEATHS_MAX) {
        ++victim->deaths;
    }
    if (killer == NULL) {
        award(victim, score->rules.suicide_penalty);
    } else if (same_side(mp_rules_effective_team(&score->rules, killer->team),
                         mp_rules_effective_team(&score->rules, victim->team))) {
        /* The teams the RULE SET gives them, not the ones they picked. With teams off everybody
         * is on nobody's side, so two players who both chose team one score off each other, and
         * the switch stays one rule rather than becoming a second one written here. Reading the
         * raw team byte was a defect during the build, caught by the test: with teams off it
         * charged a betrayal penalty for every kill between two players who happened to have
         * picked the same number in an earlier round. */
        award(killer, score->rules.team_penalty);
    } else {
        award(killer, 1);
    }
    evaluate(score);
    return true;
}

void mp_score_host_tick(mp_score_t *score, uint32_t host_tick)
{
    if (score == NULL || !score->started) {
        return;
    }
    /* Unsigned subtraction, so the counter wrapping after four years costs one round its clock
     * and nothing else. */
    score->board.elapsed = host_tick - score->started_tick;
    evaluate(score);
}

uint8_t mp_score_outcome(const mp_score_t *score, uint8_t *winner)
{
    if (score == NULL) {
        if (winner != NULL) {
            *winner = (uint8_t)MP_SCORE_NOBODY;
        }
        return (uint8_t)MP_SCORE_RUNNING;
    }
    if (winner != NULL) {
        *winner = score->board.winner;
    }
    return score->board.outcome;
}

uint32_t mp_score_elapsed(const mp_score_t *score)
{
    return score == NULL ? 0u : score->board.elapsed;
}

uint32_t mp_score_remaining(const mp_score_t *score)
{
    uint32_t limit;

    if (score == NULL || score->board.outcome != (uint8_t)MP_SCORE_RUNNING) {
        return 0u;
    }
    limit = mp_rules_time_limit_substeps(&score->rules);
    if (limit == 0u || score->board.elapsed >= limit) {
        return 0u;
    }
    return limit - score->board.elapsed;
}

/* ==============================================================================================
 * The table on the wire.
 * ============================================================================================ */

static bool winner_fits(uint8_t outcome, uint8_t winner)
{
    if (outcome == (uint8_t)MP_SCORE_WON_PLAYER) {
        return winner < MP_SCORE_MAX_PLAYERS;
    }
    if (outcome == (uint8_t)MP_SCORE_WON_TEAM) {
        return winner >= 1u && winner <= MP_LOBBY_TEAM_MAX;
    }
    /* Running and drawn both name nobody, and they name nobody in the one spelling, so a receiver
     * never has to decide which of several "no winner" bytes it is looking at. */
    return winner == (uint8_t)MP_SCORE_NOBODY;
}

static bool board_is_sound(const mp_score_board_t *board)
{
    uint8_t i;

    if (board->count > MP_SCORE_MAX_PLAYERS || board->outcome > MP_SCORE_OUTCOME_MAX) {
        return false;
    }
    if (!winner_fits(board->outcome, board->winner)) {
        return false;
    }
    for (i = 0; i < board->count; ++i) {
        const mp_score_line_t *line = &board->line[i];

        if (line->slot >= MP_SCORE_MAX_PLAYERS || line->team > MP_LOBBY_TEAM_MAX) {
            return false;
        }
        if (line->points > MP_SCORE_POINTS_MAX || line->points < MP_SCORE_POINTS_MIN) {
            return false;
        }
    }
    return true;
}

size_t mp_score_encode(const mp_score_board_t *board, uint8_t *buffer, size_t capacity)
{
    mp_wire_writer_t w;
    uint8_t          i;

    if (board == NULL || buffer == NULL || !board_is_sound(board)) {
        return 0;
    }
    if (capacity < MP_SCORE_BYTES_FOR(board->count)) {
        return 0;
    }
    mp_wire_writer_init(&w, buffer, capacity);
    mp_wire_put_u8(&w, (uint8_t)MP_SCORE_TAG);
    mp_wire_put_u8(&w, board->generation);
    mp_wire_put_u8(&w, board->count);
    mp_wire_put_u8(&w, board->outcome);
    mp_wire_put_u8(&w, board->winner);
    mp_wire_put_u32(&w, board->elapsed);
    for (i = 0; i < board->count; ++i) {
        const mp_score_line_t *line = &board->line[i];

        mp_wire_put_u8(&w, line->slot);
        mp_wire_put_u8(&w, line->team);
        mp_wire_put_u16(&w, (uint16_t)((int32_t)line->points + (int32_t)POINTS_BIAS));
        mp_wire_put_u16(&w, line->deaths);
    }
    return w.overflowed ? 0u : w.at;
}

bool mp_score_is_score(const uint8_t *buffer, size_t bytes)
{
    return buffer != NULL && bytes >= MP_SCORE_HEADER_BYTES &&
           buffer[0] == (uint8_t)MP_SCORE_TAG && buffer[2] <= MP_SCORE_MAX_PLAYERS &&
           bytes == MP_SCORE_BYTES_FOR(buffer[2]);
}

bool mp_score_decode(const uint8_t *buffer, size_t bytes, mp_score_board_t *out)
{
    mp_wire_reader_t r;
    uint8_t          tag = 0;
    uint8_t          i;

    if (out == NULL || !mp_score_is_score(buffer, bytes)) {
        return false;
    }
    memset(out, 0, sizeof *out);
    mp_wire_reader_init(&r, buffer, bytes);
    mp_wire_get_u8(&r, &tag);
    mp_wire_get_u8(&r, &out->generation);
    mp_wire_get_u8(&r, &out->count);
    mp_wire_get_u8(&r, &out->outcome);
    mp_wire_get_u8(&r, &out->winner);
    mp_wire_get_u32(&r, &out->elapsed);
    if (out->count > MP_SCORE_MAX_PLAYERS) {
        return false;
    }
    for (i = 0; i < out->count; ++i) {
        mp_score_line_t *line = &out->line[i];
        uint16_t         points = 0;

        mp_wire_get_u8(&r, &line->slot);
        mp_wire_get_u8(&r, &line->team);
        mp_wire_get_u16(&r, &points);
        mp_wire_get_u16(&r, &line->deaths);
        if (points > POINTS_SPAN) {
            return false;   /* undone before the range check, not turned into a score nobody won */
        }
        line->points = (int16_t)((int32_t)points - (int32_t)POINTS_BIAS);
    }
    if (r.overran) {
        return false;
    }
    /* A stranger wrote all of this, so it is held to exactly what the encoder promises. */
    return board_is_sound(out);
}

void mp_score_adopt(mp_score_t *score, const mp_score_board_t *board)
{
    if (score == NULL || board == NULL) {
        return;
    }
    /* The whole table, taken as it stands. Nothing here compares the host's figures against this
     * machine's and nothing merges the two: the local table only ever existed so that the display
     * did not have to wait a round trip for a kill this machine had already seen. */
    score->board = *board;
    ++score->boards_adopted;
}

void mp_score_counters(const mp_score_t *score, uint32_t *deaths_taken, uint32_t *deaths_refused,
                       uint32_t *deaths_after_end, uint32_t *boards_adopted)
{
    if (score == NULL) {
        return;
    }
    if (deaths_taken != NULL) {
        *deaths_taken = score->deaths_taken;
    }
    if (deaths_refused != NULL) {
        *deaths_refused = score->deaths_refused;
    }
    if (deaths_after_end != NULL) {
        *deaths_after_end = score->deaths_after_end;
    }
    if (boards_adopted != NULL) {
        *boards_adopted = score->boards_adopted;
    }
}
