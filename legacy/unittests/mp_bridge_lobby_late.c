/* A client whose lobby opens on a session that is already running, against the real lobby and the
 * real drain, the host played by hand over the loopback.
 *
 * In a field run with four players the fourth joined a level the host had been in for minutes:
 * the roster said "not ready", and the same second its lobby started the level with hero 0. A
 * fresh lobby had acted on no generation, so the first setup it heard was a start, and nothing
 * asked for ready. And the lobby read every note the host's level sent it, so a mover event waited
 * with no level and fired into the next one. Each check below fails if its defect comes back:
 *
 *   - the lobby says it holds a running session's start, and the wait for ready is measured;
 *   - a start taken from the lobby while this player is not ready is counted, whoever asked the
 *     rule, so a screen that does not ask shows up in the report as a number that must be 0;
 *   - a start of a session that was running when the lobby first heard its host is remembered as a
 *     late join, and one the host starts in front of the lobby is not;
 *   - the lobby takes the roster and the setup and drops the level's notes, counted;
 *   - the host's world settings (0xAB) are a lobby note, taken through the lobby's own drain.
 */
#include "unittest.h"

#include "mp_armed.h"
#include "mp_bridge_drain.h"
#include "mp_bridge_lobby.h"
#include "mp_bridge_lobby_late.h"
#include "mp_bridge_roster.h"
#include "mp_events.h"
#include "mp_host_settings.h"
#include "mp_host_settings_rule.h"
#include "mp_level_state_rule.h"
#include "mp_lobby.h"
#include "mp_loopback.h"
#include "mp_roster.h"
#include "mp_session.h"
#include "mp_transport.h"

#include "common/host_settings_note.h"

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

/* A client's lobby, open, over a fresh wire, connected to a host this test speaks for. */
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
    mp_bridge_set_lobby(MP_LOBBY_TEAM_NONE, false);   /* what the lobby screen does as it opens */
    mp_bridge_lobby_set_open(true);
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

/* What the host's running level says besides: a door, the level's state, and the roster. */
static void host_says_its_level(void)
{
    mp_event_t  door;
    mp_roster_t roster;
    uint8_t     note[MP_CHANNEL_MESSAGE_BYTES];
    size_t      bytes;

    memset(&door, 0, sizeof door);
    door.kind       = MP_EVENT_MOVER;
    door.mover_id   = 12u;
    door.mover_mode = MP_EVENT_MOVER_OPEN;
    bytes = mp_event_encode(&door, note, sizeof note);
    ut_check(bytes != 0u && mp_session_send_reliable(&s_host, 0u, note, bytes),
             "the host's level opens a door");
    memset(note, 0, sizeof note);
    note[0] = (uint8_t)MP_LEVEL_STATE_TAG;
    ut_check(mp_session_send_reliable(&s_host, 0u, note, 24u), "and says its level's state");
    memset(&roster, 0, sizeof roster);
    roster.count = 1u;
    memcpy(roster.entry[0].name, "Host", 5u);
    bytes = mp_roster_encode(&roster, note, sizeof note);
    ut_check(bytes != 0u && mp_session_send_reliable(&s_host, 0u, note, bytes),
             "and its roster");
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

static void check_a_running_session_waits_for_ready(void)
{
    mp_bridge_lobby_late_counts_t before;
    mp_bridge_lobby_late_counts_t after;
    mp_lobby_setup_t              taken;
    mp_roster_t                   table;
    uint32_t                      now = 0u;
    uint32_t                      dropped_before;
    uint32_t                      lobby_before = 0u;
    uint32_t                      lobby_after  = 0u;

    ut_section("a lobby that opens on a running session holds the start for ready");
    ut_check(a_client_lobby(&now, 0x6100u), "the client joins from its lobby");
    mp_bridge_lobby_late_counts(&before);
    dropped_before = mp_bridge_drain_lobby_counts(&lobby_before);
    host_says_setup((uint8_t)MP_LOBBY_F_STARTED, 5u);
    host_says_its_level();
    lobby_ticks(&now, 20);
    ut_check(mp_bridge_lobby_joins_running_session(),
             "the lobby holds the start of a session whose host already plays");
    ut_check(!mp_lobby_start_may_be_taken(false, mp_bridge_lobby_joins_running_session()),
             "and the rule says: not before this player is ready");
    mp_bridge_lobby_late_counts(&after);
    ut_checkf(after.held == before.held + 1u,
              "the wait for ready began once (%u)", (unsigned)(after.held - before.held));

    ut_section("the lobby took the lobby's notes and dropped the level's");
    ut_check(mp_bridge_roster_current(&table) && table.count == 1u, "the roster was taken");
    ut_checkf(mp_bridge_drain_lobby_counts(&lobby_after) == dropped_before + 2u,
              "the door and the level's state were dropped, counted (%u)",
              (unsigned)(mp_bridge_drain_lobby_counts(NULL) - dropped_before));
    ut_checkf(lobby_after >= lobby_before + 2u, "and the setup and the roster taken (%u)",
              (unsigned)(lobby_after - lobby_before));

    ut_section("a start taken while this player is not ready is counted");
    ut_check(mp_bridge_lobby_take_start(&taken), "a screen that does not ask the rule takes it");
    mp_bridge_lobby_late_counts(&after);
    ut_checkf(after.taken_not_ready == before.taken_not_ready + 1u,
              "and the report's must-be-0 count says so (%u)",
              (unsigned)(after.taken_not_ready - before.taken_not_ready));
    ut_check(mp_bridge_lobby_late_start(),
             "the start is remembered as a late join: the session ran when this lobby first "
             "heard its host");
    ut_check(!mp_bridge_lobby_joins_running_session(), "and once taken, nothing is held any more");
    mp_bridge_lobby_set_open(false);
    mp_session_disconnect(&s_client);
    pump(&now, 20);
}

static void check_ready_releases_the_start(void)
{
    mp_bridge_lobby_late_counts_t before;
    mp_bridge_lobby_late_counts_t after;
    mp_lobby_setup_t              taken;
    uint32_t                      now = 0u;

    ut_section("the player says ready, and the start is taken");
    ut_check(a_client_lobby(&now, 0x6200u), "the client joins from its lobby");
    mp_bridge_lobby_late_counts(&before);
    host_says_setup((uint8_t)MP_LOBBY_F_STARTED, 7u);
    lobby_ticks(&now, 20);
    mp_bridge_set_lobby(MP_LOBBY_TEAM_NONE, true);
    lobby_ticks(&now, 4);
    ut_check(mp_lobby_start_may_be_taken(true, mp_bridge_lobby_joins_running_session()),
             "ready, and the start offered: the rule lets it through");
    ut_check(mp_bridge_lobby_take_start(&taken) && taken.generation == 7u, "the start is taken");
    mp_bridge_lobby_late_counts(&after);
    ut_checkf(after.taken == before.taken + 1u &&
                  after.taken_not_ready == before.taken_not_ready,
              "taken with this player ready, and nothing for the must-be-0 count (%u)",
              (unsigned)(after.taken_not_ready - before.taken_not_ready));
    ut_check(after.held == before.held + 1u, "after one wait for ready");
    mp_bridge_lobby_set_open(false);
    mp_session_disconnect(&s_client);
    pump(&now, 20);
}

static void check_a_start_in_front_of_the_lobby_is_no_late_join(void)
{
    mp_lobby_setup_t taken;
    uint32_t         now = 0u;

    ut_section("a start the host makes in front of the lobby is no late join");
    ut_check(a_client_lobby(&now, 0x6300u), "the client joins from its lobby");
    host_says_setup(0u, 0u);
    lobby_ticks(&now, 20);
    ut_check(!mp_bridge_lobby_joins_running_session(), "a lobby before the start holds nothing");
    mp_bridge_set_lobby(MP_LOBBY_TEAM_NONE, true);
    host_says_setup((uint8_t)MP_LOBBY_F_STARTED, 1u);
    lobby_ticks(&now, 20);
    ut_check(mp_bridge_lobby_take_start(&taken), "everybody ready, the host starts: taken");
    ut_check(!mp_bridge_lobby_late_start(),
             "and the arrival is told this was nobody's late join, so the seats below are "
             "foreseen as before");

    ut_section("a start the follow takes, with the screen shut, is a world change");
    mp_bridge_lobby_set_open(false);
    host_says_setup((uint8_t)MP_LOBBY_F_STARTED, 2u);
    pump(&now, 20);
    mp_bridge_drain_reliable_notes(&s_drain);   /* inside a level, as a substep reads them */
    ut_check(mp_bridge_lobby_take_start(&taken) && taken.generation == 2u,
             "the next world's start is taken by the follow");
    ut_check(!mp_bridge_lobby_late_start(), "and it is no late join either");
    mp_session_disconnect(&s_client);
    pump(&now, 20);
}

/* The host's world settings are a lobby's note: a client that joins a running session takes them
 * in its lobby, so its mods run the host's values while the start waits for ready. They reach the
 * module only through the lobby's own drain, and only while that drain's rule counts 0xAB as a
 * lobby note and its band reaches the tag; without either the record would stay without running. */
static void check_the_hosts_settings_through_the_lobby(void)
{
    mp_host_settings_note_t settings;
    host_settings_t         record;
    uint8_t                 note[MP_HOST_SETTINGS_BYTES];
    uint8_t                 cheats = 0u;
    uint32_t                now = 0u;
    uint32_t                taken_before = 0u;
    uint32_t                taken_after = 0u;
    uint32_t                dropped_before;

    ut_section("the host's settings reach a client's lobby, and through it its mods");
    ut_check(a_client_lobby(&now, 0x6400u), "the client joins from its lobby");
    mp_armed_set_transport(true, false);   /* what the arming does before the lobby opens */
    dropped_before = mp_bridge_drain_lobby_counts(&taken_before);
    memset(&settings, 0, sizeof settings);
    settings.cheats  = (uint8_t)MP_HOST_SETTINGS_CHEAT_HAPPY;
    settings.present = 0x000Fu;
    settings.values[HOST_SETTING_VIEW_RANGE_SCALE]   = 1.5f;
    settings.values[HOST_SETTING_FOG_BAND_SCALE]     = 0.5f;
    settings.values[HOST_SETTING_AUTHORED_FOG_BAND]  = 1.0f;
    settings.values[HOST_SETTING_DISMEMBERMENT_MODE] = 2.0f;
    ut_check(mp_host_settings_encode(&settings, note, sizeof note) == sizeof note &&
                 mp_session_send_reliable(&s_host, 0u, note, sizeof note),
             "the host says its settings, all four and happy on");
    host_says_setup((uint8_t)MP_LOBBY_F_STARTED, 1u);
    pump(&now, 12);
    mp_bridge_drain_lobby_notes(&s_drain);
    mp_host_settings_pump();
    ut_check(host_settings_read(&record) && record.running && record.present == 0x000Fu &&
                 record.values[HOST_SETTING_VIEW_RANGE_SCALE] == 1.5f &&
                 record.values[HOST_SETTING_FOG_BAND_SCALE] == 0.5f &&
                 record.values[HOST_SETTING_AUTHORED_FOG_BAND] == 1.0f &&
                 record.values[HOST_SETTING_DISMEMBERMENT_MODE] == 2.0f,
             "the record the mods read runs, with the four values as the host said them");
    ut_check(mp_host_settings_cheats(&cheats) && cheats == MP_HOST_SETTINGS_CHEAT_HAPPY,
             "and the world holds can read the host's happy");
    ut_checkf(mp_bridge_drain_lobby_counts(&taken_after) == dropped_before &&
                  taken_after == taken_before + 2u,
              "the lobby took the settings and the setup and dropped nothing (%u taken)",
              (unsigned)(taken_after - taken_before));
    mp_host_settings_withdraw();
    mp_armed_set_transport(false, false);
    mp_bridge_lobby_set_open(false);
    mp_session_disconnect(&s_client);
    pump(&now, 20);
}

int main(void)
{
    check_a_running_session_waits_for_ready();
    check_ready_releases_the_start();
    check_a_start_in_front_of_the_lobby_is_no_late_join();
    check_the_hosts_settings_through_the_lobby();
    mp_bridge_lobby_late_report(false);
    return ut_summary("mp_bridge_lobby_late");
}
