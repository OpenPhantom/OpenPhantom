/* movie_session.c: the session side of a movie, against a gate this test files itself.
 *
 * The gate is filed through the real channel, common/movie_note over common/shared_note, exactly as
 * the multiplayer files it, and the movie state this module files back is read the same way. The
 * clock is the one thing handed in: movie_session_poll_at takes the tick count, so the quiet can be
 * driven over turns without waiting for it.
 *
 * What is pinned: with no gate nothing is filed or held; the host's movie and a held client's are
 * filed as what they are; a held client ends when the host's count grows; the quiet counts pumped
 * turns and a stall only as one turn, starts again with every loop, and lets the player go after
 * three seconds of turns; a gate taken down lets a playing movie go and ends a wait alone.
 */
#include "unittest.h"

#include "movie_rule.h"
#include "movie_session.h"

#include "common/language.h"
#include "common/movie_note.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define CONNECTION 0x0102030405060708ull

static uint32_t published;

static void file_gate(bool running, bool client, bool connected, bool moving, uint32_t payloads)
{
    movie_gate_t gate;

    memset(&gate, 0, sizeof gate);
    gate.running        = running;
    gate.client         = client;
    gate.host_connected = connected;
    gate.host_moving    = moving;
    gate.connection     = connected ? CONNECTION : 0u;
    gate.host_payloads  = payloads;
    gate.language       = (uint8_t)LANGUAGE_DE;
    gate.published      = ++published;
    (void)movie_note_publish_gate(&gate);
}

/* What the multiplayer files when its transport comes down. */
static void take_the_gate_down(void)
{
    file_gate(false, false, false, false, 0u);
}

static movie_state_t state_now(void)
{
    movie_state_t state;

    memset(&state, 0, sizeof state);
    ut_check(movie_note_read_state(&state), "the movie state reads");
    return state;
}

static void check_no_session(void)
{
    movie_state_t state;
    movie_loop_t  loop;

    ut_section("with no gate a movie is FREE, and nothing is filed or held");

    ut_check(movie_session_decide("movie\\scene1") == MOVIE_ROLE_FREE, "no gate is no session");
    movie_session_begin(MOVIE_PATH_VLC);
    movie_session_loop(&loop);
    ut_check(loop.poll == NULL && loop.exits.escape_ends && loop.exits.focus_ends &&
             loop.exits.close_ends && !loop.exits.pump_session,
             "its loop is the movie player's own: every way out, nothing pumped");
    loop.end = MOVIE_END_NATURAL;
    movie_session_end(&loop);
    ut_check(!movie_note_read_state(&state), "and no movie state was filed at all");

    movie_session_install();
    state = state_now();
    ut_check(state.state == MOVIE_STATE_IDLE && state.movies_begun == 0u,
             "the installation files an idle record, so a multiplayer knows the player is here");
}

static void check_the_host(void)
{
    movie_state_t state;
    movie_loop_t  loop;

    ut_section("the host's movie is filed as the host's and ends as a player ends it");

    file_gate(true, false, false, false, 0u);
    ut_check(movie_session_decide("movie\\scene1") == MOVIE_ROLE_HOST, "a host's gate");
    movie_session_begin(MOVIE_PATH_VLC);
    state = state_now();
    ut_check(state.state == MOVIE_STATE_PLAYING && state.path == MOVIE_PATH_VLC && !state.locked &&
             strcmp(state.stem, "scene1") == 0, "filed as playing, through libVLC, not held");
    movie_session_loop(&loop);
    ut_check(loop.poll == NULL && loop.exits.escape_ends && !loop.exits.focus_ends &&
             !loop.exits.close_ends && loop.exits.pump_session,
             "Escape ends it, a lost foreground and the close box do not, the session is pumped");
    loop.end = MOVIE_END_ESCAPE;
    movie_session_end(&loop);
    state = state_now();
    ut_check(state.state == MOVIE_STATE_ENDED && state.end_reason == MOVIE_END_ESCAPE,
             "and filed as ended by Escape");
}

static void check_a_held_client(void)
{
    movie_state_t state;
    movie_loop_t  loop;
    uint16_t      begun;
    uint32_t      t;

    ut_section("a held client ends when the host's count grows on its connection");

    begun = state_now().movies_begun;
    file_gate(true, true, true, false, 100u);
    ut_check(movie_session_decide("movie\\scene2") == MOVIE_ROLE_LOCKED, "a client's gate");
    movie_session_begin(MOVIE_PATH_VLC);
    movie_session_begin(MOVIE_PATH_BINK);   /* libVLC did not start, the retail player takes it */
    state = state_now();
    ut_check(state.locked && state.path == MOVIE_PATH_BINK &&
             state.movies_begun == (uint16_t)(begun + 1u),
             "filed held, on the retail path, and counted once for the two paths it tried");
    movie_session_loop(&loop);
    ut_check(loop.poll != NULL && loop.held_for_host && !loop.exits.escape_ends &&
             !loop.exits.focus_ends && !loop.exits.close_ends,
             "its loop asks the host and ends by none of the three");
    ut_check(strcmp(loop.wait_text, "Warte auf den Host") == 0,
             "and waits, if it must, in the language the gate carries");
    t = GetTickCount();
    ut_check(movie_session_poll_at(t + 30u) == MOVIE_VERDICT_GO_ON, "the same count goes on");
    file_gate(true, true, true, false, 101u);
    ut_check(movie_session_poll_at(t + 60u) == MOVIE_VERDICT_HOST_DONE,
             "one payload more is the host's end");
    loop.end = MOVIE_END_HOST;
    movie_session_end(&loop);
    state = state_now();
    ut_check(state.state == MOVIE_STATE_ENDED && state.end_reason == MOVIE_END_HOST,
             "filed as ended with the host's");
}

static void check_the_quiet(void)
{
    movie_loop_t loop;
    uint32_t     t;
    unsigned     turn;
    bool         quiet_early = false;

    ut_section("the quiet counts pumped turns, and a stall between two turns as one");

    file_gate(true, true, true, false, 200u);
    ut_check(movie_session_decide("movie\\scene3") == MOVIE_ROLE_LOCKED, "a held client");
    movie_session_begin(MOVIE_PATH_BINK);
    movie_session_loop(&loop);
    t = GetTickCount();
    ut_check(movie_session_poll_at(t + 10000u) == MOVIE_VERDICT_GO_ON,
             "ten seconds without a turn, a stall of the game's thread, do not let it go");
    for (turn = 1u; turn <= 28u; ++turn) {
        quiet_early |= movie_session_poll_at(t + 10000u + turn * 100u) != MOVIE_VERDICT_GO_ON;
    }
    ut_check(!quiet_early, "nor do 28 turns of a tenth of a second after it");
    ut_check(movie_session_poll_at(t + 12900u) == MOVIE_VERDICT_QUIET,
             "the turn that makes three seconds of pumping without a gate does");
    ut_check(!state_now().locked, "and the letting go is filed");

    ut_section("a gate filed again starts the quiet over");

    file_gate(true, true, true, false, 300u);
    ut_check(movie_session_decide("movie\\scene4") == MOVIE_ROLE_LOCKED, "a held client");
    movie_session_begin(MOVIE_PATH_VLC);
    movie_session_loop(&loop);
    t = GetTickCount();
    for (turn = 1u; turn <= 29u; ++turn) {
        (void)movie_session_poll_at(t + turn * 100u);
    }
    file_gate(true, true, true, false, 300u);   /* the same count, a new publication */
    ut_check(movie_session_poll_at(t + 3000u) == MOVIE_VERDICT_GO_ON,
             "a gate that moved in the turn that would have made three seconds starts it over");

    ut_section("every loop starts its quiet again, so a retail loop after libVLC starts fresh");

    for (turn = 1u; turn <= 29u; ++turn) {
        (void)movie_session_poll_at(t + 3000u + turn * 100u);
    }
    movie_session_loop(&loop);   /* libVLC gave the movie back, the retail player takes it */
    ut_check(movie_session_poll_at(t + 6000u) == MOVIE_VERDICT_GO_ON,
             "2.9 seconds of the first loop are not carried into the second one, whose first "
             "turn would otherwise make three");
}

static void check_the_gate_taken_down(void)
{
    movie_state_t state;
    movie_loop_t  loop;
    uint32_t      t;

    ut_section("a gate taken down lets a playing movie go, and it stays let go");

    file_gate(true, true, true, false, 400u);
    ut_check(movie_session_decide("movie\\scene5") == MOVIE_ROLE_LOCKED, "a held client");
    movie_session_begin(MOVIE_PATH_VLC);
    movie_session_loop(&loop);
    t = GetTickCount();
    take_the_gate_down();
    ut_check(movie_session_poll_at(t + 10u) == MOVIE_VERDICT_NOT_RUNNING,
             "the session no longer runs");
    state = state_now();
    ut_check(state.state == MOVIE_STATE_PLAYING && !state.locked,
             "the movie plays on as this side's own, and is filed so");
    movie_session_loop(&loop);
    ut_check(loop.poll == NULL && loop.exits.escape_ends && loop.exits.focus_ends,
             "a second loop of the same movie does not hold it again");
    loop.end = MOVIE_END_NATURAL;
    movie_session_end(&loop);
    ut_check(state_now().end_reason == MOVIE_END_NATURAL, "and it ends at its own end");

    ut_section("a gate taken down during the wait ends the wait alone");

    file_gate(true, true, true, false, 500u);
    ut_check(movie_session_decide("movie\\scene6") == MOVIE_ROLE_LOCKED, "a held client");
    movie_session_begin(MOVIE_PATH_VLC);
    movie_session_loop(&loop);
    movie_session_wait();
    ut_check(state_now().state == MOVIE_STATE_WAITING, "its movie ended first and it waits");
    take_the_gate_down();
    t = GetTickCount();
    ut_check(movie_session_poll_at(t + 10u) == MOVIE_VERDICT_NOT_RUNNING,
             "the wait hears that the session is gone");
    loop.end = MOVIE_END_ALONE;
    movie_session_end(&loop);
    state = state_now();
    ut_check(state.state == MOVIE_STATE_ENDED && state.end_reason == MOVIE_END_ALONE,
             "and is filed as ended alone");
}

static void check_skip_and_alone(void)
{
    movie_state_t state;
    movie_loop_t  loop;
    uint16_t      begun = state_now().movies_begun;

    ut_section("a client whose host moves skips, one whose host is gone plays its own");

    file_gate(true, true, true, true, 600u);
    ut_check(movie_session_decide("movie\\scene7") == MOVIE_ROLE_SKIP, "the host's world moves");
    state = state_now();
    ut_check(state.state == MOVIE_STATE_ENDED && state.end_reason == MOVIE_END_SKIPPED &&
             state.movies_begun == (uint16_t)(begun + 1u), "filed as skipped, and counted");

    file_gate(true, true, false, false, 600u);
    ut_check(movie_session_decide("movie\\arena") == MOVIE_ROLE_ALONE, "no host is connected");
    movie_session_loop(&loop);
    ut_check(loop.poll == NULL && loop.exits.escape_ends && loop.exits.pump_session,
             "its own movie, with the session still pumped");
}

int main(void)
{
    check_no_session();
    check_the_host();
    check_a_held_client();
    check_the_quiet();
    check_the_gate_taken_down();
    check_skip_and_alone();
    return ut_summary("movie_session");
}
