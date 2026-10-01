/* mp_enemy_limb_rule.h: a limb an enemy lost, as the number its world event carries, and what a
 * client does with it.
 *
 * Layer 1, pure.
 *
 * The engine's cut hides the node on the body and throws the piece: a second object that wears the
 * same model with everything but the limb hidden and flies for five seconds on a task of its own.
 * The piece takes its hidden nodes from the body at the moment of the cut, before the node itself
 * is hidden. A client's replica is brought to the host's hidden nodes by the record, which comes
 * before the event, so by the time a client cuts, the host's mask may already have hidden the
 * node; a piece cut then would take the hidden node with it and fly invisible. So the event carries
 * what the node was on the host just before the cut, and the client puts it back to that first.
 *
 * The flight costs one of the engine's 64 task slots, whose registration no shipped caller checks:
 * a full table answers -1 and the piece stays frozen where it was cut. A client keeps a reserve
 * free and only hides the node when the table is nearly full.
 */
#ifndef MULTIPLAYER_MP_ENEMY_LIMB_RULE_H
#define MULTIPLAYER_MP_ENEMY_LIMB_RULE_H

#include <stdbool.h>
#include <stdint.h>

/* The largest rig ordinal the event can carry. It travels offset by one in the low byte, because
 * a low byte of 0 means no limb and ordinal zero is a real node. */
#define MP_ENEMY_LIMB_MAX_ORDINAL 254u

/* How many of the engine's task slots a piece leaves free. The table has 64; this side's own tick
 * holds one, and every engine caller that registers a task and then writes through the answer
 * would write through -1 if the table ran full. */
#define MP_ENEMY_LIMB_TASKS_KEPT_FREE 8u

/* The ordinal and the node's state on the host before the cut, into the event's `a` and back. 0
 * reads back as "nothing lost". */
uint32_t mp_enemy_limb_pack(uint32_t ordinal, bool hidden_before);
bool     mp_enemy_limb_unpack(uint32_t packed, uint32_t *ordinal, bool *hidden_before);

typedef enum mp_enemy_limb_verdict {
    MP_ENEMY_LIMB_THROW = 0,       /* the engine's own cut: the node hidden, the piece thrown */
    MP_ENEMY_LIMB_HIDE_ONLY,       /* too few task slots free for the flight: the node hidden */
    MP_ENEMY_LIMB_OUT_OF_RANGE     /* the root, which the engine never cuts, or past the model */
} mp_enemy_limb_verdict_t;

/* What a client does with `ordinal` on a model of `node_count` nodes while `free_tasks` of the
 * engine's task slots are free. */
mp_enemy_limb_verdict_t mp_enemy_limb_verdict(uint32_t ordinal, uint32_t node_count,
                                              uint32_t free_tasks);

#endif /* MULTIPLAYER_MP_ENEMY_LIMB_RULE_H */
