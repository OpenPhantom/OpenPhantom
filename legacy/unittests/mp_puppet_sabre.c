/* The puppet's sabre decisions, driven over their edges with no game.
 *
 * Each decision here is one the field would answer slowly: a block whose stores miss the latch
 * ends on its first substep, a block that takes over a weapon change's continuation tears the
 * change, a swing row past the table reads behind it, and a fallback that fires early cuts a
 * swing short while one that never fires leaves the puppet's blade armed for good. The numbers
 * are pinned rather than the prose. The guard in front of the engine's starters is driven here
 * over made up bodies, because this test links the module that asks it.
 *
 * SIZE NOTE: one subject, the puppet's sabre and the guard its swing asks, driven over made up
 * bodies and records. The sections are independent and the file is read one section at a time.
 */
#include "unittest.h"

#include "mp_puppet_sabre.h"
#include "mp_puppet_starter.h"

#include "mp_events.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The two continuations as the retail build carries them; the decisions only compare them. */
#define BLOCK_AUX 0x0044BC44u
#define PARRY_AUX 0x0044BCA0u
#define OTHER_AUX 0x0044B5B8u   /* the weapon change's */

static void check_stores(void)
{
    uint32_t block = mp_puppet_sabre_stores(MP_SABRE_BLOCK);
    uint32_t parry = mp_puppet_sabre_stores(MP_SABRE_PARRY);

    ut_section("the stores an action makes");
    ut_check(mp_puppet_sabre_stores(MP_SABRE_SWING) == 0u,
             "a swing makes no store of its own: the engine's starter arms the blade");
    ut_check(mp_puppet_sabre_stores(MP_SABRE_DISARM) == 0u,
             "a disarm makes none: the engine's swing end clears the blade");
    ut_check(block == (MP_PUPPET_SABRE_STORE_NODE | MP_PUPPET_SABRE_STORE_RADIUS |
                       MP_PUPPET_SABRE_STORE_CODE | MP_PUPPET_SABRE_STORE_LATCH),
             "a block stores the node, the radius, the code and clears the latch");
    ut_check((block & MP_PUPPET_SABRE_STORE_ABSORB) == 0u,
             "and leaves the parry's absorb count alone");
    ut_check(parry == (block | MP_PUPPET_SABRE_STORE_ABSORB),
             "a parry makes the block's stores and clears the absorb count as well");
    ut_check((parry & MP_PUPPET_SABRE_STORE_LATCH) != 0u,
             "the latch is cleared for the parry too, although the engine's parry does not: a "
             "latch left at 1 by an earlier deflect would end the parry at once");
    ut_check(mp_puppet_sabre_stores(9u) == 0u, "an unknown action stores nothing");
}

static void check_operands(void)
{
    ut_section("which operands are performed");
    ut_check(mp_puppet_sabre_row_ok(0u), "row 0 is inside the swing table");
    ut_check(mp_puppet_sabre_row_ok(27u), "so is row 27, the last");
    ut_check(!mp_puppet_sabre_row_ok(28u), "row 28 lies behind the table and is refused");
    ut_check(!mp_puppet_sabre_row_ok(255u), "as is a byte's worth");
    ut_check(mp_puppet_sabre_operand_ok(MP_SABRE_SWING, 24u), "a swing names a row");
    ut_check(!mp_puppet_sabre_operand_ok(MP_SABRE_SWING, 28u), "and not one past the table");

    ut_check(mp_puppet_sabre_operand_ok(MP_SABRE_BLOCK, 0x60u), "a block names clip 0x60");
    ut_check(mp_puppet_sabre_operand_ok(MP_SABRE_BLOCK, 0x69u), "up to 0x69");
    ut_check(!mp_puppet_sabre_operand_ok(MP_SABRE_BLOCK, 0x5Fu),
             "a block naming a parry clip is refused: the state path would start it as well");
    ut_check(!mp_puppet_sabre_operand_ok(MP_SABRE_BLOCK, 0x6Au), "and so is 0x6A");

    ut_check(mp_puppet_sabre_operand_ok(MP_SABRE_PARRY, 0x5Du), "a parry names clip 0x5D");
    ut_check(mp_puppet_sabre_operand_ok(MP_SABRE_PARRY, 0x5Fu), "up to 0x5F");
    ut_check(!mp_puppet_sabre_operand_ok(MP_SABRE_PARRY, 0x60u),
             "a parry naming a block clip is refused");
    ut_check(!mp_puppet_sabre_operand_ok(MP_SABRE_PARRY, 0x5Cu), "and so is 0x5C");

    ut_check(mp_puppet_sabre_operand_ok(MP_SABRE_DISARM, 0u), "a disarm carries nothing");
    ut_check(!mp_puppet_sabre_operand_ok(MP_SABRE_DISARM, 1u),
             "a disarm carrying something is not the disarm the sender encodes");
    ut_check(!mp_puppet_sabre_operand_ok(4u, 0u), "an unknown action is refused whole");

    ut_section("which channel a swing plays on, with no table to read");
    ut_check(!mp_puppet_starter_swing_overlay(0u, 0u), "row 0 plays on the base channel");
    ut_check(!mp_puppet_starter_swing_overlay(0u, 23u), "so does row 23");
    ut_check(mp_puppet_starter_swing_overlay(0u, 24u),
             "row 24, the midair attack, plays its clip on the overlay channel");
    ut_check(!mp_puppet_starter_swing_overlay(0u, 25u), "row 25 is back on the base channel");
}

static void check_aux(void)
{
    ut_section("the aux slot: take, take over, or wait");
    ut_check(mp_puppet_sabre_aux_verdict(0u, BLOCK_AUX, PARRY_AUX) == MP_PUPPET_SABRE_AUX_FREE,
             "an empty slot is taken");
    ut_check(mp_puppet_sabre_aux_verdict(BLOCK_AUX, BLOCK_AUX, PARRY_AUX) ==
                 MP_PUPPET_SABRE_AUX_OVERWRITE,
             "a slot holding the deflect's continuation is taken over");
    ut_check(mp_puppet_sabre_aux_verdict(PARRY_AUX, BLOCK_AUX, PARRY_AUX) ==
                 MP_PUPPET_SABRE_AUX_OVERWRITE,
             "and so is one holding the parry's");
    ut_check(mp_puppet_sabre_aux_verdict(OTHER_AUX, BLOCK_AUX, PARRY_AUX) ==
                 MP_PUPPET_SABRE_AUX_WAIT,
             "a weapon change's continuation is waited for");
    ut_check(mp_puppet_sabre_aux_verdict(OTHER_AUX, 0u, 0u) == MP_PUPPET_SABRE_AUX_WAIT,
             "with both continuations unknown every busy slot is waited for");
    ut_check(mp_puppet_sabre_aux_verdict(0u, 0u, 0u) == MP_PUPPET_SABRE_AUX_FREE,
             "and an empty one is still taken");

    ut_section("which continuations the disarm clears");
    ut_check(mp_puppet_sabre_aux_is_ours(BLOCK_AUX, BLOCK_AUX, PARRY_AUX),
             "the deflect's continuation is ours");
    ut_check(mp_puppet_sabre_aux_is_ours(PARRY_AUX, BLOCK_AUX, PARRY_AUX), "so is the parry's");
    ut_check(!mp_puppet_sabre_aux_is_ours(OTHER_AUX, BLOCK_AUX, PARRY_AUX),
             "a weapon change's is not, and the disarm leaves it");
    ut_check(!mp_puppet_sabre_aux_is_ours(0u, 0u, 0u),
             "an empty slot matches nothing even when both continuations are unknown");
    ut_check(!mp_puppet_sabre_aux_is_ours(0u, BLOCK_AUX, PARRY_AUX), "an empty slot is nobody's");
}

static void check_fallback(void)
{
    ut_section("the fallback disarm");
    ut_check(!mp_puppet_sabre_fallback_due(false, false, 0u),
             "a fresh swing whose track plays is left alone");
    ut_check(!mp_puppet_sabre_fallback_due(false, false, 47u),
             "and still at 47 substeps, inside the longest swing clip");
    ut_check(mp_puppet_sabre_fallback_due(false, false, 48u),
             "at 48 substeps, a second and a half, it is disarmed whatever the track says");
    ut_check(mp_puppet_sabre_fallback_due(false, true, 1u),
             "a track that completed is disarmed at once");
    ut_check(mp_puppet_sabre_fallback_due(true, false, 1u),
             "and so is one whose track is gone: the clip was replaced or retired");
    ut_check(MP_PUPPET_SABRE_FALLBACK_SUBSTEPS == 48u,
             "the fallback span is 48 substeps: 1.5 seconds at 32 a second");
    ut_check(MP_PUPPET_SABRE_AUX_WAIT_LIMIT == 64u,
             "a block waits two seconds for the aux slot, as a push does");

    /* Through the engine half over a zeroed record and a zeroed object: with no overlay player
     * resolved the block's clip does not play, and the blade is armed with only the timer,
     * which is the state a peer change has to handle. Nothing here resolves, so the tick that
     * finds the fallback due drops the latch without a swing end to call. */
    ut_section("a blade armed across a reset");
    {
        static uint8_t record[0x3AC];
        static uint8_t object[0x100];
        mp_event_t     event;
        bool           aux_taken = false;

        memset(record, 0, sizeof record);
        memset(object, 0, sizeof object);
        memset(&event, 0, sizeof event);
        event.kind    = MP_EVENT_SABRE;
        event.action  = MP_SABRE_BLOCK;
        event.operand = 0x62u;
        ut_check(mp_puppet_sabre_perform(1u, (uint32_t)(uintptr_t)record,
                                         (uint32_t)(uintptr_t)object, &event, &aux_taken) ==
                     MP_PUPPET_SABRE_DONE,
                 "a block over a zeroed record and object is consumed");
        ut_check(mp_puppet_sabre_armed(1u), "and arms the blade, although its clip did not play");
        mp_puppet_sabre_tick(1u, (uint32_t)(uintptr_t)record, (uint32_t)(uintptr_t)object);
        ut_check(mp_puppet_sabre_armed(1u),
                 "a substep later a fresh block is still armed: the fallback waits its span");
        mp_puppet_sabre_reset(1u);
        ut_check(mp_puppet_sabre_armed(1u), "armed survives a reset");
        mp_puppet_sabre_tick(1u, (uint32_t)(uintptr_t)record, (uint32_t)(uintptr_t)object);
        ut_check(!mp_puppet_sabre_armed(1u),
                 "and is due at once: the first window after the reset disarms it");
    }
}

/* A zeroed block stands in for the record so the reads succeed; the object is left at 0, so
 * every performance that needs one refuses before it touches anything. */
static void check_engine_half_without_a_game(void)
{
    static uint8_t             record[0x3AC];
    mp_event_t                 event;
    mp_puppet_sabre_counters_t counters;
    bool                       aux_taken = false;

    ut_section("the engine half with no game");
    mp_puppet_sabre_resolve();
    ut_check(!mp_puppet_sabre_armed(1u), "nothing is armed after the resolve");

    memset(record, 0, sizeof record);
    memset(&event, 0, sizeof event);
    event.kind    = MP_EVENT_SABRE;
    event.action  = MP_SABRE_SWING;
    event.operand = 3u;
    ut_check(mp_puppet_sabre_perform(1u, 0u, 0u, &event, &aux_taken) == MP_PUPPET_SABRE_DONE,
             "with no record the event is consumed, never held");
    ut_check(mp_puppet_sabre_perform(1u, (uint32_t)(uintptr_t)record, 0u, &event, &aux_taken) ==
                 MP_PUPPET_SABRE_DONE,
             "a swing with nothing resolved is consumed");
    mp_puppet_sabre_counters(&counters);
    ut_check(counters.swings == 0u && counters.refused == 1u,
             "and counted as refused, not as performed");

    event.action  = MP_SABRE_BLOCK;
    event.operand = 0x62u;
    ut_check(mp_puppet_sabre_perform(1u, (uint32_t)(uintptr_t)record, 0u, &event, &aux_taken) ==
                 MP_PUPPET_SABRE_DONE,
             "a block with no object is consumed");
    mp_puppet_sabre_counters(&counters);
    ut_check(counters.blocks == 0u && counters.refused == 2u && !aux_taken,
             "refused before any store, and the aux slot untouched");

    event.action  = MP_SABRE_DISARM;
    event.operand = 0u;
    ut_check(mp_puppet_sabre_perform(1u, (uint32_t)(uintptr_t)record, 0u, &event, &aux_taken) ==
                 MP_PUPPET_SABRE_DONE,
             "a disarm with no swing end resolved is consumed");
    mp_puppet_sabre_counters(&counters);
    ut_check(counters.disarms == 0u && counters.refused == 3u, "and refused");

    event.action  = MP_SABRE_SWING;
    event.operand = 28u;
    (void)mp_puppet_sabre_perform(1u, (uint32_t)(uintptr_t)record, 0u, &event, &aux_taken);
    mp_puppet_sabre_counters(&counters);
    ut_check(counters.refused == 4u, "a row past the table is refused before anything else");

    event.kind = MP_EVENT_SHOT;
    (void)mp_puppet_sabre_perform(1u, (uint32_t)(uintptr_t)record, 0u, &event, &aux_taken);
    mp_puppet_sabre_counters(&counters);
    ut_check(counters.refused == 4u, "an event of another kind is not this module's to refuse");

    mp_puppet_sabre_tick(1u, (uint32_t)(uintptr_t)record, 0u);
    mp_puppet_sabre_counters(&counters);
    ut_check(counters.fallback_disarms == 0u && !mp_puppet_sabre_armed(1u),
             "the tick with nothing armed disarms nothing");
    ut_check(counters.write_faults == 0u, "and nothing was written");
}

/* Three far players are three blades. One record for all of them let a block on one far body
 * be armed, reset and disarmed by the events and the peer changes of another. */
static void check_each_bank_has_its_own_blade(void)
{
    static uint8_t record[0x3AC];
    static uint8_t object[0x100];
    mp_event_t     event;
    bool           aux_taken = false;

    ut_section("each far bank's blade is its own");
    memset(record, 0, sizeof record);
    memset(object, 0, sizeof object);
    memset(&event, 0, sizeof event);
    event.kind    = MP_EVENT_SABRE;
    event.action  = MP_SABRE_BLOCK;
    event.operand = 0x62u;
    (void)mp_puppet_sabre_perform(2u, (uint32_t)(uintptr_t)record, (uint32_t)(uintptr_t)object,
                                  &event, &aux_taken);
    ut_check(mp_puppet_sabre_armed(2u), "a block arms the blade of bank 2's puppet");
    ut_check(!mp_puppet_sabre_armed(1u) && !mp_puppet_sabre_armed(3u),
             "and nobody else's");
    mp_puppet_sabre_reset(3u);
    mp_puppet_sabre_tick(3u, (uint32_t)(uintptr_t)record, (uint32_t)(uintptr_t)object);
    mp_puppet_sabre_tick(2u, (uint32_t)(uintptr_t)record, (uint32_t)(uintptr_t)object);
    ut_check(mp_puppet_sabre_armed(2u),
             "a new player in front of bank 3 does not make bank 2's fallback due");
    mp_puppet_sabre_reset(2u);
    mp_puppet_sabre_tick(2u, (uint32_t)(uintptr_t)record, (uint32_t)(uintptr_t)object);
    ut_check(!mp_puppet_sabre_armed(2u), "while one in front of its own bank does");
}

/* A far body in a borrowed model with no weapon on the rig's hand: the swing's contact node is
 * put back to 0 after the starter, and nothing else of the sphere is touched. The object stands
 * in for the puppet's body; 12 is the blade node of obi.baf, which carries no mesh, and 9 is a
 * row that swings a blade. */
static void check_a_worn_body_swings_without_contact(void)
{
    static uint8_t             object[0x100];
    mp_puppet_sabre_counters_t before;
    mp_puppet_sabre_counters_t after;
    const uint32_t             node = 12u;
    const uint32_t             code = 0x25u;
    const uint32_t             radius_bits = 0x3E4CCCCDu;   /* 0.2 */
    uint32_t                   word = 0;

    ut_section("a worn body's swing has no contact sphere while it carries no weapon");
    memset(object, 0, sizeof object);
    memcpy(object + 0xA8u, &node, sizeof node);
    memcpy(object + 0xACu, &code, sizeof code);
    memcpy(object + 0xB0u, &radius_bits, sizeof radius_bits);
    mp_puppet_sabre_counters(&before);

    ut_check(!mp_puppet_sabre_withhold_contact((uint32_t)(uintptr_t)object, false, false, 9u),
             "a body in its hero's model keeps what the starter armed");
    memcpy(&word, object + 0xA8u, sizeof word);
    ut_check(word == 12u, "its contact node is still 12");

    ut_check(mp_puppet_sabre_withhold_contact((uint32_t)(uintptr_t)object, true, false, 9u),
             "a body in a borrowed model with no weapon on it has the node taken back");
    memcpy(&word, object + 0xA8u, sizeof word);
    ut_check(word == 0u, "the node is 0, the pair pass's own test for no sphere");
    memcpy(&word, object + 0xACu, sizeof word);
    ut_check(word == 0x25u, "the code stays, so the fallback still sees the swing");
    memcpy(&word, object + 0xB0u, sizeof word);
    ut_check(word == radius_bits, "and so does the radius");

    ut_check(!mp_puppet_sabre_withhold_contact((uint32_t)(uintptr_t)object, true, false, 9u),
             "a node that is 0 already is not written again");
    ut_check(!mp_puppet_sabre_withhold_contact(0u, true, false, 9u),
             "and without an object nothing is");
    mp_puppet_sabre_counters(&after);
    ut_checkf(after.worn_swings == before.worn_swings + 2u,
              "both swings of the worn body are counted, the one whose node was 0 already as "
              "well, and the hero's is not (%u)",
              (unsigned)(after.worn_swings - before.worn_swings));
    ut_check(after.swing_contacts_withheld == before.swing_contacts_withheld + 1u,
             "one contact node is counted as put back, the one that was not 0");
    ut_check(after.swing_contacts_kept == before.swing_contacts_kept,
             "and neither of them kept a contact, since no weapon hangs on that body");
    ut_check(after.write_faults == before.write_faults, "and no write was refused");
}

/* And with the weapon hanging: the rows that swing a weapon keep the node the starter armed,
 * which the overlay answers as the hand the drawn weapon hangs at, and the six that swing a fist
 * or a foot do not, because a weapon is answered for under the blade's name alone. */
static void check_a_worn_body_with_a_weapon_strikes(void)
{
    static uint8_t             object[0x100];
    mp_puppet_sabre_counters_t before;
    mp_puppet_sabre_counters_t after;
    const uint32_t             node = 31u;   /* the hand the drawn weapon hangs at */
    uint32_t                   kept = 0u;
    uint32_t                   word = 0;
    unsigned                   row;

    ut_section("a worn body that carries its player's weapon keeps its contact");
    memset(object, 0, sizeof object);
    memcpy(object + 0xA8u, &node, sizeof node);
    mp_puppet_sabre_counters(&before);

    for (row = MP_PUPPET_SABRE_UNARMED_ROWS; row < MP_PUPPET_SABRE_SWING_ROWS; ++row) {
        kept += mp_puppet_sabre_withhold_contact((uint32_t)(uintptr_t)object, true, true,
                                                 (uint8_t)row) ? 0u : 1u;
    }
    ut_checkf(kept == MP_PUPPET_SABRE_SWING_ROWS - MP_PUPPET_SABRE_UNARMED_ROWS,
              "all %u rows that swing a weapon keep it",
              MP_PUPPET_SABRE_SWING_ROWS - MP_PUPPET_SABRE_UNARMED_ROWS);
    memcpy(&word, object + 0xA8u, sizeof word);
    ut_check(word == 31u, "and the node the starter armed is still what the pair pass reads");

    for (row = 0u; row < MP_PUPPET_SABRE_UNARMED_ROWS; ++row) {
        memcpy(object + 0xA8u, &node, sizeof node);
        ut_checkf(mp_puppet_sabre_withhold_contact((uint32_t)(uintptr_t)object, true, true,
                                                   (uint8_t)row),
                  "row %u swings a fist or a foot, which no weapon hangs on, so its contact goes "
                  "back whatever the body carries", row);
    }
    memcpy(&word, object + 0xA8u, sizeof word);
    ut_check(word == 0u, "and the last of those left the node at 0");

    mp_puppet_sabre_counters(&after);
    ut_check(after.swing_contacts_kept == before.swing_contacts_kept + kept,
             "the swings that kept their contact are counted apart from the ones that lost it");
    ut_check(after.swing_contacts_withheld ==
                 before.swing_contacts_withheld + MP_PUPPET_SABRE_UNARMED_ROWS,
             "and the fist rows are counted as put back");
    ut_check(after.worn_swings == before.worn_swings + MP_PUPPET_SABRE_SWING_ROWS,
             "every row of the table counts as a swing of a worn body either way");
    ut_check(after.write_faults == before.write_faults, "and no write was refused");
}

/* A made up body for the starter guard: an object whose actor carries `count` clips. At +0x9C, the
 * render thing's place, stands a decoy whose word at +0xC8 fits every clip, and the actor's clip
 * table pointer at +0xE4 is a large number too, so a guard that took its count from either place
 * would answer go where the actor says never. */
typedef struct made_body {
    uint8_t object[0x100];
    uint8_t actor[0x100];
    uint8_t decoy[0x100];
} made_body_t;

static made_body_t s_body;

static void put_word(uint8_t *block, uint32_t offset, uint32_t value)
{
    memcpy(block + offset, &value, sizeof value);
}

static uint32_t made_body(uint32_t count)
{
    memset(&s_body, 0, sizeof s_body);
    put_word(s_body.object, 0x14u, (uint32_t)(uintptr_t)s_body.actor);
    put_word(s_body.object, 0x9Cu, (uint32_t)(uintptr_t)s_body.decoy);
    put_word(s_body.actor, 0xC8u, count);
    put_word(s_body.actor, 0xE4u, (uint32_t)(uintptr_t)s_body.decoy);
    put_word(s_body.decoy, 0xC8u, 200u);
    return (uint32_t)(uintptr_t)s_body.object;
}

static mp_puppet_starter_counters_t starters(void)
{
    mp_puppet_starter_counters_t out;

    mp_puppet_starter_counters(&out);
    return out;
}

static void check_the_starter_guard(void)
{
    mp_puppet_starter_counters_t before = starters();
    mp_puppet_starter_counters_t after;
    uint32_t                     object = made_body(16u);

    ut_section("the starter guard over a body whose actor carries 16 clips");
    ut_check(mp_puppet_starter_weapon(1u, object, 0u, 2u, false) == MP_STARTER_NEVER,
             "a change from empty hands to slot 2 is never started: its clip 0x22 is past 16");
    ut_check(mp_puppet_starter_weapon(1u, object, 0u, 2u, false) == MP_STARTER_NEVER,
             "asked again, as the state asks after the event, it is never again");
    ut_check(mp_puppet_starter_push(1u, object) == MP_STARTER_NEVER, "a push is never started");
    ut_check(mp_puppet_starter_swing(1u, object, true) == MP_STARTER_NEVER,
             "nor is the midair swing");
    ut_check(mp_puppet_starter_swing(1u, object, false) == MP_STARTER_GO,
             "a swing on the base channel goes: the base player refuses a missing clip itself");
    ut_check(mp_puppet_starter_weapon(1u, object, 0u, 0u, false) == MP_STARTER_GO,
             "empty hands asked for empty hands go: the setter plays nothing");
    after = starters();
    ut_checkf(after.weapon_missing == before.weapon_missing + 1u,
              "the one change is counted once, whichever path asked first (%u)",
              (unsigned)(after.weapon_missing - before.weapon_missing));
    ut_check(after.push_missing == before.push_missing + 1u &&
                 after.midair_missing == before.midair_missing + 1u,
             "the push and the midair swing are counted");
    ut_checkf(after.fewest_clips == 16u, "and the fewest clips seen are 16 (%u)",
              (unsigned)after.fewest_clips);
    ut_check(after.unread == before.unread, "every word read");

    mp_puppet_starter_reset(1u);
    (void)mp_puppet_starter_weapon(1u, object, 0u, 2u, false);
    ut_check(starters().weapon_missing == after.weapon_missing + 1u,
             "a new body in front of the bank decides afresh and counts again");
}

/* Each starter against an actor one short of its clip and one that carries it exactly: the bound
 * the engine gets wrong by one is the one that matters. */
static void check_the_starter_bound(void)
{
    mp_puppet_starter_counters_t before = starters();

    ut_section("the starter guard at the exact count of the clip it plays");
    ut_check(mp_puppet_starter_weapon(2u, made_body(34u), 0u, 2u, false) == MP_STARTER_NEVER,
             "empty hands to slot 2 plays 0x22, which is 34: an actor with 34 clips lacks it");
    ut_check(mp_puppet_starter_weapon(2u, made_body(35u), 0u, 2u, false) == MP_STARTER_GO,
             "and one with 35 has it");
    ut_check(mp_puppet_starter_weapon(2u, made_body(32u), 2u, 1u, false) == MP_STARTER_NEVER,
             "slot 2 to the sabre plays 0x20: 32 clips lack it");
    ut_check(mp_puppet_starter_weapon(2u, made_body(33u), 2u, 1u, false) == MP_STARTER_GO,
             "33 have it");
    ut_check(mp_puppet_starter_weapon(2u, made_body(33u), 2u, 3u, false) == MP_STARTER_NEVER,
             "slot 2 to slot 3 plays 0x21: 33 clips lack it");
    ut_check(mp_puppet_starter_weapon(2u, made_body(34u), 2u, 3u, false) == MP_STARTER_GO,
             "34 have it");
    ut_check(mp_puppet_starter_weapon(2u, made_body(35u), 2u, 0u, false) == MP_STARTER_NEVER,
             "slot 2 to empty hands plays 0x23: 35 clips lack it");
    ut_check(mp_puppet_starter_weapon(2u, made_body(36u), 2u, 0u, false) == MP_STARTER_GO,
             "36 have it");
    ut_check(mp_puppet_starter_push(2u, made_body(113u)) == MP_STARTER_NEVER,
             "the push plays 0x71, which is 113: 113 clips lack it");
    ut_check(mp_puppet_starter_push(2u, made_body(114u)) == MP_STARTER_GO,
             "114 have it, and only the two Jedi carry that many");
    ut_check(mp_puppet_starter_swing(2u, made_body(85u), true) == MP_STARTER_NEVER,
             "the midair swing plays 0x55, which is 85: 85 clips lack it");
    ut_check(mp_puppet_starter_swing(2u, made_body(86u), true) == MP_STARTER_GO, "86 have it");
    ut_check(mp_puppet_starter_weapon(2u, made_body(83u), 0u, 2u, false) == MP_STARTER_GO &&
                 mp_puppet_starter_push(2u, made_body(83u)) == MP_STARTER_NEVER &&
                 mp_puppet_starter_swing(2u, made_body(83u), true) == MP_STARTER_NEVER,
             "Panaka's 83 clips draw a weapon and neither push nor swing in midair");
    ut_check(mp_puppet_starter_weapon(2u, made_body(114u), 0u, 12u, false) == MP_STARTER_NEVER,
             "slot 12 is never started, not even on a Jedi: the weapon table has no row 12");
    ut_check(mp_puppet_starter_weapon(2u, made_body(114u), 0u, 11u, false) == MP_STARTER_GO,
             "slot 11 is");
    ut_check(starters().slot_past_table == before.slot_past_table + 1u,
             "and the slot past the table is counted apart");
}

static void check_the_starter_unread(void)
{
    mp_puppet_starter_counters_t before = starters();
    mp_puppet_starter_counters_t after;
    uint32_t                     object;

    ut_section("the starter guard when the body does not read");
    ut_check(mp_puppet_starter_weapon(3u, 0u, 0u, 2u, false) == MP_STARTER_NOT_YET &&
                 mp_puppet_starter_push(3u, 0u) == MP_STARTER_NOT_YET &&
                 mp_puppet_starter_swing(3u, 0u, true) == MP_STARTER_NOT_YET,
             "no object is a wait, never a call");
    after = starters();
    ut_check(after.unread == before.unread && after.push_missing == before.push_missing &&
                 after.weapon_missing == before.weapon_missing,
             "and is not counted: the caller turns back on it by itself");
    object = made_body(114u);
    put_word(s_body.object, 0x14u, 0u);
    ut_check(mp_puppet_starter_push(3u, object) == MP_STARTER_NEVER,
             "an object without an actor is never: the overlay player asserts on that first");
    put_word(s_body.object, 0x14u, 0x10u);
    ut_check(mp_puppet_starter_push(3u, object) == MP_STARTER_NOT_YET,
             "an actor whose count does not read is a wait");
    ut_check(mp_puppet_starter_push(3u, 0x20u) == MP_STARTER_NOT_YET,
             "and so is an object that does not read");
    ut_check(starters().unread == before.unread + 2u, "both waits are counted");
}

/* The clip column of the shipped swing table at 0x004B4E00, 28 rows of 0x20 bytes. */
static void check_the_swing_table_read(void)
{
    static const uint32_t SHIPPED[28] = {
        0x47u, 0x48u, 0x49u, 0x4Au, 0x4Bu, 0x4Cu, 0x47u, 0x48u, 0x49u, 0x4Au,
        0x4Bu, 0x4Cu, 0x4Du, 0x4Eu, 0x4Fu, 0x50u, 0x51u, 0x54u, 0x5Au, 0x5Bu,
        0x52u, 0x53u, 0x57u, 0x5Cu, 0x55u, 0x6Eu, 0x6Fu, 0x70u,
    };
    static uint32_t table[28u * 8u];
    uint8_t         row;
    unsigned        overlays = 0u;

    ut_section("the swing row's channel, read out of the table");
    memset(table, 0, sizeof table);
    for (row = 0u; row < 28u; ++row) {
        table[row * 8u] = SHIPPED[row];
    }
    ut_check(mp_puppet_starter_swing_overlay((uintptr_t)table, 24u),
             "row 24 of the shipped table names 0x55 and plays on the overlay channel");
    for (row = 0u; row < 28u; ++row) {
        overlays += mp_puppet_starter_swing_overlay((uintptr_t)table, row) ? 1u : 0u;
    }
    ut_checkf(overlays == 1u, "and it is the only one (%u)", overlays);
    table[5u * 8u]  = 0x55u;
    table[24u * 8u] = 0x4Fu;
    ut_check(mp_puppet_starter_swing_overlay((uintptr_t)table, 5u) &&
                 !mp_puppet_starter_swing_overlay((uintptr_t)table, 24u),
             "the word decides and not the row number: a table naming 0x55 in row 5 plays row 5 "
             "on the overlay channel and row 24 on the base channel");
    ut_check(mp_puppet_starter_swing_overlay(0x10u, 24u) &&
                 !mp_puppet_starter_swing_overlay(0x10u, 3u),
             "a table that does not read answers what the shipped one says");
}

/* The midair swing through the sabre module. The swing starter never resolves in a test, so a
 * guard that stood behind the test for the starter would never be asked here. */
static void check_the_midair_guard_comes_first(void)
{
    static uint8_t               record[0x3AC];
    mp_event_t                   event;
    mp_puppet_sabre_counters_t   sabre_before;
    mp_puppet_sabre_counters_t   sabre_after;
    mp_puppet_starter_counters_t before = starters();
    mp_puppet_starter_counters_t after;
    bool                         aux_taken = false;

    ut_section("a midair swing the actor lacks is dropped before the swing starter is looked for");
    memset(record, 0, sizeof record);
    memset(&event, 0, sizeof event);
    event.kind    = MP_EVENT_SABRE;
    event.action  = MP_SABRE_SWING;
    event.operand = 24u;
    mp_puppet_sabre_counters(&sabre_before);
    ut_check(mp_puppet_sabre_perform(1u, (uint32_t)(uintptr_t)record, made_body(16u), &event,
                                     &aux_taken) == MP_PUPPET_SABRE_DONE,
             "a midair swing on a body whose actor carries 16 clips is consumed");
    mp_puppet_sabre_counters(&sabre_after);
    after = starters();
    ut_check(after.midair_missing == before.midair_missing + 1u,
             "and counted as a midair swing the actor lacks");
    ut_check(sabre_after.refused == sabre_before.refused &&
                 sabre_after.swings == sabre_before.swings,
             "not as a refusal and not as a swing: the guard answered before the missing starter");
    ut_check(!mp_puppet_sabre_armed(1u) && !aux_taken,
             "nothing is armed and the aux slot is untouched");

    (void)mp_puppet_sabre_perform(1u, (uint32_t)(uintptr_t)record, made_body(86u), &event,
                                  &aux_taken);
    mp_puppet_sabre_counters(&sabre_after);
    ut_check(starters().midair_missing == after.midair_missing &&
                 sabre_after.refused == sabre_before.refused + 1u,
             "an actor with 86 clips lets it through to the test for the starter, which refuses");

    event.operand = 3u;
    (void)mp_puppet_sabre_perform(1u, (uint32_t)(uintptr_t)record, made_body(16u), &event,
                                  &aux_taken);
    mp_puppet_sabre_counters(&sabre_after);
    ut_check(starters().midair_missing == after.midair_missing &&
                 sabre_after.refused == sabre_before.refused + 2u,
             "and a row on the base channel is not the guard's to drop");
}

/* A worn body cannot draw: the guard writes the slot instead of refusing the change, which would
 * leave a far player in a borrowed model with one weapon for the rest of the level. */
static void check_the_starter_sets_a_worn_slot(void)
{
    static uint8_t               record[0x3ACu];
    mp_puppet_starter_counters_t before;
    mp_puppet_starter_counters_t after;
    uint32_t                     object = made_body(16u);
    uint32_t                     slot = 0u;
    uint32_t                     requested = 0u;

    ut_section("a worn body whose actor lacks the draw clip has its slot written");
    mp_puppet_starter_reset(1u);
    before = starters();
    ut_check(mp_puppet_starter_weapon(1u, object, 0u, 2u, true) == MP_STARTER_SET_ONLY,
             "a worn body's change to slot 2 is set rather than started");
    after = starters();
    ut_check(after.weapon_missing == before.weapon_missing,
             "and is not counted as a change that was not started, because it happens");
    ut_check(after.weapon_set_worn == before.weapon_set_worn,
             "nor counted as written yet: only a write that reads back is one");
    ut_check(mp_puppet_starter_weapon(1u, object, 0u, 2u, false) == MP_STARTER_NEVER &&
                 starters().weapon_missing == before.weapon_missing + 1u,
             "the same change on a body without a model is refused as it was before");
    ut_check(mp_puppet_starter_weapon(1u, made_body(114u), 0u, 2u, true) == MP_STARTER_GO,
             "a lent rig that does carry the draw clip draws it, worn or not");
    ut_check(mp_puppet_starter_weapon(1u, made_body(16u), 0u, 12u, true) == MP_STARTER_NEVER,
             "a slot past the weapon table is never for a worn body too: the bound goes first");
    object = made_body(16u);
    put_word(s_body.object, 0x14u, 0u);
    ut_check(mp_puppet_starter_weapon(1u, object, 0u, 3u, true) == MP_STARTER_NEVER,
             "an object with no actor stays never: it is no body that wears anything");
    ut_check(mp_puppet_starter_weapon(1u, 0u, 0u, 3u, true) == MP_STARTER_NOT_YET,
             "and an object the window does not have is still a wait");
    ut_check(mp_puppet_starter_weapon(1u, made_body(8u), 0u, 2u, true) == MP_STARTER_SET_ONLY &&
                 starters().fewest_clips == 8u,
             "the fewest clips seen counts a worn body's actor as well");

    ut_section("the two words a worn body's change is made of");
    memset(record, 0, sizeof record);
    before = starters();
    ut_check(mp_puppet_starter_set_slot(1u, (uint32_t)(uintptr_t)record, 2u),
             "the write answers true once both words have read back");
    memcpy(&slot, record + 0x84u, sizeof slot);
    memcpy(&requested, record + 0x88u, sizeof requested);
    ut_check(slot == 2u && requested == 2u,
             "the equipped slot and the request both hold the new slot: one without the other is "
             "a change the engine believes is still on its way, and nothing would commit it");
    ut_check(mp_puppet_starter_set_slot(1u, (uint32_t)(uintptr_t)record, 1u),
             "a second change is written on top of the first");
    memcpy(&slot, record + 0x84u, sizeof slot);
    memcpy(&requested, record + 0x88u, sizeof requested);
    ut_check(slot == 1u && requested == 1u, "and both words follow it");
    after = starters();
    ut_checkf(after.weapon_set_worn == before.weapon_set_worn + 2u, "both writes are counted (%u)",
              (unsigned)(after.weapon_set_worn - before.weapon_set_worn));
    ut_check(!mp_puppet_starter_set_slot(1u, 0u, 1u) &&
                 starters().set_writes_refused == before.set_writes_refused + 1u,
             "a record of 0 is refused and counted, and the caller asks again next substep");
    ut_check(starters().weapon_set_worn == after.weapon_set_worn,
             "and a refused write is not counted as one that went in");
}

int main(void)
{
    check_stores();
    check_operands();
    check_aux();
    /* The engine half's section pins the counters of a process where nothing was performed, so
     * it runs before the two sections that perform a block to arm a blade. */
    check_engine_half_without_a_game();
    check_fallback();
    check_each_bank_has_its_own_blade();
    check_a_worn_body_swings_without_contact();
    check_a_worn_body_with_a_weapon_strikes();
    /* The guard's first section pins the fewest clips seen, so it runs before the others. */
    check_the_starter_guard();
    check_the_starter_bound();
    check_the_starter_unread();
    check_the_swing_table_read();
    check_the_midair_guard_comes_first();
    /* Last: it lowers the fewest clips seen, which the guard's first section pins at 16. */
    check_the_starter_sets_a_worn_slot();

    return ut_summary("puppet sabre decisions");
}
