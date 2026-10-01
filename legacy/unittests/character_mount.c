/* character_mount.c: the half of the weapon point that can be checked without the game.
 *
 * What is driven here is the rule and its refusals, which is where the whole decision is. The
 * names come from the engine's own 33 entry node name table and the two tables that index it: the
 * weapon configuration, whose rows name `waist`, `sabre` and `gun01` to `gun09`, and the swing
 * table, whose reachable rows name `lhand`, `rhand`, `lfoot` and `sabreblad01`.
 *
 * Everything under the rule reads engine memory: the two anchors, the name table and the lookup
 * hook all need a live model, and they are checked by the lines a swap writes to the log.
 */
#include "unittest.h"

#include "character_mount.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The hand character_prop resolves on a borrowed rig. Any positive number does, and the value only
 * has to come back unchanged; what the rule may never do is invent one. */
#define HAND 7

static void check_the_family_rule_is_a_family_and_not_a_roster(void)
{
    ut_section("the weapon geometry family is three prefixes and no character names");
    ut_check(character_mount_name_is_weapon_geometry("gun"), "the bare gun mesh counts");
    ut_check(character_mount_name_is_weapon_geometry("gun09"), "so does the last of the rack");
    ut_check(character_mount_name_is_weapon_geometry("gunbarrel"), "and the tank's barrel");
    ut_check(character_mount_name_is_weapon_geometry("sabre"), "the hilt counts");
    ut_check(character_mount_name_is_weapon_geometry("SabreBlad01"), "case does not decide");
    ut_check(character_mount_name_is_weapon_geometry("blade"), "the gaderffii blade counts");
    ut_check(!character_mount_name_is_weapon_geometry("rhand"), "a hand is not weapon geometry");
    ut_check(!character_mount_name_is_weapon_geometry("waist"), "and neither is the unarmed slot");
    ut_check(!character_mount_name_is_weapon_geometry("glass"), "and neither is a prop");
    ut_check(!character_mount_name_is_weapon_geometry(NULL), "no name reads as no weapon");

    ut_section("the blade sub family, which is what the shared mesh resize hangs off");
    ut_check(character_mount_name_is_blade("sabreblad01"), "the shipped blade name");
    ut_check(character_mount_name_is_blade("sabreblade2"), "and its second spelling");
    ut_check(character_mount_name_is_blade("blade"), "and the bare one");
    ut_check(!character_mount_name_is_blade("sabre"), "the hilt is not the blade");
    ut_check(!character_mount_name_is_blade("gun01"), "and a gun certainly is not");
}

static void check_a_weapon_name_the_rig_lacks_lands_on_the_hand(void)
{
    ut_section("every name the two weapon tables can ask for is answered");
    ut_check(character_mount_answer("sabre", 0, HAND) == HAND,
             "the sabre slot's own muzzle name");
    ut_check(character_mount_answer("gun01", 0, HAND) == HAND, "the first blaster");
    ut_check(character_mount_answer("gun09", 0, HAND) == HAND, "and the last of the nine");

    ut_section("the one the melee damage hangs on");
    /* Nineteen of the twenty eight swing rows name this, and every row a hero can reach is one of
     * them. The answer goes into the body's contact node cell, and the contact branch opens with a
     * test that the cell is not zero, so a refusal here is a blow that never touches anything. */
    ut_check(character_mount_answer("sabreblad01", 0, HAND) == HAND,
             "the blade name every reachable swing row carries");
    ut_check(character_mount_answer("sabreblade1", 0, HAND) == HAND, "and its second spelling");
    ut_check(character_mount_answer("blade", 0, HAND) == HAND,
             "and the bare one a staff and a gaffi stick carry");
}

static void check_the_engines_own_answer_is_left_alone(void)
{
    ut_section("a rig that carries the name needs no help");
    ut_check(character_mount_answer("gun01", 5, HAND) == MOUNT_NO_NODE,
             "a found node is handed back untouched");
    ut_check(character_mount_answer("sabreblad01", 3, HAND) == MOUNT_NO_NODE,
             "including the blade of a rig that really has one");

    ut_section("nothing outside the weapon family is ever answered");
    ut_check(character_mount_answer("rhand", 0, HAND) == MOUNT_NO_NODE,
             "a limb lookup goes through untouched, and the punch rows need it to");
    ut_check(character_mount_answer("lfoot", 0, HAND) == MOUNT_NO_NODE, "so does the kick row");
    ut_check(character_mount_answer("waist", 0, HAND) == MOUNT_NO_NODE,
             "and so does the unarmed weapon row");
    ut_check(character_mount_answer("head", 0, HAND) == MOUNT_NO_NODE, "and every other body part");
    ut_check(character_mount_answer(NULL, 0, HAND) == MOUNT_NO_NODE,
             "a name id the table does not cover reads as no name");
}

static void check_no_hand_means_no_answer(void)
{
    ut_section("without a hand carrying the weapon there is nothing to point at");
    ut_check(character_mount_answer("gun01", 0, MOUNT_NO_NODE) == MOUNT_NO_NODE,
             "a rig the weapon could not be placed on is left as it was");
    ut_check(character_mount_answer("sabreblad01", 0, MOUNT_NO_NODE) == MOUNT_NO_NODE,
             "and the blade name with it");

    ut_section("zero is refused, and not only as a range test");
    /* Zero is exactly the value the contact gate rejects. Answering with it would leave the blow
     * doing nothing while every log line said the substitution had happened. */
    ut_check(character_mount_answer("gun01", 0, 0) == MOUNT_NO_NODE,
             "a hand at node zero is refused rather than reported as an answer");
    ut_check(character_mount_answer("sabre", 0, -2) == MOUNT_NO_NODE, "and so is a negative one");
}

/* ==============================================================================================
 * The walk over a rig's own weapon geometry
 *
 * It reads a model out of memory, so the model here is built in the test's own: the fields are the
 * four the walk touches, at the offsets the engine puts them at, and the child, sibling and parent
 * links are real addresses exactly as the loader leaves them (0x00401455 turns every -1 into 0 and
 * every index into a pointer, so a NULL link and a terminated list are the same thing).
 *
 * What is proven here is the part no measurement over the shipped assets can prove: that the walk
 * stops at what it collects, that it never answers with a node the weapon hangs off, and that a
 * slot outside the model's own node count is refused rather than written.
 * ============================================================================================ */

#define NODE_BYTES        0xB4u
#define NODE_MATRIX_SLOT  0x44u
#define NODE_PARENT       0x50u
#define NODE_FIRST_CHILD  0x58u
#define NODE_NEXT_SIBLING 0x5Cu
#define MODEL_NUM_NODES   0x54u
#define MODEL_NODES       0x58u
#define FAKE_NODES        8u

static uint8_t fake_nodes[FAKE_NODES * NODE_BYTES];
static uint8_t fake_model[0x60];

static uint8_t *fake_node(uint32_t index)
{
    return &fake_nodes[index * NODE_BYTES];
}

static void put_u32(uint8_t *at, uint32_t value)
{
    memcpy(at, &value, sizeof value);
}

/* One node: its name, the matrix slot it answers with, and its place in the tree. */
static void set_node(uint32_t index, const char *name, uint32_t slot, int32_t parent,
                     int32_t child, int32_t sibling)
{
    uint8_t *node = fake_node(index);

    memset(node, 0, NODE_BYTES);
    memcpy(node, name, strlen(name));
    put_u32(node + NODE_MATRIX_SLOT, slot);
    put_u32(node + NODE_PARENT,
            (parent < 0) ? 0u : (uint32_t)(uintptr_t)fake_node((uint32_t)parent));
    put_u32(node + NODE_FIRST_CHILD,
            (child < 0) ? 0u : (uint32_t)(uintptr_t)fake_node((uint32_t)child));
    put_u32(node + NODE_NEXT_SIBLING,
            (sibling < 0) ? 0u : (uint32_t)(uintptr_t)fake_node((uint32_t)sibling));
}

/* A rig shaped like the ones the census found: a hand hanging off a node whose NAME is weapon
 * geometry, a mount with two guns under it, and a hilt on the waist.
 *
 *   0 dummy01
 *   1 waist          -> 2 sabrewaist, 3 gunarms
 *   2 sabrewaist     the belt hilt six shipped rigs carry and nothing ever hid
 *   3 gunarms        -> 4 rhand      `tank` hangs its hand off a node named like this
 *   4 rhand          -> 5 weapon
 *   5 weapon         -> 6 gun01 -> 7 gun02
 */
static void build_the_rig(uint32_t nodes)
{
    memset(fake_model, 0, sizeof fake_model);
    put_u32(fake_model + MODEL_NUM_NODES, nodes);
    put_u32(fake_model + MODEL_NODES, (uint32_t)(uintptr_t)fake_node(0));

    set_node(0, "dummy01",    0, -1,  1, -1);
    set_node(1, "waist",      1,  0,  2, -1);
    set_node(2, "sabrewaist", 2,  1, -1,  3);
    set_node(3, "gunarms",    3,  1,  4, -1);
    set_node(4, "rhand",      4,  3,  5, -1);
    set_node(5, "weapon",     5,  4,  6, -1);
    set_node(6, "gun01",      6,  5, -1,  7);
    set_node(7, "gun02",      7,  5, -1, -1);
}

static bool holds(const uint32_t *slots, uint32_t count, uint32_t wanted)
{
    uint32_t i;

    for (i = 0; i < count; ++i) {
        if (slots[i] == wanted) {
            return true;
        }
    }
    return false;
}

static void check_the_rigs_own_weapons_are_collected(void)
{
    uint32_t slots[FAKE_NODES];
    uint32_t count;

    build_the_rig(FAKE_NODES);

    ut_section("the belt hilt and the mount, and the mount stands for its whole rack");
    count = character_mount_own_weapon_nodes((uintptr_t)fake_model, (uintptr_t)fake_node(4),
                                             "weapon", slots, FAKE_NODES);
    ut_checkf(count == 2u, "two subtrees answer, not two nodes and not eleven names (%u)",
              (unsigned)count);
    ut_check(holds(slots, count, 2u), "`sabrewaist` is one of them, and no weapon row names it");
    ut_check(holds(slots, count, 5u), "the mount is the other");
    ut_check(!holds(slots, count, 6u) && !holds(slots, count, 7u),
             "and its guns are NOT listed: one word in thing+0x28 takes the whole subtree");

    ut_section("the chain the weapon hangs off is never answered with");
    ut_check(!holds(slots, count, 3u),
             "`gunarms` reads as weapon geometry and carries the hand, so it stays visible");
    ut_check(!holds(slots, count, 4u), "and neither is the hand itself");

    ut_section("without the engine's name for the mount its children are collected one at a time");
    count = character_mount_own_weapon_nodes((uintptr_t)fake_model, (uintptr_t)fake_node(4),
                                             NULL, slots, FAKE_NODES);
    ut_checkf(count == 3u, "the hilt and the two guns (%u)", (unsigned)count);
    ut_check(holds(slots, count, 6u) && holds(slots, count, 7u), "both guns by name");
    ut_check(!holds(slots, count, 5u), "and the mount itself is not weapon geometry");
}

static void check_the_bounds(void)
{
    uint32_t slots[FAKE_NODES];
    uint32_t count;

    ut_section("a slot outside the model's own node count is refused, not written");
    build_the_rig(3u);          /* the same tree, with the model claiming three nodes */
    count = character_mount_own_weapon_nodes((uintptr_t)fake_model, (uintptr_t)fake_node(4),
                                             "weapon", slots, FAKE_NODES);
    ut_checkf(count == 1u, "only `sabrewaist` at slot 2 fits inside three words (%u)",
              (unsigned)count);
    ut_check(holds(slots, count, 2u), "and it is that one");

    ut_section("the caller's room is the bound on the answer");
    build_the_rig(FAKE_NODES);
    count = character_mount_own_weapon_nodes((uintptr_t)fake_model, (uintptr_t)fake_node(4),
                                             "weapon", slots, 1u);
    ut_checkf(count == 1u, "one slot of room answers one slot (%u)", (unsigned)count);

    ut_section("nothing to walk answers nothing");
    ut_check(character_mount_own_weapon_nodes(0, (uintptr_t)fake_node(4), "weapon", slots,
                                              FAKE_NODES) == 0u, "no model");
    ut_check(character_mount_own_weapon_nodes((uintptr_t)fake_model, (uintptr_t)fake_node(4),
                                              "weapon", NULL, FAKE_NODES) == 0u,
             "nowhere to put it");
    ut_check(character_mount_own_weapon_nodes((uintptr_t)fake_model, (uintptr_t)fake_node(4),
                                              "weapon", slots, 0u) == 0u, "no room");
}

/* ==============================================================================================
 * THE FOUND NODE, when the rig carries the name and the node holds no sphere where the weapon is.
 *
 *   0 dummy01     children 1 rhand and 5 lfoot
 *   1 rhand       child 2 weapon
 *   2 weapon      child 3 sabre; the rig's own mount, hidden on the handle as on obiwan.baf
 *   3 sabre       child 4 sabreblad01
 *   4 sabreblad01
 *   5 lfoot
 *
 * Every node carries a mesh until a case takes it away. `sabre` stands for the node the drawn
 * weapon's sphere answers for, resolved_node_of of the equipped slot.
 * ============================================================================================ */

#define NODE_MESH_INDEX   0x4Cu
#define EQUIPPED          3

static void build_the_blade_rig(void)
{
    uint32_t i;

    memset(fake_model, 0, sizeof fake_model);
    put_u32(fake_model + MODEL_NUM_NODES, 6u);
    put_u32(fake_model + MODEL_NODES, (uint32_t)(uintptr_t)fake_node(0));

    set_node(0, "dummy01",     0, -1,  1, -1);
    set_node(1, "rhand",       1,  0,  2,  5);
    set_node(2, "weapon",      2,  1,  3, -1);
    set_node(3, "sabre",       3,  2,  4, -1);
    set_node(4, "sabreblad01", 4,  3, -1, -1);
    set_node(5, "lfoot",       5,  0, -1, -1);
    for (i = 0; i < 6u; ++i) {
        put_u32(fake_node(i) + NODE_MESH_INDEX, i);
    }
}

static int32_t found_answer(const char *wanted, int32_t found, const uint32_t *hidden,
                            uint32_t hidden_count, int32_t equipped)
{
    return character_mount_answer_found((uintptr_t)fake_model, wanted, found, hidden,
                                        hidden_count, equipped);
}

static void check_a_found_node_without_a_sphere_is_answered_by_the_weapon(void)
{
    const uint32_t mount_hidden[] = { 2u };
    const uint32_t blade_hidden[] = { 4u };
    const uint32_t foot_hidden[] = { 5u };
    const uint32_t slot_hidden[] = { 7u };

    build_the_blade_rig();
    ut_section("a found blade under the rig's own hidden weapon takes the drawn weapon's node");
    ut_check(found_answer("sabreblad01", 4, mount_hidden, 1u, EQUIPPED) == EQUIPPED,
             "the blade two nodes below the hidden mount, as on obiwan.baf");
    ut_check(found_answer("sabreblad01", 4, blade_hidden, 1u, EQUIPPED) == EQUIPPED,
             "and a blade that is hidden itself");
    ut_check(found_answer("sabreblad01", 4, NULL, 0u, EQUIPPED) == MOUNT_NO_NODE,
             "a blade with a mesh and nothing hidden above it keeps the engine's answer");

    ut_section("the hidden words are matrix slots on the found node's own path");
    ut_check(found_answer("sabreblad01", 4, foot_hidden, 1u, EQUIPPED) == MOUNT_NO_NODE,
             "a hidden foot off the blade's path leaves the engine's answer");
    set_node(2, "weapon", 7, 1, 3, -1);
    ut_check(found_answer("sabreblad01", 4, slot_hidden, 1u, EQUIPPED) == EQUIPPED,
             "the mount is found by its slot word, not by its place in the array");
    ut_check(found_answer("sabreblad01", 4, mount_hidden, 1u, EQUIPPED) == MOUNT_NO_NODE,
             "and its index alone hides nothing");
    set_node(2, "weapon", 2, 1, 3, -1);
    put_u32(fake_node(2) + NODE_MESH_INDEX, 2u);

    ut_section("a found blade without a mesh, as obi.baf's bare sabreblad01");
    put_u32(fake_node(4) + NODE_MESH_INDEX, 0xFFFFFFFFu);
    ut_check(found_answer("sabreblad01", 4, NULL, 0u, EQUIPPED) == EQUIPPED,
             "the node sphere call would write no centre for it, so the sabre's node answers");

    ut_section("what the rule for a found node leaves alone");
    put_u32(fake_node(5) + NODE_MESH_INDEX, 0xFFFFFFFFu);
    ut_check(found_answer("lfoot", 5, NULL, 0u, EQUIPPED) == MOUNT_NO_NODE,
             "a limb without a mesh is not weapon geometry, and the kick row keeps it");
    ut_check(found_answer("sabreblad01", 4, NULL, 0u, 4) == MOUNT_NO_NODE,
             "the node the weapon's sphere already answers for is not replaced by itself");
    ut_check(found_answer("sabreblad01", 4, NULL, 0u, 0) == MOUNT_NO_NODE,
             "and nothing is replaced by zero, the contact gate's own no");
    ut_check(found_answer("sabreblad01", 6, NULL, 0u, EQUIPPED) == MOUNT_NO_NODE,
             "a node past the model's own count is not read");
    ut_check(character_mount_answer_found(0u, "sabreblad01", 4, NULL, 0u, EQUIPPED) ==
             MOUNT_NO_NODE, "and without a model nothing is");
}

int main(void)
{
    check_the_family_rule_is_a_family_and_not_a_roster();
    check_a_weapon_name_the_rig_lacks_lands_on_the_hand();
    check_the_engines_own_answer_is_left_alone();
    check_no_hand_means_no_answer();
    check_the_rigs_own_weapons_are_collected();
    check_the_bounds();
    check_a_found_node_without_a_sphere_is_answered_by_the_weapon();

    return ut_summary("character mount");
}
