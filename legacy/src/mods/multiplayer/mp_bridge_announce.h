/* mp_bridge_announce.h: what a host says about itself to a browser, for mp_bridge_lobby.c.
 *
 * The public setters this file defines (the password, the seats, the announce and the mode the
 * bridge names) keep their declarations in mp_bridge_lobby.h, because that is the header the
 * feature and the screens include. What is declared here is what the lobby itself
 * calls to bind the announce, drive it, report it, and the one fact it reads back from it.
 */
#ifndef MULTIPLAYER_MP_BRIDGE_ANNOUNCE_H
#define MULTIPLAYER_MP_BRIDGE_ANNOUNCE_H

#include "mp_session.h"
#include "mp_udp.h"

#include <stdbool.h>
#include <stdint.h>

/* The two sessions and the socket, once they exist. Also sets the seats: the number a menu named
 * earlier, or as many as one machine can show. */
void mp_bridge_announce_bind(mp_session_t *host, mp_session_t *client, mp_udp_t *udp);

/* One pump of a host that has a lobby open or a session known: the facts the announce carries are
 * refreshed and the announcer asked to speak. `advertise` is false once the session has ended, so a
 * stranger is not invited into a session that is over. */
void mp_bridge_announce_tick(uint32_t content, uint32_t now, bool advertise);

/* The port the session's socket holds, 0 on the loopback or before a bridge. What an announce
 * names as the game port. */
uint16_t mp_bridge_game_port(void);

/* The game the bridge was last told, for a session that was armed from the ini with no lobby. */
uint8_t mp_bridge_announce_game_mode(void);

/* The announcer's line in the run report. */
void mp_bridge_announce_report(void);

#endif /* MULTIPLAYER_MP_BRIDGE_ANNOUNCE_H */
