/* limb_nodes.h: which node of a model a limb is, read out of the model's own node table.
 *
 * Taken out of dismemberment.c along a seam of its own when that file came within twenty lines
 * of the hard limit: the walk from a node to the first mesh under it, and the body-part mask test
 * with the engine's own fallback. Both walk FOREIGN data, so depth and child count are capped and
 * every pointer is checked. The node layout below is the one dismemberment.c's evidence proves;
 * it is here because the walk is what reads it.
 */
#ifndef LIMB_NODES_H
#define LIMB_NODES_H

#include <stdbool.h>
#include <stdint.h>

#define NODE_STRIDE        0xB4
#define NODE_TYPE          0x48   /* the body-part mask */
#define NODE_MESH_INDEX    0x4C   /* < 0 = this node carries no mesh */
#define NODE_CHILD_COUNT   0x54
#define NODE_FIRST_CHILD   0x58
#define NODE_NEXT_SIBLING  0x5C

/* Walks a node's subtree for the first node that carries a mesh, or -1. Shared because both the
 * mesh-index translation and the blade-node gate need it. */
int32_t limb_first_mesh_in_subtree(const uint8_t *node, int depth);

/* Gate 5 of the blade-node decision: whether the node's body-part type allows a sever, with the
 * authored mask as the truth where the model carries one and the engine's own `part > 4` rule
 * where it does not. The account of the census behind that order is with the code. */
bool limb_part_mask_allows(uint32_t type, const char *model, const char *nodes,
                           uint32_t node_count, int32_t node_index);

#endif /* LIMB_NODES_H */
