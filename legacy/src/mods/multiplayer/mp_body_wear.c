/* mp_body_wear.c: a far body in the model its player wears. See the header.
 *
 * SIZE NOTE: within twenty lines of the limit, one subject: the tick, the record it keeps with the
 * overlay, and the puppet window's two blade runs that follow what the tick found. The next line
 * has to split rather than trim, and the seam is the runs: they share nothing with the tick but
 * the engine's word that a body is worn.
 *
 * Four decisions carry the module, and each was a defect in a draft before it was a rule.
 *
 * The body serial, not the object address, tells one body from the next. The engine's object list
 * hands back the lowest free slot, so a body taken down and built again in one call sits at the
 * same address; a field run showed one bank at one address through three different bodies. The
 * body module counts a serial at every spawn and every take down that succeeded, and an answer is
 * taken for the serial the bank has now or not at all.
 *
 * Worn is asked of the engine. The overlay's answer can be for an older body or never arrive, and
 * the body wears the model regardless. The blade ticks, the swept blade and the rebuild follow the
 * render handle, which draws another model than the actor names exactly when something was put on.
 *
 * A rebuild needs an answer and a settled wish. An answer the overlay owes and cannot publish
 * would otherwise rebuild the body every half second, and a table that lags an appearance event
 * flips the wish back for up to a second. The wait is wall time, because the table's period is.
 * The echo is compared, which is what the body wears after the answer, never the name that was
 * refused. A body the overlay could neither dress nor undress is the one that does not wait: half
 * its arrays are the model's and half the hero's, and only a new body is a known state. A model
 * that does that to three of a bank's bodies is not asked for again until the wish changes.
 *
 * One size, one writer. The size is written here and nowhere else, through the engine's own
 * setter, so the draw scale and the culling radius on the render handle agree; the old direct
 * write set the first and left the second at the bind's value.
 */
#include "mp_body_wear.h"

#include "mp_wear_rule.h"

#include "mp_bank.h"
#include "mp_body.h"
#include "mp_body_internal.h"
#include "mp_bridge_far.h"
#include "mp_effects.h"
#include "mp_puppet_sabre.h"
#include "mp_signatures.h"
#include "mp_starter_rule.h"
#include "mp_twist.h"
#include "mp_wallclock.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/model_wear_note.h"
#include "common/patch.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

_Static_assert(MP_BANK_FAR_MAX <= MODEL_WEAR_BANKS,
               "every far bank needs an entry in the records the multiplayer shares with the "
               "overlay");

/* What the tick reads to tell whether a body wears something: the object's actor and render
 * handle, the model the handle draws, and on the actor the model its bind handed the handle and
 * its own scale, which the bind sized the object with. */
#define BAPOBJ_ACTOR 0x14u
#define BAPOBJ_THING 0x9Cu
#define THING_MODEL  0x04u
#define ACTOR_SCALE  0xACu
#define ACTOR_MODEL  0xE0u

/* The equipped slot of a player record. The blade light tick places its light only while the
 * record holds the sabre, so a body whose light is only given back runs the tick with empty hands
 * in the field for the length of the call. */
#define RECORD_WEAPON_SLOT 0x84u

/* The four fields the length step reads and the three it writes. The substep the puppet window
 * copied in, the slot a weapon change is heading for, the blade's length, and the strength and
 * the radius of the light the length setter derives from it. */
#define RECORD_SUBSTEP        0x74u
#define RECORD_REQUESTED_SLOT 0x88u
#define RECORD_BLADE_SIZE     0x210u
#define RECORD_LIGHT_POWER    0x298u
#define RECORD_LIGHT_RANGE    0x29Cu

/* The slot the wish names for a bank with no body. */
#define NO_SLOT 0xFFu

/* Where a worn body's blade hangs, asked of the engine rather than read out of the record.
 *
 * The record's own sabre node was resolved when the body was spawned as its hero, off the hero's
 * rig; a body that has since been dressed in another rig carries its player's weapon at that
 * rig's hand instead. The engine's node lookup by name id is what the overlay's name hook sits
 * on, so asking it for the hero's blade name on such a body gives the hand the weapon is drawn
 * at, and asking it on any other body gives what the record already holds.
 *
 * The lookup's address is read out of the call the swing starter makes to it, at +0x28 of that
 * starter's own site. The displacement there is masked in the pattern the starter is matched
 * with, so the byte is free to read and nothing new has to be found. */
#define START_SWING_FIND_NODE 0x28u
#define NAME_ID_BLADE         9

/* A rebuild that cannot take the body down is tried this often for one body, then left alone
 * with a line: the take down logs every refusal itself, once per substep would be a storm. */
#define REBUILD_ATTEMPTS 3u

typedef void(__cdecl *set_scale_fn_t)(void *object, float x, float y, float z);
typedef int32_t(__cdecl *find_node_fn_t)(void *object, int32_t name_id);

typedef struct wear_bank {
    uint32_t serial;                          /* the body the fields below describe */
    bool     worn;                            /* the engine's word at the last tick */
    bool     answered;                        /* an answer for `serial` is in hand */
    uint8_t  state;                           /* its MODEL_WEAR_STATE_* */
    uint8_t  reason;                          /* its MODEL_WEAR_REASON_* */
    char     echo[MODEL_WEAR_NAME_MAX];       /* what the body wears after it */
    float    worn_scale;                      /* the worn asset's own scale, for WORN */
    char     wanted[MODEL_WEAR_NAME_MAX];     /* the wish, after the deathmatch and the listener */
    uint32_t wish_since;                      /* the wall clock in ms when the wish last changed */
    uint32_t seen_serial;                     /* the body whose first sight in a model was had */
    uint32_t rebuild_serial;                  /* the body a rebuild was asked for */
    uint32_t rebuild_attempts;
    bool     weapon;                          /* and it carries its player's own weapon */
    uint32_t node_serial;                     /* the body whose weapon node was named in the log */
    bool     ask_open;                        /* this body was asked and has no answer yet */
    char     unnamed[MODEL_WEAR_NAME_MAX];    /* the last name that could not be asked for */
    char     broken[MODEL_WEAR_NAME_MAX];     /* the model the overlay answered BROKEN for */
    uint32_t broken_bodies;                   /* the bodies it broke while it was the wish */
    bool     broken_said;
} wear_bank_t;

typedef struct wear_state {
    wear_bank_t              bank[MP_BANK_FAR_MAX];
    model_wear_want_record_t want;            /* the wish as it stands */
    model_wear_want_record_t published;       /* and as the overlay was last told it */
    bool                     ever_published;
    bool                     ready_weapon;     /* the overlay can hang a weapon on a far body */
    find_node_fn_t           find_node;        /* the engine's node lookup by name id */
    bool                     find_node_tried;
    bool                     deathmatch;
    bool                     deathmatch_said;
    bool                     deathmatch_armed; /* which of the two deathmatch lines was said */
    bool                     silence_said;
    bool                     publish_fault_said;
    bool                     scale_site_said;
    bool                     note_down_said;
    bool                     slot_fault_said;
    bool                     blade_step_said;
    bool                     blade_step_fault_said;
    bool                     own_step_said;
    bool                     own_step_fault_said;
    mp_body_wear_counters_t  counters;
} wear_state_t;

static wear_state_t wear;

static const char *const REASONS[MODEL_WEAR_REASON_MAX + 1u] = {
    "no reason given",
    "the name is no row of its model roster",
    "the asset or its model would not load",
    "the rig carries too few of the hero's node names",
    "its swap sites, blade guard or translation are missing",
    "a Jedi body, and the blade guard has not been seen yet",
    "the body did not wear its own actor's model",
    "the body still wears ",
    "no body or pair is left in its translation tables",
    "the rebind failed and the body is its hero again",
    "the rig has no node a push could leave from",
    "the block the wish names is not one it may write",
    "the rebind and the way back both failed",
};

/* A dword of the engine's, read so that a fault is an answer rather than a crash. */
static bool read_u32(uintptr_t address, uint32_t *out)
{
    return memory_try_read(address, out, sizeof *out);
}

static wear_bank_t *state_of(size_t bank)
{
    return mp_bank_index_ok(bank) ? &wear.bank[bank - 1u] : NULL;
}

/* Into a field the records carry: cut inside it and zero after the terminator, so two fields that
 * say the same name are the same bytes. */
static void copy_name(char out[MODEL_WEAR_NAME_MAX], const char *name)
{
    size_t i;

    memset(out, 0, MODEL_WEAR_NAME_MAX);
    for (i = 0; name != NULL && name[i] != '\0' && i + 1u < MODEL_WEAR_NAME_MAX; ++i) {
        out[i] = name[i];
    }
}

static mp_wear_answer_t answer_of(const wear_bank_t *st)
{
    mp_wear_answer_t answer;

    answer.read   = st->answered;
    answer.state  = st->state;
    answer.reason = st->reason;
    answer.echo   = st->echo;
    answer.scale  = st->worn_scale;
    answer.weapon = st->weapon;
    return answer;
}

/* A new body, or none: nothing said or seen about the one before describes it. The wish, how
 * long it has stood and what it broke are the far player's and stay. */
static void forget_body(wear_bank_t *st, uint32_t serial)
{
    st->serial     = serial;
    st->worn       = false;
    st->answered   = false;
    st->state      = MODEL_WEAR_STATE_NONE;
    st->reason     = MODEL_WEAR_REASON_NONE;
    st->worn_scale = 0.0f;
    st->weapon     = false;
    st->node_serial = 0u;
    memset(st->echo, 0, sizeof st->echo);
    st->rebuild_serial   = 0u;
    st->rebuild_attempts = 0u;
}

/* ==============================================================================================
 * The record the overlay reads.
 * ============================================================================================ */

/* A new body ends the ask that stood for the one before it, and a model in the entry is an ask.
 * Whether an ask was answered is asked of the body: a refusal names no model, so a second wish for
 * a body the overlay has answered once cannot be told apart from the first by its answer, and the
 * body's answer stands for both. */
static void note_ask(size_t bank, const model_wear_want_bank_t *entry, bool new_body)
{
    wear_bank_t *st = state_of(bank);

    if (new_body && st->ask_open) {
        st->ask_open = false;
        ++wear.counters.unanswered;
    }
    if (entry->model[0] == '\0') {
        return;
    }
    if (!st->answered) {
        st->ask_open = true;
    }
    ++wear.counters.asked;
    log_info("bank %u asks the overlay for the model %s (body %u)", (unsigned)bank, entry->model,
             (unsigned)entry->serial);
}

/* The wish as it stands, when it differs from what the overlay was last told. A refusal of the
 * channel is said once and tried again at the next tick, because the record is state. */
static void publish(void)
{
    model_wear_want_record_t before = wear.published;
    bool                     first = !wear.ever_published;
    size_t                   i;

    if (!first && memcmp(&wear.want, &wear.published, sizeof wear.want) == 0) {
        return;
    }
    if (!model_wear_publish_want(&wear.want)) {
        ++wear.counters.publish_faults;
        if (!wear.publish_fault_said) {
            wear.publish_fault_said = true;
            log_warning("the far models' wish could not be published to the overlay; it is tried "
                        "again at every substep");
        }
        return;
    }
    wear.published      = wear.want;
    wear.ever_published = true;
    for (i = 0; i < MP_BANK_FAR_MAX; ++i) {
        const model_wear_want_bank_t *was = &before.bank[i];
        const model_wear_want_bank_t *now = &wear.published.bank[i];

        if (first || was->serial != now->serial || strcmp(was->model, now->model) != 0) {
            note_ask(i + 1u, now, first || was->serial != now->serial);
        }
    }
}

/* Bank `bank`'s entry: the body, the persistent block the overlay writes the node indices into,
 * and the wish for a body that stands. A bank with no body wants nothing, which the record's own
 * rule demands. */
static void fill_entry(size_t bank, const wear_bank_t *st, const mp_body_wear_body_t *body)
{
    model_wear_want_bank_t *entry = &wear.want.bank[bank - 1u];

    memset(entry, 0, sizeof *entry);
    entry->serial = body->serial;
    entry->block  = (uint32_t)mp_bank_block_at(bank);
    entry->slot   = NO_SLOT;
    if (body->object != 0u && entry->block != 0u) {
        entry->object = body->object;
        if (body->slot >= 0 && body->slot < (int32_t)NO_SLOT) {
            entry->slot = (uint8_t)body->slot;
        }
        memcpy(entry->model, st->wanted, sizeof entry->model);
    }
}

/* ==============================================================================================
 * The tick.
 * ============================================================================================ */

/* A model the overlay answered BROKEN for is counted against the bank while it stays the wish. */
static void note_broken(wear_bank_t *st, const char *asked)
{
    if (asked[0] == '\0') {
        return;
    }
    if (!mp_wear_rule_same_name(asked, st->broken)) {
        copy_name(st->broken, asked);
        st->broken_bodies = 0u;
        st->broken_said   = false;
    }
    ++st->broken_bodies;
}

/* Step 1. An answer for this body's serial is taken when it differs from the one in hand; an
 * answer for an older body is left alone, because its echo is about a model this body never wore.
 * True when an overlay listens at all. */
static bool read_answer(size_t bank, wear_bank_t *st)
{
    model_wear_done_record_t      done;
    const model_wear_done_bank_t *entry;
    const char                   *asked;

    if (!model_wear_read_done(&done, NULL) || (done.ready & MODEL_WEAR_READY_LISTENING) == 0u) {
        wear.ready_weapon = false;
        return false;
    }
    wear.ready_weapon = (done.ready & MODEL_WEAR_READY_WEAPON) != 0u;
    entry = &done.bank[bank - 1u];
    if (st->serial == 0u || entry->serial != st->serial ||
        entry->state == MODEL_WEAR_STATE_NONE) {
        return true;
    }
    if (st->answered && st->state == entry->state && st->reason == entry->reason &&
        st->weapon == (entry->weapon != 0u) && st->worn_scale == entry->scale &&
        memcmp(st->echo, entry->model, sizeof st->echo) == 0) {
        return true;    /* the same answer, carried again by a publication about another bank */
    }
    st->answered   = true;
    st->state      = entry->state;
    st->reason     = entry->reason;
    st->weapon     = entry->weapon != 0u;
    st->worn_scale = entry->scale;
    memcpy(st->echo, entry->model, sizeof st->echo);

    asked = wear.published.bank[bank - 1u].model;
    if (wear.published.bank[bank - 1u].serial == st->serial) {
        st->ask_open = false;   /* the ask for this body has its answer */
        if (entry->state == MODEL_WEAR_STATE_REFUSED &&
            entry->reason == MODEL_WEAR_REASON_BROKEN) {
            note_broken(st, asked);
        }
    }
    if (entry->state == MODEL_WEAR_STATE_WORN) {
        ++wear.counters.worn;
        log_info("bank %u wears the model %s, put on by the overlay at scale %.2f, %s (body %u)",
                 (unsigned)bank, st->echo, (double)st->worn_scale,
                 st->weapon ? "carrying its player's own weapon at that rig's hand"
                            : "carrying no weapon at that rig's hand",
                 (unsigned)st->serial);
    } else {
        ++wear.counters.refused;
        log_info("bank %u's model %s was refused by the overlay: %s%s (body %u)", (unsigned)bank,
                 asked[0] != '\0' ? asked : "(none asked)",
                 entry->reason <= MODEL_WEAR_REASON_MAX ? REASONS[entry->reason] : "unknown",
                 entry->reason == MODEL_WEAR_REASON_WEARS_OTHER ? st->echo : "",
                 (unsigned)st->serial);
    }
    return true;
}

/* Said once for each deathmatch, and again where the overlay's answer moves the line from one of
 * the two to the other. */
static void say_deathmatch(bool with_weapons)
{
    if (wear.deathmatch_said && wear.deathmatch_armed == with_weapons) {
        return;
    }
    wear.deathmatch_said  = true;
    wear.deathmatch_armed = with_weapons;
    if (with_weapons) {
        log_info("the deathmatch shows far players in their models: the overlay hangs their own "
                 "weapons on the borrowed rigs, so a sabre swung in one strikes what it meets");
        return;
    }
    log_info("the deathmatch shows far players as their heroes: the overlay hangs no weapon on a "
             "far body, and a wearer would swing at a hand nothing measures and strike nobody");
}

/* Step 3. The far player's model is the wish unless no overlay listens, the name is none the
 * records can carry, the model broke MP_BODY_WEAR_BROKEN_LIMIT of the bank's bodies, or a
 * deathmatch runs and no weapon hangs on this body; each of those is said once. Another model on
 * the far player starts that count again. */
static void note_wish(size_t bank, wear_bank_t *st, bool listening, uint32_t now_ms)
{
    mp_wear_answer_t answer = answer_of(st);
    char sender[MODEL_WEAR_NAME_MAX];
    char wanted[MODEL_WEAR_NAME_MAX];
    bool has;

    memset(sender, 0, sizeof sender);
    memset(wanted, 0, sizeof wanted);
    has = mp_bridge_far_model(bank, sender, sizeof sender);
    if (!has || !mp_wear_rule_same_name(sender, st->broken)) {
        memset(st->broken, 0, sizeof st->broken);
        st->broken_bodies = 0u;
        st->broken_said   = false;
    }
    if (!has) {
        /* nothing worn over the hero, nothing to ask */
    } else if (sender[0] == '\0' || !model_wear_name_is_sound(sender)) {
        char shown[MODEL_WEAR_NAME_MAX];

        copy_name(shown, sender[0] != '\0' ? sender : "(a longer name)");
        if (memcmp(shown, st->unnamed, sizeof shown) != 0) {
            memcpy(st->unnamed, shown, sizeof shown);
            log_warning("bank %u's far player wears the model %s, which is no name the overlay can "
                        "be asked for, so the body stays its hero", (unsigned)bank, shown);
        }
    } else if (wear.deathmatch && !mp_wear_rule_deathmatch_asks(wear.ready_weapon, &answer)) {
        say_deathmatch(false);
    } else if (!listening) {
        if (!wear.silence_said) {
            wear.silence_said = true;
            log_info("no overlay listens for the far models, so the far players are shown as "
                     "their heroes");
        }
    } else if (st->broken_bodies >= MP_BODY_WEAR_BROKEN_LIMIT) {
        if (!st->broken_said) {
            st->broken_said = true;
            log_warning("bank %u's model %s broke %u of its bodies, which the overlay could "
                        "neither dress nor take back to their hero; it is not asked for again "
                        "until the far player wears another, and the body stays its hero",
                        (unsigned)bank, st->broken, (unsigned)st->broken_bodies);
        }
    } else {
        if (wear.deathmatch) {
            say_deathmatch(true);
        }
        copy_name(wanted, sender);
    }

    if (memcmp(wanted, st->wanted, sizeof wanted) != 0) {
        memcpy(st->wanted, wanted, sizeof wanted);
        st->wish_since = now_ms;
    }
}

/* Step 4. True when a rebuild was asked for, which ends the tick of this bank: a take down that
 * succeeds calls the body module's listener, and that forgets everything this tick was holding.
 * Only a take down that went through is a rebuild; one the body module refused is counted apart
 * and tried again at the next substep, up to REBUILD_ATTEMPTS times for one body. */
static bool start_rebuild(size_t bank, wear_bank_t *st, uint32_t now_ms)
{
    mp_wear_answer_t answer = answer_of(st);
    char             wore[MODEL_WEAR_NAME_MAX];
    uint32_t         serial = st->serial;
    bool             broken = st->answered && st->reason == MODEL_WEAR_REASON_BROKEN;

    if (!mp_wear_rule_rebuild(st->worn, &answer, st->wanted, now_ms - st->wish_since)) {
        return false;
    }
    if (st->rebuild_serial != serial) {
        st->rebuild_serial   = serial;
        st->rebuild_attempts = 0u;
    }
    if (st->rebuild_attempts >= REBUILD_ATTEMPTS) {
        return false;   /* given up on this body; the tick goes on with it as it stands */
    }
    ++st->rebuild_attempts;
    memcpy(wore, st->echo, sizeof wore);
    if (!mp_body_teardown_at(bank)) {
        ++wear.counters.rebuilds_refused;
        if (st->rebuild_attempts == REBUILD_ATTEMPTS) {
            log_warning("bank %u could not be taken down to be rebuilt in %u attempts; the body "
                        "keeps what it wears until it goes down for another reason",
                        (unsigned)bank, (unsigned)REBUILD_ATTEMPTS);
        }
        return true;
    }
    ++wear.counters.rebuilt;
    if (broken) {
        log_info("bank %u is rebuilt: the overlay could neither put a model on it nor take it "
                 "off again (body %u)", (unsigned)bank, (unsigned)serial);
    } else {
        log_info("bank %u is rebuilt: it wore %s and is asked for %s (body %u)", (unsigned)bank,
                 wore[0] != '\0' ? wore : "a model the overlay does not name",
                 st->wanted[0] != '\0' ? st->wanted : "its hero", (unsigned)serial);
    }
    return true;
}

/* Step 7. The one writer of a far body's size. Compared against what the object carries rather
 * than remembered, so a body built again at its own size is told again, and a body that already
 * carries the size costs one read. The setter writes the render handle's culling radius as well
 * and does not ask whether there is a handle, so an object without one is left alone. */
static void put_size(size_t bank, const wear_bank_t *st, const mp_body_wear_body_t *body)
{
    mp_wear_answer_t answer = answer_of(st);
    set_scale_fn_t   set_scale;
    float            size = 0.0f;
    float            now = 0.0f;

    if (body->object == 0u || body->thing == 0u ||
        !mp_wear_rule_scale(body->factor, body->actor_scale, st->worn, &answer, &size)) {
        return;
    }
    set_scale = (set_scale_fn_t)mp_signatures_address(MP_SITE_BAPOBJ_SET_SCALE);
    if (set_scale == NULL) {
        if (!wear.scale_site_said) {
            wear.scale_site_said = true;
            log_warning("the object scale setter did not resolve, so every far body keeps the "
                        "size it was built at");
        }
        return;
    }
    if (!memory_try_read((uintptr_t)body->object + BAPOBJ_SCALE, &now, sizeof now) ||
        now == size) {
        return;
    }
    set_scale((void *)(uintptr_t)body->object, size, size, size);
    ++wear.counters.sizes_written;
    log_info("bank %u is drawn at %.2f of its size, %.2f in all (%s own scale %.2f)",
             (unsigned)bank, (double)(body->factor > 0.0f ? body->factor : 1.0f), (double)size,
             st->worn ? "the worn model's" : "its actor's",
             (double)(st->worn ? st->worn_scale : body->actor_scale));
}

void mp_body_wear_tick_body(size_t bank, const mp_body_wear_body_t *body, uint32_t now_ms)
{
    wear_bank_t *st = state_of(bank);
    bool         listening;

    if (st == NULL || body == NULL) {
        return;
    }
    if (body->serial != st->serial) {
        forget_body(st, body->serial);
    }
    listening = read_answer(bank, st);
    st->worn  = body->object != 0u && body->worn;
    note_wish(bank, st, listening, now_ms);
    if (start_rebuild(bank, st, now_ms)) {
        return;
    }
    if (st->worn && st->seen_serial != st->serial) {
        /* The puppet's blade, rotations and effects were set up against the hero's rig. The blade
         * is disarmed through the fallback in the next window, never here: an end of a swing run
         * outside the window would land on the local player's own blade. */
        st->seen_serial = st->serial;
        mp_puppet_sabre_reset(bank);
        mp_twist_reset(bank);
        mp_effects_reset(bank);
    }
    fill_entry(bank, st, body);
    publish();
    put_size(bank, st, body);
}

/* The engine half: the body module's record, then three reads per standing body. A read that
 * faults is a body that wears nothing, which leaves the blade ticks running as they always ran. */
static void read_body(size_t bank, mp_body_wear_body_t *out)
{
    const mp_body_far_t *far = mp_body_far_at(bank);
    uint32_t             object = 0u;
    uint32_t             actor = 0u;
    uint32_t             thing = 0u;
    uint32_t             model = 0u;
    uint32_t             actor_model = 0u;

    memset(out, 0, sizeof *out);
    out->slot = -1;
    if (far == NULL) {
        return;
    }
    out->serial = far->serial;
    out->factor = far->scale_wanted;
    if (!far->spawned || !mp_bank_read_at(bank, HERO_BLOCK_HACTOR, &object, sizeof object) ||
        object == 0u) {
        return;
    }
    out->object = object;
    out->slot   = far->slot;
    if (!read_u32((uintptr_t)object + BAPOBJ_ACTOR, &actor) || actor == 0u) {
        return;
    }
    (void)memory_try_read((uintptr_t)actor + ACTOR_SCALE, &out->actor_scale,
                          sizeof out->actor_scale);
    if (!read_u32((uintptr_t)object + BAPOBJ_THING, &thing) || thing == 0u) {
        return;
    }
    out->thing = thing;
    out->worn  = read_u32((uintptr_t)thing + THING_MODEL, &model) &&
                 read_u32((uintptr_t)actor + ACTOR_MODEL, &actor_model) && model != actor_model;
}

void mp_body_wear_tick(size_t bank)
{
    mp_body_wear_body_t body;

    if (!mp_bank_index_ok(bank) || !mp_body_installed()) {
        return;
    }
    read_body(bank, &body);
    mp_body_wear_tick_body(bank, &body, mp_wallclock_ms());
}

void mp_body_wear_note_down(size_t bank)
{
    wear_bank_t         *st = state_of(bank);
    const mp_body_far_t *far = mp_body_far_at(bank);
    mp_body_wear_body_t  gone;

    if (st == NULL || far == NULL) {
        return;
    }
    memset(&gone, 0, sizeof gone);
    gone.serial = far->serial;
    gone.slot   = -1;
    forget_body(st, gone.serial);
    fill_entry(bank, st, &gone);
    ++wear.counters.notes_down;
    publish();
    if (!wear.note_down_said) {
        wear.note_down_said = true;
        log_info("the model wish was published again as bank %u's body went down (body %u); "
                 "every further one is counted in the report", (unsigned)bank,
                 (unsigned)gone.serial);
    }
}

void mp_body_wear_set_deathmatch(bool deathmatch)
{
    if (deathmatch && !wear.deathmatch) {
        wear.deathmatch_said = false;   /* said again for each deathmatch it empties a wish in */
    }
    wear.deathmatch = deathmatch;
}

bool mp_body_wear_worn(size_t bank)
{
    const wear_bank_t *st = state_of(bank);

    return st != NULL && st->worn;
}

bool mp_body_wear_weapon(size_t bank)
{
    const wear_bank_t *st = state_of(bank);

    return st != NULL && st->worn && st->answered && st->state == MODEL_WEAR_STATE_WORN &&
           st->weapon;
}

/* Read once and kept, the way every other address this feature uses is. A lookup that did not
 * come back leaves every worn body's blade at the node its record holds, which is where it was
 * before any of this: a swing with no contact rather than a contact in the wrong place. */
static find_node_fn_t node_lookup(void)
{
    uintptr_t site;
    uintptr_t target = 0u;

    if (wear.find_node_tried) {
        return wear.find_node;
    }
    wear.find_node_tried = true;
    site = mp_signatures_address(MP_SITE_PLR_START_SWING);
    if (site != 0u && patch_read_call_target(site + START_SWING_FIND_NODE, &target) &&
        target != 0u) {
        wear.find_node = (find_node_fn_t)target;
    } else {
        log_warning("the engine's node lookup by name was not read out of the swing starter, so a "
                    "worn far body's block, parry and sparks stay at the node its record holds");
    }
    return wear.find_node;
}

uint32_t mp_body_wear_worn_blade_node(size_t bank, uint32_t object)
{
    wear_bank_t   *st = state_of(bank);
    find_node_fn_t find;
    int32_t        node;

    if (st == NULL || object == 0u || !mp_body_wear_weapon(bank)) {
        return 0u;
    }
    find = node_lookup();
    if (find == NULL) {
        return 0u;
    }
    node = find((void *)(uintptr_t)object, NAME_ID_BLADE);
    if (node <= 0) {
        return 0u;   /* 0 is the engine's own answer for no node, and the pair pass reads it so */
    }
    if (st->node_serial != st->serial) {
        st->node_serial = st->serial;
        ++wear.counters.weapon_nodes;
        log_info("bank %u's block, parry and sparks take the weapon node %d, found by name on "
                 "the worn body (body %u)", (unsigned)bank, (int)node, (unsigned)st->serial);
    }
    return (uint32_t)node;
}

bool mp_body_wear_rig_is_senders(size_t bank)
{
    static const wear_bank_t nobody = { 0u };
    const wear_bank_t       *st = state_of(bank);
    char                     sender[MODEL_WEAR_NAME_MAX];
    mp_wear_answer_t         answer;

    memset(sender, 0, sizeof sender);
    if (mp_bridge_far_model(bank, sender, sizeof sender) && sender[0] == '\0') {
        return false;   /* a model too long to name: whose rig it is cannot be said */
    }
    answer = answer_of(st != NULL ? st : &nobody);
    return mp_wear_rule_rig_is_senders(st != NULL && st->worn, &answer, sender);
}

/* ==============================================================================================
 * The puppet window's two blade ticks.
 * ============================================================================================ */

/* A worn body and a body in its hero's own model are stepped alike and counted and said apart. */
static void note_step_fault(bool worn)
{
    if (worn) {
        ++wear.counters.blade_step_faults;
    } else {
        ++wear.counters.own_step_faults;
    }
    if (worn && !wear.blade_step_fault_said) {
        wear.blade_step_fault_said = true;
        log_warning("a worn body's blade length could not be read or written, so it keeps what it "
                    "held; later refusals are counted");
    } else if (!worn && !wear.own_step_fault_said) {
        wear.own_step_fault_said = true;
        log_warning("a far Jedi's blade length could not be read or written in its block, so it "
                    "keeps what it held; later refusals are counted");
    }
}

static void note_step(bool worn, float size)
{
    if (worn) {
        ++wear.counters.blade_steps;
    } else {
        ++wear.counters.own_steps;
    }
    if (worn && !wear.blade_step_said) {
        wear.blade_step_said = true;
        log_info("a worn body's blade is stepped here without its mesh: length %d thousandths, "
                 "and the light fields with it; later steps are counted", (int)(size * 1000.0f));
    } else if (!worn && !wear.own_step_said) {
        wear.own_step_said = true;
        log_info("a far Jedi's blade is stepped in its own block, never through the engine's "
                 "setter: length %d thousandths, and the light fields with it; later steps are "
                 "counted", (int)(size * 1000.0f));
    }
}

/* The length of a far body's blade, stepped in the block alone.
 *
 * The engine's own tick would hand every new length to the setter, and the setter writes the mesh
 * of the model the body's render handle draws: the hero's mesh, which the local player and every
 * other body of that model draw as well, or for a worn body the borrowed model's. So the tick is
 * not called and its arithmetic is run here over the block's own fields. Whoever draws that
 * player's blade reads the length from the block. */
static bool step_the_length(uintptr_t record, bool worn)
{
    mp_wear_blade_step_t step;
    float                size = 0.0f;
    float                substep = 0.0f;
    uint32_t             weapon = 0u;
    uint32_t             requested = 0u;

    if (record == 0u || !memory_try_read(record + RECORD_BLADE_SIZE, &size, sizeof size) ||
        !memory_try_read(record + RECORD_SUBSTEP, &substep, sizeof substep) ||
        !read_u32(record + RECORD_WEAPON_SLOT, &weapon) ||
        !read_u32(record + RECORD_REQUESTED_SLOT, &requested)) {
        note_step_fault(worn);
        return false;
    }
    if (!mp_wear_rule_blade_step(size, substep, weapon, requested, &step)) {
        return false;   /* neither step ran, or the block held no number: the length stands */
    }
    if (!memory_try_write(record + RECORD_BLADE_SIZE, &step.size, sizeof step.size) ||
        !memory_try_write(record + RECORD_LIGHT_POWER, &step.power, sizeof step.power) ||
        !memory_try_write(record + RECORD_LIGHT_RANGE, &step.range, sizeof step.range)) {
        note_step_fault(worn);
        return false;
    }
    note_step(worn, step.size);
    return true;
}

bool mp_body_wear_blade_state_run(uintptr_t record, mp_blade_tick_t verdict)
{
    if (verdict == MP_BLADE_TICK_STEP || verdict == MP_BLADE_TICK_WORN_STEP) {
        return step_the_length(record, verdict == MP_BLADE_TICK_WORN_STEP);
    }
    if (verdict == MP_BLADE_TICK_WORN) {
        ++wear.counters.blade_ticks_withheld;
    } else {
        ++wear.counters.blade_ticks_bladeless;
    }
    return false;
}

/* The light's tag and cache slot live in the record the window installed, so no tick but this one
 * can give a far body's light back; skipped whole, it left the light standing where the body was
 * dressed. Run only as far as the release for a worn body and for a body without a blade. */
void mp_body_wear_blade_light_run(uintptr_t record, mp_blade_tick_t verdict,
                                  mp_body_wear_engine_fn_t tick)
{
    if (tick == NULL) {
        return;
    }
    if (mp_blade_rule_light_whole(verdict)) {
        tick();
        return;
    }
    if (!mp_body_wear_light_release_only(record, tick)) {
        return;
    }
    if (verdict == MP_BLADE_TICK_WORN) {
        ++wear.counters.light_releases;
    } else {
        ++wear.counters.light_releases_bladeless;
    }
}

static void note_slot_fault(void)
{
    ++wear.counters.slot_writes_refused;
    if (!wear.slot_fault_said) {
        wear.slot_fault_said = true;
        log_warning("the equipped slot around a worn body's blade light tick could not be "
                    "written; later refusals are counted");
    }
}

/* Plr_TickBladeLight 0x00449B9D gives the light's slot back first and always (the call at
 * 0x00449BB7), then reads the equipped slot once (0x00449BC5) and jumps to its epilogue for any
 * slot but the sabre's; the release reads only the light. So another slot in the field for the
 * length of the call is exactly a release, and the field holds what it held before once it
 * returns. */
bool mp_body_wear_light_release_only(uintptr_t record, mp_body_wear_engine_fn_t tick)
{
    uint32_t slot = 0u;
    uint32_t other = MP_STARTER_SLOT_EMPTY;

    if (tick == NULL || record == 0u || !read_u32(record + RECORD_WEAPON_SLOT, &slot)) {
        return false;
    }
    if (slot != MP_STARTER_SLOT_SABRE) {
        tick();
        return true;
    }
    if (!memory_try_write(record + RECORD_WEAPON_SLOT, &other, sizeof other)) {
        note_slot_fault();
        return true;
    }
    tick();
    if (!memory_try_write(record + RECORD_WEAPON_SLOT, &slot, sizeof slot)) {
        note_slot_fault();
    }
    return true;
}

void mp_body_wear_get_counters(mp_body_wear_counters_t *out)
{
    size_t i;

    if (out == NULL) {
        return;
    }
    *out = wear.counters;
    for (i = 0; i < MP_BANK_FAR_MAX; ++i) {
        out->unanswered += wear.bank[i].ask_open ? 1u : 0u;
    }
}
