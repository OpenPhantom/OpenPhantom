/* character_bodies.c: the tables of the swap and every decision made over them, without a game.
 *
 * Three things are held still here. The tables themselves: a body per handle and per owner, a
 * pair shared only with a wearer found alive in the same pass, and the pair released with its last
 * wearer. The reference run: the player's own swap was a single body in a handful of globals, and
 * that logic is copied into this file as it was and run beside the table, step for step, through
 * the sequences a player can produce; wherever the two may differ the difference is named and
 * checked on its own. And the two decisions that used to live inside engine code: the blade
 * guard's question and the far path's answer for one bank.
 *
 * SIZE NOTE: over 600 lines. The reference run carries its own copy of the single body logic and a
 * pretend engine, and both have to sit beside the table they are compared against; split off,
 * the copy would be a second program linking the same module to check the same rows.
 */
#include "unittest.h"

#include "character_bodies.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* ==============================================================================================
 * A pretend engine: which handle an object owns, what a handle wears, how many nodes a model has,
 * what an actor names and which body a block holds. Addresses are small numbers, which is all the
 * tables ever compare.
 * ============================================================================================ */
#define SLOTS 16u

typedef struct engine {
    uintptr_t obj_thing[SLOTS];     /* by object                  */
    uintptr_t obj_actor_model[SLOTS];
    uintptr_t thing_worn[SLOTS];    /* by handle                  */
    uint32_t  model_nodes[SLOTS];   /* by model                   */
    uintptr_t block_body[SLOTS];    /* by block                   */
} engine_t;

static engine_t engine;

/* Every address is a distinct small number: objects 1 to 3, handles 4 to 6, the models HERO,
 * TARGET and OTHER 7 to 9, blocks 10 and 11. */
#define OBJ_PLAYER   1u
#define OBJ_FAR_A    2u
#define OBJ_FAR_B    3u
#define THING_A      4u
#define THING_B      5u
#define THING_C      6u
#define HERO         7u
#define TARGET       8u
#define OTHER        9u
#define BLOCK_A      10u
#define BLOCK_B      11u

static void engine_body(uintptr_t obj, uintptr_t thing, uintptr_t actor_model, uintptr_t worn)
{
    engine.obj_thing[obj] = thing;
    engine.obj_actor_model[obj] = actor_model;
    engine.thing_worn[thing] = worn;
}

/* The reads character_nodemap.c makes, in the same order and with the same early stops. */
static void observe(const body_entry_t *body, body_reads_t *reads)
{
    memset(reads, 0, sizeof *reads);
    if (!body->local) {
        reads->block_body = engine.block_body[body->block];
        if (reads->block_body != body->obj) {
            return;
        }
    }
    reads->obj_thing = engine.obj_thing[body->obj];
    if (reads->obj_thing != body->thing) {
        return;
    }
    reads->worn = engine.thing_worn[body->thing];
    if (reads->worn != body->target) {
        return;
    }
    reads->worn_nodes = engine.model_nodes[reads->worn];
    reads->actor_model = engine.obj_actor_model[body->obj];
}

static body_entry_t far_entry(uint8_t bank, uintptr_t obj, uintptr_t thing, uintptr_t block,
                              uint32_t pair)
{
    body_entry_t body;

    memset(&body, 0, sizeof body);
    body.bank = bank;
    body.serial = 7u;
    body.obj = obj;
    body.thing = thing;
    body.block = block;
    body.reference = HERO;
    body.target = TARGET;
    body.target_nodes = engine.model_nodes[TARGET];
    body.pair = pair;
    return body;
}

static void reset_engine(void)
{
    memset(&engine, 0, sizeof engine);
    engine.model_nodes[HERO] = 48u;
    engine.model_nodes[TARGET] = 17u;
    engine.model_nodes[OTHER] = 30u;
}

/* ============================================================================================ */

static void check_the_tables(void)
{
    character_bodies_t t;
    body_entry_t       body;
    bool               fresh = false;
    uint32_t           pair;
    uint32_t           local;
    uint32_t           far;

    reset_engine();
    memset(&t, 0, sizeof t);

    ut_section("a body per handle and per owner, and as many as the note has banks plus one");
    ut_check(BODY_MAX == 1u + MODEL_WEAR_BANKS, "the table holds the player and every far bank");
    ut_check(PAIR_MAX >= BODY_MAX, "and every body can hold a pair of its own");
    ut_check(!character_bodies_any(&t), "an empty table has nobody in it");

    pair = character_bodies_pair_take(&t, HERO, TARGET, 17u, 0u, &fresh);
    ut_check(pair != BODY_NONE && fresh, "a first pair is fresh and has to be measured");
    memset(&body, 0, sizeof body);
    body.local = true;
    body.obj = OBJ_PLAYER;
    body.thing = THING_A;
    body.reference = HERO;
    body.target = TARGET;
    body.target_nodes = 17u;
    body.pair = pair;
    local = character_bodies_add(&t, &body);
    ut_check(local != BODY_NONE, "the player's own body is filed");
    ut_check(character_bodies_add(&t, &body) == BODY_NONE, "the same handle twice is refused");

    body = far_entry(1u, OBJ_FAR_A, THING_B, BLOCK_A, pair);
    far = character_bodies_add(&t, &body);
    ut_check(far != BODY_NONE && far != local, "a far body gets a row of its own");
    ut_check(character_bodies_find(&t, THING_B) == far,
             "and it is found by its handle, whichever row it sits in");
    ut_check(character_bodies_find(&t, THING_A) == local, "as is the player's");
    ut_check(character_bodies_find_bank(&t, 0u) == local, "the player is bank 0");
    ut_check(character_bodies_find_bank(&t, 1u) == far, "and the far body its own bank");
    ut_check(character_bodies_find(&t, THING_C) == BODY_NONE, "a handle nobody filed has no row");

    body = far_entry(1u, OBJ_FAR_B, THING_C, BLOCK_B, pair);
    ut_check(character_bodies_add(&t, &body) == BODY_NONE, "a bank has one body, not two");
    body = far_entry(1u, OBJ_FAR_B, THING_C, BLOCK_B, pair);
    body.local = true;
    ut_check(character_bodies_add(&t, &body) == BODY_NONE,
             "and a row that calls itself the player's but names a far bank is refused");
    body = far_entry((uint8_t)(MODEL_WEAR_BANKS + 1u), OBJ_FAR_B, THING_C, BLOCK_B, pair);
    ut_check(character_bodies_add(&t, &body) == BODY_NONE, "as is a bank the note has not got");
    body = far_entry(2u, OBJ_FAR_B, THING_C, BLOCK_B, PAIR_MAX);
    ut_check(character_bodies_add(&t, &body) == BODY_NONE, "and a pair that does not exist");

    ut_section("a pair is released with its last wearer, not with its first");
    ut_check(t.pair[pair].wearers == 2u, "two bodies wear the one pair");
    ut_check(character_bodies_remove(&t, local) == BODY_NONE,
             "the first to go leaves the pair standing");
    ut_check(t.pair[pair].used, "and it is still there");
    ut_check(character_bodies_remove(&t, far) == pair, "the last to go gives it back");
    ut_check(!t.pair[pair].used, "and then it is gone");
    ut_check(character_bodies_remove(&t, far) == BODY_NONE, "a row taken out twice gives nothing");
    ut_check(!character_bodies_any(&t), "and the table is empty again");
}

/* The case a control pass found: two far bodies of one hero in one model share a pair, a level
 * ends, the hero asset is freed and loaded again at the same address, and the next body arrives
 * with the same two pointers. Its pair must be measured fresh, because the old one's copies point
 * into the freed pool. */
static void check_the_same_pointers_after_a_rebuild_make_a_new_pair(void)
{
    character_bodies_t t;
    body_entry_t       body;
    bool               fresh = false;
    uint32_t           pass;
    uint32_t           p;
    uint32_t           q;
    uint32_t           made;
    uint32_t           a;
    uint32_t           b;

    reset_engine();
    memset(&t, 0, sizeof t);

    ut_section("a pair is shared only with a wearer found alive in the same pass");
    pass = character_bodies_begin_pass(&t);
    ut_check(pass != 0u, "a pass is never zero");
    p = character_bodies_pair_take(&t, HERO, TARGET, 17u, pass, &fresh);
    ut_check(fresh, "nothing to share yet, so the pair is fresh");
    made = t.pair[p].made;
    body = far_entry(1u, OBJ_FAR_A, THING_A, BLOCK_A, p);
    body.checked = pass;
    a = character_bodies_add(&t, &body);
    q = character_bodies_pair_take(&t, HERO, TARGET, 17u, pass, &fresh);
    ut_check(q == p && !fresh, "a second body of the same hero in the same model plays it too");
    body = far_entry(2u, OBJ_FAR_B, THING_B, BLOCK_B, q);
    body.checked = pass;
    b = character_bodies_add(&t, &body);
    q = character_bodies_pair_take(&t, HERO, TARGET, 18u, pass, &fresh);
    ut_check(q != p && fresh, "the same two pointers with another node count are another pair");
    character_bodies_pair_drop(&t, q);
    ut_check(!t.pair[q].used, "and a pair nobody came to wear can be given back");
    q = character_bodies_pair_take(&t, HERO, TARGET, 17u, 0u, &fresh);
    ut_check(q != p && fresh, "a caller that names no pass never shares");
    character_bodies_pair_drop(&t, q);

    ut_section("both wearers taken down, then the same pointers: a new pair");
    pass = character_bodies_begin_pass(&t);   /* nobody is found alive in this one */
    (void)character_bodies_remove(&t, a);
    ut_check(character_bodies_remove(&t, b) == p, "the pair goes with its last wearer");
    q = character_bodies_pair_take(&t, HERO, TARGET, 17u, pass, &fresh);
    ut_check(fresh, "the next body with the same pointers gets a pair measured afresh");
    ut_check(t.pair[q].made != made, "and it is a new pair, not the old one handed back");

    ut_section("one wearer alive: the pair is kept and shared, because it holds the asset");
    memset(&t, 0, sizeof t);
    pass = character_bodies_begin_pass(&t);
    p = character_bodies_pair_take(&t, HERO, TARGET, 17u, pass, &fresh);
    body = far_entry(1u, OBJ_FAR_A, THING_A, BLOCK_A, p);
    a = character_bodies_add(&t, &body);
    body = far_entry(2u, OBJ_FAR_B, THING_B, BLOCK_B, p);
    b = character_bodies_add(&t, &body);
    pass = character_bodies_begin_pass(&t);
    character_bodies_mark(&t, b, pass);
    ut_check(character_bodies_remove(&t, a) == BODY_NONE, "the dead one goes, the pair stays");
    q = character_bodies_pair_take(&t, HERO, TARGET, 17u, pass, &fresh);
    ut_check(q == p && !fresh, "and the rebuilt body shares it with the living one");

    ut_section("a wearer nobody looked at in this pass is not trusted");
    pass = character_bodies_begin_pass(&t);
    q = character_bodies_pair_take(&t, HERO, TARGET, 17u, pass, &fresh);
    ut_check(q != p && fresh, "the same pointers get a fresh pair when no wearer was checked");
}

/* ============================================================================================ */

static void check_the_one_predicate(void)
{
    body_entry_t local;
    body_entry_t far;
    body_reads_t reads;

    reset_engine();
    memset(&local, 0, sizeof local);
    local.used = true;
    local.local = true;
    local.obj = OBJ_PLAYER;
    local.thing = THING_A;
    local.reference = HERO;
    local.target = TARGET;
    local.target_nodes = 17u;
    far = far_entry(1u, OBJ_FAR_A, THING_B, BLOCK_A, 0u);
    far.used = true;

    engine_body(OBJ_PLAYER, THING_A, HERO, TARGET);
    engine_body(OBJ_FAR_A, THING_B, HERO, TARGET);
    engine.block_body[BLOCK_A] = OBJ_FAR_A;

    ut_section("a body holds while every read agrees with its row");
    observe(&local, &reads);
    ut_check(character_bodies_holds(&local, &reads), "the player's own body holds");
    observe(&far, &reads);
    ut_check(character_bodies_holds(&far, &reads), "and so does the far one");

    ut_section("the player's own body is not asked about a block");
    engine.block_body[BLOCK_A] = OBJ_PLAYER;
    observe(&local, &reads);
    reads.block_body = OBJ_FAR_A;   /* a bank window: his block carries a far body's record */
    ut_check(character_bodies_holds(&local, &reads),
             "a far body's record in his block does not take his translation away");

    ut_section("a far body is, and its block is read first");
    observe(&far, &reads);
    ut_check(!character_bodies_holds(&far, &reads), "a block that names another body: no");
    ut_check(reads.obj_thing == 0u && reads.worn == 0u,
             "and nothing of the body was read after the block said so");
    engine.block_body[BLOCK_A] = OBJ_FAR_A;
    observe(&far, &reads);
    reads.block_body = OBJ_PLAYER;
    ut_check(!character_bodies_holds(&far, &reads),
             "a body taken down whose freed memory still reads as before holds nothing either");

    ut_section("each read can say no on its own");
    engine.obj_thing[OBJ_FAR_A] = THING_C;
    observe(&far, &reads);
    ut_check(!character_bodies_holds(&far, &reads), "the object owns another handle now");
    engine.obj_thing[OBJ_FAR_A] = THING_B;
    engine.thing_worn[THING_B] = HERO;
    observe(&far, &reads);
    ut_check(!character_bodies_holds(&far, &reads), "the handle wears the hero again");
    engine.thing_worn[THING_B] = TARGET;
    engine.model_nodes[TARGET] = 18u;
    observe(&far, &reads);
    ut_check(!character_bodies_holds(&far, &reads),
             "the model at the target's address has another node count");
    engine.model_nodes[TARGET] = 17u;
    engine.obj_actor_model[OBJ_FAR_A] = TARGET;
    observe(&far, &reads);
    ut_check(!character_bodies_holds(&far, &reads),
             "a body built from the target's own asset wears it as its actor's: a stranger");
    engine.obj_actor_model[OBJ_FAR_A] = HERO;
    observe(&far, &reads);
    ut_check(character_bodies_holds(&far, &reads), "and with everything back it holds again");
    ut_check(!character_bodies_holds(NULL, &reads) && !character_bodies_holds(&far, NULL),
             "a missing row or missing reads hold nothing");
}

/* ==============================================================================================
 * THE REFERENCE RUN. This is the translation's single body logic as it stood before the tables,
 * copied here and not touched again. It armed by dropping everything first, substituted for one
 * handle while that handle wore the target with the counted nodes, cleared the cursors on a disarm
 * only while it still did, and took the waterline up and down with it.
 * ============================================================================================ */
typedef struct old_nodemap {
    bool      armed;
    uintptr_t thing;
    uintptr_t target_model;
    uint32_t  target_nodes;
    bool      waterline;
} old_nodemap_t;

static bool old_handle_still_ours(const old_nodemap_t *o)
{
    uintptr_t model = engine.thing_worn[o->thing];

    return model == o->target_model && engine.model_nodes[model] == o->target_nodes;
}

static bool old_disarm(old_nodemap_t *o)
{
    bool clear;

    if (!o->armed) {
        return false;
    }
    clear = old_handle_still_ours(o);
    o->armed = false;
    o->waterline = false;
    o->thing = 0u;
    o->target_model = 0u;
    o->target_nodes = 0u;
    return clear;
}

static bool old_arm(old_nodemap_t *o, uintptr_t thing, uintptr_t target)
{
    (void)old_disarm(o);
    o->thing = thing;
    o->target_model = target;
    o->target_nodes = engine.model_nodes[target];
    o->armed = true;
    o->waterline = true;
    return true;   /* the cursors of the live puppet are cleared on every arm */
}

static bool old_substitutes(const old_nodemap_t *o, uintptr_t thing)
{
    return o->armed && thing == o->thing && old_handle_still_ours(o);
}

/* The table, driven the way character_nodemap.c drives it for the player's own body. */
typedef struct new_nodemap {
    character_bodies_t t;
    bool               waterline;
} new_nodemap_t;

static bool new_disarm(new_nodemap_t *n, uintptr_t thing)
{
    uint32_t     index = character_bodies_find(&n->t, thing);
    body_entry_t body;
    body_reads_t reads;
    body_plan_t  plan;

    if (index == BODY_NONE) {
        return false;
    }
    body = n->t.body[index];
    observe(&body, &reads);
    plan = character_bodies_on_disarm(&body, &reads);
    (void)character_bodies_remove(&n->t, index);
    if (plan.waterline) {
        n->waterline = false;
    }
    return plan.clear_cursors;
}

static bool new_arm(new_nodemap_t *n, uintptr_t obj, uintptr_t thing, uintptr_t target)
{
    body_entry_t body;
    body_plan_t  plan;
    bool         fresh = false;
    uint32_t     index;

    (void)new_disarm(n, thing);
    memset(&body, 0, sizeof body);
    body.local = true;
    body.obj = obj;
    body.thing = thing;
    body.reference = HERO;
    body.target = target;
    body.target_nodes = engine.model_nodes[target];
    body.pair = character_bodies_pair_take(&n->t, HERO, target, body.target_nodes, 0u, &fresh);
    index = character_bodies_add(&n->t, &body);
    if (index == BODY_NONE) {
        return false;
    }
    plan = character_bodies_on_arm(&n->t.body[index]);
    if (plan.waterline) {
        n->waterline = true;
    }
    return plan.clear_cursors;
}

static bool new_substitutes(const new_nodemap_t *n, uintptr_t thing)
{
    uint32_t     index = character_bodies_find(&n->t, thing);
    body_reads_t reads;

    if (index == BODY_NONE) {
        return false;
    }
    observe(&n->t.body[index], &reads);
    return character_bodies_holds(&n->t.body[index], &reads);
}

static bool agree(const old_nodemap_t *o, const new_nodemap_t *n, const char *step)
{
    bool same = old_substitutes(o, THING_A) == new_substitutes(n, THING_A) &&
                old_substitutes(o, THING_B) == new_substitutes(n, THING_B) &&
                o->waterline == n->waterline &&
                o->armed == character_bodies_any(&n->t);

    ut_checkf(same, "%s: the table answers what the single body did", step);
    return same;
}

static void check_the_reference_run(void)
{
    old_nodemap_t old;
    new_nodemap_t now;

    reset_engine();
    memset(&old, 0, sizeof old);
    memset(&now, 0, sizeof now);
    memset(&now.t, 0, sizeof now.t);
    engine_body(OBJ_PLAYER, THING_A, HERO, HERO);

    ut_section("the player's own swap, step for step, against the single body it used to be");
    (void)agree(&old, &now, "before anything");
    ut_check(old_arm(&old, THING_A, TARGET) == new_arm(&now, OBJ_PLAYER, THING_A, TARGET),
             "arming clears the cursors in both");
    (void)agree(&old, &now, "armed, not yet bound");
    engine.thing_worn[THING_A] = TARGET;
    (void)agree(&old, &now, "the swap");
    ut_check(new_substitutes(&now, THING_A), "and the swapped handle is translated");

    ut_check(old_arm(&old, THING_A, OTHER) == new_arm(&now, OBJ_PLAYER, THING_A, OTHER),
             "a second swap without going home clears as the first did");
    engine.thing_worn[THING_A] = OTHER;
    (void)agree(&old, &now, "the second swap");

    ut_check(old_disarm(&old) == new_disarm(&now, THING_A),
             "going home clears the cursors of a handle that still wears the target");
    engine.thing_worn[THING_A] = HERO;
    (void)agree(&old, &now, "home");

    (void)old_arm(&old, THING_A, TARGET);
    (void)new_arm(&now, OBJ_PLAYER, THING_A, TARGET);
    engine.thing_worn[THING_A] = TARGET;
    (void)agree(&old, &now, "swapped again");
    /* A level change: a fresh body in the hero's model, on the same handle address. */
    engine.thing_worn[THING_A] = HERO;
    (void)agree(&old, &now, "a level change, the address reused in the hero's model");
    ut_check(old_disarm(&old) == new_disarm(&now, THING_A),
             "and taking the dead swap down clears nothing in either");
    (void)agree(&old, &now, "the dead swap taken down");

    (void)old_arm(&old, THING_A, TARGET);
    (void)new_arm(&now, OBJ_PLAYER, THING_A, TARGET);
    engine.thing_worn[THING_A] = TARGET;
    engine.model_nodes[TARGET] = 23u;   /* another model loaded at the target's address */
    (void)agree(&old, &now, "the address reused in the target's model with another node count");
    ut_check(old_disarm(&old) == new_disarm(&now, THING_A), "and neither clears anything there");
    engine.model_nodes[TARGET] = 17u;

    ut_section("the one place the table is stricter, on purpose");
    /* A stranger built from the target's own asset on the recycled handle: the single body
     * translated the stranger's clips through the hero's map, the table does not. */
    (void)old_arm(&old, THING_A, TARGET);
    (void)new_arm(&now, OBJ_PLAYER, THING_A, TARGET);
    engine_body(OBJ_PLAYER, THING_A, TARGET, TARGET);
    ut_check(old_substitutes(&old, THING_A), "the single body took the stranger for its own");
    ut_check(!new_substitutes(&now, THING_A), "the table sees the stranger's actor and does not");
}

static void check_the_plans(void)
{
    body_entry_t local;
    body_entry_t far;
    body_reads_t reads;
    body_plan_t  plan;

    reset_engine();
    memset(&local, 0, sizeof local);
    local.used = true;
    local.local = true;
    local.obj = OBJ_PLAYER;
    local.thing = THING_A;
    local.reference = HERO;
    local.target = TARGET;
    local.target_nodes = 17u;
    far = far_entry(1u, OBJ_FAR_A, THING_B, BLOCK_A, 0u);
    far.used = true;

    ut_section("the waterline goes with the player's own body and with no other");
    plan = character_bodies_on_arm(&local);
    ut_check(plan.waterline && plan.clear_cursors, "the player's arm takes the waterline up");
    plan = character_bodies_on_arm(&far);
    ut_check(!plan.waterline && plan.clear_cursors,
             "a far arm clears its own cursors and leaves the swimmer alone");
    observe(&far, &reads);
    plan = character_bodies_on_disarm(&far, &reads);
    ut_check(!plan.waterline, "and a far body let go does not take the player's waterline down");
    ut_check(!plan.clear_cursors, "nor clears a puppet it cannot prove is still its own");
    engine_body(OBJ_FAR_A, THING_B, HERO, TARGET);
    engine.block_body[BLOCK_A] = OBJ_FAR_A;
    observe(&far, &reads);
    plan = character_bodies_on_disarm(&far, &reads);
    ut_check(plan.clear_cursors, "a far body that still wears the target has its cursors cleared");
    observe(&local, &reads);
    plan = character_bodies_on_disarm(&local, &reads);
    ut_check(plan.waterline, "the player's own disarm takes the waterline down");
}

/* ============================================================================================ */

/* The guard as it stood before the body comparison: it compared BLOCK addresses, and the bank
 * windows of the multiplayer keep the block address. */
static body_guard_t old_guard(bool borrowed, uintptr_t block, uintptr_t swapped_block,
                              bool swap_live)
{
    if (borrowed && block != swapped_block) {
        return BODY_GUARD_ENGINE;
    }
    if (swap_live) {
        return BODY_GUARD_DECLINE;
    }
    return borrowed ? BODY_GUARD_TAKE_DOWN : BODY_GUARD_ENGINE;
}

static void check_the_blade_guard(void)
{
    ut_section("the blade guard asks which body is in the block");
    ut_check(character_bodies_guard(false, false, 0u, 0u, false) == BODY_GUARD_ENGINE,
             "nothing borrowed: the engine answers");
    ut_check(character_bodies_guard(true, true, OBJ_PLAYER, OBJ_PLAYER, true) ==
             BODY_GUARD_DECLINE, "the swapped body, swap live: declined");
    ut_check(character_bodies_guard(true, true, OBJ_PLAYER, OBJ_PLAYER, false) ==
             BODY_GUARD_TAKE_DOWN, "the swapped body, swap over: taken down");
    ut_check(character_bodies_guard(true, false, 0u, OBJ_PLAYER, false) == BODY_GUARD_TAKE_DOWN,
             "no body to read and no live swap: taken down");

    ut_section("a bank window keeps the block address and puts a far body in it");
    /* The content window: the same block, the far body in it, and the swap is not live there
     * because the body in the block is not the swapped one. */
    ut_check(character_bodies_guard(true, true, OBJ_FAR_A, OBJ_PLAYER, false) ==
             BODY_GUARD_ENGINE, "a far body's resize is the engine's, and nothing is taken down");
    ut_check(old_guard(true, BLOCK_A, BLOCK_A, false) == BODY_GUARD_TAKE_DOWN,
             "where the old block comparison took the player's swap down");
    ut_check(character_bodies_guard(true, true, OBJ_FAR_A, OBJ_PLAYER, false) !=
             old_guard(true, BLOCK_A, BLOCK_A, false),
             "and that is the one answer the two give differently");
    ut_check(character_bodies_guard(true, true, OBJ_FAR_A, OBJ_PLAYER, false) ==
             old_guard(true, BLOCK_B, BLOCK_A, false),
             "the pointer swap, another block, was answered alike by both");
}

/* ============================================================================================ */

static far_facts_t ready_to_dress(void)
{
    far_facts_t facts;

    memset(&facts, 0, sizeof facts);
    facts.asked = true;
    facts.wants = true;
    facts.block_sound = true;
    facts.able = true;
    facts.jedi = true;
    facts.blade_seen = true;
    facts.window_shut = true;
    facts.block_names_body = true;
    facts.fresh = true;
    return facts;
}

static bool is(far_step_t step, far_verdict_t verdict, uint8_t reason)
{
    return step.verdict == verdict && step.reason == reason;
}

static void check_the_far_decision(void)
{
    far_facts_t facts;

    ut_section("a body that wears a model keeps it and the answer names it");
    facts = ready_to_dress();
    facts.entry = true;
    facts.entry_is_wish = true;
    ut_check(is(character_bodies_far_step(&facts), FAR_WORN, MODEL_WEAR_REASON_NONE),
             "the model asked for: worn");
    facts.entry_is_wish = false;
    ut_check(is(character_bodies_far_step(&facts), FAR_REFUSE, MODEL_WEAR_REASON_WEARS_OTHER),
             "another model asked for on the same serial: refused, and the row is NOT forgotten");
    facts.wants = false;
    ut_check(is(character_bodies_far_step(&facts), FAR_REFUSE, MODEL_WEAR_REASON_WEARS_OTHER),
             "no model asked for while it wears one: refused the same way");
    facts.asked = false;
    ut_check(is(character_bodies_far_step(&facts), FAR_REFUSE, MODEL_WEAR_REASON_WEARS_OTHER),
             "whatever the rest of the facts say");

    ut_section("nothing asked is answered as nothing");
    facts = ready_to_dress();
    facts.asked = false;
    ut_check(is(character_bodies_far_step(&facts), FAR_NONE, MODEL_WEAR_REASON_NONE), "no body");
    facts = ready_to_dress();
    facts.wants = false;
    ut_check(is(character_bodies_far_step(&facts), FAR_NONE, MODEL_WEAR_REASON_NONE),
             "no model");
    facts = ready_to_dress();
    facts.answered = true;
    ut_check(is(character_bodies_far_step(&facts), FAR_KEEP, MODEL_WEAR_REASON_NONE),
             "the same serial and model answered for good: the answer stands");

    ut_section("the refusals, in the order they are asked");
    facts = ready_to_dress();
    facts.block_sound = false;
    ut_check(is(character_bodies_far_step(&facts), FAR_REFUSE, MODEL_WEAR_REASON_BAD_BLOCK),
             "a block not to write");
    facts = ready_to_dress();
    facts.able = false;
    ut_check(is(character_bodies_far_step(&facts), FAR_REFUSE, MODEL_WEAR_REASON_NOT_ABLE),
             "no sites, guard or translation");
    facts = ready_to_dress();
    facts.blade_seen = false;
    ut_check(is(character_bodies_far_step(&facts), FAR_REFUSE, MODEL_WEAR_REASON_BLADE),
             "a Jedi before the guard was ever asked");
    facts.jedi = false;
    ut_check(is(character_bodies_far_step(&facts), FAR_LOAD, MODEL_WEAR_REASON_NONE),
             "a body in slot 2 or 3 needs no proof: its take down never resizes a blade");
    facts = ready_to_dress();
    facts.fresh = false;
    ut_check(is(character_bodies_far_step(&facts), FAR_REFUSE, MODEL_WEAR_REASON_NOT_FRESH),
             "a body that does not wear its own actor's model");

    ut_section("waiting is not an answer");
    facts = ready_to_dress();
    facts.window_shut = false;
    ut_check(is(character_bodies_far_step(&facts), FAR_WAIT, MODEL_WEAR_REASON_NONE),
             "a window open at the scene end: the bank waits");
    facts = ready_to_dress();
    facts.block_names_body = false;
    facts.fresh = false;
    ut_check(is(character_bodies_far_step(&facts), FAR_WAIT, MODEL_WEAR_REASON_NONE),
             "a block that names another object waits, and is not refused as not fresh");

    ut_section("and with nothing in the way the asset is loaded");
    facts = ready_to_dress();
    ut_check(is(character_bodies_far_step(&facts), FAR_LOAD, MODEL_WEAR_REASON_NONE), "load");
    ut_check(is(character_bodies_far_step(NULL), FAR_WAIT, MODEL_WEAR_REASON_NONE),
             "no facts wait");

    ut_section("which answers hold for as long as the serial and the model do");
    ut_check(character_bodies_far_final(MODEL_WEAR_STATE_WORN, MODEL_WEAR_REASON_NONE),
             "worn holds");
    ut_check(character_bodies_far_final(MODEL_WEAR_STATE_REFUSED, MODEL_WEAR_REASON_FIT),
             "a fit too weak holds");
    ut_check(character_bodies_far_final(MODEL_WEAR_STATE_REFUSED, MODEL_WEAR_REASON_BROKEN),
             "and so does broken, which the multiplayer answers with a rebuild");
    ut_check(!character_bodies_far_final(MODEL_WEAR_STATE_REFUSED, MODEL_WEAR_REASON_BLADE),
             "the blade is asked again, because the guard may be asked later");
    ut_check(!character_bodies_far_final(MODEL_WEAR_STATE_REFUSED, MODEL_WEAR_REASON_NO_ROOM),
             "as is a full table, because a row may come free");
    ut_check(!character_bodies_far_final(MODEL_WEAR_STATE_NONE, MODEL_WEAR_REASON_NONE),
             "and no answer is not one");
}

/* The pose hook's choice. A row that holds is translated; one that does not is left to the engine,
 * unless the handle still wears the row's target, whose tracks would walk a cursor of the target's
 * ordinal space through a clip of the reference's. */
static void check_the_pose(void)
{
    body_entry_t body;

    memset(&body, 0, sizeof body);
    body.used = true;
    body.target = 7u;
    body.reference = 1u;

    ut_section("the pose hook: translate, freeze, or leave it to the engine");
    ut_check(character_bodies_pose(NULL, false, 7u) == BODY_POSE_ENGINE,
             "a handle without a row is the engine's");
    ut_check(character_bodies_pose(&body, true, 7u) == BODY_POSE_TRANSLATED,
             "a row that holds is posed from its pair's copies");
    ut_check(character_bodies_pose(&body, true, 0u) == BODY_POSE_TRANSLATED,
             "whatever the handle read says, since the row's own reads decided");
    ut_check(character_bodies_pose(&body, false, 7u) == BODY_POSE_FROZEN,
             "a row that does not hold on a handle that wears its target is frozen");
    ut_check(character_bodies_pose(&body, false, 1u) == BODY_POSE_ENGINE,
             "one that wears the reference again is the engine's, clips and rig agree");
    ut_check(character_bodies_pose(&body, false, 0u) == BODY_POSE_ENGINE,
             "and so is one whose model did not read");
    body.used = false;
    ut_check(character_bodies_pose(&body, false, 7u) == BODY_POSE_ENGINE,
             "an unused row is no row");
}

static void check_every_reason_has_words(void)
{
    uint8_t reason;
    uint8_t unnamed = 0u;

    ut_section("every refusal says what it is");
    for (reason = 1u; reason <= MODEL_WEAR_REASON_MAX; ++reason) {
        if (strcmp(character_bodies_reason_text(reason), "no reason") == 0) {
            unnamed = reason;
        }
    }
    ut_checkf(unnamed == 0u, "every reason of the note has its own words (first without: %u)",
              (unsigned)unnamed);
    ut_check(strcmp(character_bodies_reason_text(0u), "no reason") == 0, "and none has none");
}

int main(void)
{
    check_the_tables();
    check_the_same_pointers_after_a_rebuild_make_a_new_pair();
    check_the_one_predicate();
    check_the_reference_run();
    check_the_plans();
    check_the_blade_guard();
    check_the_far_decision();
    check_the_pose();
    check_every_reason_has_words();

    return ut_summary("the bodies and pairs of the model swap");
}
