/* mp_node_map.h: a struck node named in a way the other machine can read.
 *
 * Layer 2, the binding. The problem this solves is one this tree has already written down once, in
 * mp_enemy_wire.h beside the node rotations: a node index is an index into ONE model's own table,
 * and it means that joint only on a machine that built the body from the same file. An enemy's
 * asset comes out of the level data, so both machines agree; a PLAYER's body does not, because a
 * player may be wearing a swapped character or a swapped model and the two rigs carry between 24
 * and 48 nodes in different orders. There the node has to travel as a NAME.
 *
 * ================================== A name for one byte ======================================
 *
 * A name is up to 0x44 bytes, which is far too much to hang on every contact. So it travels as an
 * index into a table both machines have, and the receiver looks the NAME up in its own model. One
 * byte, and it means the same joint on any rig.
 *
 * The engine keeps such a table itself, `g_actorNodeName` with 33 entries, read by
 * `bapobj_findNodeByNameId 0x00414043`. That was the obvious answer and it is NOT ENOUGH: it holds
 * the nodes the engine's own code looks up, which are the weapon mounts, the muzzles and nine
 * joints, and it holds almost none of the joints a blade takes off. The census that settled it and
 * the table that replaced it are in the source file. The engine's 33 are kept first and in their
 * own order, so an id below 33 is also a valid engine name id.
 *
 * What is still not covered, said here rather than discovered later: a model may name a joint
 * that the census did not see, and such a node cannot be expressed as an id. It travels as
 * MP_NODE_ID_NONE, which is exactly what every note carried before this file existed, so the
 * receiver is never worse off than before. The counters say how often it happens.
 *
 * ==================================== Why zero is not none ====================================
 *
 * `bapobj_findNodeByNameId` answers 0 for a name it cannot find, and 0 is also a real matrix slot:
 * the model root. player_record.h says the same of the engine's own two lookups. A miss and the
 * root are the same answer there, and this file never lets a 0 stand for a node it found.
 */
#ifndef MULTIPLAYER_MP_NODE_MAP_H
#define MULTIPLAYER_MP_NODE_MAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The table's length. Two byte values are kept back: 0xFF means none and 0xFE means no name
 * this build knows, so a torn id is refused instead of naming an arbitrary joint. */
#define MP_NODE_NAME_COUNT 254u

/* The longest name the census found is twelve characters. Sixteen is the round number above it and
 * is what this file copies out of a node, so a longer name is truncated rather than read past. */
#define MP_NODE_NAME_MAX 16u

/* No node, or a node this side cannot name. It is 0xFF rather than 0 because 0 is `waist`, a
 * perfectly good node, and because a note that carries no node has always meant "say nothing". */
#define MP_NODE_ID_NONE 0xFFu

/* Says in the log that naming is on, and what the first names are. Always true: the table is part
 * of this build, so there is nothing that can fail to resolve. */
bool mp_node_map_install(void);

/* Pure: which entry of `names` equals `name`, or MP_NODE_ID_NONE. The comparison includes the
 * terminator, as the engine's own strcmp does, so "sabreblade1" is not "sabreblad". A NULL or
 * empty name is no node. Exported so the rule can be tested without a model in memory. */
uint8_t mp_node_map_match(const char *name, const char *const *names, uint32_t count);

/* The name id of the matrix slot `slot` on `body`, or MP_NODE_ID_NONE when the body has no model,
 * no node answers to that slot, or its name is not one the table knows. */
uint8_t mp_node_map_id_of_slot(uint32_t body, uint32_t slot);

/* The matrix slot that `id` names on `body`, or 0 when the id is none, the table has no such
 * entry, or this model has no node by that name. Zero is the caller's "leave it alone". */
uint32_t mp_node_map_slot_of_id(uint32_t body, uint8_t id);

/* Whether this body's rig carries the node at all, without resolving a slot and without
 * counting: a caller asking what a rig CAN do is not a caller that named a struck node,
 * and the two must not land in one number. */
bool mp_node_map_body_has(uint32_t body, uint8_t id);

/* The seventh contact cell, which this module owns: the node a blade struck, published by the pair
 * pass beside the six that post_contact carries. `bind` is given its address once; `take` turns
 * what stands in it into an id for the wire, and answers none for a contact that carries no node
 * at all; `put` writes back what a note named, or clears the cell, so that a replay never runs on
 * what the last real contact on this machine struck. */
void mp_node_map_bind(uintptr_t cell);
uint8_t mp_node_map_take(uint32_t victim_body, bool carries_a_node);
void mp_node_map_put(uint32_t victim_body, uint8_t id);

/* The raw slot standing in the cell right now, for a caller on the machine whose own body was
 * struck. The name is for the wire; on one machine the slot is already exact, and the round trip
 * through a name would lose every joint the table cannot spell. */
uint32_t mp_node_map_cell_slot(void);

/* Replays that cleared the cell, those among them that threw away a node the pair pass had
 * written, replays that ran with the node the sender named, and writes that did not take. */
void mp_node_map_cell_counters(uint32_t *cleared, uint32_t *discarded, uint32_t *written,
                               uint32_t *faults);

/* Sent with a node; sent without one because the contact carried none; sent without one because
 * the name is not in the table; and on the receiving side, resolved against the local body, and
 * dropped because the local model has no node by that name. */
void mp_node_map_counters(uint32_t *named, uint32_t *no_contact_node, uint32_t *unnamed,
                          uint32_t *resolved, uint32_t *dropped);

#endif /* MULTIPLAYER_MP_NODE_MAP_H */
