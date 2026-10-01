/* mp_bridge_seats.h: which world slot each of a listen host's peers plays at, and telling each.
 *
 * A dedicated server tells every peer its world slot in one reliable byte as soon as the peer is
 * seated, and a client has always taken that byte. A listen host told nobody, so every client of
 * one held the listen host's convention for its ONE client, slot 1, and two clients held it
 * together: their deaths were counted as one player's, each dropped the other's appearance as its
 * own echo, and a hit on either was charged to the same seat. Which slot a peer gets is the
 * session's rule (mp_session_slot_of_peer); this is the listen host's ledger of whom it has told.
 *
 * The ledger is keyed on the connection id and not on the peer index alone. A client that
 * restarts from the same address is replaced in place by the session's parallel handshake, so its
 * index stays connected throughout and only the id says that somebody new sits there.
 */
#ifndef MULTIPLAYER_MP_BRIDGE_SEATS_H
#define MULTIPLAYER_MP_BRIDGE_SEATS_H

#include "mp_session.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Nobody has been told anything. Run at every bind: a ledger carried over from the last session
 * would believe it had told people who are not here. */
void mp_bridge_seats_reset(void);

/* One walk over every index of a host's session: who arrived and who left since the last walk,
 * then the slot note to every seated peer that has not had one on its present connection. A note
 * the channel refuses is offered again on the next walk. Cheap enough for every pump. Answers how
 * many seats changed hands, a replacement in place counting twice. */
size_t mp_bridge_seats_update(mp_session_t *host);

/* On a host, for a few seconds after it sent a player away for falling behind: who went and why,
 * for the picture, which asks on the frames it draws the band on and counts through this. NULL
 * otherwise. */
const char *mp_bridge_seats_notice(void);

/* The ledger's counts on a host; on a client, the one slot this side holds. */
void mp_bridge_seats_report(bool is_host, uint8_t my_slot);

#endif /* MULTIPLAYER_MP_BRIDGE_SEATS_H */
