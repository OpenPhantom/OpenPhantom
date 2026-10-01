/* character_prop_rig.h: what a rig says, asked of a model and of a render handle.
 *
 * The borrowed weapon reads two kinds of thing before it decides anything. One is a model: its
 * node array, the node that carries a given name, and where that node's joint sits. The other is
 * a render handle: the world matrix the engine's own concatenation left in one of its joint
 * slots. Neither is a decision, neither holds state, and every one of them takes the model or the
 * handle as an argument, so they are here and the file that decides where a weapon is drawn and
 * what the engine is answered with is next door.
 *
 * The name table is an argument rather than a field. It is resolved once, beside the other seven
 * entry points the feature stands on, and that resolution belongs to the file that owns it. A
 * second copy of the address here would be a second thing to keep right.
 */
#ifndef CHARACTER_PROP_RIG_H
#define CHARACTER_PROP_RIG_H

#include <stdbool.h>
#include <stdint.h>

/* A node name, as the engine's own table spells it. */
#define PROP_NAME_BYTES   0x40u

/* The engine's own name table has 33 entries and its own lookup asserts past the last of them,
 * which is therefore the range in which the game may not be asked anything. */
#define PROP_NAME_COUNT   33u

/* A matrix is twelve floats: three basis rows, then the translation. */
#define PROP_MATRIX_FLOATS 12u

/* The node array of a model, bounded. False for anything that is not a skeleton. */
bool character_prop_rig_nodes(uintptr_t model, uintptr_t *out_nodes, uint32_t *out_count);

/* The engine's own string for a name id, out of the table the lookup itself indexes. NULL for an
 * id the table does not cover. `buffer` holds PROP_NAME_BYTES bytes and is always terminated. */
const char *character_prop_rig_name(uintptr_t name_table, uint32_t name_id, char *buffer);

/* The engine's own answer, computed here rather than called for: walk the node array comparing
 * each node's own name against the string the id names, and hand back its matrix slot. The engine
 * answers 0 both for the root and for a name a model does not carry; this reports the two apart.
 *
 * `out_node` is the record itself and may be NULL. The callers that want a hand or a mesh ask for
 * it, because the pivot that says where that hand's joint is, and the mesh index, live in the
 * record and nowhere else. */
bool character_prop_rig_node(uintptr_t model, uintptr_t name_table, uint32_t name_id,
                             uint32_t *out_slot, uintptr_t *out_node);

/* Where a rig's joint is, which is the one thing the placement asks a hand for.
 *
 * `node+0x60` is the field `rdThing_buildWorldMatrices` reads at 0x004852FF, negates, and adds into
 * every CHILD's local transform. It is therefore not a property of the child at all: exchanging
 * the reference hand's pivot for the borrowed hand's is exactly what re-parents a weapon subtree
 * from one rig to the other, and it is the whole of the correction.
 *
 * Nothing else about the hand is read. An earlier form anchored on the hand MESH's bounding
 * sphere centre and scaled the weapon by the ratio of the two hand radii; both are gone, because
 * the engine applies neither. */
bool character_prop_rig_pivot(uintptr_t node, float out_pivot[3]);

/* One 0x30 byte row out of a handle's joint matrix array.
 *
 * The bound is the live node count and not a remembered one. rdThing_SetModel sizes that array by
 * the node count of the model it binds, so its length is a property of what the handle is wearing
 * RIGHT NOW, and every slot a caller holds was resolved against a model it read earlier. Three
 * reads to ask the question, on a path that runs a handful of times per drawn frame. */
bool character_prop_rig_joint(uintptr_t thing, uint32_t slot, float out[PROP_MATRIX_FLOATS]);

#endif /* CHARACTER_PROP_RIG_H */
