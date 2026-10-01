/* character_rebind.c: the rebind of a render handle, and the node words each owner may write. */
#include "character_rebind.h"

#include "character_mount.h"
#include "character_nodemap.h"

#include "common/logging.h"
#include "common/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define RDTHING_MODEL3       0x04u
#define RDTHING_PUPPET       0x18u
#define RDTHING_POSE_STAMP   0x1Cu
#define RDTHING_NODE_HIDDEN  0x28u   /* one word per matrix slot, nonzero hides the subtree */

#define MODEL_NODES          0x58u
#define NODE_BYTES           0xB4u
#define NODE_MATRIX_SLOT     0x44u
#define NODE_MESH            0x4Cu   /* the mesh index, negative for a node without geometry */
#define NODE_PARENT          0x50u
#define NODE_NAME_BYTES      0x40u

/* The widest shipped skeleton is 48 nodes; a parent chain longer than the cursor array is not a
 * skeleton, and the walks below end there. */
#define CHAIN_BUDGET         NODEMAP_MAX_NODES

/* The stamp has to come out unequal, and this is why it is a flip and not a number.
 *
 * Before it rebuilds a pose the engine compares thing+0x1c against its own counter and skips the
 * rebuild when they agree, and the same comparison stands in front of five collision paths as well
 * as the draw. A model swap leaves the joint matrix array freshly allocated and unwritten, so a
 * stamp that still agrees means uninitialised matrices are drawn and collided against. The counter
 * is seeded to 1000 and only counts up, so it is never negative; setting the top bit therefore
 * yields a value it cannot hold, and it does so without this module having to resolve the
 * counter's address at all.
 *
 * Set rather than flipped, and the difference is not cosmetic: two swaps between two frames would
 * flip the bit back onto a value the counter can hold. Setting a bit it cannot hold is idempotent
 * and says the same thing however often it is done. */
#define POSE_STAMP_FLIP      0x80000000u

bool character_rebind_bind(const character_model_sites_t *sites, uintptr_t thing,
                           uintptr_t model)
{
    uint32_t stamp = 0;
    uint32_t puppet = 0;

    if (sites == NULL || sites->set_model == NULL || sites->free_arrays == NULL ||
        thing == 0u || model == 0u) {
        return false;
    }
    if (!memory_try_read(thing + RDTHING_POSE_STAMP, &stamp, sizeof stamp) ||
        !memory_try_read(thing + RDTHING_PUPPET, &puppet, sizeof puppet)) {
        log_warning("the render handle at %08X could not be read", (unsigned)thing);
        return false;
    }
    *(volatile uint32_t *)(thing + RDTHING_POSE_STAMP) = stamp | POSE_STAMP_FLIP;

    /* The release frees the four per node arrays AND the puppet, and the puppet is the one thing
     * that must survive: it was made before any model existed, it holds the tracks that are
     * playing, and nothing here would rebuild it. Its own null test is what lets it be set aside
     * for the length of the call. */
    *(volatile uint32_t *)(thing + RDTHING_PUPPET) = 0u;
    sites->free_arrays((void *)thing);
    *(volatile uint32_t *)(thing + RDTHING_PUPPET) = puppet;

    return sites->set_model((void *)thing, (void *)model) != 0;
}

/* ============================================================================================ */

static bool node_record(uintptr_t model, int32_t index, uintptr_t *out)
{
    uint32_t count = character_nodemap_node_count(model);
    uint32_t nodes = 0;

    if (index < 0 || (uint32_t)index >= count || count > NODEMAP_MAX_NODES ||
        !memory_try_read(model + MODEL_NODES, &nodes, sizeof nodes) || nodes == 0u) {
        return false;
    }
    *out = (uintptr_t)nodes + (uintptr_t)index * NODE_BYTES;
    return true;
}

/* Whether the node carries a mesh: the node sphere call writes a centre for no other node. */
static bool node_has_mesh(uintptr_t model, int32_t index)
{
    uintptr_t record = 0;
    int32_t   mesh = -1;

    return node_record(model, index, &record) &&
           memory_try_read(record + NODE_MESH, &mesh, sizeof mesh) && mesh >= 0;
}

/* The node a weapon hangs on, and this file answers that once rather than once per question. The
 * rig's own weapon chain is collected from it and the push may not leave from it, and two copies
 * of the same two names would be free to drift apart.
 *
 * The names are spelled here rather than read out of the engine's name table, as the table of the
 * six words below spells them; the engine's table spells ids 4, 7 and 27 `rhand`, `weapon` and
 * `rforarm`. A rig that ends at the forearm hangs its weapon there, which is where the weapon half
 * of this feature hangs one too. -1 for a rig with neither. */
static int32_t weapon_hand_of(uintptr_t model)
{
    uintptr_t record = 0;
    int32_t   index = character_nodemap_find(model, "rhand");

    if (!node_record(model, index, &record)) {
        index = character_nodemap_find(model, "rforarm");
        if (!node_record(model, index, &record)) {
            return -1;
        }
    }
    return index;
}

/* ============================================================================================ */

/* The six skeleton node indices the player record caches at spawn, resolved again.
 *
 * The engine resolves them once, by name, against the rig the player spawned in, and never looks
 * at them again. A swap replaces that rig, so every one of them names a different joint afterwards,
 * or none at all.
 *
 * The mount is the one that cannot take the engine's own answer. Its lookup answers 0 for a name a
 * model does not carry, and 0 is the root, so every weapon change would hide the root's children,
 * which is the whole body, and the show that follows it restores nothing. An index past the end is
 * refused by the hide and by the show at their own bounds tests, and that is what is written
 * instead: the player then simply carries nothing visible. The chest, the waist, the head and the
 * blade take 0, because 0 is what the engine itself would hold if the player had spawned in that
 * rig, and the blade's 0 is the blade guard's own test for a rig without a sabre.
 *
 * The left hand is the one the engine reads for a single purpose, and it takes the rule that
 * purpose needs rather than its own name; the table still spells the name the engine resolved it
 * from at spawn. The rule is below, over character_rebind_local_nodes.
 *
 * The blade is the one that may not simply take the borrowed rig's answer. A player whose own rig
 * carries no blade node has a zero in pr+0x4c, and that zero is the engine's own proof that it
 * never enters the resize for him: the resize opens with a fatal assert on that field. Handing him
 * the borrowed rig's blade node would take the proof away and leave nothing in its place, so the
 * slot stays zero for him whatever the borrowed rig carries. A player who does own a blade takes
 * the borrowed rig's answer, which is 0 on a rig without one, and the resize guard is what stands
 * in front of that case.
 *
 * A blade node without a mesh counts as none. obi.baf carries sabreblad01 as a bare joint (index
 * 12, mesh -1; the only one of the eight shipped rigs with that name), and the node sphere call
 * turns back before it writes a centre for such a node, so the block and the parry would arm a
 * sphere whose centre the pair pass takes off its own stack. A 0 switches that sphere off, as it
 * is off on every rig without a blade, and the resize guard declines on it. */
#define PLAYER_WEAPON_MOUNT  0x40u
#define PLAYER_LEFT_HAND     0x48u
#define PLAYER_SABRE_NODE    0x4Cu

/* Declared here and written below, next to the rule it shares with the far words. The alternative
 * was to carry the whole push half of the file up in front of the six words, and the six words are
 * what this half of the file is about. */
static int32_t push_node_of(uintptr_t model, const uint32_t *hidden, uint32_t hidden_count,
                            uint32_t count, bool *out_on_the_hand);

static const struct player_node {
    uint32_t    offset;
    const char *name;
} PLAYER_NODES[REBIND_WORDS] = {
    { PLAYER_WEAPON_MOUNT, "weapon"      },
    { 0x44u,               "chest"       },
    { PLAYER_LEFT_HAND,    "lhand"       },
    { PLAYER_SABRE_NODE,   "sabreblad01" },
    { 0x50u,               "waist"       },
    { 0x54u,               "head"        }
};

/* The left hand word is the push and nothing else, and it is chosen by the rule the far words are
 * chosen by rather than by its name.
 *
 * Its one reader in the image takes the CENTRE of that node's sphere as the point the bolt leaves
 * from, and the sphere call writes nothing at all for a node without a mesh. Of the rows the panel
 * offers, five carry no node named `lhand`: `destroyr`, `jawa`, `jawagun`, `tatcrit` and
 * `tathum1`. On all five the word was 0, node 0 on all five is `dummy01` and carries no mesh, and
 * the bolt then left from twelve bytes of stack nobody had written. `0 is what the engine itself
 * would hold` says why 0 is allowed there; it does not say it is usable, and it is not.
 *
 * The hidden set is empty here. The player's own weapon half hides its nodes on the render handle
 * per frame and this file is not told which, and a node hidden after the fact is a push from a
 * matrix the concatenation stopped short of: that is a far body's case, and the far words are
 * chosen against the set the far path just collected. */
int32_t character_rebind_local_nodes(uintptr_t block, uintptr_t model, bool allow_blade,
                                     bool *out_push_on_the_hand)
{
    uint32_t count = character_nodemap_node_count(model);
    int32_t  push = -1;
    uint32_t i;

    if (out_push_on_the_hand == NULL) {
        return -1;
    }
    *out_push_on_the_hand = false;
    if (count == 0u || block == 0u) {
        return -1;
    }
    push = push_node_of(model, NULL, 0u, count, out_push_on_the_hand);
    for (i = 0; i < REBIND_WORDS; ++i) {
        int32_t found;
        int32_t value;

        if (PLAYER_NODES[i].offset == PLAYER_SABRE_NODE && !allow_blade) {
            *(volatile int32_t *)(block + PLAYER_SABRE_NODE) = 0;
            continue;
        }
        if (PLAYER_NODES[i].offset == PLAYER_LEFT_HAND) {
            *(volatile int32_t *)(block + PLAYER_LEFT_HAND) = (push < 0) ? 0 : push;
            continue;
        }
        found = character_nodemap_find(model, PLAYER_NODES[i].name);
        if (PLAYER_NODES[i].offset == PLAYER_SABRE_NODE && found >= 0 &&
            !node_has_mesh(model, found)) {
            found = -1;
        }
        value = found;
        if (found < 0) {
            value = (PLAYER_NODES[i].offset == PLAYER_WEAPON_MOUNT) ? (int32_t)count : 0;
        }
        *(volatile int32_t *)(block + PLAYER_NODES[i].offset) = value;
    }
    return push;
}

/* ============================================================================================ */

bool character_rebind_node_name(uintptr_t model, int32_t node, char *out, uint32_t size)
{
    uintptr_t record = 0;

    if (out == NULL || size == 0u) {
        return false;
    }
    out[0] = '\0';
    if (!node_record(model, node, &record)) {
        return false;
    }
    if (size > NODE_NAME_BYTES) {
        size = NODE_NAME_BYTES;
    }
    if (!memory_try_read(record, out, size)) {
        out[0] = '\0';
        return false;
    }
    out[size - 1u] = '\0';
    return true;
}

uint32_t character_rebind_own_weapons(uintptr_t model, uint32_t *slots, uint32_t max)
{
    uintptr_t hand = 0;

    if (!node_record(model, weapon_hand_of(model), &hand)) {
        hand = 0u;
    }
    return character_mount_own_weapon_nodes(model, hand, "weapon", slots, max);
}

static bool slot_is_listed(uint32_t slot, const uint32_t *hidden, uint32_t hidden_count)
{
    uint32_t i;

    for (i = 0; i < hidden_count; ++i) {
        if (hidden[i] == slot) {
            return true;
        }
    }
    return false;
}

/* Whether the node's world matrix is built at all. The concatenation stops at a hidden node and
 * does not descend, so a node under a hidden ancestor keeps its joint local matrix and a push from
 * it would leave from somewhere near the root. */
static bool node_is_hidden(uintptr_t node, const uint32_t *hidden, uint32_t hidden_count)
{
    uint32_t budget = CHAIN_BUDGET;

    while (node != 0u && budget != 0u) {
        uint32_t slot = 0;
        uint32_t parent = 0;

        if (!memory_try_read(node + NODE_MATRIX_SLOT, &slot, sizeof slot) ||
            !memory_try_read(node + NODE_PARENT, &parent, sizeof parent)) {
            return true;
        }
        if (slot_is_listed(slot, hidden, hidden_count)) {
            return true;
        }
        node = (uintptr_t)parent;
        --budget;
    }
    return node != 0u;
}

/* A node a push can leave from: it carries a mesh, whose sphere is where the push starts, and its
 * world matrix is built. */
static bool push_can_leave(uintptr_t model, int32_t index, const uint32_t *hidden,
                           uint32_t hidden_count)
{
    uintptr_t record = 0;
    int32_t   mesh = -1;

    return node_record(model, index, &record) &&
           memory_try_read(record + NODE_MESH, &mesh, sizeof mesh) && mesh >= 0 &&
           !node_is_hidden(record, hidden, hidden_count);
}

/* The weapon hand is not in this list, and that is the whole of the rule below.
 *
 * The push leaves from the centre of one node's sphere, and while a weapon is drawn on the hand
 * this feature answers that sphere for the hand: the sphere of the WEAPON, several mesh lengths
 * out along a lit blade. The push would then start at the blade instead of at the fist. `rforarm`
 * is not in the list either and is a weapon hand on the rigs that end at it, so the rule is stated
 * as the hand and not as a place in this list. */
static const char *const PUSH_NAMES[] = { "lhand", "chest", "waist" };
#define PUSH_NAME_COUNT ((uint32_t)(sizeof PUSH_NAMES / sizeof PUSH_NAMES[0]))

/* Where a push may leave from on `model`, its own weapon slots being `hidden`: the first of the
 * names above that carries a mesh and is not hidden, else the first node of the rig that is so and
 * is not the weapon hand, else the weapon hand itself, and then `out_on_the_hand` is true. -1 when
 * no node of the rig will do, and then nothing may be written: the push would leave from a matrix
 * nobody built. */
static int32_t push_node_of(uintptr_t model, const uint32_t *hidden, uint32_t hidden_count,
                            uint32_t count, bool *out_on_the_hand)
{
    int32_t hand = weapon_hand_of(model);
    int32_t found;
    uint32_t i;

    *out_on_the_hand = false;
    for (i = 0; i < PUSH_NAME_COUNT; ++i) {
        found = character_nodemap_find(model, PUSH_NAMES[i]);
        if (found >= 0 && push_can_leave(model, found, hidden, hidden_count)) {
            return found;
        }
    }
    for (i = 0; i < count; ++i) {
        if ((int32_t)i != hand && push_can_leave(model, (int32_t)i, hidden, hidden_count)) {
            return (int32_t)i;
        }
    }
    /* The last way out, and it is a worse push rather than no model at all. No shipped rig needs
     * it: refusing the dressing here would leave the far player with no body to look at, and a
     * push that starts at his weapon is the smaller of the two. It says so in the log when it is
     * ever taken. */
    if (hand >= 0 && push_can_leave(model, hand, hidden, hidden_count)) {
        *out_on_the_hand = true;
        return hand;
    }
    return -1;
}

bool character_rebind_far_nodes(uintptr_t model, const uint32_t *hidden, uint32_t hidden_count,
                                int32_t words[REBIND_WORDS], bool *out_push_on_the_hand)
{
    uint32_t count = character_nodemap_node_count(model);
    int32_t  push;
    int32_t  found;

    if (out_push_on_the_hand == NULL || words == NULL || count == 0u ||
        count > NODEMAP_MAX_NODES || (hidden == NULL && hidden_count != 0u)) {
        return false;   /* nothing is written, and the flag means nothing without words */
    }
    push = push_node_of(model, hidden, hidden_count, count, out_push_on_the_hand);
    if (push < 0) {
        return false;
    }

    words[REBIND_WORD_MOUNT] = (int32_t)count;
    found = character_nodemap_find(model, "chest");
    words[REBIND_WORD_CHEST] = (found < 0) ? 0 : found;
    words[REBIND_WORD_LHAND] = push;
    words[REBIND_WORD_SABRE] = 0;
    found = character_nodemap_find(model, "waist");
    words[REBIND_WORD_WAIST] = (found < 0) ? 0 : found;
    found = character_nodemap_find(model, "head");
    words[REBIND_WORD_HEAD] = (found < 0) ? 0 : found;
    return true;
}

/* The same guard the player's own weapon half writes its words under: the slots and the table
 * must come out of one model, or a slot of one rig is written past the end of another rig's table,
 * which is an engine allocation. So the handle has to wear `model` now, and every slot is measured
 * against that model's own node count. */
void character_rebind_hide(uintptr_t thing, uintptr_t model, const uint32_t *slots,
                           uint32_t count)
{
    const uint32_t one = 1u;
    uint32_t       worn = 0;
    uint32_t       table = 0;
    uint32_t       nodes = character_nodemap_node_count(model);
    uint32_t       i;

    if (thing == 0u || model == 0u || slots == NULL || count == 0u || nodes == 0u ||
        !memory_try_read(thing + RDTHING_MODEL3, &worn, sizeof worn) ||
        (uintptr_t)worn != model ||
        !memory_try_read(thing + RDTHING_NODE_HIDDEN, &table, sizeof table) || table == 0u) {
        return;
    }
    for (i = 0; i < count; ++i) {
        if (slots[i] < nodes) {
            (void)memory_try_write((uintptr_t)table + 4u * (uintptr_t)slots[i], &one, sizeof one);
        }
    }
}
