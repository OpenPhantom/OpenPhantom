/* mp_hit_relay_death.h: the death, which the victim reports and everybody else counts.
 *
 * Layer 1, no engine. The death message travels on the hit relay's channel and is recognised by
 * the relay's distributor, but it reads no contact global and runs no handler, so it lives here and
 * the relay hands it what it needs: the send function once, the slot this machine holds with each
 * death off the wire.
 *
 * ================================ The death, which the VICTIM reports =========================
 *
 * A hit crosses the wire dozens of times per life and carries no attacker a scoreboard could use.
 * A death crosses it once, and the machine the death happened on is the one that knows who caused
 * it: the contact came out of a bank, and each bank's body carries its own collision class.
 *
 * So the victim reports, in four bytes: the tag, the slot that died, the slot that killed it and
 * the reason. The tag is free, and it is here rather than with the other event tags because the
 * hit this message replaces the attribution for lives here too.
 *
 * Four bytes is shared with other messages on the same channel, the lobby note, the world
 * acknowledgement and the length the events keep for the old acknowledgement, and that is safe
 * in both directions: this recogniser tests the tag as well as the length, and every recogniser
 * the drain runs before it, the world, the scratch wire and the use relay, compares the first
 * byte against its own tag before it takes anything.
 *
 * The message itself lives in mp_death.h, which this includes so that nothing which already
 * reached it through mp_hit_relay.h had to change. It was taken out because a machine with no
 * engine has every reason to read a death and no way to link the relay: the dedicated server
 * keeps the score of a match it does not simulate.
 */
#ifndef MULTIPLAYER_MP_HIT_RELAY_DEATH_H
#define MULTIPLAYER_MP_HIT_RELAY_DEATH_H

#include "mp_death.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Where a death goes once it has been decoded, or once it has been noted locally. Set by whoever
 * keeps the score; without one a death is counted here and dropped. Every death this machine has
 * to know about arrives exactly once: its own through the note below, a far player's off the
 * wire. */
typedef void (*mp_hit_relay_death_fn_t)(const mp_death_note_t *note);
void mp_hit_relay_set_death_listener(mp_hit_relay_death_fn_t listener);

/* The victim's own report: this machine's player died, and this is who did it. Sends the message
 * and hands the same death to the local listener, so a host that keeps the score sees its own
 * deaths as well as everybody else's.
 *
 * The killer slot is what the caller worked out from the bank the contact came from, and the
 * mapping is the collision class: bank 0 is class 1 and bank i is class 4 + i. A contact with no
 * bank behind it is MP_DEATH_NO_KILLER. */
void mp_hit_relay_note_death(uint8_t victim_slot, uint8_t killer_slot, uint8_t reason);

/* Where a death this machine reports is sent. mp_hit_relay_set_send is the one setter and hands
 * its function on to here, so a death leaves through the channel the hits leave through. */
void mp_hit_relay_death_set_send(bool (*send)(const uint8_t *bytes, size_t count));

/* A death off the wire, handed on by mp_hit_relay_take_message once it has recognised the tag.
 * `own_slot` is the world slot this machine holds, which the relay keeps. */
void mp_hit_relay_take_death(const uint8_t *note, size_t bytes, uint32_t own_slot);

/* The line of the hit relay's report that counts the deaths. */
void mp_hit_relay_death_report(void);

#endif /* MULTIPLAYER_MP_HIT_RELAY_DEATH_H */
