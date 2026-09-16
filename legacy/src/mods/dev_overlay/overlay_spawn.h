/* overlay_spawn.h: the panel's OpenPhantom group for raising one of the level's own actors.
 *
 * Five rows and a fold under a heading of their own, the same shape as Level selection beside
 * it: the spawn itself, a note under it counting the spawns alive against the cap, the row that
 * says what is spawned, the row that says what it does, the row that removes everything
 * spawned, and "about spawned NPCs", which opens into the lines
 * that say what to expect of one. The second and third rows each open a list, one row per actor
 * file the loaded level's placements use and one per behaviour, the chosen one lit, closing on a
 * pick. The rows read unavailable with no level loaded, since there is nothing to copy and
 * nowhere to put it.
 */
#ifndef DEV_OVERLAY_OVERLAY_SPAWN_H
#define DEV_OVERLAY_OVERLAY_SPAWN_H

#include "npc_spawner.h"
#include "overlay_model.h"

#include <stdbool.h>
#include <stdint.h>

/* The slots with the lists and the fold shut: the spawn, the count of spawns alive under it,
 * the kind, the behaviour, the remove, the fold's summary. The kind row and the behaviour row
 * each open a list under themselves and push everything below down by its length, one list
 * open at a time; the fold's lines follow the summary the same way. */
#define OVERLAY_SPAWN_RUN_SLOT       0u
#define OVERLAY_SPAWN_ALIVE_SLOT     1u
#define OVERLAY_SPAWN_KIND_SLOT      2u
#define OVERLAY_SPAWN_BEHAVIOUR_SLOT 3u
#define OVERLAY_SPAWN_REMOVE_SLOT    4u
#define OVERLAY_SPAWN_SUMMARY_SLOT   5u
#define OVERLAY_SPAWN_FIXED_ROWS     6u
#define OVERLAY_SPAWN_LINE_COUNT   9u
#define OVERLAY_SPAWN_ENTRY_COUNT  (NPC_SPAWNER_KINDS_MAX + NPC_SPAWNER_FOREIGN_MAX)
#define OVERLAY_SPAWN_ROWS_MAX     (OVERLAY_SPAWN_FIXED_ROWS + OVERLAY_SPAWN_LINE_COUNT + \
                                    OVERLAY_SPAWN_ENTRY_COUNT)

/* How many rows the group draws right now: six, and the fold's lines and whichever list is
 * open. */
uint32_t overlay_spawn_row_count(void);

/* Closes the lists and the fold, so the panel opens the way the groups do: folded. */
void overlay_spawn_reset(void);

/* Fills everything about one row except `group` and `id`, which belong to the caller's
 * numbering. */
void overlay_spawn_row(uint32_t slot, overlay_row_t *out);

/* Spawns, removes, opens and closes a list or the fold, or picks from a list. False for a slot
 * that is nothing, or a refusal. */
bool overlay_spawn_toggle(uint32_t slot);

#endif /* DEV_OVERLAY_OVERLAY_SPAWN_H */
