/* mp_bridge_relay.h: a listen host passes its clients' moments on to each other.
 *
 * The dedicated server has always passed every reliable message a client sends on to the other
 * clients. A listen host passed nothing on: with one client there was nobody to pass it to, and
 * with two or three a client never saw another client shoot, push, swing or open a door. The host
 * keeps the authority over everything else a client says, a hit, a pickup, a death, the story,
 * and answers those itself; what it passes on is a player's moments and nothing else.
 *
 * Each copy is restamped on the way (mp_event_restamp): it names the slot of the peer it came
 * from, which only the host can vouch for, and it carries the host's substep instead of the
 * sender's, because the other clients see the sender's body on the host's worlds and so on the
 * host's clock.
 */
#ifndef MULTIPLAYER_MP_BRIDGE_RELAY_H
#define MULTIPLAYER_MP_BRIDGE_RELAY_H

#include "mp_session.h"

#include <stddef.h>
#include <stdint.h>

/* One message peer `from` sent the host, passed on to every other connected peer when it is one of
 * a player's moments, stamped with the host's substep `tick`; anything else is left alone. A peer
 * whose channel is full has its copy held behind what it is already owed (mp_hold); a peer whose
 * hold is full as well is sent away, and that copy is counted as refused. */
void mp_bridge_relay_pass_on(mp_session_t *host, size_t from, uint32_t tick, const uint8_t *note,
                             size_t bytes);

/* The copies passed on over the run and the ones that reached no hold either, for the report. */
void mp_bridge_relay_counts(uint32_t *passed, uint32_t *refused);

#endif /* MULTIPLAYER_MP_BRIDGE_RELAY_H */
