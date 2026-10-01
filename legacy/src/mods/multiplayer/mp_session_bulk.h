/* mp_session_bulk.h: the bulk lane's own half of the session, and the seam it is cut on.
 *
 * Split from mp_session.c on 2026-09-09 at the seam its size note named. The lane shares the
 * session's envelope and its peer table and nothing else: it has no sequence, no acknowledgement,
 * no queue and no ordering, so none of the state machine in mp_session.c is any of its business.
 * What the two halves still share is declared here rather than guessed at twice, the peer lookup in
 * particular, because a second way of deciding which peer an address is would be a second rule for
 * the same state, and that shape has already cost this feature five defects: two ways into the same
 * state share the rule, or one of them is a path nobody checked.
 *
 * The public calls are in mp_session.h with the rest of what a caller uses. This header is for
 * the two files that implement one session between them.
 */
#ifndef MULTIPLAYER_MP_SESSION_BULK_H
#define MULTIPLAYER_MP_SESSION_BULK_H

#include "mp_session.h"

#include <stddef.h>
#include <stdint.h>

/* The two bytes of the envelope the lane writes for itself: the session's magic and the packet
 * type the session dispatches on. They are named HERE and asserted equal to the session's own
 * in mp_session.c, so that an edit to either side breaks the build rather than the transfer.
 * (The first form pinned the session's constants to literals and left these two unnamed, which
 * guarded nothing: the second control pass over the lane caught it.) */
#define MP_SESSION_BULK_MAGIC 0x4F504D53u
#define MP_SESSION_BULK_TYPE  8u

/* The connected peer at an address, or NULL. The one rule for that question, shared rather than
 * repeated. */
mp_peer_t *mp_session_peer_at(mp_session_t *session, uint32_t endpoint);

/* One arrived bulk note, from the dispatch. It is refused unless the address and the connection
 * id together name a connected peer, exactly as a payload is, so the anti amplification promise
 * at the head of mp_session.h is untouched. */
void mp_session_bulk_arrived(mp_session_t *session, uint32_t endpoint, uint64_t connection_id,
                             const uint8_t *note, size_t bytes);

#endif /* MULTIPLAYER_MP_SESSION_BULK_H */
