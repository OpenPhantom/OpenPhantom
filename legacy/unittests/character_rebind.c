/* character_rebind.c: the rebind and the node words, on a rig built in the test's own memory.
 *
 * The rig is laid out the way the loader leaves one: a node array of 0xB4 byte records with the
 * name at the front, the matrix slot at +0x44, the mesh index at +0x4C and real pointers for the
 * parent, the first child and the next sibling. Everything under test reads it through the same
 * guarded reads it uses on the engine, so what is proven here is the rule and not a copy of it:
 * where the push of a far body leaves from, what its six words say, what is hidden on it and under
 * which condition, and what the rebind does to a handle and in which order.
 */
#include "unittest.h"

#include "character_bodies.h"
#include "character_model_sites.h"
#include "character_rebind.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define NODE_BYTES        0xB4u
#define NODE_MATRIX_SLOT  0x44u
#define NODE_MESH         0x4Cu
#define NODE_PARENT       0x50u
#define NODE_FIRST_CHILD  0x58u
#define NODE_NEXT_SIBLING 0x5Cu
#define MODEL_NUM_NODES   0x54u
#define MODEL_NODES       0x58u
#define RIG_NODES         10u

static uint8_t rig_nodes[RIG_NODES * NODE_BYTES];
static uint8_t rig_model[0x60];

static uint8_t *node(uint32_t index)
{
    return &rig_nodes[index * NODE_BYTES];
}

static void put_u32(uint8_t *at, uint32_t value)
{
    memcpy(at, &value, sizeof value);
}

static uint32_t get_u32(const uint8_t *at)
{
    uint32_t value;

    memcpy(&value, at, sizeof value);
    return value;
}

static void set_node(uint32_t index, const char *name, int32_t mesh, int32_t parent,
                     int32_t child, int32_t sibling)
{
    uint8_t *at = node(index);

    memset(at, 0, NODE_BYTES);
    memcpy(at, name, strlen(name));
    put_u32(at + NODE_MATRIX_SLOT, index);
    put_u32(at + NODE_MESH, (uint32_t)mesh);
    put_u32(at + NODE_PARENT, (parent < 0) ? 0u : (uint32_t)(uintptr_t)node((uint32_t)parent));
    put_u32(at + NODE_FIRST_CHILD,
            (child < 0) ? 0u : (uint32_t)(uintptr_t)node((uint32_t)child));
    put_u32(at + NODE_NEXT_SIBLING,
            (sibling < 0) ? 0u : (uint32_t)(uintptr_t)node((uint32_t)sibling));
}

/*   0 dummy01                no mesh
 *   1 waist          mesh    children 2 chest, 4 sabrewaist
 *   2 chest          mesh    children 3 head, 6 rhand
 *   3 head           mesh
 *   4 sabrewaist     mesh    a belt hilt: weapon geometry, hidden, and the left hand hangs off it
 *   5 lhand          mesh    under the hidden hilt, so its matrix is never built
 *   6 rhand          no mesh child 7 weapon
 *   7 weapon         mesh    the mount, hidden with everything under it
 *   8 gun01          mesh    under the mount
 *   9 tail           mesh    a leaf of the waist */
static void build_rig(uint32_t count)
{
    memset(rig_model, 0, sizeof rig_model);
    put_u32(rig_model + MODEL_NUM_NODES, count);
    put_u32(rig_model + MODEL_NODES, (uint32_t)(uintptr_t)node(0));

    set_node(0, "dummy01",    -1, -1,  1, -1);
    set_node(1, "waist",       0,  0,  2, -1);
    set_node(2, "chest",       1,  1,  3,  4);
    set_node(3, "head",        2,  2, -1,  6);
    set_node(4, "sabrewaist",  3,  1,  5,  9);
    set_node(5, "lhand",       4,  4, -1, -1);
    set_node(6, "rhand",      -1,  2,  7, -1);
    set_node(7, "weapon",      5,  6,  8, -1);
    set_node(8, "gun01",       6,  7, -1, -1);
    set_node(9, "tail",        7,  1, -1, -1);
}

static bool listed(const uint32_t *slots, uint32_t count, uint32_t wanted)
{
    uint32_t i;

    for (i = 0; i < count; ++i) {
        if (slots[i] == wanted) {
            return true;
        }
    }
    return false;
}

static void check_the_own_weapons(void)
{
    uint32_t slots[BODY_HIDE_MAX];
    uint32_t count;

    build_rig(RIG_NODES);
    ut_section("the rig's own weapons are the belt hilt and the mount, not the right hand's chain");
    count = character_rebind_own_weapons((uintptr_t)rig_model, slots, BODY_HIDE_MAX);
    ut_checkf(count == 2u, "two subtrees (%u)", (unsigned)count);
    ut_check(listed(slots, count, 4u) && listed(slots, count, 7u), "the hilt and the mount");
    ut_check(!listed(slots, count, 8u), "the gun under the mount goes with the mount");
    ut_check(!listed(slots, count, 6u) && !listed(slots, count, 2u),
             "and nothing the right hand hangs off");

    ut_section("a rig that ends at the forearm keeps that chain instead");
    memcpy(node(6), "rforarm\0", 8u);
    count = character_rebind_own_weapons((uintptr_t)rig_model, slots, BODY_HIDE_MAX);
    ut_checkf(count == 2u, "the same two (%u)", (unsigned)count);
    ut_check(!listed(slots, count, 6u), "and the forearm is not among them");

    /* The chain, and not the node alone. A node the weapon hand hangs UNDER that happens to be
     * named like weapon geometry would take the whole body with it, because one word in the
     * handle hides a subtree and the walk stops at what it collects. The hand that chain is
     * walked from is the same one the push rule passes over, and a second copy of the two names
     * would drift apart on exactly the rigs that end at a forearm. */
    memcpy(node(2), "sabre01\0", 8u);
    count = character_rebind_own_weapons((uintptr_t)rig_model, slots, BODY_HIDE_MAX);
    ut_check(!listed(slots, count, 2u),
             "the node the forearm hangs under is not hidden, whatever it is called");
    ut_check(listed(slots, count, 7u), "and the mount under the forearm still is");
}

static void check_the_far_words(void)
{
    uint32_t hidden[BODY_HIDE_MAX];
    uint32_t count;
    int32_t  words[REBIND_WORDS];
    bool     on_hand = true;

    build_rig(RIG_NODES);
    count = character_rebind_own_weapons((uintptr_t)rig_model, hidden, BODY_HIDE_MAX);

    ut_section("the six words of a far body");
    ut_check(character_rebind_far_nodes((uintptr_t)rig_model, hidden, count, words, &on_hand),
             "a rig with a node a push can leave from is answered");
    ut_checkf(words[REBIND_WORD_MOUNT] == (int32_t)RIG_NODES,
              "the mount is one past the last node, so the hide and the show refuse it (%d)",
              (int)words[REBIND_WORD_MOUNT]);
    ut_check(words[REBIND_WORD_SABRE] == 0, "the blade is 0, which the blade guard declines on");
    ut_check(words[REBIND_WORD_CHEST] == 2 && words[REBIND_WORD_WAIST] == 1 &&
             words[REBIND_WORD_HEAD] == 3, "the chest, the waist and the head by name");

    ut_section("the push leaves from a node that has a mesh and a built matrix");
    ut_checkf(words[REBIND_WORD_LHAND] == 2,
              "the left hand hangs under the hidden hilt, so the chest (%d)",
              (int)words[REBIND_WORD_LHAND]);
    ut_check(!on_hand, "and it is not the hand the weapon hangs on");
    ut_check(character_rebind_far_nodes((uintptr_t)rig_model, hidden, 0u, words, &on_hand) &&
             words[REBIND_WORD_LHAND] == 5 && !on_hand,
             "with nothing hidden the left hand itself");

    put_u32(node(1) + NODE_MESH, 0xFFFFFFFFu);
    put_u32(node(2) + NODE_MESH, 0xFFFFFFFFu);
    ut_check(character_rebind_far_nodes((uintptr_t)rig_model, hidden, count, words, &on_hand) &&
             words[REBIND_WORD_LHAND] == 3 && !on_hand,
             "none of the three names will do: the first node of the rig that will, the head");

    put_u32(node(3) + NODE_MESH, 0xFFFFFFFFu);
    put_u32(node(9) + NODE_MESH, 0xFFFFFFFFu);
    ut_check(!character_rebind_far_nodes((uintptr_t)rig_model, hidden, count, words, &on_hand),
             "and a rig with no such node at all is refused");

    build_rig(RIG_NODES);
    memcpy(node(3), "neck\0", 5u);
    ut_check(character_rebind_far_nodes((uintptr_t)rig_model, hidden, count, words, &on_hand) &&
             words[REBIND_WORD_HEAD] == 0, "a name the rig lacks is 0");
    ut_check(!character_rebind_far_nodes(0u, hidden, count, words, &on_hand),
             "no model, no words");
    ut_check(!character_rebind_far_nodes((uintptr_t)rig_model, hidden, count, words, NULL),
             "and nowhere to say which node it was is no words either");
}

/* The push leaves from a node, and the engine takes that node's sphere. While a weapon is drawn
 * on the hand it hangs on, that node's sphere is the WEAPON'S, so a push that left from there
 * would start at the blade. These are the rigs where the two would meet.
 *
 * The rig above carries `rhand` without a mesh, so the case never came up in it. `tathum1.baf`,
 * which the panel offers, carries a left hand named `lhand01` and a right hand with a mesh, and
 * is the one shipped rig on which the old rule put the push on the weapon hand. */
static void check_the_push_avoids_the_weapon_hand(void)
{
    uint32_t hidden[BODY_HIDE_MAX];
    uint32_t count;
    int32_t  words[REBIND_WORDS];
    bool     on_hand = true;

    ut_section("a rig with a left hand keeps it, which is what nearly every shipped rig is");

    build_rig(RIG_NODES);
    put_u32(node(6) + NODE_MESH, 8u);
    count = character_rebind_own_weapons((uintptr_t)rig_model, hidden, BODY_HIDE_MAX);
    ut_check(character_rebind_far_nodes((uintptr_t)rig_model, hidden, 0u, words, &on_hand) &&
             words[REBIND_WORD_LHAND] == 5 && !on_hand,
             "the left hand, with the right hand carrying a mesh beside it");

    ut_section("the push never leaves from the hand the weapon hangs on");

    /* The rig tathum1 is in: no `lhand`, and a right hand that carries a mesh. */
    build_rig(RIG_NODES);
    memcpy(node(5), "lhand01\0", 8u);
    put_u32(node(6) + NODE_MESH, 8u);
    count = character_rebind_own_weapons((uintptr_t)rig_model, hidden, BODY_HIDE_MAX);
    ut_check(character_rebind_far_nodes((uintptr_t)rig_model, hidden, count, words, &on_hand),
             "the rig is answered");
    ut_checkf(words[REBIND_WORD_LHAND] == 2,
              "and the push takes the chest, not the right hand the weapon hangs on (%d)",
              (int)words[REBIND_WORD_LHAND]);
    ut_check(!on_hand, "so the push leaves the fist and not the weapon");

    ut_section("and the row scan does not take it either");
    put_u32(node(1) + NODE_MESH, 0xFFFFFFFFu);
    put_u32(node(2) + NODE_MESH, 0xFFFFFFFFu);
    put_u32(node(3) + NODE_MESH, 0xFFFFFFFFu);
    ut_check(character_rebind_far_nodes((uintptr_t)rig_model, hidden, count, words, &on_hand),
             "with none of the names to leave from the rig is still answered");
    ut_checkf(words[REBIND_WORD_LHAND] == 9 && !on_hand,
              "out of the row of nodes, past the hand and onto the tail behind it (%d)",
              (int)words[REBIND_WORD_LHAND]);

    /* The rule is the hand and not a place in a list. `rforarm` was never in the list of names,
     * so moving `rhand` to the end of it would leave this rig taking its forearm out of the row
     * scan. The node the push lands on here lies AFTER the forearm in the row. */
    ut_section("a rig that ends at the forearm hangs its weapon there, and the rule follows it");
    build_rig(RIG_NODES);
    memcpy(node(5), "lhand01\0", 8u);
    memcpy(node(6), "rforarm\0", 8u);
    put_u32(node(6) + NODE_MESH, 8u);
    count = character_rebind_own_weapons((uintptr_t)rig_model, hidden, BODY_HIDE_MAX);
    ut_check(!listed(hidden, count, 6u), "the forearm is the weapon hand, so its chain is kept");
    put_u32(node(1) + NODE_MESH, 0xFFFFFFFFu);
    put_u32(node(2) + NODE_MESH, 0xFFFFFFFFu);
    put_u32(node(3) + NODE_MESH, 0xFFFFFFFFu);
    ut_check(character_rebind_far_nodes((uintptr_t)rig_model, hidden, count, words, &on_hand),
             "the rig is answered");
    ut_checkf(words[REBIND_WORD_LHAND] == 9 && !on_hand,
              "out of the row, past the forearm, on the tail behind it (%d)",
              (int)words[REBIND_WORD_LHAND]);

    ut_section("the hand is the last way out, and it is taken rather than refuse the whole rig");
    build_rig(RIG_NODES);
    memcpy(node(5), "lhand01\0", 8u);
    put_u32(node(6) + NODE_MESH, 8u);
    count = character_rebind_own_weapons((uintptr_t)rig_model, hidden, BODY_HIDE_MAX);
    put_u32(node(1) + NODE_MESH, 0xFFFFFFFFu);
    put_u32(node(2) + NODE_MESH, 0xFFFFFFFFu);
    put_u32(node(3) + NODE_MESH, 0xFFFFFFFFu);
    put_u32(node(9) + NODE_MESH, 0xFFFFFFFFu);
    ut_check(character_rebind_far_nodes((uintptr_t)rig_model, hidden, count, words, &on_hand),
             "a rig whose only node a push can leave from is that hand is still dressed");
    ut_checkf(words[REBIND_WORD_LHAND] == 6, "the push leaves from the hand (%d)",
              (int)words[REBIND_WORD_LHAND]);
    ut_check(on_hand, "and it says so, which is what the log line hangs off");

    ut_section("the hidden hilt still moves the push off the left hand, and not onto the right");
    build_rig(RIG_NODES);
    put_u32(node(6) + NODE_MESH, 8u);
    count = character_rebind_own_weapons((uintptr_t)rig_model, hidden, BODY_HIDE_MAX);
    ut_check(character_rebind_far_nodes((uintptr_t)rig_model, hidden, count, words, &on_hand) &&
             words[REBIND_WORD_LHAND] == 2 && !on_hand,
             "the left hand is under the hidden hilt, so the chest and not the right hand");

    ut_section("a rig with no node a push can leave from is refused, hand or no hand");
    build_rig(RIG_NODES);
    memcpy(node(5), "lhand01\0", 8u);
    put_u32(node(1) + NODE_MESH, 0xFFFFFFFFu);
    put_u32(node(2) + NODE_MESH, 0xFFFFFFFFu);
    put_u32(node(3) + NODE_MESH, 0xFFFFFFFFu);
    put_u32(node(5) + NODE_MESH, 0xFFFFFFFFu);
    put_u32(node(9) + NODE_MESH, 0xFFFFFFFFu);
    on_hand = true;
    ut_check(!character_rebind_far_nodes((uintptr_t)rig_model, hidden, count, words, &on_hand),
             "nothing is written for it");
}

static void check_the_hide(void)
{
    uint8_t  thing[0x40];
    uint32_t table[RIG_NODES + 4u];
    uint32_t slots[] = { 4u, 7u, RIG_NODES + 2u };

    build_rig(RIG_NODES);
    memset(thing, 0, sizeof thing);
    memset(table, 0, sizeof table);
    put_u32(thing + 0x04u, (uint32_t)(uintptr_t)rig_model);
    put_u32(thing + 0x28u, (uint32_t)(uintptr_t)table);

    ut_section("the hidden words go into the handle, measured against the worn model");
    character_rebind_hide((uintptr_t)thing, (uintptr_t)rig_model, slots, 3u);
    ut_check(table[4] == 1u && table[7] == 1u, "both listed slots are hidden");
    ut_check(table[RIG_NODES + 2u] == 0u, "and a slot past the model's own count is not written");

    memset(table, 0, sizeof table);
    put_u32(thing + 0x04u, 0x1234u);
    character_rebind_hide((uintptr_t)thing, (uintptr_t)rig_model, slots, 3u);
    ut_check(table[4] == 0u && table[7] == 0u,
             "a handle that wears another model has nothing written into its table");
}

static void check_the_names(void)
{
    char name[16];

    build_rig(RIG_NODES);
    ut_section("a node's name for the log");
    ut_check(character_rebind_node_name((uintptr_t)rig_model, 5, name, sizeof name) &&
             strcmp(name, "lhand") == 0, "node 5 is the left hand");
    ut_check(!character_rebind_node_name((uintptr_t)rig_model, (int32_t)RIG_NODES, name,
                                         sizeof name) && name[0] == '\0',
             "and a node past the end has no name");
}

/* ============================================================================================ */

static uint8_t   bind_thing[0x40];
static uint32_t  bind_calls;
static uint32_t  puppet_while_freed;
static uintptr_t model_bound;
static int32_t   bind_answer;

static void __cdecl fake_free_arrays(void *thing)
{
    puppet_while_freed = get_u32((const uint8_t *)thing + 0x18u);
    ++bind_calls;
}

static int32_t __cdecl fake_set_model(void *thing, void *model)
{
    (void)thing;
    model_bound = (uintptr_t)model;
    ++bind_calls;
    return bind_answer;
}

static void check_the_bind(void)
{
    character_model_sites_t sites;

    memset(&sites, 0, sizeof sites);
    sites.free_arrays = &fake_free_arrays;
    sites.set_model = &fake_set_model;
    memset(bind_thing, 0, sizeof bind_thing);
    put_u32(bind_thing + 0x18u, 0x00ABCDEFu);
    put_u32(bind_thing + 0x1Cu, 1000u);
    bind_answer = 1;

    ut_section("the rebind: stamp, puppet aside, arrays, bind");
    ut_check(character_rebind_bind(&sites, (uintptr_t)bind_thing, 0x4242u), "the bind holds");
    ut_check(bind_calls == 2u && model_bound == 0x4242u, "the arrays freed, the model bound");
    ut_check(puppet_while_freed == 0u, "the puppet was set aside while the arrays were freed");
    ut_check(get_u32(bind_thing + 0x18u) == 0x00ABCDEFu, "and put back afterwards");
    ut_check(get_u32(bind_thing + 0x1Cu) == (1000u | 0x80000000u),
             "the stamp carries a value the frame counter cannot hold");

    bind_answer = 0;
    ut_check(!character_rebind_bind(&sites, (uintptr_t)bind_thing, 0x4343u),
             "a bind that refuses is reported as one");
    ut_check(!character_rebind_bind(NULL, (uintptr_t)bind_thing, 0x4343u) &&
             !character_rebind_bind(&sites, 0u, 0x4343u) &&
             !character_rebind_bind(&sites, (uintptr_t)bind_thing, 0u),
             "and nothing is bound without sites, a handle or a model");
}

static void check_the_local_words(void)
{
    uint8_t  block[0x60];
    bool     on_hand = true;
    uint32_t i;

    build_rig(RIG_NODES);
    memset(block, 0xEE, sizeof block);
    memcpy(node(7), "holster\0", 8u);   /* no mount on this rig */

    ut_section("the player's own six words");
    ut_check(character_rebind_local_nodes((uintptr_t)block, (uintptr_t)rig_model, false,
                                          &on_hand) == 5,
             "the push takes the left hand of the rig he put on");
    ut_check(!on_hand, "which is not the hand his weapon hangs on");
    ut_checkf((int32_t)get_u32(block + 0x40u) == (int32_t)RIG_NODES,
              "a rig without a mount answers one past its last node (%d)",
              (int)get_u32(block + 0x40u));
    ut_check(get_u32(block + 0x44u) == 2u && get_u32(block + 0x48u) == 5u &&
             get_u32(block + 0x50u) == 1u && get_u32(block + 0x54u) == 3u,
             "the chest, the left hand, the waist and the head by name");
    ut_check(get_u32(block + 0x4Cu) == 0u, "the blade stays 0 for a player without one");
    ut_check(get_u32(block + 0x3Cu) == 0xEEEEEEEEu && get_u32(block + 0x58u) == 0xEEEEEEEEu,
             "and nothing outside the six words is touched");

    ut_section("a player who owns a blade takes the borrowed rig's only when it has a mesh");
    build_rig(RIG_NODES);
    memcpy(node(9), "sabreblad01\0", 12u);   /* the leaf of the waist, mesh 7 */
    memset(block, 0xEE, sizeof block);
    character_rebind_local_nodes((uintptr_t)block, (uintptr_t)rig_model, true, &on_hand);
    ut_checkf(get_u32(block + 0x4Cu) == 9u, "a blade node with a mesh is the blade (%u)",
              (unsigned)get_u32(block + 0x4Cu));
    put_u32(node(9) + NODE_MESH, 0xFFFFFFFFu);
    character_rebind_local_nodes((uintptr_t)block, (uintptr_t)rig_model, true, &on_hand);
    ut_checkf(get_u32(block + 0x4Cu) == 0u,
              "one without a mesh is none, as obi.baf's bare sabreblad01 (%u)",
              (unsigned)get_u32(block + 0x4Cu));
    ut_check(get_u32(block + 0x44u) == 2u && get_u32(block + 0x50u) == 1u,
             "and the other words are resolved as before");

    /* The rig the five rows are in. `destroyr`, `jawa`, `jawagun`, `tatcrit` and `tathum1` carry
     * no node named `lhand`, and the word used to be 0 on all five: node 0 is `dummy01` on all
     * five, it carries no mesh, and the sphere call then leaves the bolt's origin unwritten. */
    ut_section("a rig with no left hand gets a node a push can actually leave from");
    build_rig(RIG_NODES);
    memcpy(node(5), "lhand01\0", 8u);
    memset(block, 0xEE, sizeof block);
    on_hand = true;
    ut_check(character_rebind_local_nodes((uintptr_t)block, (uintptr_t)rig_model, false,
                                          &on_hand) == 2,
             "the chest, because it carries a mesh and 0 on this rig does not");
    ut_check(get_u32(block + 0x48u) == 2u, "and that is what the word holds");
    ut_check(!on_hand, "and it is not the hand the weapon hangs on");

    ut_section("and it is still never the hand the weapon hangs on");
    build_rig(RIG_NODES);
    memcpy(node(5), "lhand01\0", 8u);
    put_u32(node(6) + NODE_MESH, 8u);
    for (i = 1u; i <= 8u; ++i) {
        if (i != 6u) {
            put_u32(node(i) + NODE_MESH, 0xFFFFFFFFu);
        }
    }
    memset(block, 0xEE, sizeof block);
    ut_check(character_rebind_local_nodes((uintptr_t)block, (uintptr_t)rig_model, false,
                                          &on_hand) == 9 && !on_hand,
             "the row of nodes is walked past the right hand, onto the tail behind it");

    ut_section("a rig with nothing a push can leave from writes the 0 it always wrote");
    put_u32(node(6) + NODE_MESH, 0xFFFFFFFFu);
    put_u32(node(9) + NODE_MESH, 0xFFFFFFFFu);
    memset(block, 0xEE, sizeof block);
    ut_check(character_rebind_local_nodes((uintptr_t)block, (uintptr_t)rig_model, false,
                                          &on_hand) < 0,
             "the rig states none, and the caller is told so rather than shown a node");
    ut_check(get_u32(block + 0x48u) == 0u, "the word is 0, which is what the engine would hold");
    ut_check(get_u32(block + 0x44u) == 2u && get_u32(block + 0x50u) == 1u,
             "and the other five words are written as before");

    memset(block, 0xEE, sizeof block);
    ut_check(character_rebind_local_nodes((uintptr_t)block, (uintptr_t)rig_model, false,
                                          NULL) < 0 && get_u32(block + 0x48u) == 0xEEEEEEEEu,
             "and nowhere to say which node it was is nothing written at all");
}

int main(void)
{
    check_the_own_weapons();
    check_the_far_words();
    check_the_push_avoids_the_weapon_hand();
    check_the_hide();
    check_the_names();
    check_the_bind();
    check_the_local_words();

    return ut_summary("the rebind and the node words");
}
