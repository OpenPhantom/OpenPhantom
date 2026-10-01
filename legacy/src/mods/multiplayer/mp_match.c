/* mp_match.c: the match authority a machine with no game can be. See the header for what it is
 * for and why a new round does not reload the level.
 */
#include "mp_match.h"

#include <string.h>

/* How many are on each side right now. Counted rather than kept, because a kept pair of counters
 * is a pair that can disagree with the table it describes, and the table is small. */
static void census(const mp_match_t *match, size_t *on_a, size_t *on_b)
{
    size_t i;

    *on_a = 0;
    *on_b = 0;
    for (i = 0; i < MP_SCORE_MAX_PLAYERS; ++i) {
        if (!match->player[i].present) {
            continue;
        }
        if (match->player[i].team == (uint8_t)MP_MATCH_TEAM_A) {
            ++*on_a;
        } else if (match->player[i].team == (uint8_t)MP_MATCH_TEAM_B) {
            ++*on_b;
        }
    }
}

/* ==============================================================================================
 * The pure rules.
 * ============================================================================================ */

uint8_t mp_match_balanced_team(size_t on_a, size_t on_b)
{
    return on_b < on_a ? (uint8_t)MP_MATCH_TEAM_B : (uint8_t)MP_MATCH_TEAM_A;
}

bool mp_match_swap_allowed(size_t on_from, size_t on_to)
{
    size_t after_from;
    size_t after_to;

    if (on_from == 0u) {
        return false;   /* nobody is on that side to be leaving it */
    }
    after_from = on_from - 1u;
    after_to   = on_to + 1u;
    return after_from > after_to ? after_from - after_to <= 1u : after_to - after_from <= 1u;
}

/* ==============================================================================================
 * The match.
 * ============================================================================================ */

/* Begins a round on the generation the setup already carries. Everything about WHAT is played
 * stays; what goes is the table and the clock. */
static void begin_round(mp_match_t *match)
{
    mp_score_reset(&match->score, &match->setup.rules, match->setup.generation, match->tick);
    match->running    = true;
    match->decided    = false;
    match->decided_at = 0u;
    ++match->rounds_begun;

    /* Everybody still here is seated in the fresh table with the side they are already on. A
     * round change is not a reason to re-balance: the sides were balanced as people arrived, and
     * shuffling them between rounds would break up whoever was playing together. */
    {
        size_t i;

        for (i = 0; i < MP_SCORE_MAX_PLAYERS; ++i) {
            if (match->player[i].present) {
                (void)mp_score_add_player(&match->score, (uint8_t)i, match->player[i].team);
            }
        }
    }
}

void mp_match_start(mp_match_t *match, const mp_lobby_setup_t *setup)
{
    if (match == NULL || setup == NULL) {
        return;
    }
    memset(match, 0, sizeof *match);
    match->setup = *setup;
    (void)mp_rules_clamp(&match->setup.rules);
    match->setup.mode   = (uint8_t)MP_LOBBY_MODE_TDM;
    match->setup.flags |= (uint8_t)MP_LOBBY_F_STARTED;
    if (match->setup.generation == 0u) {
        /* The clients compare this for INEQUALITY against the last one they acted on, and a fresh
         * client holds zero. A first note of zero would be one they had already acted on. */
        match->setup.generation = 1u;
    }
    begin_round(match);
}

void mp_match_player_joined(mp_match_t *match, uint8_t slot)
{
    size_t on_a;
    size_t on_b;

    if (match == NULL || slot >= MP_SCORE_MAX_PLAYERS || match->player[slot].present) {
        return;
    }
    census(match, &on_a, &on_b);
    match->player[slot].present      = true;
    match->player[slot].asked_for_it = false;
    match->player[slot].team = mp_rules_teams(&match->setup.rules)
                                   ? mp_match_balanced_team(on_a, on_b)
                                   : (uint8_t)MP_LOBBY_TEAM_NONE;
    ++match->teams_assigned;
    (void)mp_score_add_player(&match->score, slot, match->player[slot].team);
}

void mp_match_player_left(mp_match_t *match, uint8_t slot)
{
    if (match == NULL || slot >= MP_SCORE_MAX_PLAYERS || !match->player[slot].present) {
        return;
    }
    memset(&match->player[slot], 0, sizeof match->player[slot]);
    /* Out of the table as well as off the side. A slot that comes back is somebody else, and a
     * table that kept the old points would hand them to whoever arrives next. */
    (void)mp_score_remove_player(&match->score, slot);
}

bool mp_match_request_team(mp_match_t *match, uint8_t slot, uint8_t team)
{
    size_t on_a;
    size_t on_b;
    size_t on_from;
    size_t on_to;

    if (match == NULL || slot >= MP_SCORE_MAX_PLAYERS || !match->player[slot].present) {
        return false;
    }
    if (!mp_rules_teams(&match->setup.rules)) {
        ++match->swaps_refused;
        return false;   /* a free for all has no sides to ask for */
    }
    if (team != (uint8_t)MP_MATCH_TEAM_A && team != (uint8_t)MP_MATCH_TEAM_B) {
        ++match->swaps_refused;
        return false;
    }
    if (match->player[slot].team == team) {
        return true;   /* already there; saying so again is not a refusal */
    }
    census(match, &on_a, &on_b);
    on_from = match->player[slot].team == (uint8_t)MP_MATCH_TEAM_A ? on_a : on_b;
    on_to   = team == (uint8_t)MP_MATCH_TEAM_A ? on_a : on_b;
    if (!mp_match_swap_allowed(on_from, on_to)) {
        ++match->swaps_refused;
        return false;
    }
    match->player[slot].team         = team;
    match->player[slot].asked_for_it = true;
    ++match->swaps_allowed;
    (void)mp_score_set_team(&match->score, slot, team);
    return true;
}

uint8_t mp_match_team_of(const mp_match_t *match, uint8_t slot)
{
    if (match == NULL || slot >= MP_SCORE_MAX_PLAYERS || !match->player[slot].present) {
        return (uint8_t)MP_LOBBY_TEAM_NONE;
    }
    return match->player[slot].team;
}

/* The victim's own machine reports its death, because only the victim can tell a fall from a
 * hit; the four byte note is relayed to every other peer unchanged, and the server reads it on
 * the way past rather than intercepting it. */
bool mp_match_take_death(mp_match_t *match, const mp_death_note_t *note)
{
    if (match == NULL || note == NULL || !match->running || match->decided) {
        return false;
    }
    if (note->victim_slot >= MP_SCORE_MAX_PLAYERS || !match->player[note->victim_slot].present) {
        ++match->deaths_refused;
        return false;   /* a death for a slot nobody is on is a decoding fault, not a kill */
    }
    if (!mp_score_death(&match->score, note)) {
        ++match->deaths_refused;
        return false;
    }
    ++match->deaths_taken;
    return true;
}

void mp_match_tick(mp_match_t *match)
{
    if (match == NULL || !match->running) {
        return;
    }
    ++match->tick;
    if (!match->decided) {
        /* The clock the time limit is measured on, and the call that evaluates both limits. */
        mp_score_host_tick(&match->score, match->tick);
        if (mp_score_outcome(&match->score, NULL) != (uint8_t)MP_SCORE_RUNNING) {
            match->decided    = true;
            match->decided_at = match->tick;
            ++match->rounds_decided;
        }
        return;
    }
    /* Decided: the table stays up, unchanged, until it has been readable for long enough. The
     * subtraction is unsigned so a wrapped counter costs one intermission and nothing else. */
    if (match->tick - match->decided_at < MP_MATCH_INTERMISSION_SUBSTEPS) {
        return;
    }
    /* And the next round, on a generation nobody has acted on. It is raised rather than counted
     * up from anything: the byte wraps, and the clients compare it for inequality. */
    ++match->setup.generation;
    if (match->setup.generation == 0u) {
        match->setup.generation = 1u;
    }
    begin_round(match);
}

uint8_t mp_match_outcome(const mp_match_t *match, uint8_t *winner)
{
    if (match == NULL) {
        if (winner != NULL) {
            *winner = (uint8_t)MP_SCORE_NOBODY;
        }
        return (uint8_t)MP_SCORE_RUNNING;
    }
    return mp_score_outcome(&match->score, winner);
}

size_t mp_match_setup_note(const mp_match_t *match, uint8_t *buffer, size_t capacity)
{
    if (match == NULL || !match->running) {
        return 0u;
    }
    return mp_lobby_setup_encode(&match->setup, buffer, capacity);
}

size_t mp_match_score_note(const mp_match_t *match, uint8_t *buffer, size_t capacity)
{
    if (match == NULL || !match->running) {
        return 0u;
    }
    return mp_score_encode(&match->score.board, buffer, capacity);
}
