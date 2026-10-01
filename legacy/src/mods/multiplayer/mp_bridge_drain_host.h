/* mp_bridge_drain_host.h: a listen host's read of every client's own state, and what a client is
 * owed when it enters the host's world.
 *
 * The first half of the seam mp_bridge_drain.c named for itself: the host's read of every client's
 * own state and the client's read of the host's world share only the ring. This is the first.
 *
 * ============================== Connection, and entry, are two moments ==========================
 *
 * A client used to be greeted when its connection appeared: its bank's history and its puppet
 * started over, and everything the others already had was told again, this player's appearance,
 * every mover of the map and the push blocks. For a client that connects in a lobby and loads the
 * level with everybody that was the same moment. For one that joins a session whose level is
 * already running it is not: it sits in its lobby until its player says ready, and what was told
 * to it there arrived while it had no level and was gone by the time it had one.
 *
 * So the greeting is two. At the connection, inside a substep as before, what belongs to the new
 * connection: its bank starts over, the world's baseline for it and the enemies it holds are
 * forgotten, and its puppet starts over. At its ENTRY, what it is owed: the appearance, the
 * movers, the push blocks, the campaign bank with its zeros and the blackboard, and the baseline
 * forgotten once more, so the first enemy payload after the entry is whole.
 *
 * An entry is the first state of a connection sent from the world this host stands in, once per
 * connection and world. A world change is therefore an entry for every player, which is where
 * nobody was greeted before. An entry noticed between two substeps is greeted in the next one,
 * because the greeting is substep work.
 *
 * Referenced practice: Quake 3 sends a client its gamestate once the client is in the world
 * (SV_ClientEnterWorld after `begin`), Source spawns it at SIGNONSTATE_SPAWN, and Unreal at
 * HandleStartingNewPlayer; the connection itself carries nothing of the world. Here the world is
 * entered when the first body of it arrives, which spares the wire a message.
 */
#ifndef MULTIPLAYER_MP_BRIDGE_DRAIN_HOST_H
#define MULTIPLAYER_MP_BRIDGE_DRAIN_HOST_H

#include "mp_bridge_drain.h"

#include <stdbool.h>
#include <stdint.h>

/* The host half of the drain between substeps: every client's states stored, and a connection
 * that changed noticed and left for the next substep, which greets it. Returns the payloads taken.
 * mp_bridge_drain_client_state, declared beside the other drains, is the same inside a substep. */
uint32_t mp_bridge_drain_host_between_substeps(mp_bridge_drain_t *drain);

/* What the host counted of its clients' connections and entries over the run. */
typedef struct mp_bridge_drain_joins {
    uint32_t joined_running;     /* connections that appeared while the session's level ran */
    uint32_t entered_late;       /* of those, the ones that entered this host's world */
    uint32_t left_before_entry;  /* and the ones that went before they did */
    uint32_t greeted_at_entry;   /* every greeting at an entry, a level's start and a world
                                  * change included */
    uint32_t greeted_unentered;  /* a greeting for a bank whose newest state is of no world this
                                  * host stands in: must be 0 */
} mp_bridge_drain_joins_t;

void mp_bridge_drain_host_joins(mp_bridge_drain_joins_t *out);

/* The report line of the counts above, on a host. */
void mp_bridge_drain_host_report(void);

#endif /* MULTIPLAYER_MP_BRIDGE_DRAIN_HOST_H */
