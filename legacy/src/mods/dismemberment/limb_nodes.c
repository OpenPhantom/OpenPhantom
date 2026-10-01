/* limb_nodes.c: see limb_nodes.h. */
#include "limb_nodes.h"

#include "common/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LIMB_MASK 0x6Eu           /* arms, head, legs, never 0x1 (generic), never 0x10 (hips) */
#define ENGINE_TORSO_NODE_LIMIT 4 /* the engine's own `part > 4` rule at 0x43389D */

#define MAX_SUBTREE_DEPTH    12
#define MAX_PLAUSIBLE_KIDS  128u

/* Does this model carry a body-part mask at all? Cached per model pointer, otherwise the scan
 * walks every node on every hit. */
static const char *mask_model;
static bool        mask_answer;

static bool node_pointer_is_plausible(const void *pointer)
{
    return pointer != NULL && ((uintptr_t)pointer & 3u) == 0 && (uintptr_t)pointer > 0x10000u;
}

/* 30 shipped nodes carry NO mesh (tusken/tathum1/handmaid rthigh+lthigh, nimoid rlegdum, tank
 * waist, ...). For those there is no `keep` that hideMeshesBelow could match, and with no mesh
 * there is nothing to show either. Rather than passing the raw node index through (the old, wrong
 * Behaviour), we look for the first mesh in the subtree of that node: the topmost visible piece
 * of exactly this limb. If there is none, the node is unusable and is rejected. */
int32_t limb_first_mesh_in_subtree(const uint8_t *node, int depth)
{
    uint32_t       child_count;
    uint32_t       index;
    const uint8_t *child;
    int32_t        mesh;

    if (!node_pointer_is_plausible(node) || depth > MAX_SUBTREE_DEPTH) {
        return -1;
    }
    if (!memory_is_readable_range((uintptr_t)node, NODE_STRIDE)) {
        return -1;
    }

    mesh = *(const int32_t *)(node + NODE_MESH_INDEX);
    if (mesh >= 0) {
        return mesh;
    }

    child_count = *(const uint32_t *)(node + NODE_CHILD_COUNT);
    if (child_count > MAX_PLAUSIBLE_KIDS) {
        return -1;
    }

    child = *(const uint8_t * const *)(node + NODE_FIRST_CHILD);
    for (index = 0; index < child_count && node_pointer_is_plausible(child); ++index) {
        mesh = limb_first_mesh_in_subtree(child, depth + 1);
        if (mesh >= 0) {
            return mesh;
        }
        child = *(const uint8_t * const *)(child + NODE_NEXT_SIBLING);
    }

    return -1;
}

static bool model_has_part_mask(const char *model, const char *nodes, uint32_t node_count)
{
    uint32_t index;

    if (mask_model == model) {
        return mask_answer;
    }

    mask_answer = false;
    for (index = 0; index < node_count; ++index) {
        if ((*(const uint32_t *)(nodes + index * NODE_STRIDE + NODE_TYPE) & ~1u) != 0) {
            mask_answer = true;
            break;
        }
    }
    mask_model = model;
    return mask_answer;
}

/* Gate 5. the authored mask, with a fallback.
 *
 * The census over 265 actor .baf files says: only ten rigs carry a body-part mask at all (anakin,
 * baron, baronsec, obiwan, quigon, queen, panaka, pitdroid, sithmrc2, jawagun); on every other
 * model EVERY node reads 0x1 = generic. A pure `type & 0x6E` gate therefore refuses on those
 * outright, so decapitation did not happen on all enemies.
 *
 * Hence two stages, and the ORDER is the statement:
 *   1. if the model carries a real mask ANYWHERE, that mask is the truth, a node without a limb
 *      bit is not severed, however large its index;
 *   2. if it carries none, the decision falls back to the engine's own rule `part > 4`. That is
 *      tuned to baronsec-shaped skeletons and protects the torso column there. On a foreign rig
 *      it is a heuristic, but it is THE ENGINE'S heuristic, not ours. */
bool limb_part_mask_allows(uint32_t type, const char *model, const char *nodes,
                           uint32_t node_count, int32_t node_index)
{
    if ((type & LIMB_MASK) != 0) {
        return true;
    }
    if (model_has_part_mask(model, nodes, node_count)) {
        return false;                              /* the model knows better */
    }
    return node_index > ENGINE_TORSO_NODE_LIMIT;   /* the engine's own bound */
}
