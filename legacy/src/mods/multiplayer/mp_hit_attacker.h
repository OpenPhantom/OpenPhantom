/* mp_hit_attacker.h: whose contact a client may report to the host as its own hit.
 *
 * Layer 1, pure. A client sees every contact the engine's pair pass makes against an actor the host
 * owns, and those include contacts that have nothing to do with this machine's player: two parked
 * actors standing against each other, the far player's puppet walking into one, a copy of a shot
 * the far player fired. Each of those happens on the host as well, or is the far player's own hit
 * there, so reporting it makes the host deal the damage a second time, from this client's puppet.
 * One field run reported 15 294 of them in 583 substeps and the host's actors died where they
 * stood. Only the local player's own body and its own shots are this client's hits.
 */
#ifndef MULTIPLAYER_MP_HIT_ATTACKER_H
#define MULTIPLAYER_MP_HIT_ATTACKER_H

#include <stdbool.h>
#include <stdint.h>

/* How many shot objects are remembered with the player who fired them. A shot lives a few
 * seconds, and an object is reused only after its shot is freed, so this covers the shots that can
 * still be flying; one that fell out of the table is taken as this player's, which is how every
 * shot was taken before the table existed. */
#define MP_HIT_SHOT_OWNERS 64u

/* A far shot with no player behind it. A copy of one of the host's actors' bolts is not
 * this machine's own and not a player's either, and naming a bank for it would charge a
 * death to whoever happened to be standing there. */
#define MP_HIT_SHOT_NO_BANK 0xFFu

typedef struct mp_hit_shot_owners {
    uint32_t object[MP_HIT_SHOT_OWNERS];
    uint8_t  whose[MP_HIT_SHOT_OWNERS];   /* this player's, a far player's or an ally's */
    uint8_t  bank[MP_HIT_SHOT_OWNERS];    /* which far player, or MP_HIT_SHOT_NO_BANK */
    uint32_t next;
} mp_hit_shot_owners_t;

typedef enum mp_hit_attacker {
    MP_HIT_ATTACKER_OWN_BODY = 0,   /* this machine's player's body with its blade armed */
    MP_HIT_ATTACKER_OWN_BUMP,       /* the same body with nothing armed: a push, no damage */
    MP_HIT_ATTACKER_OWN_SHOT,       /* a player side shot this machine's player fired */
    MP_HIT_ATTACKER_FAR_SHOT,       /* a player side shot a far player's puppet fired here */
    MP_HIT_ATTACKER_NPC_BOLT,       /* a copy of a bolt one of the host's actors fired */
    MP_HIT_ATTACKER_OTHER,          /* an actor, a puppet's body, an effect object, nothing */
    MP_HIT_ATTACKER_COUNT
} mp_hit_attacker_t;

/* Remembers who fired the shot flying as `object`. A second note for the same object replaces the
 * first, which is what an object reused by a new shot needs. Object 0 is ignored. */
void mp_hit_shot_owners_note(mp_hit_shot_owners_t *owners, uint32_t object, bool far);

/* Whether the table says a far player's puppet fired the shot flying as `object`. */
bool mp_hit_shot_owners_is_far(const mp_hit_shot_owners_t *owners, uint32_t object);

/* The same note with the far player's bank named. Two questions, not one: `is_far` answers
 * whether somebody else fired, the bank answers who, and a death report needs the second. */
void mp_hit_shot_owners_note_far_bank(mp_hit_shot_owners_t *owners, uint32_t object,
                                      uint8_t bank);

/* The bank of the far player who fired the shot flying as `object`. False when the table
 * does not hold it, when it is not a far player's, or when it was noted without a bank;
 * `bank` is left alone then. */
bool mp_hit_shot_owners_far_bank(const mp_hit_shot_owners_t *owners, uint32_t object,
                                 uint8_t *bank);

/* The same for an ally: an actor of the player's side, whose shot carries side 1 as a player's
 * does. Noted like the others, so an object reused by a player's shot is that player's again. */
void mp_hit_shot_owners_note_ally(mp_hit_shot_owners_t *owners, uint32_t object);
bool mp_hit_shot_owners_is_ally(const mp_hit_shot_owners_t *owners, uint32_t object);

/* The sender of one contact. `own_body` is this machine's player body, 0 when unknown;
 * `sender_is_shot` says the sender's owner is a shot record that names it back; `side` is the
 * sender's shooter side (1 is a player); `npc_bolt` says the sender is a copy of a host actor's
 * bolt; `far_shot` is what the owners table answered for the sender; `armed` says the sender's
 * contact node is set, which is a swing in progress. */
mp_hit_attacker_t mp_hit_attacker_of(uint32_t self, uint32_t own_body, bool sender_is_shot,
                                     uint32_t side, bool npc_bolt, bool far_shot, bool armed);

/* Whether a contact from this sender is a hit this client reports. */
bool mp_hit_attacker_is_own(mp_hit_attacker_t attacker);

#endif /* MULTIPLAYER_MP_HIT_ATTACKER_H */
