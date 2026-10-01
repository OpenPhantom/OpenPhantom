/* npc_spawn_save.h: what a savegame must not do with a spawned copy, and the hulls that see to it.
 *
 * A copy carries the engine index 256 + k, past every level's placements, and the enemy block of a
 * savegame writes each actor's index; loading such a block reads the level's directory at that
 * index without a bound and brings the game down. So for the length of enemy_saveBlock every copy
 * is taken out of the pool's chain (npc_spawn_list.c) and put back after, and the copies are
 * written to the panel's own block instead (npc_spawn_node.c). With this hull missing no copy is
 * raised at all: there is no second index rule to fall back on.
 *
 * A player mounted on a spawned tripod gun is in a mode a savegame would carry and a load could not
 * give back, since the gun is not a placement. Saving is refused then, at the head of
 * save_saveGame, before the slot's file is opened, with the engine's own message box.
 *
 * Both hulls run again, nested, whenever a message box opens while a save is being committed: the
 * box pumps frames, and every frame commits the save again until the commit flag is cleared after
 * it. Neither keeps state a nested call could overwrite, and the refusal answers a nested call at
 * once.
 *
 * Internal to dev_overlay.
 */
#ifndef DEV_OVERLAY_NPC_SPAWN_SAVE_H
#define DEV_OVERLAY_NPC_SPAWN_SAVE_H

#include <stdbool.h>
#include <stdint.h>

/* The engine's actor pool: 0x80 elements of 0x204 bytes. */
#define NPC_SPAWN_POOL_CAPACITY 0x80u

/* Places both hulls where their sites resolved. Answers whether the enemy block's did. */
bool npc_spawn_save_install(void);

/* The enemy block's hull stands: a copy may be raised under 256 + k. */
bool npc_spawn_save_keeps_copies_out(void);

/* The refusal's hull stands: a tripod gun may be raised. */
bool npc_spawn_save_can_refuse(void);

/* A save is being written, or a message box inside one is open: the panel neither raises nor
 * removes anything then, since the enemy tick is not running and the pool may be short of its
 * copies. */
bool npc_spawn_save_window_open(void);

/* The enemy pool's list, or 0 when it is not the list of that shape it was when the sites
 * resolved. */
uintptr_t npc_spawn_save_pool_list(void);

/* Whether the local player sits on the mounted gun `key` names, or, for 0, on any copy. */
bool npc_spawn_save_rides(uint32_t key);

#endif /* DEV_OVERLAY_NPC_SPAWN_SAVE_H */
