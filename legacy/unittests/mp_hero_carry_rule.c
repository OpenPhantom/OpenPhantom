/* mp_hero_carry_rule.c: what a player takes along when the hero under him changes in a session.
 *
 * The hull the rule runs in cannot be driven here: its cells resolve to nothing without the game.
 * So the engine's two writers of the hero records are modelled byte for byte from the retail
 * image, status_setActivePlayer at 0x00459AB7 and the starting kit the spawn writes after it at
 * 0x00448046 to 0x004480BB, and the rule runs over the model in the order the hull runs it: the
 * carry before the original, then the original's swap and kit.
 *
 * The same model run without the carry is the old behaviour, and it is kept here as the
 * reference: it loads the incoming hero's old six bytes into the bank, which is the quest bit a
 * client claimed away from everybody in the field. A green result means the carry removes exactly
 * that and nothing the hero owns.
 *
 * The record is written out as the engine declares it, field by field, rather than taken from the
 * rule's own offsets, so a wrong offset in the rule shows up as a failed layout check instead of
 * as a test that agrees with itself.
 */
#include "unittest.h"

#include "mp_bank_keep.h"
#include "mp_hero_carry_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The engine's playerStatus, 0x4C bytes. */
typedef struct engine_record {
    int32_t health;
    int32_t force;
    int32_t cur_weapon;
    int32_t start_weapon;
    int32_t ammo[13];
    uint8_t inventory[6];
    uint8_t pad[2];
} engine_record_t;

_Static_assert(sizeof(engine_record_t) == 0x4C, "the engine's hero record is 0x4C bytes");

enum { OBI_WAN = 0, QUI_GON = 1, PANAKA = 2, QUEEN = 3 };

/* Any stride-aligned value will do; only the distance between records is the engine's. */
#define RECORD_BASE 0x00010000u
#define NO_POINTER  (-1)

typedef struct engine {
    engine_record_t records[4];
    uint8_t         bank[6];     /* the campaign bank's bytes 6 to 11 */
    int32_t         current;     /* -1 after a new game */
    int             pointer;     /* a record index, or NO_POINTER for NULL */
} engine_t;

/* The starting kit at 0x004B51C0: the start weapon and its count per hero. */
static const int32_t kit_weapon[4] = { 1, 1, 10, 9 };
static const int32_t kit_count[4]  = { 1, 1, 200, 1 };

static uint8_t *bytes_of(engine_record_t *record)
{
    return (uint8_t *)record;
}

static uint32_t pointer_value(const engine_t *e)
{
    return e->pointer == NO_POINTER
               ? 0u
               : RECORD_BASE + (uint32_t)e->pointer * MP_HERO_CARRY_RECORD_BYTES;
}

/* 0x00459AB7: the current player first, then the live six bytes stored into the record the
 * pointer names (when there is one), the pointer moved to the new hero's record, and its six
 * bytes loaded into the bank. */
static void engine_set_active(engine_t *e, int hero)
{
    e->current = hero;
    if (e->pointer != NO_POINTER) {
        memcpy(e->records[e->pointer].inventory, e->bank, sizeof e->bank);
    }
    e->pointer = hero;
    memcpy(e->bank, e->records[hero].inventory, sizeof e->bank);
}

/* 0x00447E58 from the set_active on: the swap, then the kit into the record the pointer names. */
static void engine_spawn(engine_t *e, int hero)
{
    engine_set_active(e, hero);
    e->records[hero].ammo[kit_weapon[hero]] = kit_count[hero];
    e->records[hero].start_weapon           = kit_weapon[hero];
}

static mp_hero_carry_facts_t facts_of(const engine_t *e, int hero)
{
    mp_hero_carry_facts_t facts;

    facts.current_player = (uint32_t)e->current;
    facts.status_pointer = pointer_value(e);
    facts.record_base    = RECORD_BASE;
    facts.hero_index     = hero;
    return facts;
}

/* The hull: the carry before the original, the original after it. */
static mp_hero_carry_verdict_t spawn_with_carry(engine_t *e, int hero,
                                                mp_hero_carry_outcome_t *outcome)
{
    mp_hero_carry_facts_t   facts   = facts_of(e, hero);
    mp_hero_carry_verdict_t verdict = mp_hero_carry_rule_judge(&facts);

    memset(outcome, 0, sizeof *outcome);
    if (verdict == MP_HERO_CARRY_CARRY) {
        (void)mp_hero_carry_rule_carry(bytes_of(&e->records[e->current]), e->bank,
                                       bytes_of(&e->records[hero]), outcome);
    }
    engine_spawn(e, hero);
    return verdict;
}

/* A level running as Obi-Wan: the engine's level start gave everybody 100, the player picked up
 * a blaster, a thermal detonator and a heavy blaster, and holds quest bit 69 (byte 8 of the
 * bank, bit 5). Panaka's record still holds what the level start left there; the Queen's holds a
 * stale bit 59 from a savegame (byte 7 of the bank, bit 3). */
static void engine_in_a_level(engine_t *e)
{
    int hero;

    memset(e, 0, sizeof *e);
    for (hero = 0; hero < 4; ++hero) {
        e->records[hero].health     = 100;
        e->records[hero].cur_weapon = 0x5A5A5A5A;
        e->records[hero].pad[0]     = 0xC1;
        e->records[hero].pad[1]     = 0xC2;
    }
    e->records[OBI_WAN].force = 100;
    e->records[QUI_GON].force = 100;
    e->current = NO_POINTER;
    e->pointer = NO_POINTER;
    engine_spawn(e, OBI_WAN);

    e->records[OBI_WAN].health    = 63;
    e->records[OBI_WAN].ammo[0]   = 7;
    e->records[OBI_WAN].ammo[2]   = 250;
    e->records[OBI_WAN].ammo[5]   = 1;
    e->records[OBI_WAN].ammo[11]  = 100;
    e->records[OBI_WAN].ammo[12]  = 3;
    e->records[PANAKA].ammo[1]    = 0;
    e->records[PANAKA].ammo[9]    = 4;
    e->records[PANAKA].ammo[10]   = 150;
    e->records[PANAKA].ammo[12]   = 9;
    e->records[PANAKA].force      = 11;
    e->records[QUEEN].inventory[1] = 0x08;
    e->bank[2] = 0x20;
}

static void check_layout(void)
{
    engine_record_t         outgoing;
    engine_record_t         incoming;
    uint8_t                 window[6] = { 0 };
    mp_hero_carry_outcome_t outcome;
    uint32_t                weapon;
    uint32_t                carried = 0u;

    ut_section("the record, as the engine declares it");
    ut_check(MP_HERO_CARRY_RECORD_BYTES == sizeof(engine_record_t),
             "the record is 0x4C bytes");
    ut_check(MP_HERO_CARRY_RECORD_BYTES == MP_BANK_KEEP_RECORD_BYTES,
             "the carry and the far spawn's window stride the records alike");
    ut_check(MP_HERO_CARRY_INVENTORY_BYTES == MP_BANK_KEEP_FLAG_BYTES,
             "the carry and the window move the same six bytes");
    ut_check(MP_HERO_CARRY_HEALTH == offsetof(engine_record_t, health), "health is at +0x00");
    ut_check(MP_HERO_CARRY_AMMO == offsetof(engine_record_t, ammo), "ammo[] is at +0x10");
    ut_check(MP_HERO_CARRY_AMMO_SLOTS == sizeof(((engine_record_t *)0)->ammo) / sizeof(int32_t),
             "there are thirteen ammunition counts");
    ut_check(MP_HERO_CARRY_INVENTORY == offsetof(engine_record_t, inventory),
             "the six bytes are at +0x44");

    /* Every count distinct on both sides, so a slot that travels shows as an equal pair. */
    memset(&outgoing, 0, sizeof outgoing);
    memset(&incoming, 0, sizeof incoming);
    memset(&outcome, 0, sizeof outcome);
    for (weapon = 0u; weapon < MP_HERO_CARRY_AMMO_SLOTS; ++weapon) {
        outgoing.ammo[weapon] = 1000 + (int32_t)weapon;
        incoming.ammo[weapon] = 2000 + (int32_t)weapon;
    }
    (void)mp_hero_carry_rule_carry(bytes_of(&outgoing), window, bytes_of(&incoming), &outcome);
    for (weapon = 0u; weapon < MP_HERO_CARRY_AMMO_SLOTS; ++weapon) {
        bool expected = (weapon >= 2u && weapon <= 8u) || weapon == 11u;
        bool took     = incoming.ammo[weapon] == outgoing.ammo[weapon];

        ut_checkf(took == expected, "weapon %u is %s", (unsigned)weapon,
                  expected ? "carried" : "the hero's own");
        if (took) {
            ++carried;
        }
    }
    ut_check(carried == MP_HERO_CARRY_AMMO_CARRIED && outcome.ammo_holding == carried,
             "eight slots travel, and all eight held something");
}

static void check_the_defect_and_its_repair(void)
{
    engine_t                e;
    mp_hero_carry_outcome_t outcome;
    uint8_t                 before[6];

    ut_section("the reference: the change of hero as the engine makes it, with no carry");
    engine_in_a_level(&e);
    memcpy(before, e.bank, sizeof before);
    engine_spawn(&e, PANAKA);
    ut_check(memcmp(before, e.bank, sizeof before) != 0,
             "without the carry the swap loads Panaka's old six bytes into the bank");
    ut_check(e.bank[2] == 0x00, "and quest bit 69 is gone, which the relay then claims");
    ut_check(e.records[PANAKA].ammo[2] == 0 && e.records[PANAKA].health == 100,
             "and the blaster and the health stay behind with Obi-Wan");

    ut_section("the same change with the carry before the spawn");
    engine_in_a_level(&e);
    memcpy(before, e.bank, sizeof before);
    ut_check(spawn_with_carry(&e, PANAKA, &outcome) == MP_HERO_CARRY_CARRY,
             "Obi-Wan to Panaka is carried");
    ut_check(memcmp(before, e.bank, sizeof before) == 0,
             "the bank is what it was: the swap loads the bytes that are already there");
    ut_check(memcmp(e.records[OBI_WAN].inventory, before, sizeof before) == 0,
             "and the outgoing record holds the same bytes, stored by the swap");
    ut_check(e.current == PANAKA && e.pointer == PANAKA, "the engine's own swap still ran");
}

static void check_obi_wan_to_panaka(void)
{
    engine_t                e;
    engine_record_t         panaka_before;
    mp_hero_carry_outcome_t outcome;
    int                     weapon;
    bool                    carried_all = true;

    ut_section("Obi-Wan to Panaka, field by field");
    engine_in_a_level(&e);
    panaka_before = e.records[PANAKA];
    (void)spawn_with_carry(&e, PANAKA, &outcome);

    ut_check(e.records[PANAKA].health == 63, "the health arrives");
    for (weapon = 2; weapon <= 8; ++weapon) {
        carried_all = carried_all && e.records[PANAKA].ammo[weapon] ==
                                         e.records[OBI_WAN].ammo[weapon];
    }
    ut_check(carried_all && e.records[PANAKA].ammo[11] == 100,
             "ammo[2..8] and ammo[11] arrive, the blaster's 250 and the heavy blaster's 100 "
             "among them");
    ut_check(e.records[PANAKA].inventory[2] == 0x20, "the six bytes arrive in the record");
    ut_check(e.records[PANAKA].ammo[1] == 0, "Panaka gets no saber count");
    ut_check(e.records[PANAKA].ammo[9] == 4, "Panaka's zap gun count stays his");
    ut_check(e.records[PANAKA].ammo[10] == 200,
             "Panaka's pistol is his own, and the spawn's kit writes it as it always did");
    ut_check(e.records[PANAKA].force == panaka_before.force, "the force stays the hero's");
    ut_check(e.records[PANAKA].cur_weapon == panaka_before.cur_weapon,
             "the weapon in hand at +0x08 is not written");
    ut_check(e.records[PANAKA].start_weapon == 10, "the start weapon is the spawn's to write");
    ut_check(e.records[PANAKA].ammo[0] == 0 && e.records[PANAKA].ammo[12] == 9,
             "ammo[0] and ammo[12] are not carried");
    ut_check(e.records[PANAKA].pad[0] == 0xC1 && e.records[PANAKA].pad[1] == 0xC2,
             "the two pad bytes are not touched");

    ut_check(outcome.health_carried && outcome.health == 63, "the outcome names the health");
    ut_check(outcome.ammo_holding == 3u,
             "the outcome counts the carried slots that hold something: the blaster, the "
             "detonator and the heavy blaster, not the empty hand or slot 12");
    ut_check(outcome.inventory[2] == 0x20, "the outcome names the six bytes it wrote");
    ut_check(outcome.bytes_changed > 0u, "the outcome says the record changed");
}

static void check_heroes_keep_their_own(void)
{
    engine_t                e;
    mp_hero_carry_outcome_t outcome;
    uint8_t                 image[MP_HERO_CARRY_RECORD_BYTES];
    int32_t                 saber = -1;

    ut_section("the hero's own weapons");
    engine_in_a_level(&e);
    (void)spawn_with_carry(&e, PANAKA, &outcome);
    e.records[PANAKA].ammo[10] = 37;
    e.records[QUEEN].ammo[9]   = 0;
    ut_check(spawn_with_carry(&e, QUEEN, &outcome) == MP_HERO_CARRY_CARRY,
             "Panaka to the Queen is carried");
    ut_check(e.records[QUEEN].ammo[10] == 0, "Panaka's pistol does not go to the Queen");
    ut_check(e.records[QUEEN].ammo[9] == 1, "the Queen's zap gun is hers, from her kit");
    ut_check(e.records[QUEEN].ammo[2] == 250, "the blaster Panaka carried goes on to her");
    ut_check(e.records[QUEEN].inventory[1] == 0x00,
             "her record's stale bit 59 is overwritten by the live bytes, not loaded");

    engine_in_a_level(&e);
    e.records[OBI_WAN].ammo[1] = 5;
    e.records[QUI_GON].ammo[1] = 1;
    memcpy(image, bytes_of(&e.records[QUI_GON]), sizeof image);
    (void)mp_hero_carry_rule_carry(bytes_of(&e.records[OBI_WAN]), e.bank, image, &outcome);
    memcpy(&saber, image + MP_HERO_CARRY_AMMO + 1u * sizeof(int32_t), sizeof saber);
    ut_check(saber == 1, "Obi-Wan to Qui-Gon: the saber count is the target's own");
    (void)spawn_with_carry(&e, PANAKA, &outcome);
    ut_check(e.records[PANAKA].ammo[1] == 0, "and a Jedi's saber never reaches Panaka");
}

static void check_verdicts(void)
{
    engine_t              e;
    mp_hero_carry_facts_t facts;

    ut_section("the verdicts");
    engine_in_a_level(&e);
    facts = facts_of(&e, OBI_WAN);
    ut_check(mp_hero_carry_rule_judge(&facts) == MP_HERO_CARRY_SAME_HERO,
             "the same hero (the re-entry after a death) carries nothing");
    facts = facts_of(&e, QUEEN);
    ut_check(mp_hero_carry_rule_judge(&facts) == MP_HERO_CARRY_CARRY, "another hero is carried");

    facts.current_player = 0xFFFFFFFFu;
    facts.status_pointer = 0u;
    ut_check(mp_hero_carry_rule_judge(&facts) == MP_HERO_CARRY_NO_HERO,
             "after a new game (-1 and a null pointer) there is no hero to carry from");

    facts = facts_of(&e, 4);
    ut_check(mp_hero_carry_rule_judge(&facts) == MP_HERO_CARRY_REFUSED,
             "an incoming hero of 4 is refused, the limit of the setter's assert");
    facts = facts_of(&e, -1);
    ut_check(mp_hero_carry_rule_judge(&facts) == MP_HERO_CARRY_REFUSED,
             "a negative incoming hero is refused");
    facts = facts_of(&e, PANAKA);
    facts.current_player = 4u;
    facts.status_pointer = RECORD_BASE + 4u * MP_HERO_CARRY_RECORD_BYTES;
    ut_check(mp_hero_carry_rule_judge(&facts) == MP_HERO_CARRY_REFUSED,
             "an outgoing hero of 4 is refused");
    facts = facts_of(&e, PANAKA);
    facts.status_pointer = RECORD_BASE + 3u * MP_HERO_CARRY_RECORD_BYTES;
    ut_check(mp_hero_carry_rule_judge(&facts) == MP_HERO_CARRY_REFUSED,
             "a pointer on another hero's record is refused, as the far spawn's window "
             "refuses it");
    facts = facts_of(&e, PANAKA);
    facts.status_pointer = 0u;
    ut_check(mp_hero_carry_rule_judge(&facts) == MP_HERO_CARRY_REFUSED,
             "a hero index with a null pointer is refused, not taken for a new game");
    facts = facts_of(&e, PANAKA);
    facts.current_player = 0xFFFFFFFFu;
    ut_check(mp_hero_carry_rule_judge(&facts) == MP_HERO_CARRY_REFUSED,
             "-1 with a pointer set is refused");
    facts = facts_of(&e, PANAKA);
    facts.record_base = 0u;
    ut_check(mp_hero_carry_rule_judge(&facts) == MP_HERO_CARRY_REFUSED,
             "an unresolved record base is refused");
    ut_check(mp_hero_carry_rule_judge(NULL) == MP_HERO_CARRY_REFUSED, "no facts, refused");
}

static void check_health_of_a_dead_hero(void)
{
    engine_t                e;
    mp_hero_carry_outcome_t outcome;

    ut_section("the health of a hero who has none");
    engine_in_a_level(&e);
    e.records[OBI_WAN].health = 0;
    e.records[QUEEN].health   = 100;
    (void)spawn_with_carry(&e, QUEEN, &outcome);
    ut_check(e.records[QUEEN].health == 100,
             "an outgoing hero at 0 leaves the incoming record its own health, since the death "
             "check would fire on the next substep");
    ut_check(!outcome.health_carried && outcome.health == 100,
             "and the outcome says the health was left");
    ut_check(e.records[QUEEN].ammo[2] == 250 && e.bank[2] == 0x20,
             "the rest is carried all the same");

    engine_in_a_level(&e);
    e.records[OBI_WAN].health = -12;
    e.records[PANAKA].health  = 55;
    (void)spawn_with_carry(&e, PANAKA, &outcome);
    ut_check(e.records[PANAKA].health == 55 && !outcome.health_carried,
             "a negative health is not carried either");

    engine_in_a_level(&e);
    e.records[OBI_WAN].health = 1;
    (void)spawn_with_carry(&e, PANAKA, &outcome);
    ut_check(e.records[PANAKA].health == 1 && outcome.health_carried,
             "a health of 1 is carried");
}

static void check_round_trip(void)
{
    engine_t                e;
    mp_hero_carry_outcome_t outcome;
    uint8_t                 bank[6];

    ut_section("there and back, as a change of world makes it");
    engine_in_a_level(&e);
    (void)spawn_with_carry(&e, PANAKA, &outcome);
    e.records[PANAKA].ammo[10] = 77;
    e.records[PANAKA].ammo[3]  = 300;
    e.records[PANAKA].health   = 41;
    memcpy(bank, e.bank, sizeof bank);

    ut_check(spawn_with_carry(&e, OBI_WAN, &outcome) == MP_HERO_CARRY_CARRY,
             "the level's hero at the level's begin, 2 to 0");
    ut_check(spawn_with_carry(&e, PANAKA, &outcome) == MP_HERO_CARRY_CARRY,
             "and the player's choice after it, 0 to 2");
    ut_check(e.records[PANAKA].ammo[3] == 300 && e.records[PANAKA].health == 41,
             "what was carried arrives back unchanged");
    ut_check(memcmp(bank, e.bank, sizeof bank) == 0, "the bank never moved");
    ut_check(e.records[PANAKA].ammo[10] == 200,
             "Panaka's pistol is there after the detour, at the count his kit writes on every "
             "spawn of his");
    ut_check(e.records[OBI_WAN].ammo[10] == 0, "and it never reached Obi-Wan");
}

static void check_live_window(void)
{
    engine_t                e;
    mp_hero_carry_outcome_t outcome;

    ut_section("the six bytes come from the live bank");
    engine_in_a_level(&e);
    e.records[OBI_WAN].inventory[0] = 0xEE;     /* stale: what the last swap stored */
    e.bank[0]                       = 0x01;     /* live: a key picked up since */
    (void)spawn_with_carry(&e, PANAKA, &outcome);
    ut_check(e.bank[0] == 0x01, "the key picked up since the last swap is still in the bank");
    ut_check(e.records[PANAKA].inventory[0] == 0x01,
             "the incoming record got the live byte, not the outgoing record's stale one");
    ut_check(outcome.inventory[0] == 0x01, "and the outcome names the live byte");
}

static void check_repeats_and_nulls(void)
{
    engine_t                e;
    mp_hero_carry_outcome_t outcome;
    uint8_t                 image[MP_HERO_CARRY_RECORD_BYTES];

    ut_section("a carry into a record that already holds it, and null arguments");
    engine_in_a_level(&e);
    memcpy(image, bytes_of(&e.records[PANAKA]), sizeof image);
    ut_check(mp_hero_carry_rule_carry(bytes_of(&e.records[OBI_WAN]), e.bank, image, &outcome),
             "the first carry is made");
    ut_check(mp_hero_carry_rule_carry(bytes_of(&e.records[OBI_WAN]), e.bank, image, &outcome) &&
                 outcome.bytes_changed == 0u,
             "the second changes no byte");
    ut_check(!mp_hero_carry_rule_carry(NULL, e.bank, image, &outcome) &&
                 !mp_hero_carry_rule_carry(image, NULL, image, &outcome) &&
                 !mp_hero_carry_rule_carry(image, e.bank, NULL, &outcome) &&
                 !mp_hero_carry_rule_carry(image, e.bank, image, NULL),
             "a null argument is refused");
}

int main(void)
{
    check_layout();
    check_the_defect_and_its_repair();
    check_obi_wan_to_panaka();
    check_heroes_keep_their_own();
    check_verdicts();
    check_health_of_a_dead_hero();
    check_round_trip();
    check_live_window();
    check_repeats_and_nulls();
    return ut_summary("mp_hero_carry_rule");
}
