/* mp_bridge_lobby.c: the lobby's state across the doors into a session and out of it, driven
 * against a far end the test plays by hand over the loopback.
 *
 * Each of the first eight sections is a defect a census of the lobby's state found, and each
 * check there fails on the code as it stood before its repair:
 *
 *   - a client's lobby line said before the handshake never reached the host, because the resend
 *     rode a substep and a lobby has none, so the host listed a client who had pressed nothing as
 *     ready;
 *   - a fingerprint that was ready before anybody connected was never sent, because the gate that
 *     kept it from repeating asked whether it was settled rather than whether it had gone out;
 *   - a host that had once had a client began its next session alone as one everybody had left;
 *   - a new player on a slot wore the team of the player who left it;
 *   - the lobby's BACK and the title screen were two doors out of a session that left two
 *     different states behind;
 *   - a client carried the last host's player list into its next lobby, where it stood under
 *     "connecting" as though those players were there;
 *   - a host that said goodbye was told on the band as one that did not answer;
 *   - a host took a setup note from a client, so a changed client could end, change or start
 *     the session for everybody.
 *
 * The lobby holds one side per process, so the sections run one after another, each on a fresh
 * pair of sessions with the lobby bound to them as the side it is testing.
 */
#include "unittest.h"

#include "mp_bridge_lobby.h"
#include "mp_bridge_roster.h"
#include "mp_lobby.h"
#include "mp_loopback.h"
#include "mp_roster.h"
#include "mp_session.h"
#include "mp_transport.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Static: a session is over two megabytes now that every peer carries a bulk ring. */
static mp_loopback_t  s_net;
static mp_session_t   s_host;
static mp_session_t   s_client;
static mp_transport_t s_host_t;
static mp_transport_t s_client_t;

static uint32_t s_withdrawals_heard;

/* The wire moves and both sessions run, `ticks` times. */
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

static bool connect_pair(uint32_t *now)
{
    int tick;

    mp_session_connect(&s_client, MP_LOOPBACK_ENDPOINT_A);
    for (tick = 0; tick < 400; ++tick) {
        pump(now, 1);
        if (mp_session_is_connected(&s_client) && mp_session_peer_count(&s_host) == 1u) {
            return true;
        }
    }
    return false;
}

/* Two sessions over a fresh wire, not yet connected, and the lobby bound to them as one side. */
static void fresh_pair(bool as_client, uint32_t seed)
{
    mp_loopback_conditions_t cond;

    memset(&cond, 0, sizeof cond);
    cond.delay_ms = 16u;
    mp_loopback_init(&s_net, &cond, seed);
    s_host_t   = mp_loopback_transport(&s_net, MP_LOOPBACK_ENDPOINT_A);
    s_client_t = mp_loopback_transport(&s_net, MP_LOOPBACK_ENDPOINT_B);
    mp_session_init(&s_host, MP_SESSION_HOST, &s_host_t, seed + 1u);
    mp_session_init(&s_client, MP_SESSION_CLIENT, &s_client_t, seed + 2u);
    mp_bridge_lobby_bind(&s_host, &s_client, NULL, as_client, NULL);
    mp_bridge_lobby_reset();
}

/* Whether a reliable note of one kind has arrived at `session` from its peer 0. Everything that
 * waits is read, so a note of another kind in front of it does not hide it. */
static bool heard(mp_session_t *session, bool (*is_kind)(const uint8_t *, size_t))
{
    uint8_t note[MP_CHANNEL_MESSAGE_BYTES];
    size_t  bytes = 0;
    bool    found = false;

    while (mp_session_read_reliable(session, 0u, note, sizeof note, &bytes)) {
        if (is_kind(note, bytes)) {
            found = true;
        }
    }
    return found;
}

static void a_setup(mp_lobby_setup_t *setup)
{
    memset(setup, 0, sizeof *setup);
    setup->mode = 1u;
    memcpy(setup->level, "level\\espa.b3d", 15u);
}

static void check_a_lobby_line_said_too_early(void)
{
    uint32_t now = 0;

    ut_section("a lobby line said before the handshake reaches the host once it is through");
    fresh_pair(true, 0x5100u);
    mp_bridge_set_lobby(2u, false);   /* what the lobby screen does as it opens */
    ut_check(!mp_session_is_connected(&s_client), "the line was said with nobody on the far end");
    ut_check(connect_pair(&now), "the client then connects");
    mp_bridge_lobby_tick(false, 0u);   /* the lobby's idle tick; a lobby runs no substep */
    pump(&now, 20);
    ut_check(heard(&s_host, &mp_lobby_is_lobby),
             "and the host hears the line, sent from the idle tick rather than from a substep");
}

static void check_a_fingerprint_ready_before_anybody_came(void)
{
    uint32_t now = 0;

    ut_section("a fingerprint ready before anybody connected goes out once somebody has");
    fresh_pair(false, 0x5200u);
    mp_bridge_lobby_tick(true, 0xC0FFEE11u);   /* a level is loaded; nobody is connected yet */
    ut_check(connect_pair(&now), "a client then connects");
    mp_bridge_lobby_on_join();                  /* what the bridge calls on an arrival */
    mp_bridge_lobby_tick(true, 0xC0FFEE11u);
    pump(&now, 20);
    ut_check(heard(&s_client, &mp_lobby_is_content),
             "and the client hears the host's fingerprint, which was ready before it came");
}

static void check_a_second_session_begun_alone(void)
{
    uint32_t         now = 0;
    mp_lobby_setup_t setup;

    ut_section("a host's second session, begun alone, is not one everybody has left");
    fresh_pair(false, 0x5300u);
    ut_check(connect_pair(&now), "the first session has a client");
    (void)mp_bridge_lobby_session_over(true);   /* the frame's own question, somebody there */
    mp_session_disconnect(&s_client);
    pump(&now, 40);
    ut_check(mp_session_peer_count(&s_host) == 0u, "and the client has gone");

    mp_bridge_lobby_reset();                     /* the next lobby opens */
    a_setup(&setup);
    mp_bridge_lobby_set_setup(&setup);
    mp_bridge_lobby_start();
    ut_check(mp_bridge_lobby_session_over(true) == MP_LOBBY_OVER_NO,
             "a level begun before anybody has come is not the end of a session");
}

static void check_a_new_player_on_a_slot(void)
{
    uint32_t    now = 0;
    mp_lobby_t  lobby;
    uint8_t     note[MP_LOBBY_BYTES];
    mp_roster_t table;

    ut_section("a new player on a slot does not wear the team of the one who left it");
    fresh_pair(false, 0x5400u);
    ut_check(connect_pair(&now), "the first player is on the slot");
    memset(&lobby, 0, sizeof lobby);
    lobby.team  = 2u;
    lobby.ready = 1u;
    ut_check(mp_lobby_encode(&lobby, note, sizeof note) == MP_LOBBY_BYTES, "their line encodes");
    ut_check(mp_bridge_roster_take_lobby(0u, note, sizeof note), "and the host takes it");
    (void)mp_bridge_roster_host_tick(&s_host, 1u);
    ut_check(mp_bridge_roster_current(&table) && table.count == 2u && table.entry[1].team == 2u,
             "the roster lists them on team two");

    mp_session_disconnect(&s_client);
    pump(&now, 40);
    (void)mp_bridge_roster_host_tick(&s_host, 2u);   /* the slot stands empty for a tick */

    mp_session_init(&s_client, MP_SESSION_CLIENT, &s_client_t, 0x5401u);
    ut_check(connect_pair(&now), "a second player takes the same slot");
    (void)mp_bridge_roster_host_tick(&s_host, 3u);
    ut_check(mp_bridge_roster_current(&table) && table.count == 2u &&
                 table.entry[1].team == MP_LOBBY_TEAM_NONE,
             "and is listed on no team until they say one, rather than on the first player's");
}

static void count_a_withdrawal(void)
{
    ++s_withdrawals_heard;
}

static void check_the_one_door_out(void)
{
    uint32_t         now = 0;
    mp_lobby_setup_t setup;
    uint8_t          note[256];
    size_t           length;

    ut_section("leaving by any door disconnects, forgets the setup, and says so once");
    fresh_pair(true, 0x5500u);
    ut_check(connect_pair(&now), "the client is in a session");
    a_setup(&setup);
    length = mp_lobby_setup_encode(&setup, note, sizeof note);
    ut_check(length > 0u && mp_bridge_lobby_take_setup(note, length),
             "and holds the host's setup");

    s_withdrawals_heard = 0u;
    mp_bridge_lobby_set_withdrawn_listener(&count_a_withdrawal);
    mp_bridge_lobby_withdraw();
    pump(&now, 20);
    ut_check(!mp_session_is_connected(&s_client), "withdrawing disconnects");
    ut_check(!mp_bridge_lobby_setup(&setup), "forgets the setup the session was playing");
    ut_check(s_withdrawals_heard == 1u, "and tells the listener exactly once");
    mp_bridge_lobby_set_withdrawn_listener(NULL);
}

static void check_the_last_host_list_goes(void)
{
    uint32_t    now = 0;
    mp_roster_t table;
    uint8_t     note[MP_ROSTER_BYTES];
    size_t      bytes;

    ut_section("a client that leaves does not carry the host's player list into its next lobby");
    fresh_pair(true, 0x5600u);
    ut_check(connect_pair(&now), "the client is in a session");
    memset(&table, 0, sizeof table);
    table.count = 2u;
    memcpy(table.entry[0].name, "Host", 5u);
    table.entry[1].slot = 1u;
    memcpy(table.entry[1].name, "Guest", 6u);
    bytes = mp_roster_encode(&table, note, sizeof note);
    ut_check(bytes != 0u && mp_bridge_roster_take(note, bytes), "and takes the host's list");
    ut_check(mp_bridge_roster_current(&table), "which its lobby shows");

    mp_bridge_lobby_withdraw();
    ut_check(!mp_bridge_roster_current(&table),
             "after leaving no list stands, so the next lobby waits for its host's own");
}

static void check_a_host_that_said_goodbye(void)
{
    uint32_t now = 0;
    int      tick;

    ut_section("a host that said goodbye is told apart from one that never answered");
    fresh_pair(true, 0x5700u);
    ut_check(connect_pair(&now), "the client is in a session");
    ut_check(!mp_bridge_lobby_gave_up() && !mp_bridge_lobby_host_left(),
             "and has neither given up nor been left");
    mp_session_disconnect(&s_host);   /* the host backs out of its lobby, which says goodbye */
    pump(&now, 20);
    ut_check(mp_bridge_lobby_gave_up(), "the client stops trying once the host has gone");
    ut_check(mp_bridge_lobby_host_left(), "and knows the host went on purpose");

    fresh_pair(true, 0x5800u);
    mp_session_connect(&s_client, MP_LOOPBACK_ENDPOINT_A);
    for (tick = 0; tick < 4000 && !mp_bridge_lobby_gave_up(); ++tick) {
        now += 16u;
        mp_loopback_pump(&s_net, now);
        mp_session_update(&s_client, now);   /* the host never runs, so nothing answers */
    }
    ut_check(mp_bridge_lobby_gave_up(),
             "a client whose host never answers gives up after its window");
    ut_check(!mp_bridge_lobby_host_left(), "and that silence is not told as a goodbye");
}

/* A host that ends its session keeps the started flag on the note, and the generation it carries
 * may be one this side never acted on. Taken as a start, it sent a client whose host had just
 * left into the level the host was leaving. */
static void check_an_ended_session_starts_nobody(void)
{
    mp_lobby_setup_t setup;
    mp_lobby_setup_t taken;
    uint8_t          note[256];
    size_t           length;

    ut_section("a setup that says the session is over starts nobody");
    fresh_pair(true, 0x5A00u);
    a_setup(&setup);
    setup.flags      = (uint8_t)(MP_LOBBY_F_STARTED | MP_LOBBY_F_ENDED);
    setup.generation = 3u;
    length = mp_lobby_setup_encode(&setup, note, sizeof note);
    ut_check(length != 0u && mp_bridge_lobby_take_setup(note, length),
             "the host's ended note is taken");
    ut_check(mp_bridge_lobby_ended(), "and read as the end");
    ut_check(!mp_bridge_lobby_peek_start(&taken),
             "a generation nobody acted on is no start once the host has ended the session");
    ut_check(!mp_bridge_lobby_take_start(&taken), "and it cannot be taken either");

    setup.flags = (uint8_t)MP_LOBBY_F_STARTED;
    length = mp_lobby_setup_encode(&setup, note, sizeof note);
    ut_check(length != 0u && mp_bridge_lobby_take_setup(note, length) &&
                 mp_bridge_lobby_peek_start(&taken),
             "the same generation without the word is a start, so the refusal was the word's");
}

/* A host that leaves on purpose says goodbye, and a client inside the level heard that as a lost
 * connection: the sentence it was sent back to the menu with was the wrong one. */
static void check_a_goodbye_in_a_level_is_the_hosts_word(void)
{
    uint32_t         now = 0;
    mp_lobby_setup_t setup;
    uint8_t          note[256];
    size_t           length;

    ut_section("a host's goodbye inside a level is the host ending the session");
    fresh_pair(true, 0x5B00u);
    ut_check(connect_pair(&now), "the client is in a session");
    a_setup(&setup);
    setup.flags      = (uint8_t)MP_LOBBY_F_STARTED;
    setup.generation = 1u;
    length = mp_lobby_setup_encode(&setup, note, sizeof note);
    ut_check(length != 0u && mp_bridge_lobby_take_setup(note, length),
             "and holds the host's started setup");
    mp_session_disconnect(&s_host);
    pump(&now, 20);
    ut_check(mp_bridge_lobby_session_over(true) == MP_LOBBY_OVER_HOST_ENDED,
             "the level is left with the host's reason, not as a lost connection");
}

static void check_a_host_takes_no_setup_from_a_client(void)
{
    mp_lobby_setup_t mine;
    mp_lobby_setup_t forged;
    mp_lobby_setup_t held;
    uint8_t          note[256];
    size_t           length;

    ut_section("a host takes no setup note from a client");
    fresh_pair(false, 0x5900u);
    a_setup(&mine);
    mp_bridge_lobby_set_setup(&mine);
    a_setup(&forged);
    forged.flags = (uint8_t)(MP_LOBBY_F_STARTED | MP_LOBBY_F_ENDED);
    memcpy(forged.level, "level\\maul.b3d", 15u);
    length = mp_lobby_setup_encode(&forged, note, sizeof note);
    ut_check(length != 0u && mp_bridge_lobby_take_setup(note, length),
             "a forged note is recognised as the lobby's");
    ut_check(mp_bridge_lobby_setup(&held) && strcmp(held.level, mine.level) == 0,
             "and the host's own level stands");
    ut_check(!mp_bridge_lobby_ended() && (held.flags & MP_LOBBY_F_STARTED) == 0u,
             "with neither a start nor an end that a client asked for");
}

/* The host's cell, as the difficulty source reads it: difficulty 5, which the note says as 6. */
static uint8_t cell_says_five(void)
{
    return 6u;
}

/* The setup notes that reached `session`, the last one decoded into `out`. */
static uint32_t setups_heard(mp_session_t *session, mp_lobby_setup_t *out)
{
    uint8_t  note[MP_CHANNEL_MESSAGE_BYTES];
    size_t   bytes = 0;
    uint32_t count = 0u;

    while (mp_session_read_reliable(session, 0u, note, sizeof note, &bytes)) {
        if (mp_lobby_setup_decode(note, bytes, out)) {
            ++count;
        }
    }
    return count;
}

static void check_the_difficulty_goes_on_a_copy(void)
{
    mp_lobby_setup_t mine;
    mp_lobby_setup_t said;
    mp_lobby_setup_t held;
    uint32_t         now = 0;

    ut_section("the host's difficulty goes out on a copy of the note, never into the one it keeps");
    fresh_pair(false, 0x5A00u);
    ut_check(connect_pair(&now), "a client joins the host");
    mp_bridge_lobby_set_difficulty_source(&cell_says_five);
    a_setup(&mine);
    mp_bridge_lobby_set_setup(&mine);
    pump(&now, 8);
    memset(&said, 0, sizeof said);
    ut_check(setups_heard(&s_client, &said) == 1u && said.host_difficulty == 6u,
             "the note the client hears says difficulty 5, as 6");
    ut_check(mp_bridge_lobby_setup(&held) && held.host_difficulty == 0u,
             "and the setup the host keeps says none, as the menu built it");
    a_setup(&mine);
    mp_bridge_lobby_set_setup(&mine);
    pump(&now, 8);
    ut_check(setups_heard(&s_client, &said) == 0u,
             "so the menu making the same choice again is no news and sends nothing");
    mp_bridge_lobby_set_difficulty_source(NULL);
}

int main(void)
{
    check_a_lobby_line_said_too_early();
    check_a_fingerprint_ready_before_anybody_came();
    check_a_second_session_begun_alone();
    check_a_new_player_on_a_slot();
    check_the_one_door_out();
    check_the_last_host_list_goes();
    check_a_host_that_said_goodbye();
    check_a_host_takes_no_setup_from_a_client();
    check_an_ended_session_starts_nobody();
    check_a_goodbye_in_a_level_is_the_hosts_word();
    check_the_difficulty_goes_on_a_copy();
    return ut_summary("bridge lobby");
}
