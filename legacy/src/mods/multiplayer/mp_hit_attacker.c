/* mp_hit_attacker.c: whose contact a client may report to the host as its own hit. */
#include "mp_hit_attacker.h"

#include <stddef.h>

/* The side a player's shot carries; an actor's bolts carry their own class, above 1, and the
 * engine's effect objects carry 0. */
#define PLAYER_SIDE 1u

/* Who fired a shot the table holds. */
#define WHOSE_OWN  0u
#define WHOSE_FAR  1u
#define WHOSE_ALLY 2u

static void note_whose(mp_hit_shot_owners_t *owners, uint32_t object, uint8_t whose,
                       uint8_t bank)
{
    uint32_t i;

    if (owners == NULL || object == 0u) {
        return;
    }
    for (i = 0; i < MP_HIT_SHOT_OWNERS; ++i) {
        if (owners->object[i] == object) {
            owners->whose[i] = whose;
            owners->bank[i]  = bank;
            return;
        }
    }
    i = owners->next % MP_HIT_SHOT_OWNERS;
    owners->object[i] = object;
    owners->whose[i]  = whose;
    owners->bank[i]   = bank;
    owners->next      = (i + 1u) % MP_HIT_SHOT_OWNERS;
}

static bool is_whose(const mp_hit_shot_owners_t *owners, uint32_t object, uint8_t whose)
{
    uint32_t i;

    if (owners == NULL || object == 0u) {
        return false;
    }
    for (i = 0; i < MP_HIT_SHOT_OWNERS; ++i) {
        if (owners->object[i] == object) {
            return owners->whose[i] == whose;
        }
    }
    return false;
}

void mp_hit_shot_owners_note(mp_hit_shot_owners_t *owners, uint32_t object, bool far)
{
    note_whose(owners, object, far ? (uint8_t)WHOSE_FAR : (uint8_t)WHOSE_OWN,
               (uint8_t)MP_HIT_SHOT_NO_BANK);
}

void mp_hit_shot_owners_note_far_bank(mp_hit_shot_owners_t *owners, uint32_t object,
                                      uint8_t bank)
{
    note_whose(owners, object, (uint8_t)WHOSE_FAR, bank);
}

bool mp_hit_shot_owners_far_bank(const mp_hit_shot_owners_t *owners, uint32_t object,
                                 uint8_t *bank)
{
    uint32_t i;

    if (owners == NULL || object == 0u) {
        return false;
    }
    for (i = 0; i < MP_HIT_SHOT_OWNERS; ++i) {
        if (owners->object[i] != object) {
            continue;
        }
        if (owners->whose[i] != (uint8_t)WHOSE_FAR ||
            owners->bank[i] == (uint8_t)MP_HIT_SHOT_NO_BANK) {
            return false;
        }
        if (bank != NULL) {
            *bank = owners->bank[i];
        }
        return true;
    }
    return false;
}

bool mp_hit_shot_owners_is_far(const mp_hit_shot_owners_t *owners, uint32_t object)
{
    return is_whose(owners, object, (uint8_t)WHOSE_FAR);
}

void mp_hit_shot_owners_note_ally(mp_hit_shot_owners_t *owners, uint32_t object)
{
    note_whose(owners, object, (uint8_t)WHOSE_ALLY, (uint8_t)MP_HIT_SHOT_NO_BANK);
}

bool mp_hit_shot_owners_is_ally(const mp_hit_shot_owners_t *owners, uint32_t object)
{
    return is_whose(owners, object, (uint8_t)WHOSE_ALLY);
}

mp_hit_attacker_t mp_hit_attacker_of(uint32_t self, uint32_t own_body, bool sender_is_shot,
                                     uint32_t side, bool npc_bolt, bool far_shot, bool armed)
{
    if (self == 0u) {
        return MP_HIT_ATTACKER_OTHER;
    }
    /* The handler refuses damage from a body whose contact node is zero on the arms a body can
     * reach, so an unarmed touch is a push the far side's puppet makes on the host by itself. */
    if (own_body != 0u && self == own_body) {
        return armed ? MP_HIT_ATTACKER_OWN_BODY : MP_HIT_ATTACKER_OWN_BUMP;
    }
    if (npc_bolt) {
        return MP_HIT_ATTACKER_NPC_BOLT;
    }
    if (sender_is_shot && side == PLAYER_SIDE) {
        return far_shot ? MP_HIT_ATTACKER_FAR_SHOT : MP_HIT_ATTACKER_OWN_SHOT;
    }
    return MP_HIT_ATTACKER_OTHER;
}

bool mp_hit_attacker_is_own(mp_hit_attacker_t attacker)
{
    return attacker == MP_HIT_ATTACKER_OWN_BODY || attacker == MP_HIT_ATTACKER_OWN_SHOT;
}
