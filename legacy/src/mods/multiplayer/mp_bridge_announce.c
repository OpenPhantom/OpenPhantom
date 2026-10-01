/* mp_bridge_announce.c: what a host says about itself to a browser, and the setters it is made of.
 *
 * Split from mp_bridge_lobby.c on 2026-09-25 at the seam that file's size note named: the
 * announcer, the four setters that feed it and the flags they derive have nothing to do with
 * either note the lobby carries. Two threads tie it to the rest, and both are calls rather than
 * shared fields: the friendly fire gate reads the game mode kept here, and the password setter
 * reaches both sessions, which are bound here as well as in the lobby.
 */
#include "mp_bridge_announce.h"

#include "mp_announce.h"
#include "mp_announcer.h"
#include "mp_bank.h"
#include "mp_bridge_lobby.h"
#include "mp_bridge_public.h"
#include "mp_session.h"
#include "mp_udp.h"
#include "mp_wire.h"

#include "common/logging.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct announce_state {
    mp_session_t  *host;
    mp_session_t  *client;
    mp_udp_t      *udp;            /* NULL on the loopback, which has no port to announce */
    mp_announcer_t announcer;
    bool           enabled;
    char           session_name[MP_ANNOUNCE_NAME_MAX];
    uint8_t        slots;
    uint8_t        game_mode;      /* what the bridge was last told, for the announce's bit */
} announce_state_t;

static announce_state_t an;

static uint16_t game_port(void)
{
    if (mp_bridge_public_bound()) {
        return (uint16_t)MP_RELAY_UDP_PORT;   /* a public announce names the relay's port */
    }
    return an.udp != NULL ? mp_udp_local_port(an.udp) : 0u;
}

/* The announce's flags are derived from the setters that own them, so it does not matter which
 * of name, mode and password is set last. */
static void announcer_reconfigure(void)
{
    uint8_t flags = 0;

    if (an.host != NULL && an.host->password[0] != '\0') {
        flags |= (uint8_t)MP_ANNOUNCE_F_PASSWORD;
    }
    if (an.game_mode == 2u) {   /* MULTIPLAYER_MODE_TDM, the feature's number for it */
        flags |= (uint8_t)MP_ANNOUNCE_F_TDM;
    }
    mp_announcer_configure(&an.announcer, an.session_name,
                           an.slots != 0u ? an.slots : (uint8_t)(MP_BANK_FAR_MAX + 1u), flags,
                           an.enabled);
}

void mp_bridge_set_password(const char *password)
{
    mp_session_set_password(an.host, password);
    mp_session_set_password(an.client, password);
    announcer_reconfigure();
}

void mp_bridge_set_slots(uint8_t slots)
{
    if (slots < 2u) {
        slots = 2u;
    }
    if (slots > MP_BANK_FAR_MAX + 1u) {
        slots = (uint8_t)(MP_BANK_FAR_MAX + 1u);   /* its own body and one per far bank */
    }
    an.slots = slots;
    mp_session_set_capacity(an.host, (uint8_t)(slots - 1u));   /* the host is one of them */
    announcer_reconfigure();
}

void mp_bridge_set_announce(bool enabled, const char *session_name)
{
    an.enabled = enabled;
    mp_announce_name_clean(session_name, an.session_name);
    announcer_reconfigure();
}

uint16_t mp_bridge_game_port(void)
{
    return game_port();
}

void mp_bridge_lobby_note_mode(uint8_t game_mode)
{
    an.game_mode = game_mode;
    announcer_reconfigure();
}

void mp_bridge_lobby_close(void)
{
    mp_announcer_close(&an.announcer);
}

void mp_bridge_announce_bind(mp_session_t *host, mp_session_t *client, mp_udp_t *udp)
{
    an.host   = host;
    an.client = client;
    an.udp    = udp;
    /* A host seats no more than one machine can show, whether or not a menu ever names a number:
     * the ini's way into a session never calls the setter, and a capacity left at "every slot"
     * seated fifteen players in a game that shows four. The setter reconfigures the announce. */
    mp_bridge_set_slots(an.slots != 0u ? an.slots : (uint8_t)(MP_BANK_FAR_MAX + 1u));
}

void mp_bridge_announce_tick(uint32_t content, uint32_t now, bool advertise)
{
    /* A session that has ended is not advertised. Its note still goes out from the lobby,
     * because that is how the players still in it learn that it ended; a stranger answering the
     * advertisement would be sent into a level and thrown out of it in the same frame. */
    mp_announcer_set_facts(&an.announcer, (uint8_t)MP_WIRE_VERSION, content, game_port(),
                           (uint8_t)mp_bridge_lobby_population());
    mp_bridge_public_announce(&an.announcer, advertise, now);
}

uint8_t mp_bridge_announce_game_mode(void)
{
    return an.game_mode;
}

void mp_bridge_announce_report(void)
{
    log_info("  the announcer: %s as '%s', %u slot(s), %u sent, %u failed%s",
             an.enabled ? "on" : "off", an.session_name, (unsigned)an.slots,
             (unsigned)an.announcer.sent, (unsigned)an.announcer.failed,
             an.announcer.open_failed ? "; the broadcast socket would not open" : "");
}
