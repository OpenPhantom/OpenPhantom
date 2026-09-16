/* actor_loader.h: an actor file the level did not load, brought in through the engine's own loader.
 *
 * The engine loads a level's actor files by name from big.lab at level start, and the hero code
 * loads its own the same way whenever a hero spawns: res_Alloc with the tag 'BAFS' and the file
 * name, and res_Free when the body goes. This leans on the same pair for the NPC spawner, so a
 * file the level never listed can be raised in it: each name is loaded once and kept until the
 * level changes, then given back through res_Free, the way player_despawn gives the hero's
 * back.
 *
 * Internal to dev_overlay.
 */
#ifndef DEV_OVERLAY_ACTOR_LOADER_H
#define DEV_OVERLAY_ACTOR_LOADER_H

#include <stdbool.h>
#include <stdint.h>

/* Resolves the loader and the release from the hero code's own calls of them, which have to
 * agree with each other's shape. False, with a log line, when they did not; foreign files then
 * stay off the spawner's list. */
bool actor_loader_install(void);
bool actor_loader_is_available(void);

/* The loaded actor file for `name`, "tusken.baf": loaded on the first ask and answered from the
 * table after that. NULL when the loader is not resolved, the table is full, or the engine
 * could not find the file. */
void *actor_loader_get(const char *name);

/* Gives every loaded file back. For the level change, after the engine has deleted every body
 * that was bound to one of them. */
void actor_loader_release_all(void);

#endif /* DEV_OVERLAY_ACTOR_LOADER_H */
