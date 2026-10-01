/* mp_round.c: the round, driven off the host's repeated setup note.
 *
 * The rule set and the game both ride in that note and this file reads it once per drawn frame, so
 * the table scored against it lives here too: one reader, one cadence, one clock.
 */
#include "mp_round.h"

#include "mp_bridge_drain.h"
#include "mp_bridge_lobby.h"
#include "mp_bridge_report.h"
#include "mp_bridge_roster.h"
#include "mp_lobby.h"
#include "mp_roster.h"
#include "mp_rules.h"
#include "mp_score.h"
#include "mp_wallclock.h"

#include "common/logging.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The table goes out on the setup's own cadence, and at once when a death has changed it. */
#define ROUND_BOARD_REPEAT_MS 1000u

typedef struct multiplayer_round {
    mp_score_t score;
    bool       authority;      /* this machine keeps the truth and publishes it */
    bool       running;
    uint8_t    generation;     /* the world change this round belongs to */
    bool       dirty;          /* a death moved the table since it last went out */
    bool       end_logged;
    uint32_t   last_ms;
    uint32_t   boards_sent;
    uint32_t   boards_unsent;
    uint32_t   boards_taken;
    uint32_t   boards_torn;
    uint32_t   boards_refused;  /* a peer told the authority what the score was */
    uint32_t   deaths_outside; /* deaths heard with no round running: a co-op level, or a lobby */
    uint32_t   rounds_ended;
} multiplayer_round_t;

static multiplayer_round_t round_state;

void mp_round_set_authority(bool is_host)
{
    round_state.authority = is_host;
}

/* Everybody the authority has named, with the team the lobby gave them. Idempotent: a slot already
 * in the table has its team moved rather than being added twice, which is what lets this run before
 * every repeat and pick up both a late joiner and a team change. */
static void seed_players(void)
{
    mp_roster_t table;
    size_t      i;

    if (!mp_bridge_roster_current(&table)) {
        return;
    }
    for (i = 0; i < table.count && i < MP_ROSTER_MAX_ENTRIES; ++i) {
        (void)mp_score_add_player(&round_state.score, table.entry[i].slot, table.entry[i].team);
    }
}

static void start_round(const mp_lobby_setup_t *setup)
{
    round_state.running    = true;
    round_state.generation = setup->generation;
    round_state.dirty      = true;
    round_state.end_logged = false;
    round_state.last_ms    = 0u;
    mp_score_reset(&round_state.score, &setup->rules, setup->generation,
                   mp_bridge_drain_substeps());
    seed_players();
    log_info("the round begins on world change %u: %u point(s) to win, %u s, teams %s, friendly "
             "fire %s", (unsigned)setup->generation, (unsigned)setup->rules.score_limit,
             (unsigned)setup->rules.time_limit_s,
             mp_rules_teams(&setup->rules) ? "on" : "off",
             mp_rules_friendly_fire(&setup->rules) ? "on" : "off");
}

/* The round is decided, and this says so. It does not end the level: that is g_levelOutcome = 3
 * with g_restorePending = 1, and it belongs with the step that takes everybody into the next world
 * rather than with the arithmetic that decided this one. This is the named place for it. */
static void note_end(void)
{
    uint32_t elapsed = mp_score_elapsed(&round_state.score);
    uint32_t limit   = mp_rules_time_limit_substeps(&round_state.score.rules);
    uint8_t  winner  = (uint8_t)MP_SCORE_NOBODY;
    uint8_t  outcome = mp_score_outcome(&round_state.score, &winner);

    if (outcome == (uint8_t)MP_SCORE_RUNNING || round_state.end_logged) {
        return;
    }
    round_state.end_logged = true;
    round_state.dirty      = true;   /* the last table everybody reads is the one with the result */
    ++round_state.rounds_ended;
    log_info("the round is over after %u substep(s): %s %u, because %s. The level itself is not "
             "ended here", (unsigned)elapsed,
             outcome == (uint8_t)MP_SCORE_WON_TEAM
                 ? "the winner is team"
                 : (outcome == (uint8_t)MP_SCORE_WON_PLAYER ? "the winner is world slot"
                                                            : "nobody won, the winner byte is"),
             (unsigned)winner,
             (limit != 0u && elapsed >= limit) ? "time ran out" : "the point limit was reached");
}

void mp_round_take_death(const mp_death_note_t *note)
{
    if (!round_state.running) {
        ++round_state.deaths_outside;
        return;
    }
    if (mp_score_death(&round_state.score, note)) {
        round_state.dirty = true;
    }
}

static void send_board(uint32_t now)
{
    uint8_t note[MP_SCORE_BYTES];
    size_t  bytes;

    seed_players();
    bytes = mp_score_encode(&round_state.score.board, note, sizeof note);
    if (bytes == 0u || !mp_bridge_lobby_broadcast(note, bytes)) {
        ++round_state.boards_unsent;
        return;
    }
    round_state.dirty   = false;
    round_state.last_ms = now;
    ++round_state.boards_sent;
}

/* The client's arm on the reliable channel. The host's table is taken WHOLE: nothing compares it
 * against what this machine had worked out and nothing merges the two.
 *
 * A host never adopts. The same arm is installed on both sides, because the drain is one file and
 * the roles differ only in what reaches them; but a table arriving at the authority is a peer
 * telling it what the score is, and a peer does not get to say. It is taken off the channel and
 * counted so that it shows in the report, and then dropped. */
bool mp_round_take_note(const uint8_t *note, size_t bytes)
{
    mp_score_board_t board;

    if (!mp_score_is_score(note, bytes)) {
        return false;
    }
    ++round_state.boards_taken;
    if (!mp_score_decode(note, bytes, &board)) {
        ++round_state.boards_torn;
        return true;   /* it was addressed to the round, and the round refused it */
    }
    if (round_state.authority) {
        ++round_state.boards_refused;
        return true;
    }
    mp_score_adopt(&round_state.score, &board);
    note_end();   /* a client announces the result it was handed, never one of its own */
    return true;
}

void mp_round_pump(void)
{
    mp_lobby_setup_t setup;
    uint32_t         now;

    if (!mp_bridge_lobby_setup(&setup) || (setup.flags & MP_LOBBY_F_STARTED) == 0u) {
        return;
    }
    /* Only a deathmatch has a round to win. A campaign is a story and is not won on points; deaths
     * that arrive there are counted apart instead, so the path stays visible. */
    if (setup.mode != (uint8_t)MP_LOBBY_MODE_TDM) {
        return;
    }
    if (!round_state.running ||
        mp_lobby_generation_is_new(round_state.generation, setup.generation)) {
        start_round(&setup);
    }
    if (!round_state.authority) {
        return;   /* a client is handed the table and works nothing out against its own clock */
    }
    now = mp_wallclock_ms();
    mp_score_host_tick(&round_state.score, mp_bridge_drain_substeps());
    note_end();
    if (round_state.dirty || now - round_state.last_ms >= ROUND_BOARD_REPEAT_MS) {
        send_board(now);
    }
}

/* The two readers. A copy rather than a pointer, because the table is rewritten by a death and
 * the rule set by an arriving setup, and a display walks its rows over several statements. */
bool mp_round_board(mp_score_board_t *out)
{
    if (out == NULL || !round_state.running) {
        return false;
    }
    *out = round_state.score.board;
    return true;
}

bool mp_round_rules(mp_rules_t *out)
{
    if (out == NULL || !round_state.running) {
        return false;
    }
    *out = round_state.score.rules;
    return true;
}

void mp_round_end_session(void)
{
    if (!round_state.running) {
        return;
    }
    round_state.running    = false;
    round_state.dirty      = false;
    round_state.end_logged = false;
    round_state.generation = 0u;
    /* The table goes with it. A score kept across a session would be added to rather than
     * replaced, because seed_players is idempotent by design and would find the old slots still
     * sitting there with their old points. */
    mp_score_reset(&round_state.score, NULL, 0u, 0u);
    log_info("the round ends with the session");
}

void mp_round_report(void)
{
    mp_bridge_report_round_t line;

    memset(&line, 0, sizeof line);
    mp_score_counters(&round_state.score, &line.deaths_taken, &line.deaths_refused,
                      &line.deaths_after_end, &line.boards_adopted);
    line.outcome        = mp_score_outcome(&round_state.score, &line.winner);
    line.elapsed        = mp_score_elapsed(&round_state.score);
    line.running        = round_state.running;
    line.generation     = round_state.generation;
    line.deaths_outside = round_state.deaths_outside;
    line.boards_sent    = round_state.boards_sent;
    line.boards_unsent  = round_state.boards_unsent;
    line.boards_taken   = round_state.boards_taken;
    line.boards_torn    = round_state.boards_torn;
    line.boards_refused = round_state.boards_refused;
    line.rounds_ended   = round_state.rounds_ended;
    mp_bridge_report_round(&line);
}
