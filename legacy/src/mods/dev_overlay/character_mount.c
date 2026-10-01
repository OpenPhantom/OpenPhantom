/* character_mount.c: one answer to "which node is the weapon on this rig", for every asker.
 *
 * The reasoning and the measurement are in character_mount.h. What is here is the rule, the two
 * engine anchors it needs and the one hook that makes a weapon name resolve to the node the
 * player's weapon is visibly hanging on.
 *
 * The number is not decided here. character_prop.c owns it, because that module is the one that
 * decides where the weapon is drawn and the one that fails when the borrowed rig has no right
 * hand. Two modules computing the same node index separately is how they come to disagree, and a
 * contact point that disagrees with the picture is worse than no contact at all.
 *
 * SIZE NOTE: a little over 600 lines, one subject: which node a weapon name answers on a
 * borrowed rig, as a rule, as the two walks over the rig's nodes and as the one hook. The pure
 * half at the top is the seam if it grows.
 */
#include "character_mount.h"

#include "character_prop.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ==============================================================================================
 * THE PURE HALF
 * ============================================================================================ */

static char lower_ascii(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

static bool name_starts_with(const char *name, const char *prefix)
{
    size_t i;

    if (name == NULL) {
        return false;
    }
    for (i = 0; prefix[i] != '\0'; ++i) {
        if (lower_ascii(name[i]) != prefix[i]) {
            return false;
        }
    }
    return true;
}

static bool name_equals(const char *name, const char *other)
{
    size_t i;

    if (name == NULL || other == NULL) {
        return false;
    }
    for (i = 0; name[i] != '\0' || other[i] != '\0'; ++i) {
        if (lower_ascii(name[i]) != lower_ascii(other[i])) {
            return false;
        }
    }
    return true;
}

bool character_mount_name_is_blade(const char *name)
{
    /* `sabreblad01`, `sabreblad02`, `sabreblade1`, `sabreblade2` and the bare `blade` a gungan
     * electropole and a tusken gaderffii carry. The first prefix covers the second. */
    return name_starts_with(name, "blade") || name_starts_with(name, "sabreblad");
}

bool character_mount_name_is_weapon_geometry(const char *name)
{
    return name_starts_with(name, "gun")
        || name_starts_with(name, "sabre")
        || name_starts_with(name, "blade");
}

int32_t character_mount_answer(const char *wanted, int32_t engine_answer, int32_t hand)
{
    if (engine_answer != 0) {
        return MOUNT_NO_NODE;      /* the rig carries the name and the engine found it */
    }
    if (hand <= 0) {
        return MOUNT_NO_NODE;
    }
    if (!character_mount_name_is_weapon_geometry(wanted)) {
        return MOUNT_NO_NODE;
    }
    return hand;
}

/* ==============================================================================================
 * The weapon geometry a rig carries of its own
 *
 * A borrowed rig that authors a weapon wears it beside the player's, and the two come out of
 * different models, so the second one cannot be moved, scaled or put away with the first.
 * character_prop.c hides it in the body's RENDER HANDLE, which is per body and reversible, and
 * this walk is what says which nodes those are.
 *
 * The question is what weapon geometry is, and not which names the weapon configuration happens
 * to use. character_prop.c used to ask the rig for the eleven names of that table plus the mount.
 * Measured over the 132 shipped rigs that carry a hand at all, that left 26 of them wearing a
 * second weapon: twenty carry a node called `gun` on a hand, `tusken` and `tuskgun` add a `blade`,
 * `sithgoon` a `sabre01` in its LEFT hand, and six carry `sabrewaist`, a lightsaber hilt hanging on
 * the WAIST: `obi`, `obiwan`, `quigon`, `quigung`, `quiweap` and `mace`. Not one of those names is
 * a weapon row, so not one of them was ever hidden.
 * ============================================================================================ */

/* The model and the node, as rdThing_buildWorldMatrices reads them. The same numbers character_prop
 * uses, taken from the same disassembly rather than from that file. */
#define MODEL_NUM_NODES     0x54u
#define MODEL_NODES         0x58u
#define NODE_MATRIX_SLOT    0x44u
#define NODE_PARENT         0x50u
#define NODE_FIRST_CHILD    0x58u
#define NODE_NEXT_SIBLING   0x5Cu
/* The two bapobj_nodeSphere reads: it indexes the node array by the answer (0x0041428B) and
 * turns back at 0x0041429C when the node's mesh index is below zero. */
#define NODE_BYTES          0xB4u
#define NODE_MESH_INDEX     0x4Cu

/* The widest shipped skeleton is 48 nodes. Anything claiming more is not a skeleton, and the
 * number is also the depth budget the walks below terminate on. */
#define MOUNT_NODES_MAX      256u

typedef struct weapon_walk {
    uintptr_t   hand;      /* the joint the player's weapon is drawn on              */
    const char *mount;     /* the engine's own name for name id 7, or NULL           */
    uint32_t    nodes;     /* the model's own node count, the bound on every slot    */
    uint32_t   *slots;
    uint32_t    max;
    uint32_t    count;
} weapon_walk_t;

/* Whether `node` is the joint the weapon is drawn on, or one of its ancestors.
 *
 * Nothing on that chain may be hidden. thing+0x28 gates the CONCATENATION and not only the draw:
 * rdThing_buildWorldMatrices tests `[thing+0x28 + child->num*4]` before it descends into a child,
 * and the draw walk at 0x004100A0 makes the same test, so one word on an ancestor leaves the
 * hand's own world matrix unbuilt and the weapon is then placed against whatever that slot held.
 * `tank` is the rig this is written for: its right hand hangs off a node called `gunarms`. */
static bool node_carries(uintptr_t hand, uintptr_t node)
{
    uintptr_t walk = hand;
    uint32_t  budget = MOUNT_NODES_MAX;

    while (walk != 0u && budget != 0u) {
        uint32_t parent = 0;

        if (walk == node) {
            return true;
        }
        if (!memory_try_read(walk + NODE_PARENT, &parent, sizeof parent)) {
            return false;
        }
        walk = (uintptr_t)parent;
        --budget;
    }
    return false;
}

/* The walk STOPS at every node it collects, because one word in thing+0x28 takes that node's whole
 * subtree with it, so a mount covers its entire gun rack in one slot. Measured over the 132 rigs
 * that carry a hand: the longest list is two. */
static void collect_weapons(weapon_walk_t *walk, uintptr_t node, uint32_t budget)
{
    char     name[MOUNT_NAME_BYTES];
    uint32_t child = 0;

    if (budget == 0u || node == 0u || walk->count >= walk->max ||
        !memory_try_read(node, name, sizeof name)) {
        return;
    }
    name[sizeof name - 1u] = '\0';
    if (!node_carries(walk->hand, node) &&
        (character_mount_name_is_weapon_geometry(name) ||
         (walk->mount != NULL && name_equals(name, walk->mount)))) {
        uint32_t slot = 0;

        if (memory_try_read(node + NODE_MATRIX_SLOT, &slot, sizeof slot) && slot < walk->nodes) {
            walk->slots[walk->count++] = slot;
        }
        return;
    }
    if (!memory_try_read(node + NODE_FIRST_CHILD, &child, sizeof child)) {
        return;
    }
    while (child != 0u && budget != 0u) {
        uint32_t next = 0;

        collect_weapons(walk, (uintptr_t)child, budget - 1u);
        if (!memory_try_read((uintptr_t)child + NODE_NEXT_SIBLING, &next, sizeof next)) {
            return;
        }
        child = next;
        --budget;
    }
}

uint32_t character_mount_own_weapon_nodes(uintptr_t model, uintptr_t hand, const char *mount,
                                          uint32_t *out_slots, uint32_t max)
{
    weapon_walk_t walk;
    uint32_t      nodes = 0;
    uint32_t      count = 0;

    if (model == 0u || out_slots == NULL || max == 0u ||
        !memory_try_read(model + MODEL_NUM_NODES, &count, sizeof count) ||
        count == 0u || count > MOUNT_NODES_MAX ||
        !memory_try_read(model + MODEL_NODES, &nodes, sizeof nodes) || nodes == 0u) {
        return 0u;
    }
    walk.hand = hand;
    walk.mount = mount;
    walk.nodes = count;
    walk.slots = out_slots;
    walk.max = max;
    walk.count = 0u;
    collect_weapons(&walk, (uintptr_t)nodes, count);
    return walk.count;
}

/* ==============================================================================================
 * THE FOUND NODE
 *
 * The rig carries the name and the engine found it, and the pair pass still measures nothing where
 * the weapon is drawn. Of the eight shipped rigs that carry `sabreblad01`, `obi` has it as a bare
 * joint (index 12, mesh -1), whose centre the node sphere call never writes; on the other seven it
 * hangs under the rig's own weapon, which character_prop hides, and the concatenation does not
 * descend into a hidden node, so the matrix under it stays joint local.
 * ============================================================================================ */

static bool slot_listed(const uint32_t *slots, uint32_t count, uint32_t slot)
{
    uint32_t i;

    for (i = 0; slots != NULL && i < count; ++i) {
        if (slots[i] == slot) {
            return true;
        }
    }
    return false;
}

static bool node_unseen(uintptr_t model, int32_t found, const uint32_t *hidden,
                        uint32_t hidden_count)
{
    uint32_t  count = 0;
    uint32_t  nodes = 0;
    int32_t   mesh = 0;
    uintptr_t at;
    uint32_t  budget = MOUNT_NODES_MAX;

    if (model == 0u || found <= 0 ||
        !memory_try_read(model + MODEL_NUM_NODES, &count, sizeof count) ||
        count > MOUNT_NODES_MAX || (uint32_t)found >= count ||
        !memory_try_read(model + MODEL_NODES, &nodes, sizeof nodes) || nodes == 0u) {
        return false;
    }
    at = (uintptr_t)nodes + (uintptr_t)found * NODE_BYTES;
    if (!memory_try_read(at + NODE_MESH_INDEX, &mesh, sizeof mesh)) {
        return false;
    }
    if (mesh < 0) {
        return true;
    }
    while (at != 0u && budget != 0u) {
        uint32_t slot = 0;
        uint32_t parent = 0;

        if (!memory_try_read(at + NODE_MATRIX_SLOT, &slot, sizeof slot) ||
            !memory_try_read(at + NODE_PARENT, &parent, sizeof parent)) {
            return false;
        }
        if (slot_listed(hidden, hidden_count, slot)) {
            return true;
        }
        at = (uintptr_t)parent;
        --budget;
    }
    return false;
}

int32_t character_mount_answer_found(uintptr_t model, const char *wanted, int32_t found,
                                     const uint32_t *hidden, uint32_t hidden_count,
                                     int32_t equipped)
{
    if (equipped <= 0 || equipped == found || !character_mount_name_is_weapon_geometry(wanted) ||
        !node_unseen(model, found, hidden, hidden_count)) {
        return MOUNT_NO_NODE;
    }
    return equipped;
}

/* ==============================================================================================
 * THE ANCHORS
 *
 * One pattern carries both. The spawn resolves the mount by name and stores it, and the block is
 * unique in all three shipped builds; the two player record operands in it have to name one
 * address, which is what makes the match a proof rather than a coincidence, and the call operand
 * hands over bapobj_findNodeByNameId without a second search. The player record differs between
 * the builds (0x4B5220 in both WMAIN images, 0x4B51D0 in the recompiled obi.exe), which is exactly
 * why it is read out of the operand.
 * ============================================================================================ */
static const uint8_t SIG_MOUNT_SPAWN[] = {
    0x6A, 0x07,                                /* push 7               the name id `weapon`  */
    0x8B, 0x15, 0x00, 0x00, 0x00, 0x00,        /* mov edx,[the player record]                */
    0x8B, 0x42, 0x0C,                          /* mov eax,[edx+0x0C]   the player's body     */
    0x50,
    0xE8, 0x00, 0x00, 0x00, 0x00,              /* call bapobj_findNodeByNameId               */
    0x83, 0xC4, 0x08,
    0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00,        /* mov ecx,[the player record]                */
    0x89, 0x41, 0x40,                          /* mov [ecx+0x40],eax   the mount cell        */
    0x6A, 0x01                                 /* push 1               the next name, `head` */
};
static const uint8_t MSK_MOUNT_SPAWN[] = {
    0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF
};
_Static_assert(sizeof(SIG_MOUNT_SPAWN) == sizeof(MSK_MOUNT_SPAWN),
               "the spawn mount pattern and its mask are different lengths");

/* Offsets into the pattern above, and the shape at each one is checked before it is believed. The
 * call sits at 12, where the `E8` is; it was written as 13 once, which read the displacement one
 * byte late and derived a target outside the image from three of its four bytes plus the `83` of
 * the stack adjust behind it. That produced an address, not an error, and the only thing standing
 * between it and a branch written into the middle of an instruction was the cross check below. */
#define OFFSET_SPAWN_RECORD_A    4u
#define OFFSET_SPAWN_CALL       12u
#define OFFSET_SPAWN_RECORD_B   22u
#define LENGTH_SPAWN_CALL        5u   /* the E8 and its displacement */
#define OPCODE_CALL_REL32     0xE8u

/* bapobj_findNodeByNameId itself, as a detour target. The tail past the six byte prologue is the
 * range test against the 33 entry name table and the source line of the assert that fires above
 * it, and that tail matches exactly once in all three builds, so the two stage rule has a real
 * candidate to fall back on when somebody else hooked the function first. */
static const uint8_t SIG_FIND_NODE[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x2C,        /* push ebp; mov ebp,esp; sub esp,0x2C */
    0x8B, 0x45, 0x08,                          /* mov eax,[ebp+8]     the object      */
    0x89, 0x45, 0xEC,
    0x83, 0x7D, 0x0C, 0x00,                    /* cmp [ebp+0x0C],0    the name id     */
    0x72, 0x00,
    0x83, 0x7D, 0x0C, 0x21,                    /* cmp [ebp+0x0C],0x21 the table size  */
    0x72, 0x00,
    0x68, 0x03, 0x0A, 0x00, 0x00               /* push 2563, the asserted source line */
};
static const uint8_t MSK_FIND_NODE[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof(SIG_FIND_NODE) == sizeof(MSK_FIND_NODE),
               "the find node pattern and its mask are different lengths");

#define FIND_NODE_PROLOGUE       6u

/* The name table load inside that function, `mov ecx,[eax*4 + table]`. It is searched for within
 * the function's own body rather than anchored on its own, and the address it yields is only
 * accepted when entry 7 of the table spells `weapon`. */
static const uint8_t OPCODE_TABLE_LOAD[] = { 0x8B, 0x0C, 0x85 };
#define FIND_NODE_SCAN_BYTES   0x100u

#define NAME_ID_COUNT          0x21u   /* the table the assert above measures against */
#define NAME_ID_WEAPON            7u

typedef int32_t (__cdecl *find_node_fn_t)(void *body, int32_t name_id);

typedef struct mount_state {
    bool           armed;
    bool           held;
    uintptr_t      player_record;
    uintptr_t      name_table;
    detour_t       find_detour;
    find_node_fn_t original;
} mount_state_t;

static mount_state_t mount;

/* ==============================================================================================
 * THE NAME TABLE
 * ============================================================================================ */

/* The engine's own name for a name id, out of the table the lookup itself indexes. NULL for an id
 * the table does not cover, which is also the range the assert at the top of the lookup enforces
 * and therefore the range in which the game may not be asked anything. */
static const char *name_in_table(uintptr_t table, int32_t name_id, char *buffer)
{
    uint32_t pointer = 0;
    uint32_t i;

    if (table == 0 || name_id < 0 || (uint32_t)name_id >= NAME_ID_COUNT) {
        return NULL;
    }
    if (!memory_try_read(table + (uintptr_t)name_id * 4u, &pointer, sizeof pointer) ||
        pointer == 0) {
        return NULL;
    }
    for (i = 0; i + 1u < MOUNT_NAME_BYTES; ++i) {
        uint8_t c = 0;

        if (!memory_try_read((uintptr_t)pointer + i, &c, sizeof c)) {
            return NULL;
        }
        buffer[i] = (char)c;
        if (c == 0) {
            return buffer;
        }
    }
    buffer[MOUNT_NAME_BYTES - 1u] = '\0';
    return buffer;
}

/* Reads the name table operand out of the resolved lookup's own body and only accepts an address
 * whose entry 7 spells `weapon`. A table found by scanning is a claim; that entry is the check. */
static uintptr_t find_name_table(uintptr_t function)
{
    uint8_t   body[FIND_NODE_SCAN_BYTES];
    uint32_t  candidate = 0;
    char      buffer[MOUNT_NAME_BYTES];
    uintptr_t i;

    if (!memory_is_readable_range(function, sizeof body)) {
        return 0;
    }
    memcpy(body, (const void *)function, sizeof body);

    for (i = 0; i + sizeof OPCODE_TABLE_LOAD + 4u <= sizeof body; ++i) {
        if (memcmp(body + i, OPCODE_TABLE_LOAD, sizeof OPCODE_TABLE_LOAD) != 0) {
            continue;
        }
        memcpy(&candidate, body + i + sizeof OPCODE_TABLE_LOAD, sizeof candidate);
        if (!memory_is_inside_image((uintptr_t)candidate, NAME_ID_COUNT * 4u)) {
            continue;
        }
        if (name_equals(name_in_table((uintptr_t)candidate, NAME_ID_WEAPON, buffer), "weapon")) {
            return (uintptr_t)candidate;
        }
    }
    return 0;
}

/* ==============================================================================================
 * THE HOOK
 *
 * It runs on the swing path, on the fire path and on both weapon switches, so the order of the
 * tests is the order of their cost. A body that is not carrying a borrowed weapon is one pointer
 * compare inside character_prop_weapon_node and covers every call the engine makes about
 * anything else; only what survives it is worth a string.
 * ============================================================================================ */
static int32_t __cdecl hook_find_node_by_name_id(void *body, int32_t name_id)
{
    char    wanted[MOUNT_NAME_BYTES];
    int32_t answer;
    int32_t hand = MOUNT_NO_NODE;
    int32_t node;

    answer = mount.original(body, name_id);

    if (body == NULL || mount.held || !character_prop_weapon_node(body, &hand)) {
        return answer;
    }
    if (name_in_table(mount.name_table, name_id, wanted) == NULL) {
        return answer;
    }
    /* Zero is either the model root or the failure the engine cannot spell, and character_prop
     * tells the two apart: it answers only for the one body it is drawing a weapon on. A found
     * node can still hold no sphere where the weapon is, and the swing starter stores this
     * answer as the contact node the pair pass measures. */
    node = (answer != 0) ? character_prop_found_node(body, wanted, answer)
                         : character_mount_answer(wanted, answer, hand);
    return (node == MOUNT_NO_NODE) ? answer : node;
}

/* ==============================================================================================
 * INSTALLATION
 * ============================================================================================ */

bool character_mount_is_armed(void)
{
    return mount.armed;
}

/* Deliberately independent of `armed`: the hold has to be settable before this module has resolved
 * anything and it has to survive an install that failed, because the bracket around the sabre card
 * registration is placed by another module and must not have to ask whether this one is up. */
bool character_mount_hold(bool on)
{
    const bool was = mount.held;

    mount.held = on;
    return was;
}

uintptr_t character_mount_player_record(void)
{
    return mount.player_record;
}

/* The spawn block, and the two things read out of it: the player record both of its operands name,
 * and the lookup its call reaches. Every refusal gets its own line. A single warning saying the
 * install did not happen once cost a disassembler to read, because the number that was wrong was
 * derived rather than read and looked like an address either way. */
static bool resolve_spawn(uintptr_t *out_record, uintptr_t *out_lookup)
{
    uintptr_t spawn = 0;
    uint32_t  record_a = 0;
    uint32_t  record_b = 0;
    uint8_t   opcode = 0;
    int32_t   displacement = 0;
    uintptr_t target;
    size_t    hits;

    hits = signature_count_matches(SIG_MOUNT_SPAWN, MSK_MOUNT_SPAWN, sizeof SIG_MOUNT_SPAWN,
                                   &spawn, 1);
    if (hits != 1) {
        log_warning("the spawn mount block matched %u times and not once, so the player record and "
                    "the node lookup cannot be read out of one site", (unsigned)hits);
        return false;
    }
    /* The shape at every offset this function reads, checked before the bytes there are believed.
     * A wrong offset does not fail, it answers. */
    if (!memory_try_read(spawn + OFFSET_SPAWN_CALL, &opcode, sizeof opcode) ||
        opcode != OPCODE_CALL_REL32) {
        log_warning("the spawn mount block at %08X carries %02X where its call opcode belongs, so "
                    "the offsets this module reads it at do not describe it",
                    (unsigned)spawn, (unsigned)opcode);
        return false;
    }
    if (!memory_try_read(spawn + OFFSET_SPAWN_RECORD_A, &record_a, sizeof record_a) ||
        !memory_try_read(spawn + OFFSET_SPAWN_RECORD_B, &record_b, sizeof record_b)) {
        log_warning("the player record operands of the spawn block at %08X could not be read",
                    (unsigned)spawn);
        return false;
    }
    if (record_a == 0u || record_a != record_b) {
        log_warning("the spawn block names two different player records, %08X and %08X",
                    record_a, record_b);
        return false;
    }
    if (!memory_try_read(spawn + OFFSET_SPAWN_CALL + 1u, &displacement, sizeof displacement)) {
        log_warning("the call displacement of the spawn block at %08X could not be read",
                    (unsigned)spawn);
        return false;
    }
    target = spawn + OFFSET_SPAWN_CALL + LENGTH_SPAWN_CALL + (uintptr_t)displacement;
    if (!memory_is_inside_image(target, FIND_NODE_PROLOGUE)) {
        log_warning("the spawn block at %08X calls %08X, which is not inside the executable, so "
                    "that displacement is not the one this module meant to read",
                    (unsigned)spawn, (unsigned)target);
        return false;
    }
    *out_record = (uintptr_t)record_a;
    *out_lookup = target;
    return true;
}

/* The lookup as a detour target. When the two stage rule finds nothing, both stages are counted, so
 * the log says whether the pattern describes nothing in this build or describes too much. */
static uintptr_t resolve_lookup(void)
{
    uintptr_t function;
    size_t    whole;
    size_t    tail;

    function = signature_find_detour_target(SIG_FIND_NODE, MSK_FIND_NODE, sizeof SIG_FIND_NODE,
                                            FIND_NODE_PROLOGUE);
    if (function != 0) {
        return function;
    }
    whole = signature_count_matches(SIG_FIND_NODE, MSK_FIND_NODE, sizeof SIG_FIND_NODE, NULL, 0);
    tail = signature_count_matches(SIG_FIND_NODE + FIND_NODE_PROLOGUE,
                                   MSK_FIND_NODE + FIND_NODE_PROLOGUE,
                                   sizeof SIG_FIND_NODE - FIND_NODE_PROLOGUE, NULL, 0);
    log_warning("bapobj_findNodeByNameId did not resolve: %u matches whole and %u past its "
                "prologue, so a borrowed body deals no damage",
                (unsigned)whole, (unsigned)tail);
    return 0;
}

bool character_mount_install(void)
{
    uintptr_t function;
    uintptr_t from_call = 0;
    uintptr_t record = 0;

    if (mount.armed) {
        return true;
    }

    if (!resolve_spawn(&record, &from_call)) {
        return false;
    }
    function = resolve_lookup();
    if (function == 0) {
        return false;
    }
    if (function != from_call) {
        log_warning("the spawn calls %08X but the lookup pattern found %08X",
                    (uint32_t)from_call, (uint32_t)function);
        return false;
    }

    /* Resolved BEFORE the branch is written. The operand this scan wants sits well past the six
     * bytes a detour replaces, so the order is not what makes it work, but keeping it means the
     * scan never has to reason about whose jump it is reading. */
    mount.name_table = find_name_table(function);
    if (mount.name_table == 0) {
        log_warning("the node name table did not resolve, so a name id means nothing and no "
                    "weapon name can be answered");
        return false;
    }

    if (!detour_install(&mount.find_detour, function, (const void *)&hook_find_node_by_name_id,
                        FIND_NODE_PROLOGUE)) {
        log_warning("could not hook the node lookup at %08X", (uint32_t)function);
        mount.name_table = 0;
        return false;
    }
    mount.original = (find_node_fn_t)mount.find_detour.original;
    mount.player_record = record;
    mount.armed = true;
    log_info("a weapon name now resolves to the hand the weapon is drawn on: player record %08X, "
             "node lookup %08X, name table %08X",
             (uint32_t)record, (uint32_t)function, (uint32_t)mount.name_table);
    return true;
}
