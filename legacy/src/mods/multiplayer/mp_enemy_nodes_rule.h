/* mp_enemy_nodes_rule.h: an enemy's hidden nodes and meshes as the engine saves them, and what a
 * receiver writes to follow them.
 *
 * Layer 1, pure.
 *
 * The drawn thing of a body keeps two arrays of one i32 per node: whether the node is hidden, and
 * whether the mesh of that index is. A script's node visibility, the director's weapon put away and
 * drawn, and a limb taken off write them; on a replica nothing does, because its script is parked.
 * The engine's own save reads each array into two words, bit idx & 31 of word idx / 32 for every
 * entry that is not zero, up to the model's node count, and drops an entry past 64. The record
 * carries exactly that form.
 *
 * A receiver compares the host's bits with its own entries and writes only the ones that differ,
 * 1 for hidden and 0 for shown, which are the values the engine's own writers store. A host bit at
 * or past this side's node count means the host's rig is not this one, and the whole record is
 * refused: the low bits of another rig name other nodes.
 */
#ifndef MULTIPLAYER_MP_ENEMY_NODES_RULE_H
#define MULTIPLAYER_MP_ENEMY_NODES_RULE_H

#include <stdbool.h>
#include <stdint.h>

/* The entries the engine's save carries, and the words it carries them in. The largest shipped
 * rig has 48 nodes. */
#define MP_ENEMY_NODES_MAX   64u
#define MP_ENEMY_NODES_WORDS 2u

typedef struct mp_enemy_nodes_mask {
    uint32_t word[MP_ENEMY_NODES_WORDS];
} mp_enemy_nodes_mask_t;

/* The engine's save of `count` entries. Entries past 64 are not saved, as in the engine. */
mp_enemy_nodes_mask_t mp_enemy_nodes_rule_fold(const int32_t *entries, uint32_t count);

bool     mp_enemy_nodes_rule_bit(const mp_enemy_nodes_mask_t *mask, uint32_t index);
uint32_t mp_enemy_nodes_rule_count(const mp_enemy_nodes_mask_t *mask);

/* The lowest index set, or MP_ENEMY_NODES_MAX for none. */
uint32_t mp_enemy_nodes_rule_first(const mp_enemy_nodes_mask_t *mask);

/* Whether a mask names an index at or past `count`. */
bool mp_enemy_nodes_rule_past(const mp_enemy_nodes_mask_t *mask, uint32_t count);

/* What a receiver writes for one array: `show` the entries to clear, `hide` the ones to set. Only
 * entries below `count` and below 64 are ever named. */
typedef struct mp_enemy_nodes_step {
    mp_enemy_nodes_mask_t show;
    mp_enemy_nodes_mask_t hide;
} mp_enemy_nodes_step_t;

mp_enemy_nodes_step_t mp_enemy_nodes_rule_step(const mp_enemy_nodes_mask_t *want,
                                               const mp_enemy_nodes_mask_t *here, uint32_t count);

#endif /* MULTIPLAYER_MP_ENEMY_NODES_RULE_H */
