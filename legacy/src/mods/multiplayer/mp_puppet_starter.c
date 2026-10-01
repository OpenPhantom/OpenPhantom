/* mp_puppet_starter.c: the guard in front of the three engine starters the puppet calls. See the
 * header. */
#include "mp_puppet_starter.h"

#include "mp_starter_rule.h"

#include "mp_bank.h"
#include "mp_body_asset.h"
#include "mp_cells.h"

#include "common/logging.h"
#include "common/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The body object's actor, and on the actor its clip count: the two words the overlay player reads
 * at 0x004128F4 and 0x004128FA before its assert. */
#define OBJECT_ACTOR    0x14u
#define ACTOR_NUM_CLIPS 0xC8u

/* The swing table's rows are 0x20 bytes apart and a row's clip is its first word; the starter
 * compares that word with the midair clip at 0x0044E94D. */
#define SWING_ROW_BYTES 0x20u

/* The equipped slot of the player record, with the request of a change in flight right behind it.
 * The setter writes the request at 0x0044B4CD and its equip commit copies it into the equipped
 * slot at 0x0044B620; both hold the same value afterwards. */
#define RECORD_WEAPON_SLOT 0x84u

typedef enum starter_kind {
    KIND_WEAPON,
    KIND_SLOT,
    KIND_PUSH,
    KIND_MIDAIR,
    KIND_UNREAD,
    KIND_SET_ONLY,
    KIND_COUNT
} starter_kind_t;

/* The last weapon change refused on one far bank's body. The state path and the event path can
 * both ask about the same change, whichever arrives first, and it is one change. */
typedef struct starter_peer {
    bool     refused;
    uint32_t refused_from;
    uint32_t refused_to;
} starter_peer_t;

typedef struct starter_state {
    starter_peer_t               peer[MP_BANK_FAR_MAX + 1u];   /* by far bank, 0 the spare */
    mp_puppet_starter_counters_t counters;
    bool                         said[KIND_COUNT];
    bool                         set_fault_said;
} starter_state_t;

static starter_state_t starter;

/* An index that is no far bank lands on the spare at 0, which is what a unit test gets. */
static starter_peer_t *peer_of(size_t bank)
{
    return &starter.peer[bank <= MP_BANK_FAR_MAX ? bank : 0u];
}

static void note_unread(size_t bank)
{
    ++starter.counters.unread;
    if (!starter.said[KIND_UNREAD]) {
        starter.said[KIND_UNREAD] = true;
        log_warning("bank %u's body did not read its actor or the actor's clip count in front of "
                    "an engine starter; the starter waits and is not called, and later cases are "
                    "counted", (unsigned)bank);
    }
}

/* The actor and its count, as the overlay player reads them. An object of 0 is a body the window
 * does not have, which is not counted: the caller turns back on it by itself. */
static mp_starter_verdict_t judge(size_t bank, uint32_t object, uint32_t clip, uint32_t *actor,
                                  uint32_t *count)
{
    *actor = 0u;
    *count = 0u;
    if (object == 0u) {
        return MP_STARTER_NOT_YET;
    }
    if (!memory_try_read_u32(object + OBJECT_ACTOR, actor)) {
        note_unread(bank);
        return MP_STARTER_NOT_YET;
    }
    if (*actor == 0u) {
        return MP_STARTER_NEVER;   /* the overlay player asserts on a null actor before the count */
    }
    if (!memory_try_read_u32(*actor + ACTOR_NUM_CLIPS, count)) {
        note_unread(bank);
        return MP_STARTER_NOT_YET;
    }
    return mp_starter_clip_fits(clip, *count) ? MP_STARTER_GO : MP_STARTER_NEVER;
}

/* The shortest clip table any judged actor carried. It is a fact about the bodies this process
 * saw, so a worn body's change keeps it too, although that change is written rather than refused.
 */
static void note_fewest(uint32_t actor, uint32_t count)
{
    if (actor != 0u && count != 0u &&
        (starter.counters.fewest_clips == 0u || count < starter.counters.fewest_clips)) {
        starter.counters.fewest_clips = count;
    }
}

/* The name of the actor a line names, or what stands in for it. */
static const char *actor_name(uint32_t actor, char *name, size_t size)
{
    if (actor == 0u) {
        return "(none)";
    }
    return mp_body_asset_name_of(actor, name, size) ? name : "(unnamed)";
}

/* A refusal for a missing clip: counted by kind, the fewest clips kept, and the first of each kind
 * said with the actor's name and count. */
static void note_missing(size_t bank, starter_kind_t kind, uint32_t clip, uint32_t actor,
                         uint32_t count, uint32_t equipped, uint32_t wanted)
{
    char        name[MP_ACTOR_NAME_BYTES];
    const char *shown;

    if (kind == KIND_WEAPON) {
        ++starter.counters.weapon_missing;
    } else if (kind == KIND_PUSH) {
        ++starter.counters.push_missing;
    } else {
        ++starter.counters.midair_missing;
    }
    note_fewest(actor, count);
    if (starter.said[kind]) {
        return;
    }
    starter.said[kind] = true;
    shown = actor_name(actor, name, sizeof name);
    if (kind == KIND_WEAPON) {
        log_warning("bank %u's body cannot play clip %u, the weapon setter's draw for slot %u, "
                    "because its actor %s carries %u clip(s): the change is not started and the "
                    "body keeps slot %u; later cases are counted", (unsigned)bank, (unsigned)clip,
                    (unsigned)wanted, shown, (unsigned)count, (unsigned)equipped);
        return;
    }
    log_warning("bank %u's body cannot play clip %u, the %s's, because its actor %s carries %u "
                "clip(s): the %s is dropped; later cases are counted", (unsigned)bank,
                (unsigned)clip, kind == KIND_PUSH ? "force push" : "midair swing", shown,
                (unsigned)count, kind == KIND_PUSH ? "push" : "swing");
}

/* A worn body's change: the setter is left alone and the slot is written into the block, so the
 * weapon changes without the draw animation and without the sabre's ignition sound. */
static void note_set_only(size_t bank, uint32_t clip, uint32_t actor, uint32_t count,
                          uint32_t wanted)
{
    char        name[MP_ACTOR_NAME_BYTES];
    const char *shown;

    note_fewest(actor, count);
    if (starter.said[KIND_SET_ONLY]) {
        return;
    }
    starter.said[KIND_SET_ONLY] = true;
    shown = actor_name(actor, name, sizeof name);
    log_warning("bank %u's body cannot play clip %u, the weapon setter's draw for slot %u, "
                "because its actor %s carries %u clip(s): the body wears a model, so slot %u is "
                "written into its block and the change is not animated; later cases are counted",
                (unsigned)bank, (unsigned)clip, (unsigned)wanted, shown, (unsigned)count,
                (unsigned)wanted);
}

static void note_set_fault(size_t bank)
{
    ++starter.counters.set_writes_refused;
    if (!starter.set_fault_said) {
        starter.set_fault_said = true;
        log_warning("bank %u's worn body did not take the weapon slot written into its block; "
                    "the change is asked again next substep and later refusals are counted",
                    (unsigned)bank);
    }
}

static void note_slot(size_t bank, uint32_t equipped, uint32_t wanted)
{
    ++starter.counters.slot_past_table;
    if (!starter.said[KIND_SLOT]) {
        starter.said[KIND_SLOT] = true;
        log_warning("bank %u's body was asked for weapon slot %u, past the %u rows of the "
                    "engine's weapon table: the change is not started and the body keeps slot "
                    "%u; later cases are counted", (unsigned)bank, (unsigned)wanted,
                    (unsigned)MP_STARTER_WEAPON_ROWS, (unsigned)equipped);
    }
}

/* The setter's fold cannot be reached from here. Asking for the slot already held puts the
 * weapon away and commits 0, and both callers, apply_weapon and perform_weapon, turn back when
 * the equipped slot or the request already names the slot asked for. So the slot a worn body is
 * given is always the one that was asked for, and the fold stays where the rule keeps it. */
mp_starter_verdict_t mp_puppet_starter_weapon(size_t bank, uint32_t object, uint32_t equipped,
                                              uint32_t wanted, bool worn)
{
    starter_peer_t      *p = peer_of(bank);
    bool                 slot_ok = mp_starter_weapon_slot_ok(wanted);
    mp_starter_verdict_t verdict = MP_STARTER_NEVER;
    uint32_t             clip = 0u;
    uint32_t             actor = 0u;
    uint32_t             count = 0u;

    if (slot_ok) {
        if (!mp_starter_weapon_clip(equipped, wanted, &clip)) {
            return MP_STARTER_GO;   /* empty hands asked for empty hands: the setter plays none */
        }
        verdict = judge(bank, object, clip, &actor, &count);
        /* A never out of the clip count, not one out of a null actor: the same judgement, one
         * value further, so there is no second predicate for the same question. */
        if (verdict == MP_STARTER_NEVER && actor != 0u && worn) {
            note_set_only(bank, clip, actor, count, wanted);
            return MP_STARTER_SET_ONLY;
        }
    }
    if (verdict != MP_STARTER_NEVER ||
        (p->refused && p->refused_from == equipped && p->refused_to == wanted)) {
        return verdict;
    }
    p->refused      = true;
    p->refused_from = equipped;
    p->refused_to   = wanted;
    if (!slot_ok) {
        note_slot(bank, equipped, wanted);
    } else {
        note_missing(bank, KIND_WEAPON, clip, actor, count, equipped, wanted);
    }
    return MP_STARTER_NEVER;
}

/* Eight bytes in one call, because two words that disagree are a dead end: the engine reads that
 * as a change on its way, every path in the puppet waits for the two to agree before it acts, and
 * nothing would ever commit it. memory_try_write is a copy under a fault handler and eight bytes
 * are two stores, so both words are read back and a half written pair answers false.
 *
 * The write goes into the record the window installed, not into the bank's copy: the window ends
 * by reading the whole record back into the bank, and a write to the copy would be read over.
 *
 * A worn Jedi's light release writes the equipped slot twice in the same window and leaves it as
 * it found it. It runs in the puppet's phase one, between the state path and the events, which is
 * why neither caller here sees a slot of its own making.
 *
 * Nothing else the engine's equip commit does is written: the nodes of the lent rig belong to
 * every body that wears it, the mount node is held at the node count so the engine refuses it,
 * the animation set has no reader for a placed body, and the continuation would lock this body
 * out of every starter until a marker that no clip will ever reach. */
bool mp_puppet_starter_set_slot(size_t bank, uint32_t record, uint32_t slot)
{
    uint32_t words[2];
    uint32_t back[2] = { 0u, 0u };

    words[0] = slot;
    words[1] = slot;
    if (record == 0u ||
        !memory_try_write((uintptr_t)record + RECORD_WEAPON_SLOT, words, sizeof words) ||
        !memory_try_read((uintptr_t)record + RECORD_WEAPON_SLOT, back, sizeof back) ||
        back[0] != slot || back[1] != slot) {
        note_set_fault(bank);
        return false;
    }
    ++starter.counters.weapon_set_worn;
    return true;
}

mp_starter_verdict_t mp_puppet_starter_push(size_t bank, uint32_t object)
{
    uint32_t             actor = 0u;
    uint32_t             count = 0u;
    mp_starter_verdict_t verdict = judge(bank, object, MP_STARTER_CLIP_PUSH, &actor, &count);

    if (verdict == MP_STARTER_NEVER) {
        note_missing(bank, KIND_PUSH, MP_STARTER_CLIP_PUSH, actor, count, 0u, 0u);
    }
    return verdict;
}

bool mp_puppet_starter_swing_overlay(uintptr_t table, uint8_t row)
{
    uint32_t clip = 0u;

    if (table == 0u || !memory_try_read_u32(table + (uintptr_t)row * SWING_ROW_BYTES, &clip)) {
        return row == MP_STARTER_MIDAIR_ROW;
    }
    return mp_starter_swing_overlay_clip(clip, NULL);
}

mp_starter_verdict_t mp_puppet_starter_swing(size_t bank, uint32_t object, bool overlay)
{
    uint32_t             actor = 0u;
    uint32_t             count = 0u;
    mp_starter_verdict_t verdict;

    if (!overlay) {
        return MP_STARTER_GO;   /* the base player refuses a missing clip by itself */
    }
    verdict = judge(bank, object, MP_STARTER_CLIP_MIDAIR, &actor, &count);
    if (verdict == MP_STARTER_NEVER) {
        note_missing(bank, KIND_MIDAIR, MP_STARTER_CLIP_MIDAIR, actor, count, 0u, 0u);
    }
    return verdict;
}

void mp_puppet_starter_reset(size_t bank)
{
    peer_of(bank)->refused = false;
}

void mp_puppet_starter_counters(mp_puppet_starter_counters_t *out)
{
    if (out != NULL) {
        *out = starter.counters;
    }
}
