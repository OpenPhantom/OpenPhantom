/* mp_hero_carry_rule.h: what a player takes along when the hero under him changes in a session.
 *
 * Layer 1, pure. It works on byte images of the engine's hero records and knows no address.
 *
 * The engine keeps one record of 0x4C bytes per hero: health, force, the weapon in hand, the start
 * weapon, thirteen ammunition counts indexed by weapon id, and six bytes that are the campaign
 * bank's bytes 6 to 11 (bits 48 to 95: quest items 51 to 74, keys 75 to 81, script keys 82 to 84).
 * status_setActivePlayer swaps those six bytes on every change of hero: the live ones go into the
 * outgoing record, the incoming record's go into the bank. In a session the band 51 to 84 is the
 * host's story for every player, so that swap was a second writer of it: a client that changed
 * hero claimed the incoming hero's old bits as its own change, and the host took the claim for
 * everybody.
 *
 * The rule is "possessions belong to the player, properties to the hero". Before the engine's
 * spawn runs, the incoming record is written from what the player has now:
 *
 *   carried   the six bytes, out of the LIVE bank rather than the outgoing record, which holds only
 *             what the last swap left there; the health, when the outgoing hero has any; and the
 *             ammunition of the general weapons 2 to 8 and 11, which every hero model can hold;
 *   kept      the saber (1), the Queen's zap gun (9) and Panaka's pistol (10), which are a hero's
 *             own; force, the weapon in hand and the start weapon, which the spawn writes itself;
 *             ammo[0], the empty hand, and ammo[12], which has no weapon row; the two pad bytes.
 *
 * The saber is kept for a hard reason: a hero without a blade node that reaches weapon 1 through
 * "next weapon" runs into the blade setter's assert. A health of 0 or less is not carried because
 * the spawn does not write health and the player's death check fires on the next substep; the
 * change is then made the way the engine makes it, with the incoming record's own value.
 *
 * With the incoming record written this way, the swap loads into the bank exactly the bytes that
 * are already there, and the story relay is the only writer of the band again.
 */
#ifndef MULTIPLAYER_MP_HERO_CARRY_RULE_H
#define MULTIPLAYER_MP_HERO_CARRY_RULE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The record, as status_setActivePlayer strides it and status_addAmmo and status_setHealth write
 * it. The four heroes are the records the setter's assert allows. */
#define MP_HERO_CARRY_HEROES          4
#define MP_HERO_CARRY_RECORD_BYTES    0x4Cu
#define MP_HERO_CARRY_HEALTH          0x00u
#define MP_HERO_CARRY_AMMO            0x10u
#define MP_HERO_CARRY_AMMO_SLOTS      13u
#define MP_HERO_CARRY_INVENTORY       0x44u
#define MP_HERO_CARRY_INVENTORY_BYTES 6u

/* The ammunition slots that travel with the player: 2 to 8 and 11. */
#define MP_HERO_CARRY_AMMO_CARRIED    8u

/* What the engine held just before the spawn. `current_player` is the index cell and
 * `status_pointer` the record pointer; after a new game they read -1 and 0. */
typedef struct mp_hero_carry_facts {
    uint32_t current_player;
    uint32_t status_pointer;
    uint32_t record_base;       /* the address of hero 0's record */
    int32_t  hero_index;        /* the spawn's argument, the incoming hero */
} mp_hero_carry_facts_t;

typedef enum mp_hero_carry_verdict {
    MP_HERO_CARRY_CARRY,        /* another hero was active and the records can be named */
    MP_HERO_CARRY_SAME_HERO,    /* the re-entry after a death, or a level of the same hero */
    MP_HERO_CARRY_NO_HERO,      /* the first spawn after a new game: nothing to carry from */
    MP_HERO_CARRY_REFUSED       /* an index past the four records, or a pointer on another one */
} mp_hero_carry_verdict_t;

/* What the carry wrote into the incoming record. */
typedef struct mp_hero_carry_outcome {
    int32_t  health;                /* the incoming record's health afterwards */
    bool     health_carried;        /* false when the outgoing hero had none */
    uint32_t ammo_holding;          /* carried slots whose count is not 0 */
    uint32_t bytes_changed;         /* bytes of the incoming record the carry changed */
    uint8_t  inventory[MP_HERO_CARRY_INVENTORY_BYTES];
} mp_hero_carry_outcome_t;

/* Whether a carry is due, and why not when it is not. The pointer is asked the same question as
 * the far spawn's window asks it: does it name the current hero's record. */
mp_hero_carry_verdict_t mp_hero_carry_rule_judge(const mp_hero_carry_facts_t *facts);

/* Writes the carried fields into `incoming`, from the outgoing record and the live six bytes, and
 * leaves every other byte of it as it was. False only for a null argument. */
bool mp_hero_carry_rule_carry(const uint8_t outgoing[MP_HERO_CARRY_RECORD_BYTES],
                              const uint8_t live_window[MP_HERO_CARRY_INVENTORY_BYTES],
                              uint8_t incoming[MP_HERO_CARRY_RECORD_BYTES],
                              mp_hero_carry_outcome_t *out);

#endif /* MULTIPLAYER_MP_HERO_CARRY_RULE_H */
