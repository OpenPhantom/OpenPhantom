/* The lobby screen's start and its band, against the real lobby and the real drain, the host
 * played by hand over the loopback.
 *
 * A player who joined a running session was in the level the moment the lobby heard its host,
 * with hero 0 and without pressing ready: the screen's frame took every start it was offered. The
 * frame now takes a start only through mp_screens_lobby_start_to_take, and this program drives that
 * function exactly as the frame does, the band beside it, and reads the bridge's own account of
 * the starts, whose count of starts taken while not ready must stay 0:
 *
 *   - a lobby that opens on a running session takes nothing until this player is ready, and its
 *     band says to pick a hero and then say ready;
 *   - once ready, the start is taken, with nothing for the must-be-0 count, and the band says the
 *     level is about to begin;
 *   - an error line and a lost host are said ahead of both sentences;
 *   - a start the host makes in front of a lobby where everybody is ready is taken on its first
 *     frame.
 *
 * The screen itself cannot run in a test process, so the frame is the two lines it runs for this:
 * the band, and the take on the answer of the same function. That the frame calls it and nothing
 * else is the one line in lobby_frame this program cannot see.
 */
#include "unittest.h"

#include "mp_bridge_drain.h"
#include "mp_bridge_lobby.h"
#include "mp_bridge_lobby_late.h"
#include "mp_lobby.h"
#include "mp_loopback.h"
#include "mp_menu_screens_int.h"
#include "mp_session.h"
#include "mp_text.h"
#include "mp_transport.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Static: a session is over two megabytes now that every peer carries a bulk ring. */
static mp_loopback_t     s_net;
static mp_session_t      s_host;
static mp_session_t      s_client;
static mp_transport_t    s_host_t;
static mp_transport_t    s_client_t;
static mp_bridge_drain_t s_drain;

static void pump(uint32_t *now, int ticks)
{
    int tick;

    for (tick = 0; tick < ticks; ++tick) {
        *now += 16u;
        mp_loopback_pump(&s_net, *now);
        mp_session_update(&s_host, *now);
        mp_session_update(&s_client, *now);
    }
}

/* A client's lobby screen, open and not ready, over a fresh wire, connected to a host this test
 * speaks for. The screen's own state is set the way mp_screens_run_lobby sets it for a client. */
static bool a_client_lobby(uint32_t *now, uint32_t seed)
{
    mp_loopback_conditions_t cond;
    int                      tick;

    memset(&cond, 0, sizeof cond);
    cond.delay_ms = 16u;
    mp_loopback_init(&s_net, &cond, seed);
    s_host_t   = mp_loopback_transport(&s_net, MP_LOOPBACK_ENDPOINT_A);
    s_client_t = mp_loopback_transport(&s_net, MP_LOOPBACK_ENDPOINT_B);
    mp_session_init(&s_host, MP_SESSION_HOST, &s_host_t, seed + 1u);
    mp_session_init(&s_client, MP_SESSION_CLIENT, &s_client_t, seed + 2u);
    mp_bridge_drain_bind(&s_drain, MP_BRIDGE_UDP_CLIENT, &s_host, &s_client);
    mp_bridge_lobby_bind(&s_host, &s_client, NULL, true, &s_drain);
    mp_bridge_lobby_reset();
    mp_bridge_set_lobby(MP_LOBBY_TEAM_NONE, false);
    mp_bridge_lobby_set_open(true);
    mps.lobby_is_host  = false;
    mps.lobby_error[0] = '\0';
    mp_session_connect(&s_client, MP_LOOPBACK_ENDPOINT_A);
    for (tick = 0; tick < 400; ++tick) {
        pump(now, 1);
        if (mp_session_is_connected(&s_client) && mp_session_peer_count(&s_host) == 1u) {
            return true;
        }
    }
    return false;
}

/* The host's setup, as its repeat carries it. */
static void host_says_setup(uint8_t flags, uint8_t generation)
{
    mp_lobby_setup_t setup;
    uint8_t          note[MP_LOBBY_SETUP_BYTES];

    memset(&setup, 0, sizeof setup);
    setup.mode       = MP_LOBBY_MODE_COOP;
    setup.flags      = flags;
    setup.generation = generation;
    memcpy(setup.level, "level\\fedship.b3d", 18u);
    memcpy(setup.title, "Fedship", 8u);
    ut_check(mp_lobby_setup_encode(&setup, note, sizeof note) == MP_LOBBY_SETUP_BYTES &&
                 mp_session_send_reliable(&s_host, 0u, note, sizeof note),
             "the host says its setup");
}

/* The lobby's idle tick, as the pump runs it with the screen open, and the wire behind it. */
static void lobby_ticks(uint32_t *now, int ticks)
{
    int tick;

    for (tick = 0; tick < ticks; ++tick) {
        pump(now, 1);
        mp_bridge_lobby_tick(false, 0u);
    }
}

/* What the screen's frame does about a start: the band first, then the take on the answer of the
 * one function the frame asks. Answers whether a start was taken. */
static bool the_frame(mp_lobby_setup_t *taken)
{
    mp_lobby_setup_t setup;

    mp_screens_refresh_band();
    if (!mp_screens_lobby_start_to_take(&setup)) {
        return false;
    }
    return mp_bridge_lobby_take_start(taken);
}

static bool band_says(mp_text_id_t id)
{
    return strcmp(mps.lobby_band, mp_text(id)) == 0;
}

static void check_a_running_session_waits_for_ready(void)
{
    mp_bridge_lobby_late_counts_t before;
    mp_bridge_lobby_late_counts_t after;
    mp_lobby_setup_t              taken;
    uint32_t                      now = 0u;
    uint32_t                      frame;
    bool                          any = false;

    ut_section("a lobby that opens on a running session takes nothing before ready");
    ut_check(a_client_lobby(&now, 0x7100u), "the client joins from its lobby");
    mp_bridge_lobby_late_counts(&before);
    host_says_setup((uint8_t)MP_LOBBY_F_STARTED, 5u);
    lobby_ticks(&now, 20);
    ut_check(mp_bridge_lobby_joins_running_session(),
             "the lobby holds the start of a session whose host already plays");
    for (frame = 0; frame < 30u; ++frame) {
        any = the_frame(&taken) || any;
        lobby_ticks(&now, 1);
    }
    ut_check(!any, "thirty frames of the screen take no start while this player is not ready");
    ut_check(band_says(MP_TEXT_BAND_RUNNING_PICK),
             "and the band says to pick a hero and then say ready");
    mp_bridge_lobby_late_counts(&after);
    ut_checkf(after.taken == before.taken && after.taken_not_ready == before.taken_not_ready,
              "nothing was taken, and nothing for the must-be-0 count (%u taken, %u not ready)",
              (unsigned)(after.taken - before.taken),
              (unsigned)(after.taken_not_ready - before.taken_not_ready));
    ut_check(after.held == before.held + 1u, "the bridge counts one wait for ready");

    ut_section("an error line is said ahead of the running session");
    memcpy(mps.lobby_error, "an error line", 14u);
    (void)the_frame(&taken);
    ut_check(strcmp(mps.lobby_band, "an error line") == 0,
             "the band shows the error, which is about what the player just did");
    mps.lobby_error[0] = '\0';

    ut_section("ready, and the start is taken");
    mp_bridge_set_lobby(MP_LOBBY_TEAM_NONE, true);
    mp_screens_refresh_band();
    ut_check(band_says(MP_TEXT_BAND_RUNNING_READY),
             "once ready, the band says the level is about to begin");
    ut_check(the_frame(&taken) && taken.generation == 5u,
             "and the frame takes the start it was holding");
    mp_bridge_lobby_late_counts(&after);
    ut_checkf(after.taken == before.taken + 1u &&
                  after.taken_not_ready == before.taken_not_ready,
              "taken once, with this player ready: the must-be-0 count stays where it was (%u)",
              (unsigned)(after.taken_not_ready - before.taken_not_ready));
    ut_check(!mp_bridge_lobby_joins_running_session(), "and nothing is held any more");
    mp_screens_refresh_band();
    ut_check(band_says(MP_TEXT_JOIN_CONNECTED), "the band is back to its connected line");
    mp_bridge_lobby_set_open(false);
    mp_session_disconnect(&s_client);
    pump(&now, 20);
}

static void check_a_lost_host_is_said_first(void)
{
    mp_lobby_setup_t taken;
    uint32_t         now = 0u;

    ut_section("a host that is gone is said ahead of the running session");
    ut_check(a_client_lobby(&now, 0x7200u), "the client joins from its lobby");
    host_says_setup((uint8_t)MP_LOBBY_F_STARTED, 6u);
    lobby_ticks(&now, 20);
    ut_check(mp_bridge_lobby_joins_running_session(), "the lobby holds a running session's start");
    mp_session_disconnect(&s_client);
    pump(&now, 20);
    ut_check(!the_frame(&taken), "nothing is taken");
    ut_check(!band_says(MP_TEXT_BAND_RUNNING_PICK) && !band_says(MP_TEXT_BAND_RUNNING_READY),
             "and the band does not tell the player to pick a hero for a host that is not there");
    mp_bridge_lobby_set_open(false);
    pump(&now, 20);
}

static void check_a_start_in_front_of_the_lobby(void)
{
    mp_bridge_lobby_late_counts_t before;
    mp_bridge_lobby_late_counts_t after;
    mp_lobby_setup_t              taken;
    uint32_t                      now = 0u;

    ut_section("everybody ready, the host starts: taken on the first frame");
    ut_check(a_client_lobby(&now, 0x7300u), "the client joins from its lobby");
    mp_bridge_lobby_late_counts(&before);
    host_says_setup(0u, 0u);
    lobby_ticks(&now, 20);
    ut_check(!the_frame(&taken), "a lobby before the start takes nothing");
    ut_check(band_says(MP_TEXT_JOIN_CONNECTED), "and its band says it waits for the host");
    mp_bridge_set_lobby(MP_LOBBY_TEAM_NONE, true);
    host_says_setup((uint8_t)MP_LOBBY_F_STARTED, 1u);
    lobby_ticks(&now, 20);
    ut_check(the_frame(&taken) && taken.generation == 1u,
             "the start is taken on the first frame that sees it");
    ut_check(band_says(MP_TEXT_BAND_RUNNING_READY),
             "and in that frame the band says the level is about to begin");
    mp_bridge_lobby_late_counts(&after);
    ut_checkf(after.taken == before.taken + 1u &&
                  after.taken_not_ready == before.taken_not_ready &&
                  after.held == before.held,
              "one start taken, none held, none taken while not ready (%u, %u, %u)",
              (unsigned)(after.taken - before.taken), (unsigned)(after.held - before.held),
              (unsigned)(after.taken_not_ready - before.taken_not_ready));
    mp_bridge_lobby_set_open(false);
    mp_session_disconnect(&s_client);
    pump(&now, 20);
}

int main(void)
{
    mp_bridge_lobby_late_counts_t counts;

    check_a_running_session_waits_for_ready();
    check_a_lost_host_is_said_first();
    check_a_start_in_front_of_the_lobby();
    mp_bridge_lobby_late_counts(&counts);
    ut_checkf(counts.taken_not_ready == 0u,
              "over the whole run the screen took no start while this player was not ready (%u)",
              (unsigned)counts.taken_not_ready);
    return ut_summary("mp_menu_screens_lobby");
}
