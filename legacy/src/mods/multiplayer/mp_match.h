/* mp_match.h: what is being played, who is on which side, and who is winning.
 *
 * What it is for. A listen host answers all three of these out of the game it is running: the
 * lobby screen chose the level and the rules, the players picked their own teams in it, and
 * mp_round keeps the score off the engine's own death reports. A DEDICATED SERVER has none of
 * that. It has no lobby, no screen, no engine and no player of its own, and until this file
 * existed it could not run a deathmatch at all: nobody authored the setup note, so no client
 * ever learned what to play, and nobody counted, so no round could end. A third thing followed
 * that nothing had written down: the roster the server published set the slot, the ready bit,
 * the round trip and the name, and left the team at zero from the memset, so every player was on
 * the same side, which makes a team deathmatch a game where the damage rule refuses every shot
 * between players.
 *
 * Linking the round module into the server was measured and rejected: it includes the bridge's
 * headers, and the bridge is the listen host's binding to a running engine, so the whole of it
 * would have been dragged into a process that has no engine. The score, the rules and the lobby
 * codec are pure and link directly, and this file is what is built on them.
 *
 * So this is the authority a machine with no game can be. It holds the rule set, hands out
 * balanced teams, takes the deaths its players report to each other, decides when a round is
 * over, holds the final table up long enough to be read, and then starts the next one.
 *
 * It is pure on purpose. No socket, no file, no engine, and no clock: time arrives as a substep
 * count from the caller. That is what lets the awkward parts be driven in a test rather than in
 * a field run with four people. The awkward parts are the team balance, the swap rule, and the
 * moment a round ends, and all three are decided here rather than in the loop that calls it.
 *
 * Why a new round does not reload the level. Raising the generation is what tells every client
 * that the round it was scoring is finished and a fresh one has begun, and their own round module
 * already acts on exactly that. Sending them back through a level load would cost half a minute
 * to achieve the same empty scoreboard. It is also the only thing that WORKS today: a client acts
 * on a start note from its lobby screen, and in a running level that screen is not open.
 *
 * SIZE NOTE: under 200 lines, no seam.
 */
#ifndef MULTIPLAYER_MP_MATCH_H
#define MULTIPLAYER_MP_MATCH_H

#include "mp_death.h"
#include "mp_lobby.h"
#include "mp_rules.h"
#include "mp_score.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* How long the decided table stays up before the next round begins, in substeps of the thirty
 * two per second ladder. Ten seconds: long enough to read a table of eight, short enough that
 * nobody goes to get a coffee. */
#define MP_MATCH_INTERMISSION_SUBSTEPS 320u

/* The two teams a match can put somebody on. Team none is what a free for all gives everybody,
 * and it is not a third side. */
#define MP_MATCH_TEAM_A 1u
#define MP_MATCH_TEAM_B 2u

typedef struct mp_match_player {
    bool    present;
    uint8_t team;
    bool    asked_for_it;   /* the player chose this side rather than being placed on it */
} mp_match_player_t;

typedef struct mp_match {
    mp_lobby_setup_t  setup;    /* mode, level, title, the rule set, and the generation */
    mp_score_t        score;
    bool              running;
    bool              decided;  /* the round is over and the table is being held up */
    uint32_t          tick;     /* the server's own substep count */
    uint32_t          decided_at;
    mp_match_player_t player[MP_SCORE_MAX_PLAYERS];

    uint32_t rounds_begun;
    uint32_t rounds_decided;
    uint32_t deaths_taken;
    uint32_t deaths_refused;
    uint32_t teams_assigned;
    uint32_t swaps_allowed;
    uint32_t swaps_refused;
} mp_match_t;

/* ---- the pure rules, which are the part worth testing --------------------------------------- */

/* Which side a joining player goes on, given how many are already on each. The emptier one, and
 * team A on a tie so that a server filling up alternates rather than stacking. */
uint8_t mp_match_balanced_team(size_t on_a, size_t on_b);

/* Whether a player may move from a side holding `on_from` to one holding `on_to`, counting
 * themselves in `on_from`. Allowed when the sides are no more than one apart afterwards, which
 * refuses the stack and permits the correction: nobody can leave 2v2 to make it 1v3, and anybody
 * can leave 3v1 to make it 2v2. */
bool mp_match_swap_allowed(size_t on_from, size_t on_to);

/* ---- the match ------------------------------------------------------------------------------ */

/* Starts a match on a level under a rule set. The generation begins at one rather than zero: the
 * clients compare it for INEQUALITY against what they last acted on, and a fresh client holds
 * zero, so a first note of zero would be one it had already acted on. */
void mp_match_start(mp_match_t *match, const mp_lobby_setup_t *setup);

/* A player arrived or left. Joining assigns a balanced side; leaving frees it and takes the
 * player out of the table, so a slot that comes back is a new player rather than an inheritance. */
void mp_match_player_joined(mp_match_t *match, uint8_t slot);
void mp_match_player_left(mp_match_t *match, uint8_t slot);

/* A player asked for a side. False when the swap rule refuses it, and the caller tells them so;
 * with teams off it is refused too, because there are no sides to be on. */
bool mp_match_request_team(mp_match_t *match, uint8_t slot, uint8_t team);

uint8_t mp_match_team_of(const mp_match_t *match, uint8_t slot);

/* A death one player reported to the others. False when the note is one this match will not
 * count, which is either a slot nobody is on or a round that is already decided. */
bool mp_match_take_death(mp_match_t *match, const mp_death_note_t *note);

/* One substep. It advances the clock the time limit is measured on, decides the round when the
 * rules say so, and starts the next one when the table has been up long enough. */
void mp_match_tick(mp_match_t *match);

/* MP_SCORE_RUNNING, or one of the three ways a round ends, with the winning slot or team. */
uint8_t mp_match_outcome(const mp_match_t *match, uint8_t *winner);

/* The two notes the server repeats: what is being played, and how it is going. Both return the
 * byte count, or zero when there is nothing to say. */
size_t mp_match_setup_note(const mp_match_t *match, uint8_t *buffer, size_t capacity);
size_t mp_match_score_note(const mp_match_t *match, uint8_t *buffer, size_t capacity);

#endif /* MULTIPLAYER_MP_MATCH_H */
