/* mp_bridge_public.h: a public session's relay, as the lobby and the screens see it.
 *
 * The bridge puts up the relay's transport for a public session (mp_bridge_install_public) and
 * binds it here; the LAN binds nothing, and every call below then answers as the LAN would. What
 * the lobby asks of the relay is three things:
 *
 *   - The advertisement. A LAN host says who it is once a second on the LAN; a public host says it
 *     in its refreshes to the relay instead, and only when the player chose to be listed. The LAN
 *     announce is never sent in public, and the relay never hears of a LAN session.
 *   - A player's join waits for the relay. The session's handshake rides the relay's leg, so a join
 *     asked for before the relay has seated the player is held here and begun the moment it has;
 *     until then the lobby reads it as a join under way, not one that gave up.
 *   - What the screens show: whether the relay is reached, the session's code, and why not.
 */
#ifndef MULTIPLAYER_MP_BRIDGE_PUBLIC_H
#define MULTIPLAYER_MP_BRIDGE_PUBLIC_H

#include "mp_announcer.h"
#include "mp_relay_transport.h"

#include <stdbool.h>
#include <stdint.h>

void mp_bridge_public_bind(mp_relay_transport_t *relay, bool is_client);
void mp_bridge_public_unbind(void);

/* Whether the session runs over the relay. */
bool mp_bridge_public_bound(void);

/* The lobby's tick: the relay pumped, and a held join begun once the relay seated the player. */
void mp_bridge_public_tick(void);

/* Holds a player's join while the relay has not seated the player yet. True when it was held. */
bool mp_bridge_public_hold_join(void);

/* A join is held for the relay. */
bool mp_bridge_public_join_held(void);

/* The host's advertisement for this tick, with the facts already set on `announcer`: over the relay
 * in public, on the LAN otherwise. `advertise` false keeps a session off both. */
void mp_bridge_public_announce(mp_announcer_t *announcer, bool advertise, uint32_t now);

/* Whether a public host is shown in the relay's list. Off until the menu says so. */
void mp_bridge_set_list_public(bool listed);

mp_relay_state_t mp_bridge_public_state(void);

/* The session's code as text, "ABCD-EFGH". False before a host has one, and on the LAN. */
bool mp_bridge_public_code(char out[MP_RELAY_CODE_TEXT_BYTES]);

/* The link's own reason when it gave up, for the few a player can act on: no such session, a full
 * one, an ended one. MP_RELAY_LINK_FINE on the LAN. */
mp_relay_link_failure_t mp_bridge_public_link_failure(void);

#endif /* MULTIPLAYER_MP_BRIDGE_PUBLIC_H */
