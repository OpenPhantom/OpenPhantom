/* mp_bridge_public.c: a public session's relay, as the lobby and the screens see it. See the
 * header.
 */
#include "mp_bridge_public.h"

#include "mp_announce.h"
#include "mp_bridge_lobby.h"
#include "mp_relay_wire.h"

#include "common/logging.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

typedef struct public_state {
    mp_relay_transport_t *relay;
    bool                  is_client;
    bool                  listed;
    bool                  join_held;
    bool                  listing_logged;
    bool                  listed_told;
} public_state_t;

static public_state_t pub;

void mp_bridge_public_bind(mp_relay_transport_t *relay, bool is_client)
{
    bool listed = pub.listed;

    memset(&pub, 0, sizeof pub);
    pub.relay     = relay;
    pub.is_client = is_client;
    pub.listed    = listed;   /* the menu's choice outlives a transport */
}

void mp_bridge_public_unbind(void)
{
    mp_bridge_public_bind(NULL, false);
}

bool mp_bridge_public_bound(void)
{
    return pub.relay != NULL;
}

void mp_bridge_public_tick(void)
{
    if (pub.relay == NULL) {
        return;
    }
    mp_relay_transport_pump(pub.relay);
    if (pub.join_held && mp_relay_transport_state(pub.relay) == MP_RELAY_STATE_READY) {
        pub.join_held = false;
        log_info("the relay has seated this player: the join held for it begins now");
        mp_bridge_connect_in_lobby(NULL);
    }
}

bool mp_bridge_public_hold_join(void)
{
    if (pub.relay == NULL || !pub.is_client ||
        mp_relay_transport_state(pub.relay) == MP_RELAY_STATE_READY) {
        pub.join_held = false;
        return false;
    }
    if (!pub.join_held) {
        log_info("the join waits for the relay (%s): the handshake rides the relay's leg",
                 mp_relay_state_name(mp_relay_transport_state(pub.relay)));
    }
    pub.join_held = true;
    return true;
}

bool mp_bridge_public_join_held(void)
{
    return pub.relay != NULL && pub.join_held;
}

void mp_bridge_public_announce(mp_announcer_t *announcer, bool advertise, uint32_t now)
{
    uint8_t announce[MP_RELAY_ANNOUNCE_BYTES];
    bool    listed;

    if (pub.relay == NULL) {
        if (advertise) {
            (void)mp_announcer_tick(announcer, now, (uint16_t)MP_ANNOUNCE_PORT);
        }
        return;
    }
    memset(announce, 0, sizeof announce);
    listed = advertise && pub.listed &&
             mp_announce_encode(&announcer->announce, announce, sizeof announce) ==
                 MP_RELAY_ANNOUNCE_BYTES;
    if (advertise && pub.listed && !listed && !pub.listing_logged) {
        pub.listing_logged = true;
        log_warning("the session is not listed on the relay: its announce did not encode (a name "
                    "or a count the announce refuses)");
    }
    if (listed != pub.listed_told) {
        pub.listed_told = listed;
        log_info("the relay's public list %s this session", listed ? "shows" : "no longer shows");
    }
    mp_relay_transport_set_listing(pub.relay, listed, announce);
}

void mp_bridge_set_list_public(bool listed)
{
    pub.listed = listed;
}

mp_relay_state_t mp_bridge_public_state(void)
{
    return mp_relay_transport_state(pub.relay);
}

bool mp_bridge_public_code(char out[MP_RELAY_CODE_TEXT_BYTES])
{
    uint8_t code[MP_RELAY_CODE_BYTES];

    if (!mp_relay_transport_code(pub.relay, code)) {
        return false;
    }
    mp_relay_code_text(code, out);
    return true;
}

mp_relay_link_failure_t mp_bridge_public_link_failure(void)
{
    return pub.relay != NULL ? pub.relay->link.failure : MP_RELAY_LINK_FINE;
}
