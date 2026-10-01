/* mp_bridge_roster.h: the roster on the bridge, both halves.
 *
 * Layer 3. The listen host builds the roster out of its session, its own name in slot 0 and each
 * peer's name and round trip in the slot its body rides, and broadcasts it once a second and
 * whenever it changed; a client keeps the newest one it received. Both print it in the report.
 * The dedicated server does the same for itself in mp_server, without a line of its own.
 *
 * The local player's name lives here as well, cleaned once, because the two things that need it
 * are the session's handshake and the roster, and both are this file's callers' business.
 */
#ifndef MULTIPLAYER_MP_BRIDGE_ROSTER_H
#define MULTIPLAYER_MP_BRIDGE_ROSTER_H

#include "mp_roster.h"
#include "mp_session.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The local player's name, cleaned by the roster's rule; NULL or empty becomes the default. */
void mp_bridge_roster_set_name(const char *name);
const char *mp_bridge_roster_name(void);

/* A host's substep: builds the roster from the session and broadcasts it when it changed or a
 * second has passed. Returns whether one went out. */
bool mp_bridge_roster_host_tick(mp_session_t *host, uint32_t substep);

/* Takes a roster off the reliable channel, on either role. True when it was one. A changed roster
 * is written to the log once, line by line, so a join, a leave or a rename can be read there. */
bool mp_bridge_roster_take(const uint8_t *note, size_t bytes);

/* The lobby's three bytes. The host's own line is set by the first; a peer's by the note it sent,
 * taken by the second, which answers false for a note that is not a lobby note. Both show in the
 * next roster the host builds.
 *
 * The hero is here and not beside it because the roster is the only place it can be read back
 * from: the note that carries it is written by one side and read by the other, and everything
 * that acts on a hero (the swap after a level starts, the puppet's appearance) reads the table.
 * A setter that took only team and ready left the field at zero for every player, which is what
 * made the lobby's hero row a label. */
void mp_bridge_roster_set_own(uint8_t team, bool ready, uint8_t hero);
bool mp_bridge_roster_take_lobby(size_t peer_index, const uint8_t *note, size_t bytes);

/* What each player looks like, which travels here and not only as an event.
 *
 * An appearance event is an edge, and somebody who joins after it has missed it: they would see
 * that player wearing the level's own hero for the rest of the session with no message left that
 * could correct them. The roster is repeated once a second, so a line in it is a state rather than
 * an edge, and a late joiner reads the current answer out of the next repeat.
 *
 * The two setters exist because the two answers come from different places. This side's own comes
 * from watching the local player's loaded actor; a peer's comes off the wire. Neither is a name
 * this module trusts: both are cleaned by the roster's own rule, and a name that cannot pass it
 * becomes empty, which reads as "not known" rather than refusing the whole table for fifteen
 * other people. */
/* The hero a player is WEARING, as the appearance that crossed named it. The roster's own hero
 * field is the lobby pick until a level puts one on; from then on the pick is history and what
 * matters to everybody reading the list is the figure in the world. Zero is a hero, so a worn
 * hero is remembered as hero + 1 and nought means nobody has said yet. */
void mp_bridge_roster_set_own_hero(uint8_t hero);
void mp_bridge_roster_note_peer_hero(size_t peer_index, uint8_t hero);

/* The asset goes with its kind (MP_SKIN_CHARACTER or MP_SKIN_MODEL), because the one name means
 * two different bodies: an actor to build, or a model worn over the hero's own. An asset the
 * roster's rule empties takes its kind with it, and so does a kind this build does not know. */
void mp_bridge_roster_set_own_asset(const char *asset, uint8_t kind);

/* And how big this machine's own body is drawn, in the hundredths the wire carries. The table is
 * the state behind the appearance event's edge, so a player who joins later reads it here. */
void mp_bridge_roster_set_own_scale(uint16_t scale);

/* The same for a peer, as its appearance event said. An authority repeats it to everybody else;
 * on a client the write is kept and never sent, which is one path for both roles. */
void mp_bridge_roster_note_peer_scale(size_t peer_index, uint16_t scale);
void mp_bridge_roster_note_peer_asset(size_t peer_index, const char *asset, uint8_t kind);

/* The newest roster this side holds. False when none arrived or was built yet. */
bool mp_bridge_roster_current(mp_roster_t *out);

/* Forgets the table, so the next session's lobby does not show the last one's players. A host
 * builds a new one on its next pump; a client waits for the host's. */
void mp_bridge_roster_forget(void);

/* The report: the table as this side knows it, and on a client its own measured round trip to the
 * host beside the host's measurement of it. */
void mp_bridge_roster_report(const mp_session_t *client, bool is_host);

#endif /* MULTIPLAYER_MP_BRIDGE_ROSTER_H */
