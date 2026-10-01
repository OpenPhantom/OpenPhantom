/* mp_hit_relay.h: a client's hit, performed by the host.
 *
 * ==================================== Why this exists =========================================
 *
 * The host owns an enemy's health, and it writes it into every replica on the client every substep.
 * So a client that shoots an enemy changes nothing: whatever its own simulation did is overwritten
 * on the next packet, and the host never learns that a shot connected at all. Enemies are not
 * "sometimes invulnerable" to a client, they are invulnerable to it entirely.
 *
 * ================================= Why a replay, not a number =================================
 *
 * The obvious shape is to send "take 12 damage" and have the host subtract it. That would be one
 * write and it would be wrong in every way that matters: no flinch, no knockback, no reaction
 * state, no death animation, no kill counted, no limb detached, no script noticing a death.
 *
 * The engine's contact handler does all of that, and it takes NO arguments: it reads six globals.
 * So the host can set those six and call it, and the whole damage path then runs as if the host
 * had made the hit itself. That is the same trick the use relay uses, for the same reason: the
 * engine's own interface is unattributed, so a client's action can be handed to it directly.
 *
 * The sixth global is not an argument. Five of them are the parameters of the function that
 * publishes a contact; the sixth is the sender's own impact code, stamped from the object at +0x0c.
 * A replay that sets five and forgets the sixth deals the PREVIOUS contact's damage, which on a
 * quiet frame is whatever last happened to anybody.
 *
 * ================================== What the client sends =====================================
 *
 * The placement index of the victim, the contact code, and the impact code it saw, which is the
 * damage (see below). So the number is the client's word: a co-op host takes it, and a deathmatch
 * host performs no client's hit at all. No host performs one with a wave's code, because it fires
 * that wave itself from the player's puppet (mp_trust.h).
 */
#ifndef MULTIPLAYER_MP_HIT_RELAY_H
#define MULTIPLAYER_MP_HIT_RELAY_H

#include "mp_hit_relay_death.h"
#include "mp_hit_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ============================ What the client sends, and why five bytes ======================
 *
 * The first form sent the placement index and the contact code, and the host filled the rest in
 * from its own puppet. That was wrong twice over, and the fifth field run proved it: 99 hits
 * reported, 99 performed, not one enemy killed.
 *
 * The impact code is the damage. `enemy_receiveDamage` looks the code up in the table at
 * 0x4b4840 and takes the row's number; the attacker's class has nothing to do with it. The host
 * read that code off the puppet's own body at +0x0c, and a body that is not swinging carries
 * 0x29, the row whose damage is ZERO in every difficulty column. By the time a report crossed
 * the wire the puppet's swing was usually over, so the replay dealt nothing, honestly and
 * silently. The code the CLIENT saw is the only one that means anything.
 *
 * The two flag channels are the fork. A body-to-body contact with `b` clear is a SHOVE and
 * deals no damage at all; with `b` set and the attacker carrying an armed node it is a hit. The
 * block arms read `a`. The replay wrote four of the six globals and left those two holding
 * whatever the last contact on the host had put there.
 *
 * So the client sends what it saw: the victim, the contact code, the impact code, and the two
 * flags with the one thing the host cannot know, whether the attacker's weapon was armed. The
 * message, its flags and its codec are in mp_hit_wire.h. */

typedef bool (*mp_hit_relay_send_fn_t)(const uint8_t *bytes, size_t count);

/* What became of a hit on a far player addressed to the machine of the player it struck. */
typedef enum mp_hit_relay_addressed {
    MP_HIT_ADDRESSED_SENT = 0,      /* to that peer's channel, or held behind what it is owed */
    MP_HIT_ADDRESSED_NO_PEER,       /* nobody plays at the slot on this side */
    MP_HIT_ADDRESSED_REFUSED        /* that peer could be held for no more and was sent away */
} mp_hit_relay_addressed_t;

typedef mp_hit_relay_addressed_t (*mp_hit_relay_send_to_slot_fn_t)(uint8_t slot,
                                                                   const uint8_t *bytes,
                                                                   size_t count);

/* Resolves the cells and hooks the contact handler. False when anything is missing, and nothing is
 * patched in that case. */
bool mp_hit_relay_install(void);
bool mp_hit_relay_installed(void);

/* A client watches its own contacts and reports the ones against a replica; a host performs what
 * it is told and watches nothing. */
void mp_hit_relay_set_client(bool is_client);

/* Where a reported hit goes. Set once the bridge exists; without it a client observes and drops.
 * It is also where a death goes: this is the one setter, and it hands the function on. */
void mp_hit_relay_set_send(mp_hit_relay_send_fn_t send);

/* Where a hit the host saw on a far player goes: to the one machine whose player it struck, and
 * to nobody else, who would only discard it. NULL sends it to everybody, as before. */
void mp_hit_relay_set_send_to_slot(mp_hit_relay_send_to_slot_fn_t send);

/* Whether a contact's sender is an NPC's bolt that travels, whose hit belongs to the machine
 * its victim sits at. Neither half reports such a contact: the host not on a far player, a
 * client not on an actor the host owns. Unset, every contact is reported as before. */
typedef bool (*mp_hit_relay_npc_owned_fn_t)(uint32_t object);
void mp_hit_relay_set_npc_owned(mp_hit_relay_npc_owned_fn_t test);

/* Takes a hit message if it is one of ours and performs it on a host, with the far body of bank
 * `bank`, the sender's, as the attacker. */
bool mp_hit_relay_take_message(size_t bank, const uint8_t *note, size_t bytes);

/* The host's replay on its own: the far body of bank `bank` as the sender, the actor a placement
 * index names as the receiver, `code` as the contact code, and the engine's own handler run on
 * it. The hit message is one caller; a pickup claim, whose contact code is the pickup's own
 * "taken" message and whose bank is the claimant's, is another. False when there is no victim or
 * no attacker to read the impact from. */
bool mp_hit_relay_perform(size_t bank, uint32_t key, uint8_t code);

/* The full replay, with everything the client saw. `impact` is the code the damage table is
 * indexed by and `flags` carries the two channels plus the attacker's armed node. The two
 * argument form above keeps the pickup path, which wants a touch and no damage. */
bool mp_hit_relay_perform_full(size_t bank, uint32_t key, uint8_t code, uint8_t impact,
                               uint8_t flags, uint8_t node_id);

/* ============================== The same crossing, the other way =============================
 *
 * An enemy that shoots a far player hits a PUPPET, and a puppet's contacts are dropped on the
 * host: what a far player is worth in health, pain, HUD and death belongs to the machine that
 * player is on. Dropped is where it ended, so nothing at all happened to that player and the
 * enemies could not kill him.
 *
 * The answer is the mirror of the client half above. The host reports the contact it dropped,
 * naming the attacker by its PLACEMENT rather than by a pointer, because a pointer means
 * nothing on the other machine and every client has that placement's replica already. The
 * client sets the same six globals on its own player's body and calls the engine's own
 * handler, and from there the engine does what it always does.
 *
 * The message and its codec are in mp_hit_wire.h: the slot the hit is for, the attacker's key,
 * the contact code, the impact code and the flags. */

/* =========================== Who the attacker is, and how that is decided ====================
 *
 * An object names its owner in one slot, and the engine writes two different kinds of thing into
 * it: the ACTOR a character's body belongs to, and the SHOT RECORD a projectile flies as. The two
 * are told apart by asking the owner to name the body back, because each kind holds that back
 * pointer at its own offset: an actor keeps its body at +0x34, a shot record keeps its at +0xA0.
 * Only one of the two round trips can close, and which one closes is the answer.
 *
 * Guessing instead was a real misattribution rather than a placeholder. The placement index sits
 * at +0x18 of an actor, and +0x18 of a shot record is its lifetime as a float. Read as an integer
 * a lifetime is usually a large number and falls out of range on its own, which is why this
 * mostly looked like it worked; but one projectile kind is authored with a lifetime of zero, its
 * bits are zero, and the hit was then reported as coming from placement zero of the level.
 *
 * An owner that closes neither round trip is not named at all. There is no third kind to guess
 * at, and a wrong attacker is worse than none.
 *
 * The death message, which the victim reports, is declared in mp_hit_relay_death.h. This header
 * includes it, so everything that reached the death's names through here still does. */

/* The host: a contact against the far body of `bank` was dropped here. Reads the six globals
 * and sends what it found. */
void mp_hit_relay_note_puppet_hit(size_t bank);

/* A shot this machine's player fired, and one a far player's puppet fired here, by the object it
 * flies as. A contact from the second is that player's hit on their own machine and is not
 * reported from here. */
void mp_hit_relay_note_own_shot(uint32_t object);
void mp_hit_relay_note_far_shot(uint32_t object);

/* The same with the far player's bank named, for a shot a puppet fired: the bank is what
 * names a killer when the engine leaves the shot with no side of its own. */
void mp_hit_relay_note_far_shot_of(uint32_t object, size_t bank);

/* Which far player's bank fired the shot flying as `object`, when the memory holds it. */
bool mp_hit_relay_far_shot_bank(uint32_t object, size_t *bank);

/* An ally fired the shot flying as `object`: a hit it lands on a far player is not sent, as the
 * engine's pair filter keeps it off the local player in single player. */
void mp_hit_relay_note_ally_shot(uint32_t object);

/* Whether a contact's sender is a projectile's body: its owner is a shot record that names it back
 * as the body it flies as. The one test for every reader that has to tell a bolt from a body. */
bool mp_hit_relay_is_a_shot(uint32_t object);

/* Whether the contact being delivered was sent by an ally: an ally's bolt, or the body of an actor
 * of the player's side. Counted here; the gate between players asks it before anything else, since
 * an ally reads as the local player's side there and would be counted as a player. */
bool mp_hit_relay_sender_is_an_ally(void);

/* The client: which world slot this machine holds, so a hit meant for somebody else is left
 * alone. Set from the bridge, which is told it by the host or by the server. */
void mp_hit_relay_set_slot(uint32_t slot);

void mp_hit_relay_report(void);

#endif /* MULTIPLAYER_MP_HIT_RELAY_H */
