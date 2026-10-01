/* mp_host_settings.c: the host's world settings from the host's setup to the record the mods of a
 * client read, and every way out of a session, against the real lobby and the real drain.
 *
 * The module runs a protocol, so the test compiles it with the modules it talks through and plays
 * only the far side. As the host it is the lobby's own send that puts the note on the wire, in
 * front of the setup note; as a client the test plays the host by hand and the note goes through
 * the drain's receive_event, the one road a reliable note takes to a module. After each way out of
 * the session (the lobby's back door, the host ending it, the host's goodbye, the transport coming
 * down) the record a mod reads says no session, and a second session in the same process does not
 * carry the first host's values. A host never publishes the record at all.
 *
 * The first section runs before anything has published, because the record is the process's.
 */
#include "unittest.h"

#include "mp_armed.h"
#include "mp_bridge_drain.h"
#include "mp_bridge_lobby.h"
#include "mp_host_settings.h"
#include "mp_host_settings_rule.h"
#include "mp_lobby.h"
#include "mp_loopback.h"
#include "mp_session.h"
#include "mp_session_now.h"
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
static uint32_t          s_now;

static void pump(int ticks)
{
    int tick;

    for (tick = 0; tick < ticks; ++tick) {
        s_now += 16u;
        mp_loopback_pump(&s_net, s_now);
        mp_session_update(&s_host, s_now);
        mp_session_update(&s_client, s_now);
    }
}

/* Two sessions over a fresh wire, connected, with the lobby and the drain bound as one side and a
 * transport standing, as the arming leaves them. */
static bool a_session(bool as_client, uint32_t seed)
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
    mp_bridge_lobby_bind(&s_host, &s_client, NULL, as_client, NULL);
    mp_bridge_lobby_reset();
    mp_bridge_drain_bind(&s_drain, as_client ? MP_BRIDGE_UDP_CLIENT : MP_BRIDGE_UDP_HOST, &s_host,
                         &s_client);
    mp_armed_set_transport(true, !as_client);
    mp_session_connect(&s_client, MP_LOOPBACK_ENDPOINT_A);
    for (tick = 0; tick < 400; ++tick) {
        pump(1);
        if (mp_session_is_connected(&s_client) && mp_session_peer_count(&s_host) == 1u) {
            return true;
        }
    }
    return false;
}

static void a_setup(mp_lobby_setup_t *setup, uint8_t flags, uint8_t generation)
{
    memset(setup, 0, sizeof *setup);
    setup->mode       = (uint8_t)MP_LOBBY_MODE_COOP;
    setup->flags      = flags;
    setup->generation = generation;
    memcpy(setup->level, "level\\fedship.b3d", 18u);
}

/* The host the test plays: its settings note, then its setup, as the lobby's send puts them. */
static void the_host_says(bool with_settings, uint8_t flags, uint8_t generation)
{
    mp_host_settings_note_t settings;
    mp_lobby_setup_t        setup;
    uint8_t                 note[256];
    size_t                  length;

    if (with_settings) {
        memset(&settings, 0, sizeof settings);
        settings.cheats  = (uint8_t)MP_HOST_SETTINGS_CHEAT_HAPPY;
        settings.present = 0x000Fu;
        settings.values[HOST_SETTING_VIEW_RANGE_SCALE]   = 1.5f;
        settings.values[HOST_SETTING_FOG_BAND_SCALE]     = 0.5f;
        settings.values[HOST_SETTING_AUTHORED_FOG_BAND]  = 1.0f;
        settings.values[HOST_SETTING_DISMEMBERMENT_MODE] = 2.0f;
        length = mp_host_settings_encode(&settings, note, sizeof note);
        ut_check(length != 0u && mp_session_broadcast_reliable(&s_host, note, length) == 1u,
                 "the host sends its settings");
    }
    a_setup(&setup, flags, generation);
    length = mp_lobby_setup_encode(&setup, note, sizeof note);
    ut_check(length != 0u && mp_session_broadcast_reliable(&s_host, note, length) == 1u,
             "and its setup");
    pump(12);
    mp_bridge_drain_reliable_notes(&s_drain);
}

/* What both pumps do; the module reads the session itself. */
static void the_pumps_run(void)
{
    mp_host_settings_pump();
}

static bool the_record_runs(void)
{
    host_settings_t record;

    return host_settings_read(&record) && record.running;
}

/* ============================================================================================ */

static void check_the_host(void)
{
    mp_lobby_setup_t        setup;
    mp_host_settings_note_t heard;
    host_settings_t         record;
    uint8_t                 note[MP_CHANNEL_MESSAGE_BYTES];
    size_t                  bytes = 0;
    int                     order = 0;
    int                     settings_at = 0;
    int                     setup_at = 0;

    ut_section("the host says its settings in front of its setup, and never files the record");
    memset(&heard, 0x5A, sizeof heard);
    ut_check(a_session(false, 0x6100u), "a host and a client");
    a_setup(&setup, 0u, 0u);
    mp_bridge_lobby_set_setup(&setup);
    mp_bridge_lobby_start();
    pump(12);
    while (mp_session_read_reliable(&s_client, 0u, note, sizeof note, &bytes)) {
        ++order;
        if (settings_at == 0 && mp_host_settings_decode(note, bytes, &heard) ==
                                    MP_HOST_SETTINGS_TAKEN) {
            settings_at = order;
        }
        if (setup_at == 0 && mp_lobby_setup_decode(note, bytes, &setup)) {
            setup_at = order;
        }
    }
    ut_check(settings_at != 0 && setup_at != 0 && settings_at < setup_at,
             "the client hears a settings note, and before the first setup");
    ut_check(heard.present == 0u && heard.cheats == 0u,
             "a host with none of these mods loaded and no cheat on names nothing");
    ut_check(mp_session_now_plays_in_a_running_session(NULL),
             "the host plays in the session it started");
    the_pumps_run();
    mp_host_settings_withdraw();
    ut_check(!host_settings_read(&record),
             "and no record was filed on the host, not by the pump and not by the exit");
    mp_armed_set_transport(false, false);
}

static void check_a_client_takes_and_files(void)
{
    host_settings_t record;
    uint8_t         cheats = 0u;

    ut_section("a client takes the host's note off its channel and files it for its mods");
    ut_check(a_session(true, 0x6200u), "a client and its host");
    the_host_says(true, 0u, 0u);
    the_pumps_run();
    ut_check(!host_settings_read(&record),
             "in the lobby before the start nothing is filed: no level runs that could use it");
    ut_check(mp_host_settings_cheats(&cheats) && cheats == MP_HOST_SETTINGS_CHEAT_HAPPY,
             "but the world holds can read the host's cheats already");

    the_host_says(true, (uint8_t)MP_LOBBY_F_STARTED, 1u);
    the_pumps_run();
    ut_check(host_settings_read(&record) && record.running && record.present == 0x000Fu &&
                 record.values[HOST_SETTING_VIEW_RANGE_SCALE] == 1.5f &&
                 record.values[HOST_SETTING_FOG_BAND_SCALE] == 0.5f &&
                 record.values[HOST_SETTING_AUTHORED_FOG_BAND] == 1.0f &&
                 record.values[HOST_SETTING_DISMEMBERMENT_MODE] == 2.0f,
             "once the host has started, the record runs with the host's four values");
    ut_check(record.published == 1u && record.generation == 1u, "filed once, generation 1");
    the_host_says(true, (uint8_t)MP_LOBBY_F_STARTED, 1u);
    the_pumps_run();
    the_pumps_run();
    ut_check(host_settings_read(&record) && record.published == 1u,
             "the same note again, and a pump with nothing new, file nothing new");
}

static void check_the_lobbys_back_door(void)
{
    ut_section("every way out reads running = false: the lobby's back door");
    ut_check(the_record_runs(), "the record runs");
    mp_bridge_lobby_withdraw();
    the_pumps_run();
    ut_check(!the_record_runs(), "after BACK the record says no session");
    mp_host_settings_withdraw();
    mp_armed_set_transport(false, false);
}

static void check_the_host_ending_it(void)
{
    ut_section("every way out reads running = false: the host ends the session");
    ut_check(a_session(true, 0x6300u), "a client and its host");
    the_host_says(true, (uint8_t)MP_LOBBY_F_STARTED, 1u);
    the_pumps_run();
    ut_check(the_record_runs(), "the record runs");
    the_host_says(true, (uint8_t)(MP_LOBBY_F_STARTED | MP_LOBBY_F_ENDED), 1u);
    the_pumps_run();
    ut_check(!the_record_runs(), "the host's word that it is over is heard at the next pump");
    mp_host_settings_withdraw();
    mp_armed_set_transport(false, false);
}

static void check_the_hosts_goodbye(void)
{
    ut_section("every way out reads running = false: the host says goodbye");
    ut_check(a_session(true, 0x6400u), "a client and its host");
    the_host_says(true, (uint8_t)MP_LOBBY_F_STARTED, 1u);
    the_pumps_run();
    ut_check(the_record_runs(), "the record runs");
    mp_session_disconnect(&s_host);
    pump(20);
    the_pumps_run();
    ut_check(!the_record_runs(), "with no host on the line nothing of the host's is in force");
    mp_host_settings_withdraw();
    mp_armed_set_transport(false, false);
}

/* The frame pump that takes a host's goodbye ends the session in the same run, and the exit resets
 * the lobby, so the pump after it finds no setup note at all. That lobby is nobody's session: the
 * record stops running, and no second record goes out claiming a session in which the host names
 * nothing. */
static void check_the_goodbye_and_the_reset(void)
{
    host_settings_t record;
    uint32_t        published_before;

    ut_section("a host's goodbye and the lobby's reset in the same pump file one withdrawal");
    ut_check(a_session(true, 0x6700u), "a client and its host");
    the_host_says(true, (uint8_t)MP_LOBBY_F_STARTED, 1u);
    the_pumps_run();
    ut_check(host_settings_read(&record) && record.running, "the record runs");
    published_before = record.published;
    mp_session_disconnect(&s_host);
    pump(20);
    mp_bridge_lobby_reset();   /* what the exit does when the session is over */
    the_pumps_run();
    the_pumps_run();
    ut_checkf(host_settings_read(&record) && !record.running &&
                  record.published == published_before + 1u,
              "the record says no session, filed once after the goodbye (%u publication(s))",
              (unsigned)(record.published - published_before));
    ut_check(!mp_session_now_plays_in_a_running_session(NULL),
             "and a lobby with no setup note is nobody's session");
    mp_host_settings_withdraw();
    mp_armed_set_transport(false, false);
}

static void check_the_transport_coming_down(void)
{
    host_settings_t record;

    ut_section("every way out reads running = false: the transport comes down");
    ut_check(a_session(true, 0x6500u), "a client and its host");
    the_host_says(true, (uint8_t)MP_LOBBY_F_STARTED, 1u);
    the_pumps_run();
    ut_check(the_record_runs(), "the record runs");
    mp_armed_set_transport(false, false);
    mp_host_settings_withdraw();
    ut_check(host_settings_read(&record) && !record.running && record.present == 0u,
             "the exit files a record that says no session, at once");
    the_pumps_run();
    ut_check(!the_record_runs(), "and no pump after it files anything, the transport being down");
}

static void check_a_second_join(void)
{
    host_settings_t record;
    uint8_t         cheats = 0u;

    ut_section("a second session in the same process does not carry the first host's values");
    ut_check(a_session(true, 0x6600u), "the same process joins another host");
    ut_check(!mp_host_settings_cheats(&cheats), "whose cheats are unknown until it has said them");
    the_host_says(false, (uint8_t)MP_LOBBY_F_STARTED, 1u);
    the_pumps_run();
    ut_check(host_settings_read(&record) && record.running && record.present == 0u,
             "a started session with no note yet runs and names nothing, so every mod keeps "
             "its own");
    the_host_says(true, (uint8_t)MP_LOBBY_F_STARTED, 1u);
    the_pumps_run();
    ut_check(host_settings_read(&record) && record.present == 0x000Fu,
             "and takes this host's values once it has said them");
    mp_armed_set_transport(false, false);
    mp_host_settings_withdraw();
    ut_check(!the_record_runs(), "until its own exit");
    mp_host_settings_report();
}

static void check_a_refused_note(void)
{
    uint8_t note[MP_HOST_SETTINGS_BYTES];

    ut_section("a note no host here would send is refused and taken off the channel");
    memset(note, 0, sizeof note);
    note[0] = (uint8_t)MP_HOST_SETTINGS_TAG;
    note[1] = 9u;
    ut_check(mp_host_settings_take(true, note, sizeof note), "a note of another version is ours");
    ut_check(!mp_host_settings_cheats(note), "and says nothing");
    ut_check(mp_host_settings_take(false, note, sizeof note),
             "a host or the loopback takes it too, and holds it for nobody");
    note[0] = 0x92u;
    ut_check(!mp_host_settings_take(true, note, sizeof note), "a note of another tag is not ours");
}

int main(void)
{
    check_the_host();
    check_a_client_takes_and_files();
    check_the_lobbys_back_door();
    check_the_host_ending_it();
    check_the_hosts_goodbye();
    check_the_goodbye_and_the_reset();
    check_the_transport_coming_down();
    check_a_second_join();
    check_a_refused_note();
    return ut_summary("mp_host_settings");
}
