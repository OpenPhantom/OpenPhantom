/* mp_follow_difficulty.c: every player of a session plays the host's difficulty.
 *
 * The module is the real one; what the test plays is what it asks of the session and of the
 * engine: whether a transport is up, whether this side is a client, the setup note the host last
 * said, whether the game mode says a level runs, whether an exit waits, and the difficulty cell,
 * which is a field of this file. Without the difficulty in the note a client begins every level
 * at the value its own new game put there, four in these checks.
 */
#include "unittest.h"

#include "mp_armed.h"
#include "mp_bridge_drain.h"
#include "mp_bridge_lobby.h"
#include "mp_cells.h"
#include "mp_follow_difficulty.h"
#include "mp_lobby.h"
#include "mp_start.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ---- the session and the engine, as this test plays them ------------------------------------ */

typedef struct stand_in {
    bool             armed;
    bool             client;
    bool             setup_known;
    mp_lobby_setup_t setup;
    bool             level_running;
    bool             exit_waits;
    uint32_t         cell;
    bool             cell_resolved;
} stand_in_t;

static stand_in_t s;

bool mp_armed_transport(void)
{
    return s.armed;
}

bool mp_bridge_drain_is_client(void)
{
    return s.client;
}

bool mp_bridge_lobby_setup(mp_lobby_setup_t *out)
{
    if (!s.setup_known) {
        return false;
    }
    *out = s.setup;
    return true;
}

bool mp_start_level_running(void)
{
    return s.level_running;
}

uintptr_t mp_cells_address(mp_cell_t cell)
{
    return cell == MP_CELL_IMPACT_DIFFICULTY && s.cell_resolved ? (uintptr_t)&s.cell : 0u;
}

static bool an_exit_waits(void)
{
    return s.exit_waits;
}

/* A client of a started co-op session whose host says `difficulty`, its own cell at `cell`, in a
 * level that runs. */
static void a_client(uint32_t cell, uint8_t difficulty)
{
    memset(&s, 0, sizeof s);
    s.armed         = true;
    s.client        = true;
    s.setup_known   = true;
    s.setup.mode    = (uint8_t)MP_LOBBY_MODE_COOP;
    s.setup.flags   = (uint8_t)MP_LOBBY_F_STARTED;
    s.setup.host_difficulty = (uint8_t)(difficulty + 1u);
    s.level_running = true;
    s.cell          = cell;
    s.cell_resolved = true;
}

/* ---- the checks ------------------------------------------------------------------------------ */

static void check_the_level_begin(void)
{
    ut_section("a client's level begins at the host's difficulty");
    mp_follow_difficulty_set_exit_question(&an_exit_waits);
    a_client(4u, 5u);
    mp_follow_difficulty_note_level_begin();
    ut_checkf(s.cell == 5u, "its own new game put 4 there, the host said 5, and 5 stands (%u)",
              (unsigned)s.cell);

    ut_section("and nothing is written where no session of this client's is");
    a_client(4u, 5u);
    s.client = false;
    mp_follow_difficulty_note_level_begin();
    ut_check(s.cell == 4u, "a host keeps its own, which it is the one to say");
    a_client(4u, 5u);
    s.setup.flags |= (uint8_t)MP_LOBBY_F_ENDED;
    mp_follow_difficulty_note_level_begin();
    ut_check(s.cell == 4u, "a session the host ended writes nothing");
    a_client(4u, 5u);
    s.setup.host_difficulty = (uint8_t)MP_LOBBY_DIFFICULTY_NONE;
    mp_follow_difficulty_note_level_begin();
    ut_check(s.cell == 4u, "a host that says none leaves this side's own");
    a_client(4u, 5u);
    s.armed = false;
    mp_follow_difficulty_note_level_begin();
    mp_follow_difficulty_hold(false);
    mp_follow_difficulty_note_restore();
    ut_check(s.cell == 4u, "and single player, with no transport, is never touched");
    a_client(4u, 5u);
    s.exit_waits = true;
    mp_follow_difficulty_note_level_begin();
    ut_check(s.cell == 4u,
             "a level that begins while an exit waits is single player, and keeps its own");
}

static void check_the_hold(void)
{
    ut_section("while a level runs the cell is held to the note");
    a_client(4u, 5u);
    mp_follow_difficulty_note_level_begin();
    s.cell = 3u;
    mp_follow_difficulty_hold(false);
    ut_check(s.cell == 5u, "a change to 3 here is put back at the next frame");
    s.setup.host_difficulty = 7u;
    mp_follow_difficulty_hold(false);
    ut_check(s.cell == 6u, "a newer note's 6 is taken over");
    s.cell = 2u;
    mp_follow_difficulty_hold(true);
    ut_check(s.cell == 2u, "nothing is held on the way out of a level, whose end raises it here");
    s.level_running = false;
    mp_follow_difficulty_hold(false);
    ut_check(s.cell == 2u, "nor while the game mode says no level runs");
    s.level_running = true;
    s.exit_waits    = true;
    mp_follow_difficulty_hold(false);
    ut_check(s.cell == 2u, "nor while an exit waits, when a change here is this side's own");
}

static void check_a_savegame(void)
{
    ut_section("a savegame's restore runs after the level begin, and is answered after it");
    a_client(4u, 6u);
    mp_follow_difficulty_note_level_begin();
    s.cell = 6u;   /* the restore wrote the file's own */
    mp_follow_difficulty_note_restore();
    ut_check(s.cell == 6u, "a file of 6 under a note of 6 is left as it is");
    a_client(4u, 5u);
    mp_follow_difficulty_note_level_begin();
    s.cell = 6u;
    mp_follow_difficulty_note_restore();
    ut_check(s.cell == 5u, "a file of 6 under a note of 5 is put back to 5");
    s.exit_waits = true;
    s.cell       = 6u;
    mp_follow_difficulty_note_restore();
    ut_check(s.cell == 6u, "and not while an exit waits");
}

static void check_what_the_host_says(void)
{
    ut_section("the host says its cell plus one, or none");
    memset(&s, 0, sizeof s);
    s.cell_resolved = true;
    s.cell          = 5u;
    ut_check(mp_follow_difficulty_to_say() == 6u, "a cell of 5 is said as 6");
    s.cell = 0u;
    ut_check(mp_follow_difficulty_to_say() == 1u, "a cell of 0 as 1, which is not none");
    s.cell = 10u;
    ut_check(mp_follow_difficulty_to_say() == MP_LOBBY_DIFFICULTY_NONE,
             "a cell past 9 holds no difficulty, and is said as none");
    s.cell_resolved = false;
    ut_check(mp_follow_difficulty_to_say() == MP_LOBBY_DIFFICULTY_NONE,
             "and so is a cell that did not resolve");
    mp_follow_difficulty_report();
}

int main(void)
{
    check_the_level_begin();
    check_the_hold();
    check_a_savegame();
    check_what_the_host_says();
    return ut_summary("mp_follow_difficulty");
}
