/* mp_hero_carry_rule.c: what a player takes along when the hero under him changes. See the
 * header. */
#include "mp_hero_carry_rule.h"

#include "mp_bank_keep.h"

#include <string.h>

_Static_assert(MP_HERO_CARRY_RECORD_BYTES == MP_BANK_KEEP_RECORD_BYTES,
               "the carry and the far spawn's window must stride the same records");
_Static_assert(MP_HERO_CARRY_INVENTORY_BYTES == MP_BANK_KEEP_FLAG_BYTES,
               "the carry and the far spawn's window must move the same six bytes");
_Static_assert(MP_HERO_CARRY_AMMO + MP_HERO_CARRY_AMMO_SLOTS * 4u == MP_HERO_CARRY_INVENTORY,
               "the thirteen counts end where the six bytes begin");
_Static_assert(MP_HERO_CARRY_INVENTORY + MP_HERO_CARRY_INVENTORY_BYTES + 2u ==
                   MP_HERO_CARRY_RECORD_BYTES,
               "two pad bytes close the record");

/* After a new game the engine clears the index to -1 and the pointer to 0 together. */
#define NO_CURRENT_PLAYER 0xFFFFFFFFu

/* The general weapons: blaster 2, gatling 3, bazooka 4, thermal detonator 5, gungan ball 6,
 * grenade 7, bubble 8 and the heavy blaster 11. Every hero model carries their nodes. */
static const uint8_t carried_weapons[MP_HERO_CARRY_AMMO_CARRIED] = { 2, 3, 4, 5, 6, 7, 8, 11 };

static int32_t read_i32(const uint8_t *record, uint32_t offset)
{
    int32_t value;

    memcpy(&value, record + offset, sizeof value);
    return value;
}

static void write_i32(uint8_t *record, uint32_t offset, int32_t value)
{
    memcpy(record + offset, &value, sizeof value);
}

static uint32_t ammo_offset(uint32_t weapon)
{
    return MP_HERO_CARRY_AMMO + weapon * (uint32_t)sizeof(int32_t);
}

mp_hero_carry_verdict_t mp_hero_carry_rule_judge(const mp_hero_carry_facts_t *facts)
{
    if (facts == NULL || facts->hero_index < 0 || facts->hero_index >= MP_HERO_CARRY_HEROES) {
        return MP_HERO_CARRY_REFUSED;
    }
    if (facts->current_player == NO_CURRENT_PLAYER && facts->status_pointer == 0u) {
        return MP_HERO_CARRY_NO_HERO;
    }
    if (facts->current_player >= (uint32_t)MP_HERO_CARRY_HEROES) {
        return MP_HERO_CARRY_REFUSED;
    }
    if (facts->current_player == (uint32_t)facts->hero_index) {
        return MP_HERO_CARRY_SAME_HERO;
    }
    /* The swap stores the live bytes into the record the pointer names. A pointer that is not the
     * current hero's would make the carry read one record while the swap wrote another. */
    if (!mp_bank_keep_precondition(facts->status_pointer, facts->record_base,
                                   facts->current_player, (int32_t)facts->current_player)) {
        return MP_HERO_CARRY_REFUSED;
    }
    return MP_HERO_CARRY_CARRY;
}

bool mp_hero_carry_rule_carry(const uint8_t outgoing[MP_HERO_CARRY_RECORD_BYTES],
                              const uint8_t live_window[MP_HERO_CARRY_INVENTORY_BYTES],
                              uint8_t incoming[MP_HERO_CARRY_RECORD_BYTES],
                              mp_hero_carry_outcome_t *out)
{
    uint8_t before[MP_HERO_CARRY_RECORD_BYTES];
    int32_t health;
    size_t  at;

    if (outgoing == NULL || live_window == NULL || incoming == NULL || out == NULL) {
        return false;
    }
    memset(out, 0, sizeof *out);
    memcpy(before, incoming, sizeof before);

    memcpy(incoming + MP_HERO_CARRY_INVENTORY, live_window, MP_HERO_CARRY_INVENTORY_BYTES);

    health = read_i32(outgoing, MP_HERO_CARRY_HEALTH);
    if (health > 0) {
        write_i32(incoming, MP_HERO_CARRY_HEALTH, health);
        out->health_carried = true;
    }

    for (at = 0; at < sizeof carried_weapons; ++at) {
        uint32_t offset = ammo_offset(carried_weapons[at]);
        int32_t  count  = read_i32(outgoing, offset);

        write_i32(incoming, offset, count);
        if (count != 0) {
            ++out->ammo_holding;
        }
    }

    out->health = read_i32(incoming, MP_HERO_CARRY_HEALTH);
    memcpy(out->inventory, incoming + MP_HERO_CARRY_INVENTORY, MP_HERO_CARRY_INVENTORY_BYTES);
    for (at = 0; at < sizeof before; ++at) {
        if (before[at] != incoming[at]) {
            ++out->bytes_changed;
        }
    }
    return true;
}
