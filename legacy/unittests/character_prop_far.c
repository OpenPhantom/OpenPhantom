/* character_prop_far.c: a far player's own weapon, on the body he is wearing a borrowed rig on.
 *
 * The reference run next door proves that the table answers for the PLAYER'S body exactly as the
 * single body logic did. This program is the other half: everything only a far row does, and that
 * a field run cannot be asked to prove, because a wrong answer here is a blow landing somewhere
 * plausible rather than an error.
 *
 * Four of the checks are about something that must NOT happen. A far row may not write the words
 * that hide its rig's own weapon, because the far path writes them and one visibility word with
 * two writers leaves a rig half invisible. It may not follow its body onto another rig under
 * itself, for the same reason. It may not answer out of a block that has stopped naming its body.
 * And the sphere hook may not fall through to the engine for the node a far weapon hangs on: that
 * node is the fist, the engine answers the fist's own sphere for it, and the shot would leave the
 * hand while the blade did the swinging.
 *
 * SIZE NOTE: over 600 lines, one check function per property of a far row, each building its own
 * world first so that no check can be read as depending on the one before it. The seam, if it
 * grows, is the sphere half: the seven functions from the measurement without a draw down are
 * about what is answered, and the ones above them about what a row is and how long it lasts.
 */
#include "unittest.h"

#include "character_prop_engine.h"

#include "character_mount.h"
#include "character_prop.h"
#include "character_prop_blade.h"
#include "character_prop_body.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static const float ROOT[MATRIX_FLOATS] = {
    1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 3.0f, 0.0f, 0.0f
};

/* The words the far path collects for a rig and lends to the row. `worn` and `guard` both hang
 * their own weapon geometry under the mount at slot 4, and one word covers the whole subtree. */
static const uint32_t LENT[1] = { 4u };

/* The node a bank's weapon answers for: the one the engine itself answers on the borrowed rig,
 * which is what the caller has resolved one step before it asks. Both rigs carry `weapon` at 4. */
#define WEAPON_NODE   4

static bool arm_far(uint8_t bank, uint32_t b, const uint32_t *hidden, uint32_t hidden_count)
{
    prop_far_t spec;

    memset(&spec, 0, sizeof spec);
    spec.bank = bank;
    spec.record = (uintptr_t)&player_cell;
    spec.block = (uintptr_t)bank_block[b];
    spec.obj = (uintptr_t)bank_obj[b];
    spec.thing = (uintptr_t)bank_thing[b].block;
    spec.reference = (uintptr_t)hero.model;
    spec.hidden = hidden;
    spec.hidden_count = hidden_count;
    spec.serial = 100u + (uint32_t)bank;
    return character_prop_far_arm(&spec);
}

static void draw(const fake_thing_t *thing)
{
    character_prop_before_thing_draw(thing->block, ROOT);
}

/* A frame went by, and then something was dispatched in the next one. That pair is what opens a
 * pass, and neither half does it alone. */
static void next_pass(void)
{
    character_prop_body_frame_ended();
    character_prop_body_begin_pass();
}

static bool sphere_of(const void *obj, int32_t node, float centre[3], float *radius)
{
    return character_prop_node_sphere(obj, node, centre, radius);
}

/* Every row let go, then a world nothing has been done to yet. In this order, because letting go
 * reads the body a row is on and that body belongs to the world it was armed in. */
static void a_fresh_world(void)
{
    uint8_t bank;

    for (bank = 1u; bank <= FAKE_BANKS; ++bank) {
        character_prop_far_disarm(bank);
    }
    character_prop_disarm();
    build_world();
}

/* ============================================================================================ */

static void check_the_hand_is_resolved_when_the_row_is_armed(void)
{
    prop_body_t *body;

    ut_section("a far row knows its hand before it is ever drawn");

    a_fresh_world();
    ut_check(arm_far(1u, 0u, LENT, 1u), "the far body in bank one carries its player's weapon");
    body = character_prop_body_of_bank(1u);
    ut_check(body != NULL && body->armed, "and the row is carrying it");
    ut_check(body->target_model == (uintptr_t)worn.model,
             "the rig it is hung on is the one the body wears");
    ut_check(body->target_hand_slot == 3u, "the hand was resolved without waiting for a draw");
    ut_check(body->reference_model == (uintptr_t)hero.model,
             "and the meshes come from the far player's own hero model");
    ut_check(body->block == (uintptr_t)bank_block[0],
             "the row carries the block its bank's record is in");
    ut_check(body->hidden_count == 1u && body->hidden[0] == 4u,
             "the words that hide the rig's own weapon were handed over");
    ut_check(body->hidden_from == PROP_HIDDEN_LENT, "and the row knows they are not its own");

    /* The swing starter asks through the node lookup, and it asks inside a bank window that can
     * open before the body has ever been drawn. */
    ut_check(character_prop_body_for_obj(bank_obj[0]) == body,
             "the sphere and the lookup find the row by the object");
    ut_check(character_prop_body_for_thing(bank_thing[0].block) == body,
             "and the draw finds it by the render handle");
}

static void check_the_far_row_is_refused_and_says_so(void)
{
    float centre[3];
    float radius = 0.0f;

    ut_section("a far body that cannot carry one");

    a_fresh_world();
    ut_check(!arm_far(PROP_BANK_LOCAL, 0u, LENT, 1u),
             "bank zero is the player's own and is not a far bank");
    ut_check(!arm_far((uint8_t)(FAKE_BANKS + 9u), 0u, LENT, 1u), "nor is a bank past the note's");
    ut_check(character_prop_body_of_bank(1u) == NULL, "and neither took a row");

    put_u32(bank_obj[0] + OBJ_THING, (uint32_t)(uintptr_t)other_thing.block);
    ut_check(!arm_far(1u, 0u, LENT, 1u), "a body that gave up its handle carries nothing");
    put_u32(bank_obj[0] + OBJ_THING, (uint32_t)(uintptr_t)bank_thing[0].block);

    /* A rig with neither a fist nor a forearm to hang one on. */
    memset(&guard.node[3u * NODE_BYTES], 0, 8u);
    memcpy(&guard.node[3u * NODE_BYTES], "elbow", 6u);
    ut_check(!arm_far(2u, 1u, LENT, 1u), "a rig that states no hand carries nothing");
    ut_check(character_prop_body_of_bank(2u) != NULL &&
             !character_prop_body_of_bank(2u)->armed,
             "and the row it took is not carrying anything");
    ut_check(!sphere_of(bank_obj[1], WEAPON_NODE, centre, &radius),
             "so nothing is answered for that body");

    a_fresh_world();
    ut_check(arm_far(2u, 1u, LENT, 1u), "the same bank carries one once its rig states a hand");
}

static void check_three_bodies_at_once(void)
{
    prop_body_t *local;
    prop_body_t *one;
    prop_body_t *two;
    int32_t      node = -7;

    ut_section("the player and two far players, side by side");

    a_fresh_world();
    ut_check(character_prop_arm((uintptr_t)&player_cell, (uintptr_t)body_obj,
                                (uintptr_t)hero.model),
             "the player's own body carries his weapon");
    ut_check(arm_far(1u, 0u, LENT, 1u), "bank one is dressed and carries its weapon");
    ut_check(arm_far(2u, 1u, LENT, 1u), "bank two as well");

    local = character_prop_body_of_bank(PROP_BANK_LOCAL);
    one = character_prop_body_of_bank(1u);
    two = character_prop_body_of_bank(2u);
    ut_check(local != NULL && one != NULL && two != NULL && local != one && one != two,
             "three rows, one per body");
    ut_check(character_prop_body_armed() == 3u, "and all three are carrying");

    ut_check(character_prop_body_for_obj(body_obj) == local &&
             character_prop_body_for_obj(bank_obj[0]) == one &&
             character_prop_body_for_obj(bank_obj[1]) == two,
             "each object finds its own row and no other");
    ut_check(character_prop_body_for_thing(body_thing.block) == local &&
             character_prop_body_for_thing(bank_thing[0].block) == one &&
             character_prop_body_for_thing(bank_thing[1].block) == two,
             "and each render handle does too");
    ut_check(character_prop_body_for_obj(orphan_obj) == NULL,
             "and a body no row holds finds nothing");

    ut_check(character_prop_weapon_node(bank_obj[0], &node) && node == 3,
             "bank one's weapon hangs on its own rig's hand");
    node = -7;
    ut_check(character_prop_weapon_node(bank_obj[1], &node) && node == 3,
             "and bank two's on its own");
    ut_check(one->target_model == (uintptr_t)worn.model &&
             two->target_model == (uintptr_t)guard.model,
             "and the two far rows name the rig their own body wears, not each other's");

    draw(&bank_thing[0]);
    ut_check(character_prop_body_is_placed(one), "drawing one of them measures that one");
    ut_check(!character_prop_body_is_placed(two) && !character_prop_body_is_placed(local),
             "and measures nothing for the other two");
}

static void check_one_pass_covers_a_whole_frame(void)
{
    prop_body_t *local;
    prop_body_t *one;

    ut_section("a pass is a frame and not a body");

    a_fresh_world();
    ut_check(character_prop_arm((uintptr_t)&player_cell, (uintptr_t)body_obj,
                                (uintptr_t)hero.model),
             "the player's own body carries his weapon");
    ut_check(arm_far(1u, 0u, LENT, 1u), "and a far player's body carries his");
    local = character_prop_body_of_bank(PROP_BANK_LOCAL);
    one = character_prop_body_of_bank(1u);

    /* One frame, two bodies drawn in it. The second draw may not age the first: the player's own
     * row has no way of measuring a sphere outside a draw, so a pass opened by each body in turn
     * would leave his weapon answering nothing from the moment a far player was drawn after him. */
    next_pass();
    draw(&body_thing);
    draw(&bank_thing[0]);
    ut_check(character_prop_body_is_placed(local) && character_prop_body_is_placed(one),
             "both bodies drawn in one frame are fresh at the end of it");

    /* And the next frame ages both, which is what a scene end is for. */
    character_prop_body_frame_ended();
    ut_check(character_prop_body_is_placed(local) && character_prop_body_is_placed(one),
             "a scene end on its own opens nothing: the substeps after it still ask");
    character_prop_body_begin_pass();
    ut_check(!character_prop_body_is_placed(local), "the first object of the next frame does");
    draw(&body_thing);
    ut_check(character_prop_body_is_placed(local), "and his draw in that frame stamps it again");
}

static void check_the_row_writes_none_of_the_lent_words(void)
{
    ut_section("the far path holds the words that hide the rig's own weapon");

    a_fresh_world();
    ut_check(arm_far(1u, 0u, LENT, 1u), "bank one carries its player's weapon");

    /* A value neither a hide nor a show would ever write, so any write at all is visible. */
    bank_thing[0].node_hidden[4] = 7u;
    draw(&bank_thing[0]);
    ut_check(bank_thing[0].node_hidden[4] == 7u,
             "the draw wrote none of them, because they were lent and not collected");

    character_prop_far_disarm(1u);
    ut_check(bank_thing[0].node_hidden[4] == 7u,
             "and letting go gave back nothing it had not taken");
}

static void check_the_rig_under_a_far_row_is_not_re_resolved(void)
{
    prop_body_t *body;

    ut_section("a far row is settled on the rig it was armed on");

    a_fresh_world();
    ut_check(arm_far(1u, 0u, LENT, 1u), "bank one carries its player's weapon");
    body = character_prop_body_of_bank(1u);
    draw(&bank_thing[0]);
    ut_check(character_prop_body_is_placed(body), "and its weapon was measured");

    put_u32(bank_thing[0].block + THING_MODEL, (uint32_t)(uintptr_t)guard.model);
    draw(&bank_thing[0]);
    ut_check(body->target_model == (uintptr_t)worn.model,
             "a handle wearing something else does not move the row onto it");
    ut_check(body->hidden_count == 1u && body->hidden_from == PROP_HIDDEN_LENT,
             "and the lent words are still exactly the ones that were lent");
    ut_check(!character_prop_body_is_placed(body), "nothing is measured for it either");
}

/* ============================================================================================ */

static void check_the_sphere_is_measured_without_a_draw(void)
{
    prop_body_t *body;
    float        centre[3];
    float        radius = 0.0f;

    ut_section("a body this pass did not draw still answers where its weapon is");

    a_fresh_world();
    ut_check(arm_far(1u, 0u, LENT, 1u), "bank one carries its player's weapon");
    body = character_prop_body_of_bank(1u);

    ut_check(pose_was_built(&bank_thing[0]), "the engine has posed the body at least once");
    ut_check(!character_prop_body_is_placed(body), "and nothing has drawn it yet");
    ut_check(sphere_of(bank_obj[0], WEAPON_NODE, centre, &radius),
             "the weapon is measured here rather than left to the engine");
    ut_check(radius > 0.0f, "and the sphere has a size");
    ut_check(centre[0] == body->shown_centre[0] && radius == body->shown_radius,
             "it is the sphere the row just measured");
    ut_check(character_prop_body_is_placed(body), "stamped with the pass that measured it");
    ut_check(body->shown_name_id == NAME_WEAPON,
             "and what the block says is equipped is what was made visible and measured");
    ut_check(body->shown_part_count == 2u,
             "out of every mesh of it, not out of the node the weapon table names");

    draw(&bank_thing[0]);
    ut_check(sphere_of(bank_obj[0], WEAPON_NODE, centre, &radius),
             "and after a draw it is answered from the draw");
    ut_check(centre[0] == body->shown_centre[0] && radius == body->shown_radius,
             "which is the sphere that draw measured");
}

static void check_what_a_body_without_a_pose_answers(void)
{
    prop_body_t *body;
    float        centre[3];
    float        first[3];
    float        radius = 0.0f;

    ut_section("a handle no pose was ever built on");

    a_fresh_world();
    ut_check(arm_far(1u, 0u, LENT, 1u), "bank one carries its player's weapon");
    body = character_prop_body_of_bank(1u);

    put_u32(bank_thing[0].block + THING_POSE_STAMP, POSE_STAMP_UNBUILT);
    ut_check(!pose_was_built(&bank_thing[0]), "the stamp says no pose was built on it");
    ut_check(!sphere_of(bank_obj[0], WEAPON_NODE, centre, &radius),
             "so nothing is measured out of matrices nobody wrote");
    ut_check(!body->placed, "and nothing was recorded");

    put_u32(bank_thing[0].block + THING_POSE_STAMP, 1000u);
    ut_check(sphere_of(bank_obj[0], WEAPON_NODE, first, &radius),
             "once a pose is there it is measured");

    /* A pass in which the body is neither drawn nor measurable. The sphere it last did measure is
     * still better than the stack the engine's own query would leave behind. */
    next_pass();
    put_u32(bank_thing[0].block + THING_POSE_STAMP, POSE_STAMP_UNBUILT);
    ut_check(sphere_of(bank_obj[0], WEAPON_NODE, centre, &radius),
             "a pass later it is answered out of the sphere it last measured");
    ut_check(centre[0] == first[0] && centre[1] == first[1] && centre[2] == first[2],
             "and that is that sphere, not a new one");
}

static void check_the_far_row_never_falls_through_for_its_weapon(void)
{
    float centre[3];
    float radius = 0.0f;

    ut_section("the node a far weapon hangs on is never left to the engine");

    a_fresh_world();
    ut_check(arm_far(1u, 0u, LENT, 1u), "bank one carries its player's weapon");

    ut_check(sphere_of(bank_obj[0], WEAPON_NODE, centre, &radius), "before any draw");
    draw(&bank_thing[0]);
    ut_check(sphere_of(bank_obj[0], WEAPON_NODE, centre, &radius), "in the pass that drew it");
    next_pass();
    ut_check(sphere_of(bank_obj[0], WEAPON_NODE, centre, &radius), "in a pass that did not");
    next_pass();
    ut_check(sphere_of(bank_obj[0], WEAPON_NODE, centre, &radius), "and in the pass after that");

    /* Everything else still belongs to the engine, and that is the other half of the rule. */
    ut_check(!sphere_of(bank_obj[0], 2, centre, &radius),
             "a node that is not the weapon's is the engine's own answer");
    ut_check(!sphere_of(bank_obj[0], -1, centre, &radius), "and so is no node at all");
    ut_check(!sphere_of(bank_obj[1], WEAPON_NODE, centre, &radius),
             "and so is every body no row of this table holds");

    put_u32(bank_block[0] + BLOCK_WEAPON_SLOT, 0u);
    ut_check(!sphere_of(bank_obj[0], WEAPON_NODE, centre, &radius),
             "and an empty hand, where the engine's own sphere is the fist itself");
}

static void check_a_weapon_change_inside_a_window(void)
{
    prop_body_t *body;
    float        centre[3];
    float        radius = 0.0f;

    ut_section("the weapon changed in a window, and the picture is a frame behind");

    a_fresh_world();
    ut_check(arm_far(1u, 0u, LENT, 1u), "bank one carries its player's weapon");
    body = character_prop_body_of_bank(1u);
    draw(&bank_thing[0]);
    ut_check(sphere_of(bank_obj[0], WEAPON_NODE, centre, &radius), "the weapon it was drawn with");

    /* Weapon row 3 names another node of the rig, which is what a weapon change looks like from
     * here: the block says one thing and the last draw showed another. */
    put_u32(bank_block[0] + BLOCK_WEAPON_SLOT, 3u);
    ut_check(body->shown_name_id == NAME_WEAPON, "the row still shows what it drew");
    ut_check(sphere_of(bank_obj[0], 3, centre, &radius),
             "the new weapon's node is answered in the same pass, measured again for it");
    ut_check(body->shown_name_id == NAME_RHAND, "and the row now shows the new one");
    ut_check(!sphere_of(bank_obj[0], WEAPON_NODE, centre, &radius),
             "while the node it has stopped showing goes back to the engine");
}

static void check_the_player_holds_the_sphere_his_draw_measured(void)
{
    prop_body_t *local;
    float        drawn[3];
    float        centre[3];
    float        radius = 0.0f;
    int32_t      node = -7;

    ut_section("the player's own body is measured by its draw, and holds what that draw measured");

    a_fresh_world();
    ut_check(character_prop_arm((uintptr_t)&player_cell, (uintptr_t)body_obj,
                                (uintptr_t)hero.model),
             "the player's own body carries his weapon");
    local = character_prop_body_of_bank(PROP_BANK_LOCAL);
    draw(&body_thing);
    ut_check(character_prop_weapon_node(body_obj, &node) && node == 3,
             "his weapon hangs on the borrowed rig's hand");
    ut_check(sphere_of(body_obj, WEAPON_NODE, drawn, &radius), "and the draw measured it");

    /* A frame his body was not dispatched in. The engine's own answer for that node is the fist
     * the weapon hangs on, so falling through would take the shot off the weapon and put it in
     * the hand. The sphere of his last draw is a real point on that weapon one frame ago. */
    next_pass();
    ut_check(!character_prop_body_is_placed(local), "the stamp on his sphere is a pass old");
    put_u32(body_thing.block + THING_POSE_STAMP, POSE_STAMP_UNBUILT);
    ut_check(sphere_of(body_obj, WEAPON_NODE, centre, &radius),
             "and his weapon answers out of the sphere his last draw measured");
    ut_check(centre[0] == drawn[0] && centre[1] == drawn[1] && centre[2] == drawn[2],
             "which is that sphere, on a handle no pose could have been read off");

    /* Holding is not measuring, and this is where the two part: the block says another weapon is
     * in his hand and the row has never shown that one. A far row measures it here. */
    put_u32(player_block + BLOCK_WEAPON_SLOT, 3u);
    ut_check(!sphere_of(body_obj, 3, centre, &radius),
             "a weapon he was never drawn with is the engine's own answer until he is drawn");
    put_u32(player_block + BLOCK_WEAPON_SLOT, 1u);

    put_u32(body_thing.block + THING_POSE_STAMP, 1000u);
    draw(&body_thing);
    ut_check(character_prop_body_is_placed(local), "and his draw in this frame stamps it again");
}

static void check_a_failed_measurement_keeps_the_last_good_sphere(void)
{
    prop_body_t *body;
    float        first[3];
    float        centre[3];
    float        radius = 0.0f;

    ut_section("a measurement that comes to nothing does not throw away the one that did");

    a_fresh_world();
    ut_check(arm_far(1u, 0u, LENT, 1u), "bank one carries its player's weapon");
    body = character_prop_body_of_bank(1u);
    ut_check(sphere_of(bank_obj[0], WEAPON_NODE, first, &radius), "and it measures one");
    ut_check(body->placed, "which is recorded on the row");

    /* A weapon change onto a node the model the meshes live in does carry, whose whole subtree
     * carries no mesh: everything the measurement needs is there except something to measure. It
     * is the last step that fails, and only the last step ever wrote the row. */
    next_pass();
    put_u32(bank_block[0] + BLOCK_WEAPON_SLOT, 3u);
    put_u32(&hero.node[4u * NODE_BYTES] + NODE_MESH_INDEX, 0xFFFFFFFFu);
    put_u32(&hero.node[5u * NODE_BYTES] + NODE_MESH_INDEX, 0xFFFFFFFFu);

    ut_check(sphere_of(bank_obj[0], 3, centre, &radius),
             "the row answers out of the sphere it last did measure");
    ut_check(centre[0] == first[0] && centre[1] == first[1] && centre[2] == first[2],
             "and that is that sphere, not one a failed attempt left behind");
    ut_check(body->placed, "the attempt that came to nothing left the record where it was");
}

static void check_the_player_inside_somebody_elses_window(void)
{
    float centre[3];
    float radius = 0.0f;

    ut_section("the player's own row, while another body's record is in the cell");

    a_fresh_world();
    ut_check(character_prop_arm((uintptr_t)&player_cell, (uintptr_t)body_obj,
                                (uintptr_t)hero.model),
             "the player's own body carries his weapon");
    draw(&body_thing);
    ut_check(sphere_of(body_obj, WEAPON_NODE, centre, &radius), "his weapon answers for its node");
    ut_check(character_prop_found_node(body_obj, "sabreblad01", 5) == WEAPON_NODE,
             "and his rig's own gun is passed over for it");

    /* A BANK WINDOW. The multiplayer puts a far body's record in the player's cell for the length
     * of a call, and that block names another body and says another weapon is in its hand. The
     * player's row reads the cell and nothing else, so what keeps it from answering out of a
     * stranger's record is that it asks whether the block still names ITS body. */
    player_cell = (uint32_t)(uintptr_t)bank_block[0];
    ut_check(!sphere_of(body_obj, WEAPON_NODE, centre, &radius),
             "no sphere of his is answered out of a record that is not his");
    ut_check(character_prop_found_node(body_obj, "sabreblad01", 5) == MOUNT_NO_NODE,
             "and no node of his either");

    player_cell = (uint32_t)(uintptr_t)player_block;
    ut_check(sphere_of(body_obj, WEAPON_NODE, centre, &radius),
             "and once the window closes he is answered for again");
    ut_check(character_prop_found_node(body_obj, "sabreblad01", 5) == WEAPON_NODE,
             "for both questions, without a draw in between");
}

/* ============================================================================================ */

static void check_the_lent_words_decide_a_found_node(void)
{
    ut_section("a weapon name that lands on the rig's own hidden blade");

    a_fresh_world();
    ut_check(arm_far(1u, 1u, LENT, 1u), "bank one wears the rig that carries a blade of its own");
    draw(&bank_thing[1]);

    /* `guard` carries sabreblad01 at node 6, under its own weapon mount at slot 4, which the far
     * path hides. A node under a hidden one keeps its joint local matrix, so a contact measured
     * there would sit near the rig's root. */
    ut_check(character_prop_found_node(bank_obj[1], "sabreblad01", 6) == WEAPON_NODE,
             "the node the drawn weapon's sphere answers for is given instead");
    ut_check(character_prop_found_node(bank_obj[1], "chest", 2) == MOUNT_NO_NODE,
             "and a name outside the weapon family is left alone");

    /* The same rig and the same node, with no words lent: the rule has nothing to go on. */
    a_fresh_world();
    ut_check(arm_far(1u, 1u, NULL, 0u), "the same body, armed without the far path's words");
    draw(&bank_thing[1]);
    ut_check(character_prop_found_node(bank_obj[1], "sabreblad01", 6) == MOUNT_NO_NODE,
             "and the rig's own blade is taken for a node that is drawn");
}

static void check_a_dead_body_is_answered_for_by_nobody(void)
{
    float centre[3];
    float radius = 0.0f;

    ut_section("the body died and the draw that takes the weapon down has not run");

    a_fresh_world();
    ut_check(arm_far(1u, 1u, LENT, 1u), "bank one carries its player's weapon");
    draw(&bank_thing[1]);
    ut_check(sphere_of(bank_obj[1], WEAPON_NODE, centre, &radius), "and it answers for it");
    ut_check(character_prop_found_node(bank_obj[1], "sabreblad01", 6) == WEAPON_NODE,
             "and gives its node instead of the rig's hidden one");

    /* The block now names another body. No draw has run since, and the row must not go on
     * answering out of a record that is no longer its body's. */
    put_u32(bank_block[1] + BLOCK_ACTOR, (uint32_t)(uintptr_t)other_obj);
    ut_check(!sphere_of(bank_obj[1], WEAPON_NODE, centre, &radius),
             "no sphere is answered out of a block that names another body");
    ut_check(character_prop_found_node(bank_obj[1], "sabreblad01", 6) == MOUNT_NO_NODE,
             "and no node is given instead of a found one either");

    put_u32(bank_block[1] + BLOCK_ACTOR, (uint32_t)(uintptr_t)bank_obj[1]);
    ut_check(sphere_of(bank_obj[1], WEAPON_NODE, centre, &radius),
             "and a body its block names again is answered for again");

    /* The object gave up its render handle, which is the other half of the same question. */
    put_u32(bank_obj[1] + OBJ_THING, (uint32_t)(uintptr_t)other_thing.block);
    ut_check(!sphere_of(bank_obj[1], WEAPON_NODE, centre, &radius),
             "a body that no longer owns the handle the row was armed on answers nothing");
    ut_check(character_prop_found_node(bank_obj[1], "sabreblad01", 6) == MOUNT_NO_NODE,
             "and the lookup is not answered for it either");
}

/* A far Jedi's blade at his own length, and the mesh he borrows it from left as it was.
 *
 * The engine's own length setter cannot run for him: it writes the mesh his body's render handle
 * draws, which is the borrowed rig's. So his length lives in his block, the four vertices are
 * worked out into a field of his row, and the shared mesh's position pointer names that field
 * for the length of one draw. What this checks is that it names it DURING the draw and the
 * model's own vertices again the moment the draw has returned.
 */
static void check_the_far_blade(void)
{
    const uintptr_t mesh = (uintptr_t)&hero.mesh[HERO_BLADE_MESH * MESH_BYTES];
    const uint32_t  own = (uint32_t)(uintptr_t)&hero.position[HERO_BLADE_MESH][0][0];
    prop_body_t    *body;
    float           vectors[18];
    float           size = 0.5f;

    ut_section("a far Jedi's blade is drawn at his own length");

    a_fresh_world();
    /* The shape the engine's own capture leaves in a Jedi block: two hilt vertices, two tip
     * vertices, and behind them the two deltas the tip is carried out along. */
    memset(vectors, 0, sizeof vectors);
    vectors[4] = 0.02f;                       /* the hilt has a width */
    vectors[14] = 1.0f;                       /* delta 0, along z     */
    vectors[17] = 1.0f;                       /* delta 1, along z     */
    memcpy(bank_block[0] + BLOCK_BLADE_VECTORS, vectors, sizeof vectors);
    memcpy(bank_block[0] + BLOCK_BLADE_SIZE, &size, sizeof size);
    put_u32(bank_block[0] + BLOCK_WEAPON_SLOT, 2u);

    ut_check(arm_far(1u, 0u, LENT, 1u), "the far body in bank one carries its player's weapon");
    body = character_prop_body_of_bank(1u);
    ut_check(body != NULL && body->blade_mesh == mesh,
             "the row found the blade mesh of the model the weapon meshes live in");
    ut_check(get_u32((uint8_t *)mesh + MESH_POSITIONS) == own,
             "and nothing of that mesh is touched before a draw");

    next_pass();
    draw(&bank_thing[0]);
    ut_check(drawn_blade_positions == (uint32_t)(uintptr_t)&body->blade_vert[0],
             "during the draw the mesh names this row's own vertices");
    ut_check(get_u32((uint8_t *)mesh + MESH_POSITIONS) == own,
             "and the model's own are back before the draw returned, so every other body wearing "
             "that asset reads what it always read");
    ut_check(!body->blade_open, "no exchange is left open");
    ut_check(body->blade_vert[2] == 0.0f && body->blade_vert[5] == 0.0f,
             "the two hilt vertices stand wherever the block put them");
    ut_check(body->blade_vert[8] == 0.5f && body->blade_vert[11] == 0.5f &&
                 body->blade_vert[10] == 0.02f,
             "and each tip vertex is its hilt vertex plus the delta behind it times the length");

    size = 1.0f;
    memcpy(bank_block[0] + BLOCK_BLADE_SIZE, &size, sizeof size);
    next_pass();
    draw(&bank_thing[0]);
    ut_check(body->blade_vert[8] == 1.0f && body->blade_vert[11] == 1.0f,
             "a blade that has finished lighting reaches the full delta");

    size = 0.0f;
    memcpy(bank_block[0] + BLOCK_BLADE_SIZE, &size, sizeof size);
    next_pass();
    draw(&bank_thing[0]);
    ut_check(body->blade_vert[8] == 0.0f && body->blade_vert[11] == 0.0f,
             "and one that has been put away is four vertices in one place, which draws nothing");

    /* The player's own row is not exchanged under. His block is ticked by the engine and the
     * blade guard is what keeps its setter off a borrowed rig; a second answer here would be a
     * second answer to a question that already has one. */
    ut_check(character_prop_arm((uintptr_t)&player_cell, (uintptr_t)body_obj,
                                (uintptr_t)hero.model),
             "the player's own body carries his weapon as well");
    put_u32(player_block + BLOCK_WEAPON_SLOT, 2u);
    memcpy(player_block + BLOCK_BLADE_VECTORS, vectors, sizeof vectors);
    next_pass();
    draw(&body_thing);
    ut_check(drawn_blade_positions == own,
             "the mesh names its own vertices throughout his draw");
    ut_check(get_u32((uint8_t *)mesh + MESH_POSITIONS) == own, "and after it");

    character_prop_disarm();
    character_prop_far_disarm(1u);
    ut_check(body->blade_mesh == 0u,
             "a row that let its reference model go forgets the mesh in it, because the asset it "
             "is part of may be handed back");
}

static void check_letting_go(void)
{
    prop_body_t *local;
    uint32_t     freed_before;

    ut_section("letting a far body go");

    a_fresh_world();
    ut_check(character_prop_arm((uintptr_t)&player_cell, (uintptr_t)body_obj,
                                (uintptr_t)hero.model),
             "the player's own body carries his weapon");
    local = character_prop_body_of_bank(PROP_BANK_LOCAL);
    ut_check(arm_far(1u, 0u, LENT, 1u), "and bank one carries its player's");
    draw(&bank_thing[0]);
    freed_before = freed_arrays;

    character_prop_far_disarm(1u);
    ut_check(character_prop_body_of_bank(1u) == NULL, "the row is free for the next body");
    ut_check(freed_arrays == freed_before + 1u,
             "and the handle's arrays went back with it, because its hero asset may be freed");
    ut_check(character_prop_body_for_obj(bank_obj[0]) == NULL,
             "nothing is answered for the body any more");
    ut_check(character_prop_body_armed() == 1u, "and only the player's row is still carrying");

    character_prop_far_disarm(1u);
    ut_check(character_prop_body_of_bank(1u) == NULL, "letting go twice is letting go once");
    character_prop_far_disarm(PROP_BANK_LOCAL);
    ut_check(character_prop_body_of_bank(PROP_BANK_LOCAL) == local && local->armed,
             "and bank zero is not a far bank, so the player's row is not touched by it");

    ut_check(arm_far(1u, 0u, LENT, 1u), "the bank takes a row again when a body is dressed again");
    a_fresh_world();
}

int main(void)
{
    check_the_hand_is_resolved_when_the_row_is_armed();
    check_the_far_row_is_refused_and_says_so();
    check_three_bodies_at_once();
    check_one_pass_covers_a_whole_frame();
    check_the_row_writes_none_of_the_lent_words();
    check_the_rig_under_a_far_row_is_not_re_resolved();
    check_the_sphere_is_measured_without_a_draw();
    check_what_a_body_without_a_pose_answers();
    check_the_far_row_never_falls_through_for_its_weapon();
    check_a_weapon_change_inside_a_window();
    check_the_player_holds_the_sphere_his_draw_measured();
    check_a_failed_measurement_keeps_the_last_good_sphere();
    check_the_player_inside_somebody_elses_window();
    check_the_lent_words_decide_a_found_node();
    check_a_dead_body_is_answered_for_by_nobody();
    check_the_far_blade();
    check_letting_go();

    /* The line a level end writes, so that its arguments are proven to match its format. */
    character_prop_body_report();
    character_prop_blade_report();
    return ut_summary("character prop far");
}
