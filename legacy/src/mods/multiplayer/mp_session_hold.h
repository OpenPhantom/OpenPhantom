/* mp_session_hold.h: the holds' half of the session, for the files that make one session.
 *
 * mp_session_hold.c keeps the rules of the per peer hold: what a send does when a channel has no
 * room, handing what is held on to the channel, and when a peer has fallen so far behind that it
 * is sent away. mp_session.c keeps the state machine, so the one exit a peer takes is still its
 * own: the send away is defined there, beside the drop and the leave, and called from here.
 *
 * The public calls are in mp_session.h with the rest of what a caller uses.
 */
#ifndef MULTIPLAYER_MP_SESSION_HOLD_H
#define MULTIPLAYER_MP_SESSION_HOLD_H

#include "mp_session.h"

#include <stdbool.h>
#include <stdint.h>

/* Hands what the peer's hold keeps on to its channel, oldest first, for as long as the channel
 * takes it. The service calls it first for every connected peer, so an empty hold and a channel
 * with nothing left over mean the same thing by the time a packet is built. */
void mp_session_hold_flush(mp_session_t *session, mp_peer_t *peer);

/* After the flush: whether the peer's oldest held message has waited past MP_SESSION_HOLD_AGE_MS
 * while the peer is still sending. It is sent away then, and this answers true. */
bool mp_session_hold_overdue(mp_session_t *session, mp_peer_t *peer);

/* A payload packet under a connection id no connected peer holds; a bulk packet is not asked
 * about. If it is a peer this host sent away for falling behind a moment ago, the reason goes back
 * to where it came from. */
void mp_session_hold_answer(mp_session_t *session, uint32_t endpoint, uint64_t connection_id);

/* Kept so that the answer above can be given: which connection was sent away, and until when. */
void mp_session_hold_remember(mp_session_t *session, const mp_peer_t *peer);

/* Defined in mp_session.c, where every exit of a peer is: the notice goes out twice with
 * MP_DENY_BEHIND, the connection is remembered, and the slot is freed. Counted as a peer sent
 * away, never as a denied request. */
void mp_session_send_away_behind(mp_session_t *session, mp_peer_t *peer);

#endif /* MULTIPLAYER_MP_SESSION_HOLD_H */
