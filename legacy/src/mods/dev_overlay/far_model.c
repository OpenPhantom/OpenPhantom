/* far_model.c: see far_model.h.
 *
 * SIZE NOTE: a little over six hundred lines, one path: the two passes of a scene end, the facts
 * the decision reads and the dressing it leads to. The seam, if it grows, is the dressing (`dress`,
 * `arm_the_far_weapon` and `go_back`), which is the only part that writes into a body and could
 * take the sites and the table as parameters.
 */
#include "far_model.h"

#include "character_bodies.h"
#include "character_clipcopy.h"
#include "character_facing.h"
#include "character_model.h"
#include "character_model_sites.h"
#include "character_nodemap.h"
#include "character_prop.h"
#include "character_prop_blade.h"
#include "character_prop_body.h"
#include "character_rebind.h"
#include "model_blade_guard.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/model_wear_note.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define BLOCK_BYTES        0x3ACu   /* a player block, as the multiplayer keeps one per bank */
#define BLOCK_BODY         0x0Cu
#define BLOCK_NODE_WORDS   0x40u    /* the six words, the one write this file makes into it  */
#define BLOCK_HERO         0x6Cu
#define BAPOBJ_ACTOR       0x14u
#define BAPOBJ_THING       0x9Cu
#define RDTHING_MODEL3     0x04u
#define BAPACTOR_SCALE     0xACu
#define BAPACTOR_MODEL     0xE0u

/* Hero slots 0 and 1 are the two whose blade the engine resizes when their body is taken down. */
#define JEDI_SLOTS         2u

/* The bounds the note takes a worn asset's own scale inside. An asset outside them is refused
 * before anything is put on, because an answer the note would not take is an answer that could
 * never be given. */
#define SCALE_MIN          0.1f
#define SCALE_MAX          10.0f

#define NAME_BYTES         16u

_Static_assert(sizeof(int32_t) * REBIND_WORDS == 0x18u,
               "the six node words are +0x40 to +0x57 of the block and nothing past them");

typedef struct far_bank {
    uint32_t               block;         /* the first block the bank named, for the process   */
    bool                   answered;      /* `answer` holds for good for this serial and model */
    uint32_t               answered_serial;
    char                   answered_model[MODEL_WEAR_NAME_MAX];
    model_wear_done_bank_t answer;
    char                   worn[MODEL_WEAR_NAME_MAX];   /* what the bank's row was dressed with */
    float                  worn_scale;
    uint32_t               said_serial;   /* the last refusal in the log, said once */
    uint8_t                said_reason;
    char                   said_model[MODEL_WEAR_NAME_MAX];
} far_bank_t;

typedef struct far_state {
    bool                     owed;          /* the channel refused the last answer         */
    bool                     owed_said;
    bool                     probe_said;
    bool                     block_moved;   /* a bank named a second block: nothing is written */
    bool                     want_read;
    uint32_t                 epoch;
    uint32_t                 dressed;       /* since the level began, for the report      */
    uint32_t                 refused;
    uint32_t                 push_away;     /* dressed with the push away from the weapon hand */
    uint32_t                 push_on_hand;  /* and dressed with it on that hand                */
    model_wear_want_record_t want;          /* the last wish that read whole              */
    model_wear_done_record_t published;     /* the last answer the channel took           */
    far_bank_t               bank[MODEL_WEAR_BANKS];
} far_state_t;

static far_state_t far;

/* What the second pass learnt about a bank's body on the way to dressing it. */
typedef struct far_body {
    uintptr_t thing;
    uintptr_t reference;   /* the model the body's actor names: the hero's own */
} far_body_t;

typedef struct far_probe {
    bool done;
    bool shut;
} far_probe_t;

/* ============================================================================================ */

static void copy_name(char out[MODEL_WEAR_NAME_MAX], const char *name)
{
    size_t length = 0;

    memset(out, 0, MODEL_WEAR_NAME_MAX);
    if (name == NULL) {
        return;
    }
    while (length + 1u < MODEL_WEAR_NAME_MAX && name[length] != '\0') {
        ++length;
    }
    memcpy(out, name, length);
}

/* One model is one name whatever its case, the rule the multiplayer holds an echo to its wish with
 * (mp_wear_rule_same_name). A row dressed in a name the wish spells another way is the wish; told
 * apart, the body would be answered as wearing another model, which the multiplayer reads as the
 * wish and does not rebuild, and its rotations would stay withheld. Both are fields of the note,
 * terminated inside their width. */
static bool same_model(const char *a, const char *b)
{
    size_t i;

    for (i = 0; i < MODEL_WEAR_NAME_MAX; ++i) {
        char x = (a[i] >= 'A' && a[i] <= 'Z') ? (char)(a[i] - 'A' + 'a') : a[i];
        char y = (b[i] >= 'A' && b[i] <= 'Z') ? (char)(b[i] - 'A' + 'a') : b[i];

        if (x != y) {
            return false;
        }
        if (x == '\0') {
            return true;
        }
    }
    return true;
}

/* `weapon` is what the multiplayer withholds a worn body's contact, its swept blade and, in a
 * deathmatch, its wish on: a body that wears a rig without its player's weapon on the rig's hand
 * swings at a node nothing measures. */
static void answer_worn(model_wear_done_bank_t *out, const char *model, float scale, bool weapon)
{
    out->state = (uint8_t)MODEL_WEAR_STATE_WORN;
    out->reason = (uint8_t)MODEL_WEAR_REASON_NONE;
    out->weapon = weapon ? 1u : 0u;
    out->scale = scale;
    copy_name(out->model, model);
}

/* The echo is what the body wears after the answer: a refusal because it wears another model
 * names that model, and every other refusal names nothing. */
static void answer_refused(model_wear_done_bank_t *out, uint8_t reason, const char *worn)
{
    out->state = (uint8_t)MODEL_WEAR_STATE_REFUSED;
    out->reason = reason;
    out->scale = 0.0f;
    copy_name(out->model, (reason == (uint8_t)MODEL_WEAR_REASON_WEARS_OTHER) ? worn : NULL);
}

static void publish(const model_wear_done_record_t *record)
{
    if (!far.owed && memcmp(record, &far.published, sizeof *record) == 0) {
        return;
    }
    if (model_wear_publish_done(record)) {
        far.published = *record;
        far.owed = false;
        far.owed_said = false;
        return;
    }
    far.owed = true;
    if (!far.owed_said) {
        far.owed_said = true;
        log_warning("the answer about the far models could not be published; it is owed and said "
                    "again at the next scene end");
    }
}

static uint8_t ready_bits(void)
{
    uint8_t ready = (uint8_t)MODEL_WEAR_READY_LISTENING;

    if (character_model_sites() != NULL && model_blade_guard_is_armed() &&
        character_model_sites_halo_free() != NULL && character_clipcopy_far_reserved()) {
        ready |= (uint8_t)MODEL_WEAR_READY_ABLE;
    }
    /* A reading, and it may be answered before a single detour is installed. The three that draw
     * and measure a borrowed weapon go in at the first far dressing, and a deathmatch that waited
     * for them would never ask for a model on a machine whose own player never swapped one. */
    if (character_prop_sites_ready()) {
        ready |= (uint8_t)MODEL_WEAR_READY_WEAPON;
    }
    return ready;
}

void far_model_install(void)
{
    model_wear_done_record_t record;

    /* Found at load, so the answer can say from the start whether a far body can be dressed. The
     * arc release is no condition of that; it is found here so its line stands at the load too. */
    (void)character_model_sites_halo_free();
    (void)character_model_sites_arc_release();
    memset(&record, 0, sizeof record);
    record.ready = ready_bits();
    publish(&record);
}

/* ============================================================================================ */

/* The first pass. A far body is alive while the note still gives its bank the serial it was
 * dressed for and the same object, and the one predicate of the table holds for it; anything else
 * leaves the table without a byte of the body being written, and a pair goes with its last
 * wearer before the second pass could hand it on. The player's own row was settled by his own tick
 * just before and is only marked here, so a far body may share his pair. */
static void let_go_of_the_dead(uint32_t pass)
{
    const character_bodies_t *table = character_nodemap_bodies();
    uint32_t                  i;

    for (i = 0; i < BODY_MAX; ++i) {
        body_entry_t                  body = table->body[i];
        const model_wear_want_bank_t *wish;
        uint32_t                      b;

        if (!body.used) {
            continue;
        }
        if (body.local) {
            if (character_nodemap_row_holds(i)) {
                character_nodemap_mark(i, pass);
            }
            continue;
        }
        b = (uint32_t)body.bank - 1u;
        wish = &far.want.bank[b];
        if (far.want_read && wish->serial == body.serial && wish->object == body.obj &&
            far.bank[b].worn[0] != '\0' && character_nodemap_row_holds(i)) {
            character_nodemap_mark(i, pass);
            /* Again at the scene end as well as before each draw, for a build whose draw hook
             * could not be placed: then a weapon node the engine showed is seen for one frame. */
            character_rebind_hide(body.thing, body.target, body.hidden, body.hidden_count);
            continue;
        }
        character_nodemap_disarm(body.thing);
        character_prop_far_disarm(body.bank);
        log_info("a far body's entry was let go: bank %u, it no longer wears %s", b + 1u,
                 far.bank[b].worn);
        far.bank[b].worn[0] = '\0';
        far.bank[b].worn_scale = 0.0f;
    }
}

/* ============================================================================================ */

/* The block a wish names is the multiplayer's, and the contract says what makes it one to write:
 * the same block for the bank for the whole process, aligned, outside the executable's image,
 * readable over a whole player block, and a hero index that is the slot the body was built on. A
 * bank that names a second block breaks the first of those for every bank, and from then on no
 * far body is dressed. */
static bool block_is_sound(uint32_t b, const model_wear_want_bank_t *wish)
{
    uint32_t hero = 0;

    if (far.block_moved) {
        return false;
    }
    if (far.bank[b].block == 0u) {
        far.bank[b].block = wish->block;
    } else if (far.bank[b].block != wish->block) {
        far.block_moved = true;
        log_warning("bank %u names the block %08X after %08X, and a bank's block is the "
                    "multiplayer's for the whole process; no far body is dressed from now on",
                    b + 1u, (unsigned)wish->block, (unsigned)far.bank[b].block);
        return false;
    }
    return (wish->block & 3u) == 0u && !memory_is_inside_image(wish->block, BLOCK_BYTES) &&
           memory_try_readable(wish->block, BLOCK_BYTES) &&
           memory_try_read(wish->block + BLOCK_HERO, &hero, sizeof hero) &&
           hero == (uint32_t)wish->slot;
}

/* The swap's own resolution, asked for here too: a player who never opened the panel has to see
 * the far models as well. And what only the far bodies need: one site, and the arena shares of
 * the pairs past the player's own, which may be refused while his stands. */
static bool far_able(void)
{
    return character_model_resolve() && character_model_sites_halo_free() != NULL &&
           character_clipcopy_far_reserved();
}

/* No bank window can be open at the end of a scene: every one of them opens and closes inside one
 * call of a substep. This asks anyway, because a window that were open would put a far body's
 * record where the player's block is, and dressing then writes the wrong body's words. The cell
 * names none of the banks' blocks, and the block it names holds none of their objects. */
static bool window_is_shut(void)
{
    const character_model_sites_t *sites = character_model_sites();
    uint32_t                       block = 0;
    uint32_t                       body = 0;
    uint32_t                       i;

    if (sites == NULL || !memory_try_read(sites->player_record, &block, sizeof block)) {
        return false;
    }
    if (block == 0u) {
        return true;
    }
    for (i = 0; i < MODEL_WEAR_BANKS; ++i) {
        if (far.want.bank[i].block != 0u && block == far.want.bank[i].block) {
            return false;
        }
    }
    if (!memory_try_read((uintptr_t)block + BLOCK_BODY, &body, sizeof body)) {
        return false;
    }
    for (i = 0; i < MODEL_WEAR_BANKS; ++i) {
        if (far.want.bank[i].object != 0u && body == far.want.bank[i].object) {
            return false;
        }
    }
    return true;
}

static bool probe_window(far_probe_t *probe)
{
    if (!probe->done) {
        probe->done = true;
        probe->shut = window_is_shut();
        if (!probe->shut && !far.probe_said) {
            far.probe_said = true;
            log_warning("the window probe found a window open at the scene end; the far bodies "
                        "wait");
        }
    }
    return probe->shut;
}

/* A fresh hero wears the model its actor names; only this file ever puts another on a far body. */
static bool body_is_fresh(uintptr_t object, far_body_t *body)
{
    uint32_t thing = 0;
    uint32_t worn = 0;
    uint32_t actor = 0;
    uint32_t model = 0;

    if (!memory_try_read(object + BAPOBJ_THING, &thing, sizeof thing) || thing == 0u ||
        !memory_try_read((uintptr_t)thing + RDTHING_MODEL3, &worn, sizeof worn) ||
        !memory_try_read(object + BAPOBJ_ACTOR, &actor, sizeof actor) || actor == 0u ||
        !memory_try_read((uintptr_t)actor + BAPACTOR_MODEL, &model, sizeof model) ||
        model == 0u) {
        return false;
    }
    body->thing = (uintptr_t)thing;
    body->reference = (uintptr_t)model;
    return worn == model;
}

/* The facts in the order the decision reads them, and nothing past the first that fails. */
static void gather(uint32_t b, const model_wear_want_bank_t *wish, far_probe_t *probe,
                   far_facts_t *facts, far_body_t *body)
{
    uint32_t hero = 0;
    uint32_t named = 0;

    facts->block_sound = block_is_sound(b, wish);
    if (!facts->block_sound) {
        return;
    }
    facts->able = far_able();
    if (!facts->able) {
        return;
    }
    facts->jedi = memory_try_read(wish->block + BLOCK_HERO, &hero, sizeof hero) &&
                  hero < JEDI_SLOTS;
    facts->blade_seen = character_model_blade_lock_seen();
    if (facts->jedi && !facts->blade_seen) {
        return;
    }
    facts->window_shut = probe_window(probe);
    if (!facts->window_shut) {
        return;
    }
    facts->block_names_body = memory_try_read(wish->block + BLOCK_BODY, &named, sizeof named) &&
                              named == wish->object;
    if (!facts->block_names_body) {
        return;
    }
    facts->fresh = body_is_fresh(wish->object, body);
}

/* ============================================================================================ */

/* Back to the hero after the rebind or what followed it failed: the row goes, and the hero's own
 * model is bound again, which sizes the arrays for his rig. The six words were not written and no
 * card was taken off, so nothing else is owed. When even that bind fails the body is broken, and
 * the multiplayer builds it again. */
static void go_back(uint32_t b, const far_body_t *body, uint8_t reason,
                    model_wear_done_bank_t *out)
{
    character_nodemap_disarm(body->thing);
    character_prop_far_disarm((uint8_t)(b + 1u));
    if (character_rebind_bind(character_model_sites(), body->thing, body->reference)) {
        answer_refused(out, reason, NULL);
        return;
    }
    log_warning("bank %u's body could not be bound back to its own model %08X after a failed "
                "dressing; it is answered as broken and the multiplayer builds it again", b + 1u,
                (unsigned)body->reference);
    answer_refused(out, (uint8_t)MODEL_WEAR_REASON_BROKEN, NULL);
}

/* The far player's own weapon, drawn at the borrowed rig's hand. The borrowed rig has nothing to
 * show, so what is drawn is a second render handle bound to the far player's HERO model, which is
 * where his weapon meshes live, placed with the one matrix that re-parents them onto the hand of
 * the rig he is wearing.
 *
 * The hidden words are handed over rather than collected again: this file writes them per frame
 * and takes them back, and one visibility word with two writers is how a rig ends up with half of
 * itself invisible.
 *
 * Resolve first, then hook, the order character_model.c keeps for the player's own swap. The three
 * detours the weapon needs are cut from untouched bytes, and one of them sits on a prologue the
 * arm itself matches; a branch written there first leaves the pattern invisible to the arm. On a
 * machine whose player never swapped a model himself they are not installed at all, so this is
 * also the only place they ever come up for a far body. */
static void arm_the_far_weapon(uint32_t b, const model_wear_want_bank_t *wish,
                               const far_body_t *body, const uint32_t *hidden,
                               uint32_t hidden_count)
{
    const character_model_sites_t *sites = character_model_sites();
    prop_far_t                     spec;

    if (sites == NULL) {
        return;
    }
    memset(&spec, 0, sizeof spec);
    spec.bank = (uint8_t)(b + 1u);
    spec.record = sites->player_record;
    spec.block = wish->block;
    spec.obj = wish->object;
    spec.thing = body->thing;
    spec.reference = body->reference;
    spec.hidden = hidden;
    spec.hidden_count = hidden_count;
    spec.serial = wish->serial;
    if (!character_prop_far_arm(&spec)) {
        return;
    }
    /* The install is asked whether it held. Without the two detours the weapon is neither drawn
     * nor measured nor answered for, and the line the arm has just written would stand in the log
     * beside a body carrying nothing. The row goes back, so what is said and what is carried are
     * the same thing. */
    if (!character_prop_draw_install()) {
        log_warning("the far body in bank %u carries nothing after all: the places a borrowed "
                    "weapon is drawn and shot from are not hooked", b + 1u);
        character_prop_far_disarm(spec.bank);
    }
}

/* The dressing, in the order the control passes settled on: everything that can refuse without
 * touching the body first, then the translation, then the rebind, and the words, the cards and
 * the hidden nodes only after a rebind that held. */
static void dress(uint32_t b, const model_wear_want_bank_t *wish, uint32_t pass,
                  const far_body_t *body, model_wear_done_bank_t *out)
{
    halo_free_fn_t halo_free = character_model_sites_halo_free();
    nodemap_body_t spec;
    nodemap_fit_t  fit;
    uintptr_t      model = 0u;
    void          *asset;
    int32_t        id = character_model_candidate_of(wish->model);
    int32_t        words[REBIND_WORDS];
    uint32_t       hidden[BODY_HIDE_MAX];
    uint32_t       hidden_count;
    uint32_t       bits = 0;
    uint32_t       named = 0;
    float          scale = 0.0f;
    bool           push_on_hand = false;
    char           push[NAME_BYTES];

    if (id < 0) {
        answer_refused(out, (uint8_t)MODEL_WEAR_REASON_NO_ROW, NULL);
        return;
    }
    asset = character_model_load((uint32_t)id, &model);
    if (asset == NULL || model == 0u ||
        !memory_try_read((uintptr_t)asset + BAPACTOR_SCALE, &bits, sizeof bits)) {
        answer_refused(out, (uint8_t)MODEL_WEAR_REASON_NO_ASSET, NULL);
        return;
    }
    memcpy(&scale, &bits, sizeof scale);
    if (!(scale >= SCALE_MIN && scale <= SCALE_MAX)) {
        answer_refused(out, (uint8_t)MODEL_WEAR_REASON_NO_ASSET, NULL);
        return;
    }
    /* The model the body's own actor names: there is nothing to translate and nothing to bind,
     * and no row of the prop table either. The body carries a weapon all the same, the one the
     * engine draws for its own actor, so the answer says so rather than leaving the multiplayer
     * to read an unarmed row and rebuild a body that is already right. */
    if (model == body->reference) {
        answer_worn(out, wish->model, scale, true);
        return;
    }
    if (!character_nodemap_measure(body->reference, model, &fit) ||
        !character_nodemap_fit_is_offered(&fit)) {
        answer_refused(out, (uint8_t)MODEL_WEAR_REASON_FIT, NULL);
        return;
    }
    /* The hidden set comes before the words, because the left hand may not be chosen inside it. */
    hidden_count = character_rebind_own_weapons(model, hidden, BODY_HIDE_MAX);
    if (!character_rebind_far_nodes(model, hidden, hidden_count, words, &push_on_hand)) {
        answer_refused(out, (uint8_t)MODEL_WEAR_REASON_NO_HAND, NULL);
        return;
    }

    memset(&spec, 0, sizeof spec);
    spec.thing = body->thing;
    spec.obj = wish->object;
    spec.block = wish->block;
    spec.reference = body->reference;
    spec.target = model;
    spec.bank = (uint8_t)(b + 1u);
    spec.serial = wish->serial;
    spec.share_pass = pass;
    if (!character_nodemap_arm(&spec)) {
        answer_refused(out, (uint8_t)MODEL_WEAR_REASON_NO_ROOM, NULL);
        return;
    }
    (void)character_facing_arm(body->thing);

    /* The lightning arcs on the body hold node indices of the hero's rig, like its glow cards. */
    character_model_sites_let_go_of_arcs((uintptr_t)wish->object);
    if (!character_rebind_bind(character_model_sites(), body->thing, model)) {
        go_back(b, body, (uint8_t)MODEL_WEAR_REASON_BIND_FAILED, out);
        return;
    }
    /* The block is asked again, and then the row as every hook will ask it: a body the pose hook
     * would not recognise would be driven by the hero's clips by ordinal, so it goes back. */
    if (!memory_try_read(wish->block + BLOCK_BODY, &named, sizeof named) ||
        named != wish->object || !character_nodemap_holds(body->thing, NULL)) {
        go_back(b, body, (uint8_t)MODEL_WEAR_REASON_BIND_FAILED, out);
        return;
    }
    if (!memory_try_write(wish->block + BLOCK_NODE_WORDS, words, sizeof words)) {
        go_back(b, body, (uint8_t)MODEL_WEAR_REASON_BAD_BLOCK, out);
        return;
    }
    /* The glow cards hang on node indices of the hero's rig, and those name other joints now. */
    halo_free((void *)wish->object);
    character_rebind_hide(body->thing, model, hidden, hidden_count);
    (void)character_nodemap_set_hidden(body->thing, hidden, hidden_count);
    arm_the_far_weapon(b, wish, body, hidden, hidden_count);

    answer_worn(out, wish->model, scale, character_prop_far_carries((uint8_t)(b + 1u)));
    copy_name(far.bank[b].worn, wish->model);
    far.bank[b].worn_scale = scale;
    far.dressed++;
    if (!character_rebind_node_name(model, words[REBIND_WORD_LHAND], push, sizeof push)) {
        memcpy(push, "?", 2u);
    }
    log_info("the far body in bank %u wears %s: %u of its %u nodes driven, the push leaves from "
             "%s (body %u)", b + 1u, wish->model, fit.matched, fit.target_nodes, push,
             (unsigned)wish->serial);
    if (!push_on_hand) {
        far.push_away++;
        return;
    }
    far.push_on_hand++;
    log_warning("the only node this rig's push can leave from is the hand its weapon hangs on, so "
                "the push starts at the weapon rather than at the fist");
}

static void say_refusal(uint32_t b, const model_wear_want_bank_t *wish,
                        const model_wear_done_bank_t *out)
{
    far_bank_t *fb = &far.bank[b];

    if (out->state != (uint8_t)MODEL_WEAR_STATE_REFUSED ||
        (fb->said_serial == wish->serial && fb->said_reason == out->reason &&
         same_model(fb->said_model, wish->model))) {
        return;
    }
    fb->said_serial = wish->serial;
    fb->said_reason = out->reason;
    copy_name(fb->said_model, wish->model);
    far.refused++;
    log_info("the far body in bank %u is not dressed: %s (body %u)", b + 1u,
             character_bodies_reason_text(out->reason), (unsigned)wish->serial);
}

/* The second pass, for one bank. */
static void answer_bank(uint32_t b, uint32_t pass, far_probe_t *probe,
                        model_wear_done_bank_t *out)
{
    const character_bodies_t     *table = character_nodemap_bodies();
    const model_wear_want_bank_t *wish = &far.want.bank[b];
    far_bank_t                   *fb = &far.bank[b];
    far_facts_t                   facts;
    far_body_t                    body;
    far_step_t                    step;

    memset(out, 0, sizeof *out);
    memset(&facts, 0, sizeof facts);
    memset(&body, 0, sizeof body);
    out->serial = wish->serial;

    facts.asked = wish->serial != 0u && wish->object != 0u;
    facts.wants = wish->model[0] != '\0';
    facts.entry = character_bodies_find_bank(table, b + 1u) != BODY_NONE;
    facts.entry_is_wish = facts.entry && same_model(fb->worn, wish->model);
    facts.answered = fb->answered && fb->answered_serial == wish->serial &&
                     same_model(fb->answered_model, wish->model);
    if (!facts.entry && facts.asked && facts.wants && !facts.answered) {
        gather(b, wish, probe, &facts, &body);
    }

    step = character_bodies_far_step(&facts);
    switch (step.verdict) {
    case FAR_KEEP:
        *out = fb->answer;
        return;
    case FAR_WORN:
        answer_worn(out, fb->worn, fb->worn_scale,
                    character_prop_far_carries((uint8_t)(b + 1u)));
        return;
    case FAR_REFUSE:
        answer_refused(out, step.reason, fb->worn);
        break;
    case FAR_LOAD:
        dress(b, wish, pass, &body, out);
        break;
    case FAR_NONE:
    case FAR_WAIT:
    default:
        return;
    }
    say_refusal(b, wish, out);
    if (character_bodies_find_bank(table, b + 1u) == BODY_NONE &&
        character_bodies_far_final(out->state, out->reason)) {
        fb->answered = true;
        fb->answered_serial = wish->serial;
        copy_name(fb->answered_model, wish->model);
        fb->answer = *out;
    }
}

/* What the far bodies came to in the level that ended, said once when the panel's engine node
 * counts a new world. Nothing is said for a level in which no far body asked for anything. */
static void report_level(void)
{
    const character_bodies_t *table = character_nodemap_bodies();
    uint32_t                  pairs = 0;
    uint32_t                  p;

    character_prop_body_report();
    character_prop_blade_report();
    if (far.dressed == 0u && far.refused == 0u) {
        return;
    }
    for (p = 0; p < PAIR_MAX; ++p) {
        if (table->pair[p].used) {
            ++pairs;
        }
    }
    log_info("far bodies dressed: %u, refused %u, pairs in use %u", far.dressed, far.refused,
             pairs);
    /* A line of its own, and one number per direction. The first grows in the ordinary case, so a
     * run comparison can say the rule stopped running; the second must be 0, so a run comparison
     * can say it started. A number more in the line above would have made that line a different
     * line to a reader comparing two runs. */
    log_info("far pushes: %u dressed with the push away from the weapon hand, %u with it on the "
             "hand", far.push_away, far.push_on_hand);
    far.dressed = 0u;
    far.refused = 0u;
    far.push_away = 0u;
    far.push_on_hand = 0u;
}

void far_model_tick(uint32_t epoch)
{
    model_wear_done_record_t record;
    model_wear_want_record_t want;
    far_probe_t              probe;
    uint32_t                 pass;
    uint32_t                 b;

    /* A frame went by. The next object dispatched opens the pass the weapon spheres are stamped
     * with, which is what keeps one measured in the last frame's draws answering through this
     * frame's substeps and stops one older than that. */
    character_prop_body_frame_ended();
    if (epoch != far.epoch) {
        report_level();
        far.epoch = epoch;
    }
    /* A wish that does not read whole leaves the last one standing: a row can only have come from
     * a wish, and the last one read is what it is checked against. */
    if (model_wear_read_want(&want, NULL)) {
        far.want = want;
        far.want_read = true;
    }
    pass = character_nodemap_begin_pass();
    let_go_of_the_dead(pass);

    memset(&record, 0, sizeof record);
    memset(&probe, 0, sizeof probe);
    if (far.want_read) {
        for (b = 0; b < MODEL_WEAR_BANKS; ++b) {
            answer_bank(b, pass, &probe, &record.bank[b]);
        }
    }
    record.ready = ready_bits();
    publish(&record);
}
