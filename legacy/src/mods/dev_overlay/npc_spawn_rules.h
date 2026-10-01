/* npc_spawn_rules.h: the decisions of the copies' savegame, apart from the engine that asks them.
 *
 * Three rules the hulls and the module node apply, each a pure function of values the caller has
 * read, so each is tested on its own: whether the player sits on a spawned gun, whether a block
 * handed over by the loader is read or stepped over, and when a block read from a savegame may
 * raise its copies. Internal to dev_overlay.
 */
#ifndef DEV_OVERLAY_NPC_SPAWN_RULES_H
#define DEV_OVERLAY_NPC_SPAWN_RULES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The player's mode is `mode` and the last gun it mounted carries index `gun`; `mounted` is the
 * mode of a mounted gun. Whether it sits on the copy `key`, or for key 0 on any copy. The mode
 * decides: the gun's index stays in the player after it dismounts. */
bool npc_spawn_rides(uint32_t mode, uint32_t mounted, uint32_t gun, uint32_t key);

/* What the module node does with a block the loader hands it: read it into a buffer of `room`
 * bytes, or step over it, which it must also do for a version it does not know and for a length
 * that would not fit, since the loader goes on from wherever the node stops. */
typedef enum npc_spawn_block_plan {
    NPC_SPAWN_BLOCK_READ = 0,
    NPC_SPAWN_BLOCK_STEP_OVER
} npc_spawn_block_plan_t;

npc_spawn_block_plan_t npc_spawn_block_plan(uint32_t subversion, int32_t length, size_t room);

/* A block read from a savegame is held until the load that read it has finished, which message
 * 0x18 says. Every world the engine builds and every level end begins a new epoch and drops what is
 * held, since a load that brings a new world before its 0x18 did not finish; that one rule is why
 * nothing held can outlive the world it was read in. */
typedef struct npc_spawn_hold {
    uint32_t epoch;        /* the world this machine is in */
    uint32_t held;         /* copies held, 0 for none */
    bool     finished;     /* the load that read them has finished */
} npc_spawn_hold_t;

/* A world was built or a level ended. Answers how many held copies that dropped. */
uint32_t npc_spawn_hold_new_world(npc_spawn_hold_t *hold);

/* A block of `count` copies was read. */
void npc_spawn_hold_read(npc_spawn_hold_t *hold, uint32_t count);

/* Message 0x18: the load is over. */
void npc_spawn_hold_load_over(npc_spawn_hold_t *hold);

/* Whether the held copies may be raised now. */
bool npc_spawn_hold_due(const npc_spawn_hold_t *hold);

/* They were raised, or given up: nothing is held any more. */
void npc_spawn_hold_clear(npc_spawn_hold_t *hold);

#endif /* DEV_OVERLAY_NPC_SPAWN_RULES_H */
