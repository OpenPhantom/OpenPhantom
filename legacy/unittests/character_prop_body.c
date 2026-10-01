/* character_prop_body.c: the rows of the borrowed weapon, and the single body logic they replaced.
 *
 * The state of this feature used to be one static record. It is a table of rows now, and the two
 * searches, the block rule and the freshness stamp are new code on a path a field run cannot be
 * asked to prove: a wrong answer there is a weapon in the wrong hand, or a shot from the body's
 * centre, and both look exactly like the bugs this feature was built to end.
 *
 * So the old logic is copied into this file as it was and run beside the module, over the pretend
 * engine in character_prop_engine.c. Every event goes to both, and after every event both are
 * asked the same questions: which block, is the body still ours, which hand does the weapon hang
 * on, which sphere is answered and for which node, and which node is given instead of a found one.
 * A difference fails here rather than in a level.
 */
#include "unittest.h"

#include "character_prop_engine.h"

#include "character_mount.h"
#include "character_prop.h"
#include "character_prop_body.h"
#include "character_prop_sites.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* ==============================================================================================
 * The single body logic, copied from character_prop.c as it stood before the cut. Nothing here
 * calls the module; it reads the same engine bytes through its own copies of the same rules.
 * ============================================================================================ */

static struct {
    bool      armed;
    bool      placed;
    uintptr_t record;
    uintptr_t body_obj;
    uintptr_t body_thing;
    uintptr_t target_model;
    uint32_t  target_hand_slot;
    uintptr_t target_hand_node;
    uint32_t  target_hidden[PROP_HIDE_MAX];
    uint32_t  target_hidden_count;
    uint32_t  shown_name_id;
} old;

static bool old_live_block(uintptr_t *out_block)
{
    uint32_t block;

    if (old.record == 0) {
        return false;
    }
    block = get_u32((const void *)old.record);
    if (block == 0u) {
        return false;
    }
    *out_block = (uintptr_t)block;
    return true;
}

static bool old_body_is_still_ours(uintptr_t block)
{
    return get_u32((const void *)(block + BLOCK_ACTOR)) == (uint32_t)old.body_obj &&
           get_u32((const void *)(old.body_obj + OBJ_THING)) == (uint32_t)old.body_thing;
}

static const char *old_name_of_id(uint32_t name_id, char *buffer)
{
    if (name_id >= NAMES || name_table[name_id] == 0u) {
        return NULL;
    }
    memcpy(buffer, (const void *)(uintptr_t)name_table[name_id], NAME_BYTES);
    buffer[NAME_BYTES - 1u] = '\0';
    return buffer;
}

static bool old_node_by_name_id(uintptr_t model, uint32_t name_id, uint32_t *out_slot,
                                uintptr_t *out_node)
{
    uintptr_t nodes;
    uint32_t  count;
    char      want[NAME_BYTES];
    uint32_t  i;

    if (model == 0 || old_name_of_id(name_id, want) == NULL) {
        return false;
    }
    nodes = (uintptr_t)get_u32((const void *)(model + MODEL_NODES));
    count = get_u32((const void *)(model + MODEL_NUM_NODES));
    if (nodes == 0 || count == 0u) {
        return false;
    }
    for (i = 0; i < count; ++i) {
        uintptr_t node = nodes + (uintptr_t)i * NODE_BYTES;

        if (strcmp((const char *)node, want) != 0) {
            continue;
        }
        *out_slot = get_u32((const void *)(node + NODE_MATRIX_SLOT));
        if (out_node != NULL) {
            *out_node = node;
        }
        return *out_slot < count;
    }
    return false;
}

static bool old_equipped_name_id(uintptr_t block, uint32_t *out_name_id)
{
    uint32_t slot = get_u32((const void *)(block + BLOCK_WEAPON_SLOT));
    uint32_t name_id;

    if (slot == 0u || slot >= WEAPON_ROWS) {
        return false;
    }
    name_id = get_u32(&weapon_cfg[slot * WEAPON_ROW_BYTES]);
    if (name_id >= NAMES) {
        return false;
    }
    *out_name_id = name_id;
    return true;
}

static void old_forget_target(void)
{
    old.target_model = 0;
    old.target_hidden_count = 0;
    old.target_hand_slot = 0;
    old.target_hand_node = 0;
    old.placed = false;
}

static bool old_target_hand(void)
{
    uintptr_t model = (uintptr_t)get_u32((const void *)(old.body_thing + THING_MODEL));
    uintptr_t node = 0;
    char      mount[NAME_BYTES];

    if (model == 0) {
        return false;
    }
    if (model == old.target_model) {
        return true;
    }
    old_forget_target();
    if (!old_node_by_name_id(model, NAME_RHAND, &old.target_hand_slot, &node)) {
        if (!old_node_by_name_id(model, NAME_RFOREARM, &old.target_hand_slot, &node)) {
            return false;
        }
    }
    old.target_model = model;
    old.target_hand_node = node;
    old.target_hidden_count =
        character_mount_own_weapon_nodes(model, node, old_name_of_id(NAME_WEAPON, mount),
                                         old.target_hidden, PROP_HIDE_MAX);
    return true;
}

static bool old_weapon_node(const void *obj, int32_t *out_node)
{
    if (!old.armed || obj == NULL || out_node == NULL || (uintptr_t)obj != old.body_obj ||
        old.target_model == 0) {
        return false;
    }
    if (get_u32((const void *)((uintptr_t)obj + OBJ_THING)) != (uint32_t)old.body_thing ||
        get_u32((const void *)(old.body_thing + THING_MODEL)) != (uint32_t)old.target_model) {
        return false;
    }
    *out_node = (int32_t)old.target_hand_slot;
    return true;
}

static uint32_t old_resolved_node_of(uint32_t name_id)
{
    char     wanted[NAME_BYTES];
    uint32_t answered = 0;
    int32_t  substitute;

    if (old_node_by_name_id(old.target_model, name_id, &answered, NULL)) {
        return answered;
    }
    if (!character_mount_is_armed() || old_name_of_id(name_id, wanted) == NULL) {
        return 0u;
    }
    substitute = character_mount_answer(wanted, 0, (int32_t)old.target_hand_slot);
    return (substitute == MOUNT_NO_NODE) ? 0u : (uint32_t)substitute;
}

static int32_t old_found_node(const void *obj, const char *wanted, int32_t found)
{
    uintptr_t block = 0;
    uint32_t  name_id = 0;
    int32_t   hand = 0;

    if (!old_weapon_node(obj, &hand) || !old_live_block(&block) ||
        !old_equipped_name_id(block, &name_id)) {
        return MOUNT_NO_NODE;
    }
    return character_mount_answer_found(old.target_model, wanted, found, old.target_hidden,
                                        old.target_hidden_count,
                                        (int32_t)old_resolved_node_of(name_id));
}

static bool old_node_sphere(const void *obj, int32_t node_index)
{
    uintptr_t block = 0;
    uint32_t  name_id = 0;

    if (!old.armed || !old.placed || obj == NULL || (uintptr_t)obj != old.body_obj) {
        return false;
    }
    if (!old_live_block(&block) || !old_equipped_name_id(block, &name_id) ||
        name_id != old.shown_name_id) {
        return false;
    }
    return node_index >= 0 && (uint32_t)node_index == old_resolved_node_of(name_id);
}

/* The field writes the old arm and the old draw made, in their own order. */
static void old_arm(uintptr_t record, uintptr_t obj, uintptr_t thing)
{
    if (old.armed) {
        if (old.body_thing != thing) {
            old.target_model = 0;
        }
        old_forget_target();
    }
    old.record = record;
    old.body_obj = obj;
    old.body_thing = thing;
    old.target_model = 0;
    old.target_hand_slot = 0;
    old.target_hidden_count = 0;
    old.shown_name_id = PROP_NAME_ID_NONE;
    old.placed = false;
    old.armed = true;
}

static void old_draw(bool the_draw_answered)
{
    uintptr_t block = 0;
    uint32_t  name_id = 0;

    old.placed = false;
    if (!old_live_block(&block) || !old_body_is_still_ours(block)) {
        old_forget_target();
        old.armed = false;
        old.shown_name_id = PROP_NAME_ID_NONE;
        return;
    }
    if (!old_target_hand() || !old_equipped_name_id(block, &name_id)) {
        return;
    }
    old.shown_name_id = name_id;
    old.placed = the_draw_answered;
}

/* ==============================================================================================
 * The two run beside each other.
 * ============================================================================================ */

static prop_body_t *row(void)
{
    return character_prop_body_of_bank(PROP_BANK_LOCAL);
}

static void agree(const char *what)
{
    prop_body_t *body = row();
    uintptr_t    new_block = 0;
    uintptr_t    old_block = 0;
    int32_t      new_node = -7;
    int32_t      old_node = -7;
    float        centre[3];
    float        radius = 0.0f;
    int32_t      i;

    ut_checkf(character_prop_is_armed() == old.armed, "%s: armed", what);
    ut_checkf(character_prop_block_of(body, &new_block) == old_live_block(&old_block) &&
              new_block == old_block, "%s: the block the record is in", what);
    new_block = 0;
    old_block = 0;
    ut_checkf(character_prop_body_still_ours(body, &new_block) ==
              (old_live_block(&old_block) && old_body_is_still_ours(old_block)),
              "%s: the body is still the one this was armed on", what);
    if (body != NULL) {
        ut_checkf(body->target_hand_slot == old.target_hand_slot &&
                  body->target_model == old.target_model &&
                  body->target_hand_node == old.target_hand_node &&
                  body->hidden_count == old.target_hidden_count,
                  "%s: the hand the weapon hangs on, and the rig's own weapon words", what);
        ut_checkf(body->shown_name_id == old.shown_name_id, "%s: the weapon shown", what);
        ut_checkf(body->placed == old.placed, "%s: a sphere was measured", what);
    }
    ut_checkf(character_prop_weapon_node(body_obj, &new_node) ==
              old_weapon_node(body_obj, &old_node) && new_node == old_node,
              "%s: the node the weapon hangs on", what);
    new_node = -7;
    old_node = -7;
    ut_checkf(character_prop_weapon_node(other_obj, &new_node) ==
              old_weapon_node(other_obj, &old_node),
              "%s: and nothing is answered for another body", what);
    ut_checkf(character_prop_found_node(body_obj, "sabreblad01", 5) ==
              old_found_node(body_obj, "sabreblad01", 5),
              "%s: the node given instead of a found one", what);
    ut_checkf(character_prop_found_node(body_obj, "chest", 2) ==
              old_found_node(body_obj, "chest", 2),
              "%s: and a name outside the weapon family is left alone", what);
    for (i = -1; i < (int32_t)RIG_NODES; ++i) {
        bool answered = character_prop_node_sphere(body_obj, i, centre, &radius);

        ut_checkf(answered == old_node_sphere(body_obj, i), "%s: the sphere for node %d", what, i);
        if (answered && body != NULL) {
            ut_checkf(centre[0] == body->shown_centre[0] && radius == body->shown_radius,
                      "%s: and it is the sphere the draw measured", what);
        }
    }
}

static void draw_both(void)
{
    static const float ROOT[MATRIX_FLOATS] = {
        1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 3.0f, 0.0f, 0.0f
    };
    uint32_t before = draws;

    character_prop_before_thing_draw(body_thing.block, ROOT);
    old_draw(draw_answers && draws != before);
}

static void arm_both(void)
{
    ut_check(character_prop_arm((uintptr_t)&player_cell, (uintptr_t)body_obj,
                                (uintptr_t)hero.model),
             "the player's own weapon is mounted on the borrowed body");
    old_arm((uintptr_t)&player_cell, (uintptr_t)body_obj, (uintptr_t)body_thing.block);
}

static void check_the_two_run_the_same(void)
{
    ut_section("the single body logic and the table, step for step");

    build_world();
    memset(&old, 0, sizeof old);

    agree("before anything is armed");

    arm_both();
    agree("armed, before the first draw");

    draw_both();
    ut_check(row()->placed && old.placed, "the first draw measured a weapon sphere");
    ut_check(body_thing.node_hidden[4] == 1u,
             "and the borrowed rig's own gun is hidden on its handle");
    agree("after the first draw");

    draw_both();
    agree("after a second draw with nothing changed");

    put_u32(player_block + BLOCK_WEAPON_SLOT, 2u);
    agree("the weapon changed, before the draw that would show it");
    draw_both();
    agree("and after it");

    put_u32(player_block + BLOCK_WEAPON_SLOT, 0u);
    draw_both();
    agree("the empty hand");
    put_u32(player_block + BLOCK_WEAPON_SLOT, 1u);
    draw_both();
    agree("and the weapon back");

    draw_answers = false;
    draw_both();
    ut_check(!row()->placed && !old.placed, "a draw that did not draw measures nothing");
    agree("after a draw the engine declined");
    draw_answers = true;
    draw_both();
    agree("and after it drew again");

    put_u32(body_thing.block + THING_MODEL, (uint32_t)(uintptr_t)stump.model);
    agree("the body was rebound to another rig, before the draw that would notice");
    draw_both();
    ut_check(row()->target_model == (uintptr_t)stump.model, "the body wears the forearm rig now");
    ut_check(row()->target_hand_slot == 3u, "and the weapon hangs on the forearm");
    agree("on a rig with no fist");
    put_u32(body_thing.block + THING_MODEL, (uint32_t)(uintptr_t)worn.model);
    draw_both();
    agree("and back on the rig with a hand");

    player_cell = (uint32_t)(uintptr_t)far_block;
    agree("the cell names another body's block");
    draw_both();
    ut_check(!character_prop_is_armed() && !old.armed, "both put the weapon away");
    agree("and the draw that followed it");

    player_cell = 0u;
    agree("the cell holds nothing");
}

static void check_the_body_goes_away_under_the_draw(void)
{
    ut_section("the body gives up its render handle");

    build_world();
    memset(&old, 0, sizeof old);
    arm_both();
    draw_both();
    agree("drawn once");

    put_u32(body_obj + OBJ_THING, (uint32_t)(uintptr_t)other_thing.block);
    draw_both();
    ut_check(!character_prop_is_armed() && !old.armed, "both put the weapon away");
    ut_check(body_thing.node_hidden[4] == 0u,
             "and the borrowed rig has its own gun back");
    agree("after the body was taken away under it");
}

/* ==============================================================================================
 * The table, the block rule and the stamp, which the single body logic had no answer for.
 * ============================================================================================ */

static void check_the_table(void)
{
    prop_body_t *local;
    prop_body_t *far;

    ut_section("one row per bank, found by handle and by object");

    build_world();
    memset(&old, 0, sizeof old);
    arm_both();
    local = row();
    ut_check(local != NULL && local->bank == PROP_BANK_LOCAL, "the player's body is bank zero");
    ut_check(character_prop_body_hold(PROP_BANK_LOCAL) == local,
             "holding the same bank again is that row, not a second one");
    ut_check(character_prop_body_armed() == 1u, "one row is carrying a body");
    ut_check(character_prop_body_for_thing(body_thing.block) == local,
             "the draw finds it by the render handle");
    ut_check(character_prop_body_for_obj(body_obj) == local, "the sphere finds it by the object");
    ut_check(character_prop_body_for_thing(other_thing.block) == NULL &&
             character_prop_body_for_obj(other_obj) == NULL,
             "and neither finds anything for another body");
    ut_check(character_prop_body_for_thing(NULL) == NULL &&
             character_prop_body_for_obj(NULL) == NULL, "nor for nothing at all");

    far = character_prop_body_hold(2u);
    ut_check(far != NULL && far != local && far->bank == 2u, "a second bank gets a row of its own");
    ut_check(character_prop_body_armed() == 1u, "a row that carries nothing is not counted");

    /* A far bank's row, wired the way the far path wires one. It holds its body before
     * it carries anything on it, and until it does, nothing is answered for that body. */
    far->obj = (uintptr_t)other_obj;
    far->thing = (uintptr_t)other_thing.block;
    far->record = (uintptr_t)&player_cell;
    far->block = (uintptr_t)far_block;
    ut_check(character_prop_body_for_obj(other_obj) == NULL &&
             character_prop_body_for_thing(other_thing.block) == NULL,
             "a row that carries nothing answers for neither its object nor its handle");
    far->armed = true;
    ut_check(character_prop_body_for_obj(other_obj) == far, "now it answers for its own object");
    ut_check(character_prop_body_for_thing(other_thing.block) == far, "and for its own handle");
    ut_check(character_prop_body_armed() == 2u, "and two rows carry a body");
    ut_check(character_prop_body_for_obj(body_obj) == local, "the player's row is untouched");
    far->armed = false;
    far->used = false;
}

static void check_the_block_rule(void)
{
    static uint8_t live[0x100];
    prop_body_t   *local;
    prop_body_t    far;
    uintptr_t      block = 0;

    ut_section("which block a body's record is in");

    build_world();
    memset(&old, 0, sizeof old);
    arm_both();
    local = row();

    ut_check(character_prop_block_of(local, &block) && block == (uintptr_t)player_block,
             "the player's own body is read through the cell");
    player_cell = (uint32_t)(uintptr_t)far_block;
    ut_check(character_prop_block_of(local, &block) && block == (uintptr_t)far_block,
             "and through the cell even while another body's block is in it, as it always was");
    player_cell = 0u;
    ut_check(!character_prop_block_of(local, &block), "an empty cell is no block");
    player_cell = (uint32_t)(uintptr_t)player_block;

    memset(&far, 0, sizeof far);
    far.used = true;
    far.armed = true;
    far.bank = 3u;
    far.obj = (uintptr_t)other_obj;
    far.thing = (uintptr_t)other_thing.block;
    far.record = (uintptr_t)&player_cell;
    far.block = (uintptr_t)far_block;

    ut_check(character_prop_block_of(&far, &block) && block == (uintptr_t)far_block,
             "outside its window a far body is read out of its own block");
    player_cell = (uint32_t)(uintptr_t)far_block;
    ut_check(character_prop_block_of(&far, &block) && block == (uintptr_t)far_block,
             "and inside its window the cell names the same block");

    /* The case the order exists for: the cell holds a block for this body that is not the row's
     * own copy, and the cell's is the one a weapon change in this window wrote. */
    memset(live, 0, sizeof live);
    put_u32(live + BLOCK_ACTOR, (uint32_t)(uintptr_t)other_obj);
    put_u32(live + BLOCK_WEAPON_SLOT, 5u);
    player_cell = (uint32_t)(uintptr_t)live;
    ut_check(character_prop_block_of(&far, &block) && block == (uintptr_t)live,
             "the cell is asked first, so a weapon change inside the window is seen");

    player_cell = (uint32_t)(uintptr_t)player_block;
    ut_check(character_prop_block_of(&far, &block) && block == (uintptr_t)far_block,
             "a cell naming somebody else is skipped for the row's own block");

    far.block = 0;
    ut_check(character_prop_block_of(&far, &block) && block == (uintptr_t)player_block,
             "a row with no block of its own reads the cell and asks nothing else");
    far.block = (uintptr_t)far_block;
    far.obj = (uintptr_t)orphan_obj;
    ut_check(!character_prop_block_of(&far, &block),
             "and a row whose body no block names is answered by nobody");

    ut_check(!character_prop_block_of(NULL, &block), "no row is no block");
    ut_check(!character_prop_block_of(local, NULL), "and nowhere to put it is no block");
}

static void check_the_stamp(void)
{
    prop_body_t *local;
    float        centre[3];
    float        radius = 0.0f;
    int32_t      answered_for = -1;
    int32_t      i;

    ut_section("the stamp says which pass measured a sphere, and the row holds the last one");

    build_world();
    memset(&old, 0, sizeof old);
    arm_both();
    local = row();
    draw_both();
    ut_check(character_prop_body_is_placed(local), "the draw placed a sphere in this pass");
    for (i = 0; i < (int32_t)RIG_NODES; ++i) {
        if (character_prop_node_sphere(body_obj, i, centre, &radius)) {
            answered_for = i;
        }
    }
    ut_check(answered_for >= 0, "and one node is answered with it");

    /* A frame went by, and the next object dispatched opens the pass. The player's own body is
     * not always dispatched in it, and the answer the engine would give for that node is the fist
     * the weapon hangs on, so the row holds the sphere its last draw measured rather than let go
     * of it. That is what answered before the stamp existed, and the run beside the old logic
     * below is what says so. */
    character_prop_body_frame_ended();
    character_prop_body_begin_pass();
    ut_check(local->placed, "the placement itself is still recorded");
    ut_check(!character_prop_body_is_placed(local), "but it belongs to the pass before this one");
    ut_check(character_prop_node_sphere(body_obj, answered_for, centre, &radius),
             "and the node is answered out of it for a body this pass did not draw");
    ut_check(centre[0] == local->shown_centre[0] && radius == local->shown_radius,
             "with that sphere, not a new one");
    agree("a pass that did not draw him");

    /* And holding is not measuring: a weapon he was never drawn with is the engine's. */
    put_u32(player_block + BLOCK_WEAPON_SLOT, 3u);
    for (i = 0; i < (int32_t)RIG_NODES; ++i) {
        ut_checkf(!character_prop_node_sphere(body_obj, i, centre, &radius),
                  "no sphere of a weapon he was never drawn with, node %d", i);
    }
    agree("a weapon change without a draw");
    put_u32(player_block + BLOCK_WEAPON_SLOT, 1u);

    draw_both();
    ut_check(character_prop_body_is_placed(local), "a draw in the new pass stamps it again");
    ut_check(character_prop_node_sphere(body_obj, answered_for, centre, &radius),
             "and the node is answered again");
    ut_check(!character_prop_body_is_placed(NULL), "no row is never placed");
}

int main(void)
{
    check_the_two_run_the_same();
    check_the_body_goes_away_under_the_draw();
    check_the_table();
    check_the_block_rule();
    check_the_stamp();

    return ut_summary("character prop body");
}
