/* mp_world_holds.c: the cheat cells and the 60fps cell a session holds, and every way they come
 * back.
 *
 * The module is the real one, and so is the reading of the session it asks (mp_session_now); what
 * the test plays is what that reading asks of the bridge and of the engine: whether a transport
 * stands and who put it up, the setup note the host last said, the drain's role and the connection
 * to the host, whether a peer is connected, the host's cheat bits, and the three cells, which are
 * fields of this file. The checks that matter are the exits: each way out of a session puts this
 * side's own values back, a host's goodbye among them, before and after the exit resets the lobby
 * in the same pump, the transport coming down without another frame, and a second session in the
 * same process holds from the values this side has by then, not from the first session's.
 */
#include "unittest.h"

#include "mp_armed.h"
#include "mp_bridge.h"
#include "mp_bridge_drain.h"
#include "mp_bridge_lobby.h"
#include "mp_cells.h"
#include "mp_host_settings.h"
#include "mp_host_settings_rule.h"
#include "mp_lobby.h"
#include "mp_world_holds.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ---- the session and the engine, as this test plays them ------------------------------------ */

typedef struct stand_in {
    bool     armed;
    bool     by_menu;       /* the menu put the transport up, not the ini's way in */
    bool     note_known;
    uint8_t  flags;         /* the host's setup note: started, ended */
    bool     client;
    uint64_t connection;    /* the connection to the host, 0 when there is none */
    bool     joined;
    bool     host_said;
    uint8_t  host_cheats;
    bool     cells_resolved;
    uint32_t happy;
    uint32_t evil;
    uint32_t fast;
} stand_in_t;

static stand_in_t s;

bool mp_armed_transport(void)
{
    return s.armed;
}

bool mp_bridge_lobby_setup(mp_lobby_setup_t *out)
{
    if (out == NULL || !s.note_known) {
        return false;
    }
    memset(out, 0, sizeof *out);
    out->flags      = s.flags;
    out->generation = 1u;
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

bool mp_bridge_joined(void)
{
    return s.joined;
}

bool mp_bridge_armed_by_menu(void)
{
    return s.armed && s.by_menu;
}

bool mp_bridge_lobby_connected(void)
{
    return s.client ? s.connection != 0u : s.joined;
}

bool mp_host_settings_cheats(uint8_t *cheats)
{
    if (!s.host_said) {
        return false;
    }
    *cheats = s.host_cheats;
    return true;
}

uintptr_t mp_cells_address(mp_cell_t cell)
{
    if (!s.cells_resolved) {
        return 0u;
    }
    switch (cell) {
    case MP_CELL_CHEAT_HAPPY:         return (uintptr_t)&s.happy;
    case MP_CELL_CHEAT_EVIL_FORCE:    return (uintptr_t)&s.evil;
    case MP_CELL_SUBSTEP_RATE_SWITCH: return (uintptr_t)&s.fast;
    default:                          return 0u;
    }
}

/* A client of a started session the menu put up, on a standing connection, its own cells at happy
 * 0, evil force 1 and 60fps 1, whose host said happy on and evil force off. */
static void a_client(void)
{
    memset(&s, 0, sizeof s);
    s.armed          = true;
    s.by_menu        = true;
    s.note_known     = true;
    s.flags          = (uint8_t)MP_LOBBY_F_STARTED;
    s.client         = true;
    s.connection     = 0x1234u;
    s.joined         = true;
    s.host_said      = true;
    s.host_cheats    = (uint8_t)MP_HOST_SETTINGS_CHEAT_HAPPY;
    s.cells_resolved = true;
    s.happy          = 0u;
    s.evil           = 1u;
    s.fast           = 1u;
}

static bool cells_are(uint32_t happy, uint32_t evil, uint32_t fast)
{
    return s.happy == happy && s.evil == evil && s.fast == fast;
}

/* ---- the checks ------------------------------------------------------------------------------ */

static void check_a_client_holds_the_hosts_values(void)
{
    ut_section("a client holds the host's cheats and 60fps at 0 while the session runs");
    a_client();
    mp_world_holds_frame();
    ut_check(cells_are(1u, 0u, 0u), "happy on and evil force off as the host said, 60fps off");
    s.evil = 1u;   /* a code typed on this machine */
    s.fast = 1u;
    mp_world_holds_frame();
    ut_check(cells_are(1u, 0u, 0u), "a cheat typed here is put back at the next frame");
    s.host_cheats = (uint8_t)MP_HOST_SETTINGS_CHEAT_EVIL_FORCE;
    mp_world_holds_frame();
    ut_check(cells_are(0u, 1u, 0u), "and a change of the host's is followed at the next frame");
}

static void check_the_transport_coming_down(void)
{
    ut_section("the transport coming down puts this side's own values back without a frame");
    a_client();
    mp_world_holds_frame();
    ut_check(cells_are(1u, 0u, 0u), "the cells are held");
    s.armed = false;   /* what the exit does before it withdraws */
    mp_world_holds_withdraw();
    ut_check(cells_are(0u, 1u, 1u),
             "and this side's own happy 0, evil force 1 and 60fps 1 are back");
    mp_world_holds_frame();
    ut_check(cells_are(0u, 1u, 1u), "no frame of single player after it touches them");
    mp_world_holds_withdraw();
    ut_check(cells_are(0u, 1u, 1u), "and a second withdrawal writes nothing");
}

static void check_the_session_ending(void)
{
    ut_section("a session that stops running while the transport stands gives the cells back");
    a_client();
    mp_world_holds_frame();
    s.flags = (uint8_t)(MP_LOBBY_F_STARTED | MP_LOBBY_F_ENDED);   /* the host ended it */
    mp_world_holds_frame();
    ut_check(cells_are(0u, 1u, 1u), "at the next frame begin");
}

/* The host said goodbye. The frame pump that takes it ends the session in the same run: the lobby
 * is reset and its setup note gone, while the bridge's joined flag still says yes until a substep
 * runs. The first version of the one answer asked that flag for a lobby with no note, said "plays",
 * and the holds went on holding the cheats at 0 for want of a host value. A frame begin can also
 * fall between the goodbye and the reset (the timer pump takes the goodbye, the frame pump ends
 * the session), and there the note still says started with the connection gone. Both end every
 * hold at the next frame, 60fps included. */
static void check_the_hosts_goodbye(void)
{
    mp_world_holds_counts_t before;
    mp_world_holds_counts_t after;

    ut_section("a host's goodbye gives every cell back at the next frame, 60fps included");
    a_client();
    mp_world_holds_frame();
    ut_check(cells_are(1u, 0u, 0u), "the cells are held on the host's values");
    s.connection = 0u;   /* the goodbye, before the frame pump has ended the session */
    mp_world_holds_frame();
    ut_check(cells_are(0u, 1u, 1u),
             "with the note still saying started, the connection alone gives them back");

    a_client();
    mp_world_holds_frame();
    s.connection = 0u;     /* the goodbye */
    s.note_known = false;  /* and the exit's reset of the lobby in the same pump */
    s.joined     = true;   /* while the flag only a substep writes still says joined */
    mp_world_holds_counts(&before);
    mp_world_holds_frame();
    mp_world_holds_counts(&after);
    ut_check(cells_are(0u, 1u, 1u) &&
                 after.frames_without_host_value == before.frames_without_host_value &&
                 after.frames_at_host_value == before.frames_at_host_value,
             "with the lobby reset no cell is held and no frame is counted, whatever the bridge's "
             "joined flag says");
}

static void check_a_second_session(void)
{
    ut_section("a second session in the same process holds from the values this side has then");
    a_client();
    mp_world_holds_frame();
    mp_world_holds_withdraw();
    s.happy = 1u;   /* this side turned happy on while it played alone */
    s.evil  = 0u;
    s.fast  = 0u;
    s.armed = true;
    s.host_cheats = 0u;
    mp_world_holds_frame();
    ut_check(cells_are(0u, 0u, 0u), "the new host says both off");
    mp_world_holds_withdraw();
    ut_check(cells_are(1u, 0u, 0u),
             "and the exit gives back happy on, not the first session's happy off");
}

static void check_no_host_value_yet(void)
{
    ut_section("with no value of the host's yet a client holds both cheats at 0");
    a_client();
    s.host_said = false;   /* before the first note, or against a dedicated server */
    s.happy     = 1u;
    mp_world_holds_frame();
    ut_check(cells_are(0u, 0u, 0u), "happy and evil force at 0, and 60fps at 0 regardless");
    s.host_said = true;
    mp_world_holds_frame();
    ut_check(cells_are(1u, 0u, 0u), "the host's value is held once it has come");
    mp_world_holds_withdraw();
    ut_check(cells_are(1u, 1u, 1u), "and the exit gives back this side's own, happy on as it was");
}

/* The report's line said "held at the host's values on N frame(s)" for every frame a client held
 * the cheats, the frames at 0 for want of a note among them, so a dedicated server's client read as
 * one that had held its host's values the whole time. */
static void check_the_count_tells_the_two_apart(void)
{
    mp_world_holds_counts_t before;
    mp_world_holds_counts_t after;
    int                     frame;

    ut_section("the count tells the host's values from 0 for want of them");
    a_client();
    mp_world_holds_counts(&before);
    s.host_said = false;
    for (frame = 0; frame < 5; ++frame) {
        mp_world_holds_frame();
    }
    s.host_said = true;
    for (frame = 0; frame < 5; ++frame) {
        mp_world_holds_frame();
    }
    mp_world_holds_counts(&after);
    ut_checkf(after.frames_without_host_value - before.frames_without_host_value == 5u &&
                  after.frames_at_host_value - before.frames_at_host_value == 5u,
              "five frames at 0 and five at the host's values are counted as five and five "
              "(%u and %u)",
              (unsigned)(after.frames_without_host_value - before.frames_without_host_value),
              (unsigned)(after.frames_at_host_value - before.frames_at_host_value));
    mp_world_holds_withdraw();

    a_client();
    s.client = false;
    mp_world_holds_counts(&before);
    mp_world_holds_frame();
    mp_world_holds_counts(&after);
    ut_check(after.frames_at_host_value == before.frames_at_host_value &&
                 after.frames_without_host_value == before.frames_without_host_value &&
                 after.fast_frames == before.fast_frames + 1u,
             "a host counts neither, and one more frame of 60fps held");
    mp_world_holds_withdraw();
}

static void check_a_host(void)
{
    ut_section("a host holds only 60fps at 0 and keeps its own cheats, which are the session's");
    a_client();
    s.client = false;
    s.happy  = 1u;
    mp_world_holds_frame();
    ut_check(cells_are(1u, 1u, 0u), "happy and evil force as the host has them, 60fps off");
    mp_world_holds_withdraw();
    ut_check(cells_are(1u, 1u, 1u), "and its own 60fps is back at the exit");
}

static void check_single_player(void)
{
    ut_section("with no transport nothing is held");
    a_client();
    s.armed = false;
    mp_world_holds_frame();
    ut_check(cells_are(0u, 1u, 1u), "single player keeps every cell as it is");
    a_client();
    s.cells_resolved = false;
    mp_world_holds_frame();
    mp_world_holds_withdraw();
    ut_check(cells_are(0u, 1u, 1u), "and a cell that did not resolve is left alone");
    mp_world_holds_report();
}

int main(void)
{
    check_a_client_holds_the_hosts_values();
    mp_world_holds_withdraw();
    check_the_transport_coming_down();
    check_the_session_ending();
    check_the_hosts_goodbye();
    check_a_second_session();
    check_no_host_value_yet();
    check_the_count_tells_the_two_apart();
    check_a_host();
    check_single_player();
    return ut_summary("mp_world_holds");
}
