/* mp_bridge_install.c: every way a transport goes up arms the detonation flash, and the way down
 * puts the engine's own arm back.
 *
 * The real module, with the real flash rule, the real record of the standing transport and the
 * real menu settings, against stand-ins for what a transport runs on: the cells, the sessions,
 * the socket, the relay, the lobby and the halves the install binds. The shot table is the test's
 * own, with the engine's handler of the thermal detonator in the row and the field the engine keeps
 * it in, so an arming is seen in the table and not only in a count. The ways in are a host and a
 * client over the LAN, the same two through the relay, the menu's door onto two of them, and the
 * loopback, which never comes down in a process and so goes last. The menu's door is also held to
 * saying that the transport is the menu's before the statement is handed over, since the statement
 * asks.
 *
 * The reader of the developer menu's two buttons is armed on the same ways in and withdrawn on
 * the same way down. It is a stand-in here that counts, and it is held to being armed with the
 * transport already standing and withdrawn with it already down: the reader takes its mark when
 * a session begins to exist, and says that nobody listens once none does.
 */
#include "unittest.h"

#include "mp_actions.h"
#include "mp_armed.h"
#include "mp_bank.h"
#include "mp_body.h"
#include "mp_bridge.h"
#include "mp_bridge_drain.h"
#include "mp_bridge_lobby.h"
#include "mp_bridge_public.h"
#include "mp_bridge_shared.h"
#include "mp_bridge_statement.h"
#include "mp_cells.h"
#include "mp_chat.h"
#include "mp_enemy_sync.h"
#include "mp_flash.h"
#include "mp_host_settings.h"
#include "mp_loopback.h"
#include "mp_movie_gate.h"
#include "mp_pause.h"
#include "mp_player_help.h"
#include "mp_puppet.h"
#include "mp_relay_transport.h"
#include "mp_relay_wire.h"
#include "mp_scratch_wire.h"
#include "mp_session.h"
#include "mp_settings.h"
#include "mp_udp.h"
#include "mp_world.h"
#include "mp_world_apply.h"
#include "mp_world_holds.h"

#include "common/logging.h"
#include "common/text.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The engine's shot table as the rule reads it: rows of 0x44 bytes with the event handler at 0x3C,
 * and the thermal detonator is kind 10, whose handler in the retail image is 00455DBD. */
#define SHOT_ROWS            38u
#define SHOT_ROW_BYTES       0x44u
#define SHOT_ROW_HANDLER     0x3Cu
#define SHOT_KIND_THERMAL    10u
#define SHOT_HANDLER_THERMAL 0x00455DBDu

/* The line the rule writes each time it takes the entry. */
#define FLASH_ARMED_LINE "a detonation flashes the screen only within"

#define PORT 27960u

/* Static, not on the stack: a session is megabytes. */
static mp_session_t         s_host;
static mp_session_t         s_client;
static mp_bridge_state_t    s_state;
static mp_loopback_t        s_net;
static mp_udp_t             s_udp;
static mp_relay_transport_t s_relay;
static mp_transport_t       s_host_transport;
static mp_transport_t       s_client_transport;
static const mp_bridge_shared_t s_shared = {
    &s_state, &s_net, &s_udp, &s_relay, &s_host, &s_client, &s_host_transport, &s_client_transport
};

static uint32_t s_shot_table[SHOT_ROWS * SHOT_ROW_BYTES / sizeof(uint32_t)];
static uint8_t  s_hero_block[0x400];

static struct {
    bool     udp_fails;
    unsigned pause_arms;
    unsigned pause_disarms;
    unsigned flash_lines;
    unsigned learned;
    bool     learned_by_menu;
    unsigned help_arms;             /* the reader of the two buttons, armed */
    unsigned help_arms_standing;    /* of those, with the transport already standing */
    unsigned help_withdrawals;
    unsigned help_withdrawals_down; /* of those, with the transport already down */
} s_world;

/* ===================================== What the module calls ============================== */

const mp_bridge_shared_t *mp_bridge_shared(void)
{
    return &s_shared;
}

void mp_bridge_reset_peer_state(void)
{
}

uintptr_t mp_cells_address(mp_cell_t cell)
{
    if (cell == MP_CELL_HERO_BLOCK) {
        return (uintptr_t)s_hero_block;
    }
    if (cell == MP_CELL_SHOT_TABLE) {
        return (uintptr_t)s_shot_table;
    }
    return 0u;
}

size_t mp_bank_active(void)
{
    return 0u;
}

bool mp_pause_arm(void)
{
    ++s_world.pause_arms;
    return true;
}

void mp_pause_disarm(void)
{
    ++s_world.pause_disarms;
}

void mp_loopback_init(mp_loopback_t *net, const mp_loopback_conditions_t *conditions,
                      uint32_t seed)
{
    (void)net;
    (void)conditions;
    (void)seed;
}

mp_transport_t mp_loopback_transport(mp_loopback_t *net, uint32_t endpoint)
{
    mp_transport_t transport;

    (void)net;
    (void)endpoint;
    memset(&transport, 0, sizeof transport);
    return transport;
}

void mp_session_init(mp_session_t *session, mp_session_role_t role,
                     const mp_transport_t *transport, uint32_t seed)
{
    (void)session;
    (void)role;
    (void)transport;
    (void)seed;
}

void mp_session_connect(mp_session_t *session, uint32_t endpoint)
{
    (void)session;
    (void)endpoint;
}

void mp_session_disconnect(mp_session_t *session)
{
    (void)session;
}

size_t mp_session_reliable_pending(const mp_session_t *session)
{
    (void)session;
    return 0u;
}

void mp_bridge_drain_bind(mp_bridge_drain_t *drain, mp_bridge_mode_t mode, mp_session_t *host,
                          mp_session_t *client)
{
    (void)drain;
    (void)mode;
    (void)host;
    (void)client;
}

void mp_bridge_lobby_bind(mp_session_t *host, mp_session_t *client, mp_udp_t *udp,
                          bool is_client, mp_bridge_drain_t *drain)
{
    (void)host;
    (void)client;
    (void)udp;
    (void)is_client;
    (void)drain;
}

void mp_bridge_lobby_close(void)
{
}

void mp_chat_bind(mp_session_t *host, mp_session_t *client, bool is_client)
{
    (void)host;
    (void)client;
    (void)is_client;
}

void mp_bridge_public_bind(mp_relay_transport_t *relay, bool is_client)
{
    (void)relay;
    (void)is_client;
}

void mp_bridge_public_unbind(void)
{
}

void mp_world_set_correcting(bool correcting)
{
    (void)correcting;
}

void mp_world_apply_set_authority(bool host)
{
    (void)host;
}

void mp_puppet_resolve(void)
{
}

bool mp_actions_install(void)
{
    return true;
}

void mp_body_set_second_is_puppet(bool is_puppet)
{
    (void)is_puppet;
}

/* Kept with who the transport belonged to at that moment, which the real statement asks. */
void mp_bridge_statement_learn(bool in_substep)
{
    (void)in_substep;
    ++s_world.learned;
    s_world.learned_by_menu = mp_bridge_armed_by_menu();
}

bool mp_udp_init(mp_udp_t *udp, uint16_t port)
{
    (void)udp;
    (void)port;
    return !s_world.udp_fails;
}

int mp_udp_last_error(const mp_udp_t *udp)
{
    (void)udp;
    return 10048;
}

mp_transport_t mp_udp_transport(mp_udp_t *udp)
{
    mp_transport_t transport;

    (void)udp;
    memset(&transport, 0, sizeof transport);
    return transport;
}

uint16_t mp_udp_local_port(const mp_udp_t *udp)
{
    (void)udp;
    return (uint16_t)PORT;
}

uint32_t mp_udp_resolve(mp_udp_t *udp, const char *address)
{
    (void)udp;
    return address != NULL && address[0] != '\0' ? 1u : 0u;
}

void mp_udp_shutdown(mp_udp_t *udp)
{
    (void)udp;
}

bool mp_relay_code_parse(const char *text, uint8_t code[MP_RELAY_CODE_BYTES])
{
    memset(code, 0, MP_RELAY_CODE_BYTES);
    return text != NULL && text[0] != '\0';
}

void mp_relay_code_text(const uint8_t code[MP_RELAY_CODE_BYTES],
                        char out[MP_RELAY_CODE_TEXT_BYTES])
{
    (void)code;
    out[0] = '\0';
}

bool mp_relay_transport_start_host(mp_relay_transport_t *t, const mp_relay_services_t *services,
                                   uint8_t seats)
{
    (void)t;
    (void)services;
    (void)seats;
    return true;
}

bool mp_relay_transport_start_join(mp_relay_transport_t *t, const mp_relay_services_t *services,
                                   const uint8_t code[MP_RELAY_CODE_BYTES])
{
    (void)t;
    (void)services;
    (void)code;
    return true;
}

const mp_relay_services_t *mp_relay_services_real(void)
{
    return NULL;
}

const char *mp_relay_transport_failure(const mp_relay_transport_t *t)
{
    (void)t;
    return "a stand-in";
}

mp_transport_t mp_relay_transport_face(mp_relay_transport_t *t)
{
    mp_transport_t transport;

    (void)t;
    memset(&transport, 0, sizeof transport);
    return transport;
}

bool mp_relay_transport_code(const mp_relay_transport_t *t, uint8_t code[MP_RELAY_CODE_BYTES])
{
    (void)t;
    (void)code;
    return false;
}

void mp_relay_transport_farewell(mp_relay_transport_t *t)
{
    (void)t;
}

void mp_relay_transport_close(mp_relay_transport_t *t)
{
    (void)t;
}

void mp_scratch_wire_reset(void)
{
}

void mp_enemy_sync_reset(void)
{
}

void mp_movie_gate_withdraw(void)
{
}

void mp_host_settings_withdraw(void)
{
}

void mp_player_help_arm(void)
{
    ++s_world.help_arms;
    s_world.help_arms_standing += mp_armed_transport() ? 1u : 0u;
}

void mp_player_help_withdraw(void)
{
    ++s_world.help_withdrawals;
    s_world.help_withdrawals_down += mp_armed_transport() ? 0u : 1u;
}

void mp_world_holds_withdraw(void)
{
}

/* ===================================== The log, counted =================================== */

/* Every entry of common/logging is defined here, so the linker never takes the library's file and
 * nothing is written to disk. What is kept is how often the rule said it took the entry. */
static void keep(const char *format, va_list arguments)
{
    char line[1024];

    (void)text_vformat(line, sizeof line, format, arguments);
    if (strstr(line, FLASH_ARMED_LINE) != NULL) {
        ++s_world.flash_lines;
    }
}

void log_init(const char *feature_name, bool truncate)
{
    (void)feature_name;
    (void)truncate;
}

void log_shutdown(void)
{
}

void log_info(const char *format, ...)
{
    va_list arguments;

    va_start(arguments, format);
    keep(format, arguments);
    va_end(arguments);
}

void log_warning(const char *format, ...)
{
    va_list arguments;

    va_start(arguments, format);
    keep(format, arguments);
    va_end(arguments);
}

void log_error(const char *format, ...)
{
    va_list arguments;

    va_start(arguments, format);
    keep(format, arguments);
    va_end(arguments);
}

const char *log_path(void)
{
    return "";
}

/* ===================================== The checks ========================================= */

static uint32_t thermal_entry(void)
{
    return s_shot_table[(SHOT_KIND_THERMAL * SHOT_ROW_BYTES + SHOT_ROW_HANDLER) /
                        sizeof(uint32_t)];
}

/* A way in: the transport stands, the entry holds the rule's arm in place of the engine's, and the
 * rule and the pause were each armed once. The reader of the two buttons is not counted here by
 * the caller: every way in arms it once, so the two counts move together over the whole run. */
static void check_armed(const char *way, bool put_up, unsigned said, unsigned paused)
{
    ut_checkf(put_up && mp_armed_transport(), "%s: the transport stands", way);
    ut_checkf(s_world.help_arms == s_world.pause_arms &&
                  s_world.help_arms_standing == s_world.help_arms,
              "%s: the reader of the two buttons was armed with it, the transport standing", way);
    ut_checkf(thermal_entry() != SHOT_HANDLER_THERMAL && thermal_entry() != 0u,
              "%s: the thermal detonator's entry holds the rule's arm (%08X)", way,
              (unsigned)thermal_entry());
    ut_checkf(s_world.flash_lines == said + 1u, "%s: the rule was armed once (%u line(s))", way,
              s_world.flash_lines - said);
    ut_checkf(s_world.pause_arms == paused + 1u, "%s: and the pause beside it", way);
}

/* The way down, which every network transport shares. */
static void check_taken_down(const char *way)
{
    unsigned disarms   = s_world.pause_disarms;
    unsigned withdrawn = s_world.help_withdrawals;

    ut_checkf(mp_bridge_uninstall_udp() && !mp_armed_transport(),
              "%s: the transport comes down", way);
    ut_checkf(s_world.help_withdrawals == withdrawn + 1u &&
                  s_world.help_withdrawals_down == s_world.help_withdrawals,
              "%s: the reader of the two buttons is withdrawn once, the transport down", way);
    ut_checkf(thermal_entry() == SHOT_HANDLER_THERMAL,
              "%s: the entry holds the engine's own arm again", way);
    ut_checkf(s_world.pause_disarms == disarms + 1u, "%s: and the pause is put back", way);
}

static bool put_up(unsigned way)
{
    switch (way) {
    case 0u:
        return mp_bridge_install_udp(true, NULL, (uint16_t)PORT);
    case 1u:
        return mp_bridge_install_udp(false, "127.0.0.1:27960", (uint16_t)PORT);
    case 2u:
        return mp_bridge_install_public(true, NULL, 4u);
    default:
        return mp_bridge_install_public(false, "ABCD-EFGH", 0u);
    }
}

static void check_the_four_network_ways(void)
{
    static const char *const WAYS[] = {
        "a host over the LAN", "a client over the LAN", "a host through the relay",
        "a client through the relay",
    };
    unsigned way;

    ut_section("each network way in arms the flash, and the way down disarms it");

    ut_check(!mp_armed_transport() && thermal_entry() == SHOT_HANDLER_THERMAL &&
                 s_world.flash_lines == 0u,
             "before any transport the entry is the engine's and nothing was armed");
    for (way = 0u; way < sizeof WAYS / sizeof WAYS[0]; ++way) {
        unsigned said    = s_world.flash_lines;
        unsigned paused  = s_world.pause_arms;
        unsigned learned = s_world.learned;

        check_armed(WAYS[way], put_up(way), said, paused);
        ut_checkf(s_world.learned == learned + 1u && !s_world.learned_by_menu,
                  "%s: the statement was handed over once, as the ini's transport", WAYS[way]);
        check_taken_down(WAYS[way]);
    }
}

static void check_the_menu_door(void)
{
    mp_settings_t settings;
    unsigned      said;
    unsigned      paused;

    ut_section("the menu's door arms it on both networks, and says whose it is first");

    mp_settings_default(&settings);
    settings.role = MP_SETTINGS_ROLE_HOST;
    settings.net  = MP_SETTINGS_NET_LAN;
    settings.port = (uint16_t)PORT;
    said   = s_world.flash_lines;
    paused = s_world.pause_arms;
    check_armed("the menu's host on the LAN", mp_bridge_install_for(&settings), said, paused);
    ut_check(mp_bridge_armed_by_menu() && s_world.learned_by_menu,
             "the transport is the menu's, and was already when the statement was handed over");
    check_taken_down("the menu's host on the LAN");
    ut_check(!mp_bridge_armed_by_menu(), "and once it is down it is nobody's");

    settings.role = MP_SETTINGS_ROLE_JOIN;
    settings.net  = MP_SETTINGS_NET_PUBLIC;
    (void)text_format(settings.join_code, sizeof settings.join_code, "%s", "ABCD-EFGH");
    said   = s_world.flash_lines;
    paused = s_world.pause_arms;
    check_armed("the menu's client through the relay", mp_bridge_install_for(&settings), said,
                paused);
    ut_check(mp_bridge_armed_by_menu() && s_world.learned_by_menu,
             "the relay's client is the menu's as well, from before the statement");
    check_taken_down("the menu's client through the relay");

    ut_section("a menu's door that does not open arms nothing and leaves nothing the menu's");
    s_world.udp_fails = true;
    settings.role     = MP_SETTINGS_ROLE_HOST;
    settings.net      = MP_SETTINGS_NET_LAN;
    said              = s_world.flash_lines;
    ut_check(!mp_bridge_install_for(&settings) && !mp_armed_transport() &&
                 !mp_bridge_armed_by_menu(),
             "a socket that does not come up puts no transport up");
    ut_check(thermal_entry() == SHOT_HANDLER_THERMAL && s_world.flash_lines == said,
             "and the entry keeps the engine's arm");
    s_world.udp_fails = false;
}

static void check_the_loopback(void)
{
    unsigned said   = s_world.flash_lines;
    unsigned paused = s_world.pause_arms;

    ut_section("the loopback arms it too, and is never taken down in a process");
    check_armed("the loopback", mp_bridge_install(10u), said, paused);
    ut_check(!mp_bridge_armed_by_menu(),
             "the loopback is not the menu's, whatever a menu's door tried before it");
    ut_check(!mp_bridge_uninstall_udp() && thermal_entry() != SHOT_HANDLER_THERMAL,
             "and the network's way down refuses it, so the rule's arm stays");
}

int main(void)
{
    s_shot_table[(SHOT_KIND_THERMAL * SHOT_ROW_BYTES + SHOT_ROW_HANDLER) / sizeof(uint32_t)] =
        SHOT_HANDLER_THERMAL;

    check_the_four_network_ways();
    check_the_menu_door();
    check_the_loopback();

    return ut_summary("mp_bridge_install");
}
