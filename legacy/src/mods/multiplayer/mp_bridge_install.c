/* mp_bridge_install.c: putting the bridge's transport up, and taking it down again.
 *
 * The cut mp_bridge.c named for itself when it reached the size limit: the pump is three
 * shapes of one substep, and this is the moment before any of them runs. It is a cut in time
 * rather than by subject, and its price is mp_bridge_shared.h. Taking the transport down is new
 * with it: a menu that asks for the other side, or a host on another port, used to be refused
 * until the game was restarted, because nothing could take a bound socket and its sessions
 * down again.
 */
#include "mp_bridge.h"
#include "mp_bridge_shared.h"

#include "mp_actions.h"
#include "mp_armed.h"
#include "mp_flash.h"
#include "mp_body.h"
#include "mp_bridge_lobby.h"
#include "mp_bridge_public.h"
#include "mp_bridge_statement.h"
#include "mp_cells.h"
#include "mp_chat.h"
#include "mp_enemy_sync.h"
#include "mp_host_settings.h"
#include "mp_movie_gate.h"
#include "mp_pause.h"
#include "mp_puppet.h"
#include "mp_relay_transport.h"
#include "mp_relay_wire.h"
#include "mp_scratch_wire.h"
#include "mp_world.h"
#include "mp_world_apply.h"
#include "mp_world_holds.h"

#include "common/logging.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The loopback's delay is one shy of a substep, so a packet sent this substep is receivable the
 * next, like a fast real link. */
#define BRIDGE_LINK_DELAY_MS 16u

/* The transport went up or is about to come down, said in one place. The cell and the session
 * note are written together by mp_armed, which the developer overlay and enhanced_input read; the
 * pause menu's call is repointed for as long as a transport stands, and put back before it goes. */
static void the_transport_is(bool standing, bool is_host)
{
    /* The detonation flash is armed here, beside the pause, and not by each way in: a way in
     * that forgot it would leave every detonation of that kind of session flashing the whole
     * screen, and nothing but the report would say so. */
    if (standing) {
        mp_armed_set_transport(true, is_host);
        (void)mp_pause_arm();
        (void)mp_flash_arm();
        return;
    }
    mp_flash_disarm();   /* the shot table's arm goes back to the engine's own */
    mp_pause_disarm();
    mp_armed_set_transport(false, false);
}

bool mp_bridge_install(uint32_t loss_percent)
{
    const mp_bridge_shared_t *b  = mp_bridge_shared();
    mp_bridge_state_t        *st = b->state;
    mp_loopback_conditions_t  conditions;

    if (mp_armed_transport()) {
        return true;
    }
    if (mp_cells_address(MP_CELL_HERO_BLOCK) == 0) {
        log_warning("the bridge cannot install: the hero block cell did not resolve");
        return false;
    }
    if (loss_percent > 90u) {
        log_warning("a loss of %u%% is capped at 90: total loss proves nothing but a timeout",
                    (unsigned)loss_percent);
        loss_percent = 90u;
    }

    memset(&conditions, 0, sizeof conditions);
    conditions.loss_percent = loss_percent;
    conditions.delay_ms     = BRIDGE_LINK_DELAY_MS;
    mp_loopback_init(b->net, &conditions, 0x0B71D6E5u);
    *b->host_transport   = mp_loopback_transport(b->net, MP_LOOPBACK_ENDPOINT_A);
    *b->client_transport = mp_loopback_transport(b->net, MP_LOOPBACK_ENDPOINT_B);

    mp_session_init(b->host, MP_SESSION_HOST, b->host_transport, 0xB01DFACEu);
    mp_session_init(b->client, MP_SESSION_CLIENT, b->client_transport, 0xC11EA7E5u);
    mp_bridge_reset_peer_state();
    mp_session_connect(b->client, MP_LOOPBACK_ENDPOINT_A);

    /* The mode and the drain first, `installed` last. Every entry point of the pump gates on
     * `installed`, so setting it before the drain is bound would leave a window, however short, in
     * which an idle pump could drain through a record that still reads as a loopback. */
    st->mode = MP_BRIDGE_LOOPBACK;
    mp_bridge_drain_bind(&st->drain, st->mode, b->host, b->client);
    mp_bridge_lobby_bind(b->host, b->client,
                         st->mode == MP_BRIDGE_LOOPBACK ? NULL : b->udp,
                         st->mode == MP_BRIDGE_UDP_CLIENT, &st->drain);
    mp_chat_bind(b->host, b->client, st->mode == MP_BRIDGE_UDP_CLIENT);
    the_transport_is(true, true);
    log_info("the bridge stands: a host and a client session in this process, joined over the "
             "loopback at %u%% loss; commands and snapshots take the whole wire path",
             (unsigned)loss_percent);
    return true;
}

/* Real entropy for the salt generator: the two constant seeds the sessions used made every salt
 * and connection id of a match predictable, so a spoofer who knew the client's address (trivial
 * on a LAN) could forge payload packets with a valid connection id. A performance counter, the
 * process id and a stack address together are not cryptographic, but they defeat the "the seed
 * is a literal" case the control pass named. */
static uint32_t bridge_entropy(void)
{
    LARGE_INTEGER counter;
    uintptr_t     here = (uintptr_t)&counter;
    uint32_t      seed;

    QueryPerformanceCounter(&counter);
    seed = (uint32_t)counter.LowPart ^ (uint32_t)counter.HighPart ^
           (uint32_t)GetCurrentProcessId() ^ (uint32_t)here;
    return seed != 0u ? seed : 0x9E3779B9u;
}

_Static_assert(MP_SETTINGS_CODE_MAX == MP_RELAY_CODE_TEXT_BYTES,
               "the settings keep a code in the relay's text");

/* The far players' bodies under either role and either network: a puppet each, dressed once. A
 * build where a function did not resolve places the puppet and dresses it less, and the puppet
 * module says so. The second body is a puppet of the far player, whose contacts are that player's
 * machine to judge. */
static void players_stand(void)
{
    mp_bridge_reset_peer_state();
    mp_puppet_resolve();
    (void)mp_actions_install();
    mp_body_set_second_is_puppet(true);
}

/* The halves that read and describe the world, tied to the sessions once the shape is known: they
 * gate on the mode, and a drain bound before the mode was decided would read as a loopback. */
static void bind_the_halves(const mp_bridge_shared_t *b, mp_bridge_state_t *st)
{
    /* The map has one owner, and over a real link that is the host. The client is the side that
     * puts its free runners back in step; the loopback is neither, because both of its ends share
     * one map and a controller there would push the movers against their own measurement. */
    mp_world_set_correcting(st->mode == MP_BRIDGE_UDP_CLIENT);

    /* The same ownership, said to the half that describes the map rather than measures it. Only a
     * real host describes: the loopback's two ends share one map, so a note there would cost a
     * message a second to tell a machine what it already knows. */
    mp_world_apply_set_authority(st->mode == MP_BRIDGE_UDP_HOST);

    mp_bridge_drain_bind(&st->drain, st->mode, b->host, b->client);
    mp_bridge_lobby_bind(b->host, b->client, st->public_net ? NULL : b->udp,
                         st->mode == MP_BRIDGE_UDP_CLIENT, &st->drain);
    mp_chat_bind(b->host, b->client, st->mode == MP_BRIDGE_UDP_CLIENT);
    mp_bridge_public_bind(st->public_net ? b->relay : NULL, st->mode == MP_BRIDGE_UDP_CLIENT);
    the_transport_is(true, st->mode == MP_BRIDGE_UDP_HOST);
    mp_bridge_statement_learn(false);   /* a lobby's join states the game data from its first try */
}

bool mp_bridge_install_udp(bool as_host, const char *address, uint16_t port)
{
    const mp_bridge_shared_t *b  = mp_bridge_shared();
    mp_bridge_state_t        *st = b->state;

    if (mp_armed_transport()) {
        return true;
    }
    if (mp_cells_address(MP_CELL_HERO_BLOCK) == 0) {
        log_warning("the bridge cannot install: the hero block cell did not resolve");
        return false;
    }
    if (!mp_udp_init(b->udp, as_host ? port : 0u)) {
        log_warning("the bridge cannot install: the UDP socket did not come up (error %d)",
                    mp_udp_last_error(b->udp));
        return false;
    }

    players_stand();
    st->public_net = false;

    if (as_host) {
        *b->host_transport = mp_udp_transport(b->udp);
        mp_session_init(b->host, MP_SESSION_HOST, b->host_transport, bridge_entropy());
        st->mode = MP_BRIDGE_UDP_HOST;
        log_info("the bridge stands as HOST on UDP port %u: the client's own state drives the "
                 "second body as a puppet, and snapshots of the real banks go back",
                 (unsigned)mp_udp_local_port(b->udp));
    } else {
        uint32_t endpoint;

        *b->client_transport = mp_udp_transport(b->udp);
        mp_session_init(b->client, MP_SESSION_CLIENT, b->client_transport,
                        bridge_entropy());
        endpoint = mp_udp_resolve(b->udp, address);
        if (endpoint == 0u) {
            log_warning("the bridge cannot install: '%s' did not parse as a.b.c.d:port",
                        address != NULL ? address : "(null)");
            mp_udp_shutdown(b->udp);
            return false;
        }
        /* The join is not begun here: the request carries the content fingerprint, and that is
         * read from the first substep, inside a level, once the shot module has loaded its
         * damage table. The pre-tick half connects when it has it. */
        st->host_endpoint   = endpoint;
        st->connect_pending = true;
        st->mode            = MP_BRIDGE_UDP_CLIENT;
        log_info("the bridge stands as CLIENT for %s: it joins from the first substep of a level, "
                 "with the content fingerprint in its request; the own state goes onto the wire, "
                 "and the second body becomes a puppet of the peer's", address);
    }

    bind_the_halves(b, st);
    return true;
}

bool mp_bridge_install_public(bool as_host, const char *code, uint8_t seats)
{
    const mp_bridge_shared_t *b  = mp_bridge_shared();
    mp_bridge_state_t        *st = b->state;
    uint8_t                   session[MP_RELAY_CODE_BYTES];
    bool                      started;

    if (mp_armed_transport()) {
        return true;
    }
    if (mp_cells_address(MP_CELL_HERO_BLOCK) == 0) {
        log_warning("the bridge cannot install: the hero block cell did not resolve");
        return false;
    }
    if (!as_host && (code == NULL || !mp_relay_code_parse(code, session))) {
        log_warning("the bridge cannot install: '%s' is not a session code",
                    code != NULL ? code : "(null)");
        return false;
    }
    started = as_host ? mp_relay_transport_start_host(b->relay, mp_relay_services_real(), seats)
                      : mp_relay_transport_start_join(b->relay, mp_relay_services_real(),
                                                      session);
    if (!started) {
        log_warning("the bridge cannot install: %s", mp_relay_transport_failure(b->relay));
        return false;
    }
    players_stand();
    st->public_net = true;
    if (as_host) {
        *b->host_transport = mp_relay_transport_face(b->relay);
        mp_session_init(b->host, MP_SESSION_HOST, b->host_transport, bridge_entropy());
        st->mode = MP_BRIDGE_UDP_HOST;
        log_info("the bridge stands as HOST through the relay: the code comes with the relay's "
                 "answer, the players reach this session by it");
    } else {
        /* The relay seats this player; the host is the one peer, at endpoint 1. The join waits for
         * the fingerprint in a level, or begins from the lobby once the relay has seated it. */
        *b->client_transport = mp_relay_transport_face(b->relay);
        mp_session_init(b->client, MP_SESSION_CLIENT, b->client_transport, bridge_entropy());
        st->host_endpoint   = 1u;
        st->connect_pending = true;
        st->mode            = MP_BRIDGE_UDP_CLIENT;
        log_info("the bridge stands as CLIENT of the session %s through the relay", code);
    }
    bind_the_halves(b, st);
    return true;
}

/* Whether the transport that stands was put up by the menu rather than by the ini's way in. A
 * menu's session always has a lobby, and a lobby that holds no setup note is one before the host's
 * first note or one reset at a session's end: nobody plays there. Only a transport of the ini's
 * way has no setup note while a session runs. Set at the menu's door before it opens, taken back
 * where the door does not open and where the transport comes down. */
static bool armed_by_menu;

bool mp_bridge_armed_by_menu(void)
{
    return mp_armed_transport() && armed_by_menu;
}

static bool install_what_the_menu_asks(const mp_settings_t *settings)
{
    char endpoint[MP_SETTINGS_ENDPOINT_MAX];

    switch (mp_settings_transport_for(settings)) {
    case MP_SETTINGS_TRANSPORT_LAN:
        log_info("the transport: LAN, direct UDP (the relay is never used on this path)");
        if (settings->role == MP_SETTINGS_ROLE_HOST) {
            return mp_bridge_install_udp(true, NULL, settings->port);
        }
        if (!mp_settings_format_endpoint(settings->address, settings->port, endpoint,
                                         sizeof endpoint)) {
            return false;
        }
        log_warning("joining from the menu: this instance keeps its window class, so a SECOND "
                    "instance on this machine would still be refused by the game's own instance "
                    "guard. Two machines are unaffected");
        return mp_bridge_install_udp(false, endpoint, settings->port);
    case MP_SETTINGS_TRANSPORT_RELAY:
        return mp_bridge_install_public(settings->role == MP_SETTINGS_ROLE_HOST,
                                        settings->join_code, mp_settings_relay_seats(settings));
    case MP_SETTINGS_TRANSPORT_NONE:
    default:
        return false;
    }
}

bool mp_bridge_install_for(const mp_settings_t *settings)
{
    bool was_standing = mp_armed_transport();
    bool standing;

    /* Said before the doors, because a door hands this side's statement to its session before
     * it returns, and the statement asks whose transport it is. A transport the ini put up
     * earlier stays the ini's: the doors return early when one stands. */
    if (!was_standing) {
        armed_by_menu = true;
    }
    standing = install_what_the_menu_asks(settings);
    if (!standing && !was_standing) {
        armed_by_menu = false;
    }
    return standing;
}

void mp_bridge_armed(uint32_t role, mp_settings_armed_t *out)
{
    const mp_bridge_shared_t *b = mp_bridge_shared();
    uint8_t                   code[MP_RELAY_CODE_BYTES];

    memset(out, 0, sizeof *out);
    out->role = role;
    if (!mp_armed_transport() || !b->state->public_net) {
        out->net        = MP_SETTINGS_NET_LAN;
        out->bound_port = mp_bridge_bound_port();
        return;
    }
    out->net = MP_SETTINGS_NET_PUBLIC;
    if (b->state->mode == MP_BRIDGE_UDP_HOST) {
        out->seats = b->relay->link.seats;
    } else if (mp_relay_transport_code(b->relay, code)) {
        mp_relay_code_text(code, out->code);
    }
}

bool mp_bridge_uninstall_udp(void)
{
    const mp_bridge_shared_t *b  = mp_bridge_shared();
    mp_bridge_state_t        *st = b->state;

    if (!mp_armed_transport() || st->mode == MP_BRIDGE_LOOPBACK) {
        return false;
    }
    /* The goodbye first, while the socket is still open, so the far side frees its slot at once
     * instead of waiting out a timeout; then the lobby and the socket; then everything the far
     * side left behind here. */
    mp_session_disconnect(b->host);
    mp_session_disconnect(b->client);
    mp_bridge_lobby_close();
    if (st->public_net) {
        /* The relay is told as well, so it frees the session or the seat at once; its keys go
         * with the sockets. */
        mp_relay_transport_farewell(b->relay);
        mp_relay_transport_close(b->relay);
        mp_bridge_public_unbind();
        st->public_net = false;
    } else {
        mp_udp_shutdown(b->udp);
    }
    /* And the timer that pumped it, which used to outlive the transport and was joined by a
     * second one at the next arming. */
    if (st->pump_timer != 0u) {
        KillTimer(NULL, (UINT_PTR)st->pump_timer);
        st->pump_timer = 0u;
    }
    mp_scratch_wire_reset();
    mp_enemy_sync_reset();
    mp_bridge_reset_peer_state();

    /* What belonged to the sessions and not to the process. The join count starts over with the
     * new sessions, and one carried over would make their first join look like an old one; the
     * fingerprint is named to a session, and the new ones have none yet. */
    st->joined          = false;
    st->joins_seen      = 0u;
    st->connect_pending = false;
    st->content_decided = false;
    st->content_tries   = 0u;
    armed_by_menu       = false;
    the_transport_is(false, false);
    mp_movie_gate_withdraw();   /* and no movie waits on the session that has just gone */
    /* Nor does any mod go on reading the host's settings, and the cheat and 60fps cells get this
     * side's own values back now: no frame begin of a session runs after this line. */
    mp_host_settings_withdraw();
    mp_world_holds_withdraw();
    log_info("the network transport is taken down: every peer was told goodbye, the socket is "
             "closed and the pump timer stopped");
    return true;
}

uint16_t mp_bridge_bound_port(void)
{
    const mp_bridge_shared_t *b = mp_bridge_shared();

    return mp_armed_transport() && b->state->mode == MP_BRIDGE_UDP_HOST && !b->state->public_net
               ? mp_udp_local_port(b->udp)
               : 0u;
}

uint32_t mp_bridge_reliable_pending(void)
{
    const mp_bridge_shared_t *b = mp_bridge_shared();

    if (!mp_armed_transport() || b->state->mode == MP_BRIDGE_LOOPBACK) {
        return 0u;
    }
    return (uint32_t)mp_session_reliable_pending(b->state->mode == MP_BRIDGE_UDP_HOST
                                                     ? b->host
                                                     : b->client);
}
