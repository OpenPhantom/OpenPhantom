/* mp_session_now.c: the one answer to "does this machine play in a running session", against every
 * kind of machine and every way a session looks from it.
 *
 * The module is the real one; what the test plays is what it reads: whether a transport stands and
 * who put it up (the menu or the ini's way in), the host's setup note, the drain's role, the
 * connection to the host, and whether a peer of the session is connected. The bridge's own joined
 * flag is played as well, because it is the answer the first version of this reading took for a
 * lobby with no note, and only a substep writes it: after a host's goodbye it still says yes.
 *
 * Three modules asked this question with answers of their own before, and they came apart at a
 * host's goodbye. Each row also checks the other question of this module, so the difference
 * between the two is written down.
 */
#include "unittest.h"

#include "mp_armed.h"
#include "mp_bridge.h"
#include "mp_bridge_drain.h"
#include "mp_bridge_lobby.h"
#include "mp_lobby.h"
#include "mp_session_now.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ---- what the reading asks, as this test plays it ------------------------------------------- */

typedef struct stand_in {
    bool     armed;
    bool     by_menu;      /* the menu put the transport up, not the ini's way in */
    bool     note_known;
    uint8_t  flags;
    bool     client;
    uint64_t connection;   /* a client's connection to its host, 0 when gone */
    bool     peer;         /* a host's session has a connected peer */
    bool     joined;       /* the bridge's own flag, as the last substep left it */
} stand_in_t;

static stand_in_t s;

bool mp_armed_transport(void)
{
    return s.armed;
}

bool mp_bridge_armed_by_menu(void)
{
    return s.armed && s.by_menu;
}

bool mp_bridge_lobby_setup(mp_lobby_setup_t *out)
{
    if (out == NULL || !s.note_known) {
        return false;
    }
    memset(out, 0, sizeof *out);
    out->flags      = s.flags;
    out->generation = 3u;
    return true;
}

bool mp_bridge_drain_is_client(void)
{
    return s.client;
}

uint64_t mp_bridge_drain_host_connection(void)
{
    return s.client ? s.connection : 0u;
}

bool mp_bridge_lobby_connected(void)
{
    return s.client ? s.connection != 0u : s.peer;
}

bool mp_bridge_joined(void)
{
    return s.joined;
}

/* ---- the table ------------------------------------------------------------------------------- */

#define STARTED ((uint8_t)MP_LOBBY_F_STARTED)
#define ENDED   ((uint8_t)(MP_LOBBY_F_STARTED | MP_LOBBY_F_ENDED))
#define MENU    true
#define INI     false

typedef struct row {
    const char *what;
    stand_in_t  given;               /* armed, by_menu, note, flags, client, connection, peer,
                                      * joined */
    bool        plays;
    bool        old_client_answer;   /* what mp_session_now_client_of_a_started_session says */
} row_t;

static const row_t ROWS[] = {
    { "a menu's host whose session runs",
      { true, MENU, true, STARTED, false, 0u, true, true }, true, false },
    { "a menu's host whose session runs with nobody joined yet",
      { true, MENU, true, STARTED, false, 0u, false, false }, true, false },
    { "a menu's host in its lobby before the start",
      { true, MENU, true, 0u, false, 0u, true, true }, false, false },
    { "a menu's host that ended its session",
      { true, MENU, true, ENDED, false, 0u, true, true }, false, false },
    { "a menu's client of a running session on a standing connection",
      { true, MENU, true, STARTED, true, 0x55u, false, true }, true, true },
    { "a menu's client whose host said goodbye, its last note still saying started",
      { true, MENU, true, STARTED, true, 0u, false, true }, false, true },
    { "a menu's client whose host ended the session",
      { true, MENU, true, ENDED, true, 0x55u, false, true }, false, false },
    { "a menu's client in its lobby before the host's start",
      { true, MENU, true, 0u, true, 0x55u, false, true }, false, false },
    /* Four with no lobby note: the bridge's joined flag still says yes, and the answer is no. */
    { "a menu's client after its host's goodbye, the lobby reset, joined still set",
      { true, MENU, false, 0u, true, 0u, false, true }, false, false },
    { "a menu's client that saw the end, the lobby reset and its exit waiting, still connected",
      { true, MENU, false, 0u, true, 0x55u, false, true }, false, false },
    { "a menu's host with a peer, the lobby reset at the session's end",
      { true, MENU, false, 0u, false, 0u, true, true }, false, false },
    { "the ini's way in, a client whose host has gone while joined still says yes",
      { true, INI, false, 0u, true, 0u, false, true }, false, false },
    { "the ini's way in, a host with a peer connected and no setup note",
      { true, INI, false, 0u, false, 0u, true, true }, true, false },
    { "the ini's way in, a host with nobody connected, joined still set",
      { true, INI, false, 0u, false, 0u, false, true }, false, false },
    { "the ini's way in, a client connected to its host",
      { true, INI, false, 0u, true, 0x55u, false, true }, true, false },
    { "no transport, whatever the rest says",
      { false, MENU, true, STARTED, true, 0x55u, true, true }, false, true },
};

static void check_the_table(void)
{
    size_t i;

    ut_section("does this machine play in a running session");
    for (i = 0; i < sizeof ROWS / sizeof ROWS[0]; ++i) {
        bool client = !ROWS[i].given.client;   /* the opposite, so the write shows */
        bool plays;

        s     = ROWS[i].given;
        plays = mp_session_now_plays_in_a_running_session(&client);
        ut_checkf(plays == ROWS[i].plays && client == ROWS[i].given.client,
                  "%s: %s, and the role says %s", ROWS[i].what, plays ? "plays" : "does not play",
                  client ? "client" : "host");
        ut_checkf(mp_session_now_client_of_a_started_session(NULL, NULL) ==
                      ROWS[i].old_client_answer,
                  "%s: the other question, client of a started session, says %s",
                  ROWS[i].what, ROWS[i].old_client_answer ? "yes" : "no");
    }
    s = ROWS[0].given;
    ut_check(mp_session_now_plays_in_a_running_session(NULL),
             "and a caller that does not ask the role still gets the answer");
}

int main(void)
{
    check_the_table();
    return ut_summary("mp_session_now");
}
