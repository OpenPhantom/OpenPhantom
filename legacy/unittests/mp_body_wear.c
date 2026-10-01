/* The tick that asks the overlay for a far body's model and follows its answer, with no game.
 *
 * The bodies are made up here, which is the whole of the engine the tick reads, and so is the wall
 * clock the wait before a rebuild is measured on. The overlay's answer is published by hand over
 * the channel the overlay uses, and the far player's model is noted where the appearance notes
 * it. What cannot be reached without a game is the body module's own count at a spawn and a take
 * down that succeeded; that the refused ones count nothing is checked in mp_body.c, and the count
 * is set here by hand where a take down would have set it. With no body module installed every
 * take down is refused, so a rebuild the tick asks for shows here as one refused take down.
 *
 * SIZE NOTE: over six hundred lines, one check function per property of the tick, each starting
 * from a world of its own so that no check can be read as depending on the one before it. The
 * seam, if it grows, is the blade: its three runs share nothing with the other checks but the
 * engine's word that a body wears something.
 */
#include "unittest.h"

#include "mp_body.h"
#include "mp_body_internal.h"
#include "mp_body_wear.h"
#include "mp_bridge_far.h"
#include "mp_events.h"
#include "mp_wear_rule.h"

#include "common/model_wear_note.h"
#include "common/shared_note.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The shipped substep of 1/32 of a second and the 60fps cheat's of 1/64, in microseconds, so a
 * run of either adds up to whole milliseconds exactly. */
#define SUBSTEP_US      31250u
#define SUBSTEP_FAST_US 15625u

/* A player record and the fields the light's release and the blade length step read in it. */
#define RECORD_BYTES          0x3ACu
#define RECORD_SUBSTEP        0x74u
#define RECORD_WEAPON_SLOT    0x84u
#define RECORD_REQUESTED_SLOT 0x88u
#define RECORD_BLADE_SIZE     0x210u
#define RECORD_LIGHT_POWER    0x298u
#define RECORD_LIGHT_RANGE    0x29Cu

static model_wear_done_record_t s_answer;
static uint64_t                 s_clock_us;
static uint32_t                 s_step_us = SUBSTEP_US;

static void set_name(char field[MODEL_WEAR_NAME_MAX], const char *name)
{
    memset(field, 0, MODEL_WEAR_NAME_MAX);
    memcpy(field, name, strlen(name));
}

/* The overlay has loaded and says so, with nothing answered yet. */
static void overlay_listens(void)
{
    memset(&s_answer, 0, sizeof s_answer);
    s_answer.ready = MODEL_WEAR_READY_LISTENING | MODEL_WEAR_READY_ABLE;
    ut_check(model_wear_publish_done(&s_answer), "the overlay's first record is published");
}

/* Whether the overlay says the places a borrowed weapon is drawn and shot from have resolved. */
static void overlay_hangs_weapons(bool can)
{
    s_answer.ready = (uint8_t)(MODEL_WEAR_READY_LISTENING | MODEL_WEAR_READY_ABLE |
                               (can ? MODEL_WEAR_READY_WEAPON : 0u));
    ut_checkf(model_wear_publish_done(&s_answer),
              "the overlay says it %s hang a weapon on a far body", can ? "can" : "cannot");
}

static void overlay_answers_with(size_t bank, uint32_t serial, uint8_t state, uint8_t reason,
                                 const char *echo, float scale, uint8_t weapon)
{
    model_wear_done_bank_t *entry = &s_answer.bank[bank - 1u];

    entry->serial = serial;
    entry->state  = state;
    entry->reason = reason;
    entry->weapon = weapon;
    entry->scale  = scale;
    set_name(entry->model, echo);
    ut_checkf(model_wear_publish_done(&s_answer), "the overlay answers bank %u's body %u",
              (unsigned)bank, (unsigned)serial);
}

static void overlay_answers(size_t bank, uint32_t serial, uint8_t state, uint8_t reason,
                            const char *echo, float scale)
{
    overlay_answers_with(bank, serial, state, reason, echo, scale, 0u);
}

/* The far player behind `bank` wears `model` over hero 2, or nothing for "". */
static void sender_wears(size_t bank, const char *model)
{
    (void)mp_bridge_far_note_look(bank, model[0] != '\0' ? MP_SKIN_MODEL : MP_SKIN_CHARACTER,
                                  model, 2u);
}

static mp_body_wear_body_t body(uint32_t serial, uint32_t object, bool worn)
{
    mp_body_wear_body_t made;

    memset(&made, 0, sizeof made);
    made.serial      = serial;
    made.object      = object;
    made.thing       = object + 0x100u;
    made.slot        = 2;
    made.worn        = worn;
    made.actor_scale = 1.1f;
    return made;
}

/* One substep each: the clock moves on by the substep, then the tick runs. */
static void tick(size_t bank, const mp_body_wear_body_t *made, unsigned times)
{
    unsigned i;

    for (i = 0; i < times; ++i) {
        s_clock_us += s_step_us;
        mp_body_wear_tick_body(bank, made, (uint32_t)(s_clock_us / 1000u));
    }
}

/* The substeps of the current length that make up the wait. */
static unsigned settle_ticks(void)
{
    return (unsigned)((uint64_t)MP_WEAR_SETTLE_MS * 1000u / s_step_us);
}

/* Bank `bank`'s entry of the wish as the overlay would read it, zero when none was published. */
static model_wear_want_bank_t wished(size_t bank, uint32_t *publications)
{
    model_wear_want_record_t want;

    memset(&want, 0, sizeof want);
    (void)model_wear_read_want(&want, publications);
    return want.bank[bank - 1u];
}

static mp_body_wear_counters_t counted(void)
{
    mp_body_wear_counters_t counters;

    mp_body_wear_get_counters(&counters);
    return counters;
}

static void check_the_listener(void)
{
    mp_body_wear_body_t    standing = body(1u, 0x10000u, false);
    model_wear_want_bank_t entry;
    uint32_t               before = 0u;
    uint32_t               after = 0u;

    ut_section("only an overlay that listens is asked, and the record is state");
    shared_note_forget(MODEL_WEAR_DONE_NOTE_NAME);
    sender_wears(1u, "anakin.baf");
    tick(1u, &standing, 1u);
    entry = wished(1u, NULL);
    ut_check(entry.serial == 1u && entry.object == 0x10000u && entry.model[0] == '\0',
             "with nobody listening the body is described and no model is asked for");
    ut_check(counted().asked == 0u, "and no ask is counted");

    overlay_listens();
    tick(1u, &standing, 1u);
    entry = wished(1u, NULL);
    ut_check(strcmp(entry.model, "anakin.baf") == 0 && entry.slot == 2u && entry.block != 0u,
             "an overlay that listens is asked for the model, with the slot and the block");
    ut_check(counted().asked == 1u, "which is one ask");

    (void)wished(1u, &before);
    tick(1u, &standing, 5u);
    (void)wished(1u, &after);
    ut_check(after == before, "five ticks that change nothing publish nothing");
}

static void check_the_serial(void)
{
    mp_body_far_t         *far = mp_body_far_at(1u);
    model_wear_want_bank_t entry;
    mp_body_wear_body_t    again = body(3u, 0x10000u, false);

    ut_section("the serial: a take down is published at once, and the next body is a new one");
    ut_check(far != NULL, "bank 1 has a record in the body module");
    if (far == NULL) {
        return;
    }
    far->serial = 2u;   /* what the take down that succeeded counted */
    mp_body_wear_note_down(1u);
    entry = wished(1u, NULL);
    ut_check(entry.serial == 2u && entry.object == 0u && entry.model[0] == '\0',
             "the body that went is published without its object and its model before any "
             "further tick, which after a level end or a session end would never come");
    ut_check(counted().notes_down == 1u && counted().unanswered == 1u,
             "and the body asked for, never answered, is counted as such");
    ut_check(!mp_body_wear_worn(1u), "a body that went wears nothing");

    far->serial = 3u;   /* and what the spawn after it counted */
    tick(1u, &again, 1u);
    entry = wished(1u, NULL);
    ut_check(entry.serial == 3u && entry.object == 0x10000u && counted().asked == 2u,
             "the body built at the same address is a new body with a new ask");
    far->serial = 0u;
}

static void check_an_older_answer(void)
{
    mp_body_wear_body_t dressed = body(3u, 0x10000u, true);

    ut_section("an answer is for one body, and an older body's echo is left alone");
    overlay_answers(1u, 1u, MODEL_WEAR_STATE_WORN, MODEL_WEAR_REASON_NONE, "anakin.baf", 1.1f);
    tick(1u, &dressed, 1u);
    ut_check(counted().worn == 0u && !mp_body_wear_rig_is_senders(1u),
             "the WORN of body 1 is not taken for body 3, which sits at the same address: its "
             "rotations stay withheld");
    ut_check(mp_body_wear_worn(1u), "while the engine's word says body 3 wears something");

    overlay_answers(1u, 3u, MODEL_WEAR_STATE_WORN, MODEL_WEAR_REASON_NONE, "anakin.baf", 1.1f);
    tick(1u, &dressed, 1u);
    ut_check(counted().worn == 1u && mp_body_wear_rig_is_senders(1u),
             "its own answer is taken, and the rotations are the sender's again");
    ut_check(counted().unanswered == 1u, "and that ask was answered");
}

static void check_the_wait(void)
{
    mp_body_wear_body_t dressed = body(3u, 0x10000u, true);
    uint32_t            before = counted().rebuilds_refused;

    ut_section("a changed wish is waited out before a worn body is built again");
    sender_wears(1u, "padme.baf");
    tick(1u, &dressed, settle_ticks());
    ut_check(counted().rebuilds_refused == before,
             "a second and a half less one substep rebuilds nothing, though the body wears "
             "anakin.baf");
    ut_check(!mp_body_wear_rig_is_senders(1u),
             "and padme's rotations are withheld from anakin's rig meanwhile");
    tick(1u, &dressed, 1u);
    ut_check(counted().rebuilds_refused == before + 1u,
             "the substep at a second and a half asks for the take down");
    tick(1u, &dressed, 4u);
    ut_check(counted().rebuilds_refused == before + 3u,
             "which is asked three times for one body and then left: the body module warns of "
             "every refusal itself");
    ut_check(counted().rebuilt == 0u, "and none of them is a rebuild, since no body went down");
}

static void check_the_wait_is_wall_time(void)
{
    mp_body_wear_body_t dressed = body(21u, 0x10000u, true);
    uint32_t            before;

    ut_section("the wait is wall time, whatever the length of a substep");
    overlay_answers(1u, 21u, MODEL_WEAR_STATE_WORN, MODEL_WEAR_REASON_NONE, "padme.baf", 1.0f);
    tick(1u, &dressed, 1u);
    before = counted().rebuilds_refused;
    s_step_us = SUBSTEP_FAST_US;
    sender_wears(1u, "obiwan.baf");
    tick(1u, &dressed, settle_ticks());
    ut_checkf(counted().rebuilds_refused == before,
              "%u substeps of the 60fps cheat's 1/64, twice the shipped build's wait in "
              "substeps, are less than a second and a half and rebuild nothing",
              settle_ticks());
    tick(1u, &dressed, 1u);
    ut_check(counted().rebuilds_refused == before + 1u,
             "the substep at a second and a half asks for the take down, as at 1/32");
    s_step_us = SUBSTEP_US;
}

static void check_the_answer_comes_first(void)
{
    mp_body_wear_body_t dressed = body(5u, 0x20000u, true);
    uint32_t            before = counted().rebuilds_refused;

    ut_section("the answer is read before the rebuild is decided");
    sender_wears(2u, "anakin.baf");
    tick(2u, &dressed, 3u * settle_ticks());
    ut_check(counted().rebuilds_refused == before,
             "a worn body with no answer for it waits however long the wish has stood");
    ut_check(!mp_body_wear_rig_is_senders(2u), "and so do its rotations");

    overlay_answers(2u, 5u, MODEL_WEAR_STATE_REFUSED, MODEL_WEAR_REASON_WEARS_OTHER, "padme.baf",
                    0.0f);
    tick(2u, &dressed, 1u);
    ut_check(counted().rebuilds_refused == before + 1u,
             "an answer that the body wears padme.baf rebuilds it in the substep it arrives in");
    ut_check(counted().refused == 1u, "and it is counted as a refusal");
}

static void check_the_deathmatch(void)
{
    mp_body_wear_body_t    standing = body(7u, 0x30000u, false);
    mp_body_wear_body_t    dressed = body(9u, 0x30000u, true);
    model_wear_want_bank_t entry;
    uint32_t               before;

    ut_section("a deathmatch asks no model, and the rotations still follow the sender's");
    sender_wears(3u, "anakin.baf");
    mp_body_wear_set_deathmatch(true);
    tick(3u, &standing, 1u);
    entry = wished(3u, NULL);
    ut_check(entry.object == 0x30000u && entry.model[0] == '\0',
             "the body is described and no model asked for");
    ut_check(!mp_body_wear_rig_is_senders(3u),
             "and the borrowed rig's rotations stay off the hero's rig: the sender still wears "
             "the model, only the wish is empty");
    mp_body_wear_set_deathmatch(false);
    tick(3u, &standing, 1u);
    ut_check(strcmp(wished(3u, NULL).model, "anakin.baf") == 0,
             "out of the deathmatch the model is asked for again");

    tick(3u, &dressed, 1u);
    overlay_answers(3u, 9u, MODEL_WEAR_STATE_WORN, MODEL_WEAR_REASON_NONE, "anakin.baf", 1.1f);
    tick(3u, &dressed, 1u);
    ut_check(mp_body_wear_rig_is_senders(3u), "a body dressed in the sender's model before it");
    before = counted().rebuilds_refused;
    mp_body_wear_set_deathmatch(true);
    tick(3u, &dressed, settle_ticks());
    ut_check(counted().rebuilds_refused == before, "a body dressed before a deathmatch waits");
    tick(3u, &dressed, 1u);
    ut_check(counted().rebuilds_refused == before + 1u, "and is then rebuilt as its hero");
    mp_body_wear_set_deathmatch(false);
}

static void check_one_answer_per_body(void)
{
    mp_body_far_t      *far = mp_body_far_at(3u);
    mp_body_wear_body_t standing = body(51u, 0x30000u, false);
    uint32_t            before = counted().unanswered;
    uint32_t            asked;

    ut_section("a body is answered once, whatever it is asked for after");
    ut_check(far != NULL, "bank 3 has a record in the body module");
    if (far == NULL) {
        return;
    }
    sender_wears(3u, "nute.baf");
    tick(3u, &standing, 1u);
    overlay_answers(3u, 51u, MODEL_WEAR_STATE_REFUSED, MODEL_WEAR_REASON_NO_ROW, "", 0.0f);
    tick(3u, &standing, 1u);
    asked = counted().asked;
    sender_wears(3u, "sio.baf");
    tick(3u, &standing, 1u);
    ut_check(counted().asked == asked + 1u, "a second model for the same body is a second ask");
    overlay_answers(3u, 51u, MODEL_WEAR_STATE_REFUSED, MODEL_WEAR_REASON_NO_ROW, "", 0.0f);
    tick(3u, &standing, 1u);
    far->serial = 52u;   /* what the take down that succeeded would have counted */
    mp_body_wear_note_down(3u);
    ut_check(counted().unanswered == before,
             "and the body that goes is not one never answered: its refusal names no model, so "
             "it answers the second ask as it answered the first");
    far->serial = 0u;
}

static void check_a_model_that_breaks_bodies(void)
{
    mp_body_far_t         *far = mp_body_far_at(2u);
    mp_body_wear_body_t    standing;
    model_wear_want_bank_t entry;
    uint32_t               before = counted().rebuilds_refused;
    uint32_t               serial;
    uint32_t               broke = 0u;

    ut_section("a body the overlay broke is rebuilt at once, and its model is given up after "
               "three");
    ut_check(far != NULL, "bank 2 has a record in the body module");
    if (far == NULL) {
        return;
    }
    sender_wears(2u, "maul.baf");
    for (serial = 31u; broke < MP_BODY_WEAR_BROKEN_LIMIT; serial += 2u) {
        standing = body(serial, 0x20000u, false);
        far->serial = serial;
        tick(2u, &standing, 1u);
        ut_checkf(strcmp(wished(2u, NULL).model, "maul.baf") == 0,
                  "body %u is asked for maul.baf", (unsigned)serial);
        overlay_answers(2u, serial, MODEL_WEAR_STATE_REFUSED, MODEL_WEAR_REASON_BROKEN, "", 0.0f);
        tick(2u, &standing, 1u);
        ++broke;
        ut_checkf(counted().rebuilds_refused == before + broke,
                  "and broken, body %u is taken down in the substep the answer arrives in, with "
                  "no wait", (unsigned)serial);
        far->serial = serial + 1u;   /* what the take down that succeeded would have counted */
        mp_body_wear_note_down(2u);
    }
    standing = body(serial, 0x20000u, false);
    far->serial = serial;
    tick(2u, &standing, 1u);
    entry = wished(2u, NULL);
    ut_check(entry.object == 0x20000u && entry.model[0] == '\0',
             "after the third body it broke, maul.baf is not asked for this bank again, and the "
             "body stands as its hero");

    standing = body(41u, 0x30000u, false);
    sender_wears(3u, "maul.baf");
    tick(3u, &standing, 1u);
    ut_check(strcmp(wished(3u, NULL).model, "maul.baf") == 0,
             "bank 3 is still asked for it: the count is the bank's");

    standing = body(serial, 0x20000u, false);
    sender_wears(2u, "padme.baf");
    tick(2u, &standing, 1u);
    ut_check(strcmp(wished(2u, NULL).model, "padme.baf") == 0, "another model is asked for");
    sender_wears(2u, "maul.baf");
    tick(2u, &standing, 1u);
    ut_check(strcmp(wished(2u, NULL).model, "maul.baf") == 0,
             "and maul.baf again once the far player wore another in between");
    far->serial = 0u;
}

static uint8_t *s_record;
static uint32_t s_slot_seen;
static unsigned s_light_ticks;

static uint32_t slot_in(const uint8_t *record)
{
    uint32_t slot;

    memcpy(&slot, record + RECORD_WEAPON_SLOT, sizeof slot);
    return slot;
}

static void put_slot(uint8_t *record, uint32_t slot)
{
    memcpy(record + RECORD_WEAPON_SLOT, &slot, sizeof slot);
}

/* The engine's light tick as far as the test needs it: the equipped slot it finds. */
static void __cdecl light_tick(void)
{
    s_slot_seen = slot_in(s_record);
    ++s_light_ticks;
}

static void check_the_light(void)
{
    uint32_t before = counted().light_releases;

    ut_section("a worn body's blade light is given back and not placed again");
    s_record = (uint8_t *)calloc(1u, RECORD_BYTES);
    ut_check(s_record != NULL, "a record stands in for the one the window installs");
    if (s_record == NULL) {
        return;
    }
    put_slot(s_record, 1u);
    mp_body_wear_light_release_only((uintptr_t)s_record, &light_tick);
    ut_check(s_light_ticks == 1u, "the engine's tick runs, so the light's slot is given back");
    ut_check(s_slot_seen != 1u,
             "and it finds another weapon than the sabre, so it places no light at a node of the "
             "hero's rig");
    ut_check(slot_in(s_record) == 1u, "the record holds the sabre again once the tick returned");

    put_slot(s_record, 3u);
    mp_body_wear_light_release_only((uintptr_t)s_record, &light_tick);
    ut_check(s_light_ticks == 2u && s_slot_seen == 3u && slot_in(s_record) == 3u,
             "a body that holds another weapon runs the tick as it stands, and nothing is "
             "written");

    ut_check(!mp_body_wear_light_release_only(0u, &light_tick) &&
                 !mp_body_wear_light_release_only((uintptr_t)s_record, NULL),
             "no record or no tick answers false");
    ut_check(s_light_ticks == 2u, "and runs nothing");
    ut_check(counted().light_releases == before && counted().slot_writes_refused == 0u,
             "the release alone counts nothing, and no write was refused");
    free(s_record);
    s_record = NULL;
}

static float float_at(const uint8_t *record, uint32_t offset)
{
    float value;

    memcpy(&value, record + offset, sizeof value);
    return value;
}

static void put_float(uint8_t *record, uint32_t offset, float value)
{
    memcpy(record + offset, &value, sizeof value);
}

/* The two runs follow the one answer mp_blade gives for the window's record. No answer calls the
 * engine's length tick, whose setter writes the mesh every body of the model draws. A body whose
 * spawn read no blade, Panaka's or a character's on the foreign slot, would have its light stand
 * at the sphere of node 0. */
static void check_the_blade_runs(void)
{
    mp_body_wear_counters_t before = counted();
    unsigned                lights = s_light_ticks;

    ut_section("the blade runs follow the one answer");
    s_record = (uint8_t *)calloc(1u, RECORD_BYTES);
    ut_check(s_record != NULL, "a record stands in for the one the window installs");
    if (s_record == NULL) {
        return;
    }
    put_slot(s_record, 1u);
    memcpy(s_record + RECORD_REQUESTED_SLOT, "\x01\x00\x00", 3u);
    put_float(s_record, RECORD_SUBSTEP, 1.0f / 32.0f);
    ut_check(mp_body_wear_blade_state_run((uintptr_t)s_record, MP_BLADE_TICK_STEP) &&
                 fabs(float_at(s_record, RECORD_BLADE_SIZE) - 0.0625) < 1e-6,
             "a Jedi body in its hero's own model steps its length in its block");
    ut_check(counted().own_steps == before.own_steps + 1u &&
                 counted().blade_steps == before.blade_steps,
             "counted as its own step, never as a worn body's");
    mp_body_wear_blade_light_run((uintptr_t)s_record, MP_BLADE_TICK_STEP, &light_tick);
    ut_check(s_light_ticks == lights + 1u && s_slot_seen == 1u,
             "and its light tick runs whole, with the sabre in the field");

    ut_check(!mp_body_wear_blade_state_run((uintptr_t)s_record, MP_BLADE_TICK_BLADELESS),
             "a body with the sabre out and no blade steps nothing");
    ut_check(counted().blade_ticks_bladeless == before.blade_ticks_bladeless + 1u &&
                 counted().blade_ticks_withheld == before.blade_ticks_withheld,
             "and is counted as bladeless, not as worn");
    mp_body_wear_blade_light_run((uintptr_t)s_record, MP_BLADE_TICK_BLADELESS, &light_tick);
    ut_check(s_light_ticks == lights + 2u && s_slot_seen != 1u && slot_in(s_record) == 1u,
             "its light tick runs only as far as the release, with another slot in the field");
    ut_check(counted().light_releases_bladeless == before.light_releases_bladeless + 1u &&
                 counted().light_releases == before.light_releases,
             "and that release is counted as a bladeless body's, not a worn one's");

    ut_check(!mp_body_wear_blade_state_run((uintptr_t)s_record, MP_BLADE_TICK_WORN),
             "a worn body whose hero carries no blade steps nothing");
    ut_check(counted().blade_ticks_withheld == before.blade_ticks_withheld + 1u &&
                 counted().blade_ticks_bladeless == before.blade_ticks_bladeless + 1u,
             "and is counted as worn, which the line of the far models reads");
    mp_body_wear_blade_light_run((uintptr_t)s_record, MP_BLADE_TICK_WORN, &light_tick);
    ut_check(s_light_ticks == lights + 3u && s_slot_seen != 1u, "its light only released");
    ut_check(counted().light_releases == before.light_releases + 1u &&
                 counted().light_releases_bladeless == before.light_releases_bladeless + 1u,
             "and counted as a worn body's");
    ut_check(counted().own_steps == before.own_steps + 1u,
             "and neither of the two was counted as a step");
    free(s_record);
    s_record = NULL;
}

/* The worn body's step: the same arithmetic as a body in its own model, written into the block
 * and into the two light fields, and counted in the line of the far models. */
static void check_the_step_without_a_mesh(void)
{
    mp_body_wear_counters_t before = counted();

    ut_section("a worn Jedi body's length is stepped without its mesh");
    s_record = (uint8_t *)calloc(1u, RECORD_BYTES);
    ut_check(s_record != NULL, "a record stands in for the one the window installs");
    if (s_record == NULL) {
        return;
    }
    put_slot(s_record, 1u);
    memcpy(s_record + RECORD_REQUESTED_SLOT, "\x01\x00\x00", 3u);
    put_float(s_record, RECORD_SUBSTEP, 1.0f / 32.0f);
    put_float(s_record, RECORD_BLADE_SIZE, 0.0f);

    ut_check(mp_body_wear_blade_state_run((uintptr_t)s_record, MP_BLADE_TICK_WORN_STEP),
             "a worn Jedi body steps its length in the block");
    ut_check(fabs(float_at(s_record, RECORD_BLADE_SIZE) - 0.0625) < 1e-6,
             "the length in the block grew by twice the substep");
    ut_check(fabs(float_at(s_record, RECORD_LIGHT_POWER) - 0.0625) < 1e-6 &&
                 fabs(float_at(s_record, RECORD_LIGHT_RANGE) - 0.09375) < 1e-6,
             "and the two fields the setter derives the light from went with it");
    ut_check(counted().blade_steps == before.blade_steps + 1u &&
                 counted().own_steps == before.own_steps &&
                 counted().blade_ticks_withheld == before.blade_ticks_withheld,
             "counted as a worn body's step, not as a step of a body in its own model and not as "
             "a tick withheld");

    put_float(s_record, RECORD_BLADE_SIZE, 1.0f);
    ut_check(!mp_body_wear_blade_state_run((uintptr_t)s_record, MP_BLADE_TICK_WORN_STEP) &&
                 float_at(s_record, RECORD_BLADE_SIZE) == 1.0f &&
                 counted().blade_steps == before.blade_steps + 1u,
             "a blade already full takes no step and nothing is written");

    (void)mp_body_wear_blade_state_run(0u, MP_BLADE_TICK_WORN_STEP);
    ut_check(counted().blade_step_faults == before.blade_step_faults + 1u &&
                 counted().own_step_faults == before.own_step_faults,
             "a window with no record is a refusal, counted as the worn body's");
    (void)mp_body_wear_blade_state_run(0u, MP_BLADE_TICK_STEP);
    ut_check(counted().own_step_faults == before.own_step_faults + 1u &&
                 counted().blade_step_faults == before.blade_step_faults + 1u,
             "and one for a body in its own model is counted as that body's");
    free(s_record);
    s_record = NULL;
}

/* The weapon flag, from the overlay's record into the bank and out again through the deathmatch.
 * Everything a worn body's blade does hangs off it, and none of it can be reached without a game,
 * so what is pinned here is that the flag travels and that the deathmatch reads it. */
static void check_the_deathmatch_with_weapons(void)
{
    mp_body_wear_body_t    standing = body(71u, 0x40000u, false);
    mp_body_wear_body_t    dressed  = body(71u, 0x40000u, true);
    model_wear_want_bank_t entry;
    uint32_t               before;

    ut_section("a deathmatch asks for a model where the overlay hangs the weapon with it");
    sender_wears(2u, "anakin.baf");
    mp_body_wear_set_deathmatch(true);
    overlay_hangs_weapons(false);
    tick(2u, &standing, 1u);
    entry = wished(2u, NULL);
    ut_check(entry.model[0] == '\0',
             "an overlay whose weapon sites have not resolved is asked for nothing");

    overlay_hangs_weapons(true);
    tick(2u, &standing, 1u);
    ut_check(strcmp(wished(2u, NULL).model, "anakin.baf") == 0,
             "one whose sites have resolved is asked, deathmatch or not");

    overlay_answers_with(2u, 71u, MODEL_WEAR_STATE_WORN, MODEL_WEAR_REASON_NONE, "anakin.baf",
                         1.1f, 0u);
    tick(2u, &dressed, 1u);
    ut_check(!mp_body_wear_weapon(2u), "a body it dressed without a weapon says so");
    entry = wished(2u, NULL);
    ut_check(entry.model[0] == '\0', "and the wish is emptied again");
    before = counted().rebuilds_refused;
    tick(2u, &dressed, settle_ticks() - 1u);
    ut_check(counted().rebuilds_refused == before,
             "which nothing takes back before the wait the wish of every mode is held to");
    tick(2u, &dressed, 1u);
    ut_check(counted().rebuilds_refused == before + 1u,
             "and the substep at a second and a half takes it back to its hero, through the one "
             "rebuild rule and no second one for the deathmatch");

    overlay_answers_with(2u, 71u, MODEL_WEAR_STATE_WORN, MODEL_WEAR_REASON_NONE, "anakin.baf",
                         1.1f, 1u);
    tick(2u, &dressed, 1u);
    ut_check(mp_body_wear_weapon(2u),
             "an answer that the weapon hangs is taken, though nothing else of it changed");
    ut_check(strcmp(wished(2u, NULL).model, "anakin.baf") == 0, "and the wish stands again");
    ut_check(!mp_body_wear_weapon(1u) && !mp_body_wear_weapon(3u),
             "and no other bank carries a weapon on the strength of that answer");

    mp_body_wear_set_deathmatch(false);
    overlay_hangs_weapons(false);
    memset(&s_answer.bank[1], 0, sizeof s_answer.bank[1]);
    ut_check(model_wear_publish_done(&s_answer), "the world is put back for what comes after");
}

static void check_the_doors(void)
{
    mp_body_wear_body_t made = body(1u, 0x40000u, true);
    uint32_t            before = 0u;
    uint32_t            after = 0u;

    ut_section("the engine half, with no game");
    mp_body_wear_tick_body(0u, &made, 0u);
    mp_body_wear_tick_body(4u, &made, 0u);
    mp_body_wear_tick_body(1u, NULL, 0u);
    mp_body_wear_note_down(4u);
    ut_check(!mp_body_wear_worn(0u) && !mp_body_wear_worn(4u),
             "an index that is no bank is ticked, noted and asked about as nothing");
    ut_check(!mp_body_wear_weapon(0u) && !mp_body_wear_weapon(4u) &&
                 mp_body_wear_worn_blade_node(0u, 0x40000u) == 0u &&
                 mp_body_wear_worn_blade_node(4u, 0x40000u) == 0u,
             "and carries no weapon and names no weapon node");
    ut_check(mp_body_wear_worn_blade_node(1u, 0u) == 0u,
             "nor does a bank with no body object to ask the engine about");
    (void)wished(1u, &before);
    mp_body_wear_tick(1u);
    (void)wished(1u, &after);
    ut_check(after == before && !mp_body_installed(),
             "and the engine half publishes nothing before the bodies installed");
}

int main(void)
{
    check_the_listener();
    check_the_serial();
    check_an_older_answer();
    check_the_wait();
    check_the_wait_is_wall_time();
    check_the_answer_comes_first();
    check_the_deathmatch();
    check_one_answer_per_body();
    check_a_model_that_breaks_bodies();
    check_the_light();
    check_the_blade_runs();
    check_the_step_without_a_mesh();
    check_the_deathmatch_with_weapons();
    check_the_doors();

    return ut_summary("mp_body_wear");
}
