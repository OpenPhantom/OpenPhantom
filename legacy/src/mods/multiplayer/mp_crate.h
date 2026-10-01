/* mp_crate.h: push blocks as the host's state, bound to the engine.
 *
 * Layer 2. The two machines in mp_crate_host.c and mp_crate_client.c decide; this file reads the
 * mover records, makes the engine's calls and repoints four calls while a session's transport
 * stands:
 *
 *   the player's push      a client asks its gate before its own engine pushes, and remembers
 *                          where the block went; the host asks the owner rule for its own player
 *   the push's drop        the host tells a fall as a moment, once
 *   the landing's sink     a client never sinks by itself: its landing is held for the host
 *   the sink's crush       left out while a client sinks a block the host sank, because the host's
 *                          crush already reaches every body standing there, and counted
 *
 * Every hook hands the engine its own call when no session is running over a socket, so a single
 * player game, and the loopback, run exactly as the engine does. The install runs on the
 * session's way in, beside the map's, never at the DLL's load.
 */
#ifndef MULTIPLAYER_MP_CRATE_H
#define MULTIPLAYER_MP_CRATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Resolves the sites, reads the engine's functions out of their calls and repoints the four
 * calls, all or none. Once per process; answers whether it stands. */
bool mp_crate_install(void);

/* The start of a substep: `tick` is the bridge's substep count, and a client puts its blocks
 * where the host has them. */
void mp_crate_apply_pending(uint32_t tick);

/* A host, after the puppets are placed and outside every bank window: the clients' wishes pushed
 * through the engine with their puppets as the bodies pushing. */
void mp_crate_run_due(uint32_t tick);

/* The end of a substep: a host's note and falls, a client's wishes. `send` answers whether the
 * channel took the message. */
typedef bool (*mp_crate_send_fn_t)(const uint8_t *bytes, size_t count);
void mp_crate_send(uint32_t tick, mp_crate_send_fn_t send);

/* One reliable message, from the peer in `sender_slot`. True when it is one of the push block
 * messages this side takes, torn or not. */
bool mp_crate_take(uint8_t sender_slot, const uint8_t *note, size_t bytes);

/* A peer arrived: a host's next note is whole. */
void mp_crate_note_arrival(void);

void mp_crate_report(void);

#endif /* MULTIPLAYER_MP_CRATE_H */
