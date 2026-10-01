/* mp_npc_copies_bridge.h: the NPC copies' protocol in the running game.
 *
 * mp_npc_copies_run decides; this gives it what it needs and carries out what it says. Once an
 * armed substep it walks the actor pool for the copies' keys, reads the record the developer
 * overlay wrote, runs the protocol and publishes the grant record when it changed. It hands the
 * copies' tables to the enemy block, parks a copy a client's overlay builds before the engine can
 * tick it, and hands a removal the host sent to the table instead of performing it, because only
 * the overlay removes a copy (common/npc_spawn_note.h).
 *
 * Over a socket only; on the loopback and in single player nothing here runs. Without an overlay
 * the walk and the record still run, and no copy exists, because nothing writes a wish.
 */
#ifndef MULTIPLAYER_MP_NPC_COPIES_BRIDGE_H
#define MULTIPLAYER_MP_NPC_COPIES_BRIDGE_H

#include "mp_session.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A session's arming, with the host's cap and the corpse sweep in seconds (0 leaves a copy's corpse
 * to the engine). A host over a socket hands copies out, a client over one builds them, anything
 * else runs nothing. The epoch rises and the record is published at once. */
void mp_npc_copies_bridge_arm(bool over_a_socket, bool as_client, uint32_t cap,
                              uint32_t corpse_seconds);

/* The session is over: the record says so, and no table is handed out any more. */
void mp_npc_copies_bridge_disarm(void);

/* The world is another one: a level ended or restarted, a world was built, a session ended while
 * the transport stays. Before the enemies are let go, so that nothing is given up for it. */
void mp_npc_copies_bridge_new_world(void);

/* A level began: the world this machine entered is the one the setup names now, and the copies
 * may be decided on again. Until then, after a new world, nothing is. */
void mp_npc_copies_bridge_level_begin(void);

/* One armed substep, after the tick ran, with or without a peer. `send` reaches everybody;
 * `host` is the host's session, for a refusal to one client, and NULL on a client. */
void mp_npc_copies_bridge_substep(bool joined, bool (*send)(const uint8_t *bytes, size_t count),
                                  mp_session_t *host);

/* A reliable note from the world slot `slot` on `connection`: true when it was a wish or an entry
 * of the copies, whatever became of it. */
bool mp_npc_copies_bridge_take(uint8_t slot, uint64_t connection, const uint8_t *note,
                               size_t bytes);

/* The copies' line for the run report, the host's or a client's. */
void mp_npc_copies_bridge_report(void);

#endif /* MULTIPLAYER_MP_NPC_COPIES_BRIDGE_H */
