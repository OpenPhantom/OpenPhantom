/* mp_seat_order.h: which clients of the session an arriving client is seated after.
 *
 * Layer 3. Two clients arriving beside the host at the same moment each search their own ring, and
 * a ring is only a starting direction: a slot that loses two of its own directions goes on into the
 * first of the next slot's, and the other client, still standing at the level start, is no body on
 * that point yet. So every client seats itself after the client slots below its own, foreseeing
 * their seats around the same anchor (mp_seat.h, "Two clients arriving together").
 *
 * The slots come from the roster, not from the far banks. The roster is the session's own list,
 * repeated reliably and the same on every client from the lobby on; a bank is set only once the
 * host's world carries that player's body, and a client sends its body only from a substep in the
 * level, so the bank of a slower lower slot is missing in exactly the case this is for. The bank is
 * read only to know which far body a lower slot's own machine does not see: its own.
 *
 * If the field shows two hand-overs within a unit of each other all the same, the next form is the
 * host handing the arrival seats out in slot order, as it hands out a scene's seats.
 */
#ifndef MULTIPLAYER_MP_SEAT_ORDER_H
#define MULTIPLAYER_MP_SEAT_ORDER_H

#include "mp_seat.h"

#include <stdbool.h>
#include <stdint.h>

/* Starts `wish` beside the host for the player of `my_slot`, seated after every client slot of the
 * session below it. With no roster read yet the wish has no order, and its report says so.
 *
 * `late` says this player joined a session that was already running. Its lower slots that stand in
 * this world already are left out of the order: they are bodies the search steps around, and a
 * seat held for them would be held for nobody. */
void mp_seat_order_wish_beside_host(mp_seat_wish_t *wish, const char *who, uint8_t my_slot,
                                    bool late);

#endif /* MULTIPLAYER_MP_SEAT_ORDER_H */
