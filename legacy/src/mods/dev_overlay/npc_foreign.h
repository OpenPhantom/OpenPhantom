/* npc_foreign.h: the archive's creatures a level did not load, offered to the NPC spawner.
 *
 * Taken out of npc_spawner.c at the seam its size note named: which of the archive's files are
 * offered beyond the level's own, and the record written for a copy of one, share nothing with
 * the raising. The list is counted with the level from the catalogue (actor_catalog.c), every
 * file with a body the level's placements do not use, plus the tripod gun, which has none and
 * can be mounted, and nothing is loaded until one is first raised, through the engine's own
 * loader (actor_loader.c). Without the loader the list is empty and the spawner offers the
 * level's alone.
 *
 * Internal to dev_overlay.
 */
#ifndef DEV_OVERLAY_NPC_FOREIGN_H
#define DEV_OVERLAY_NPC_FOREIGN_H

#include "npc_spawner.h"

#include <stdbool.h>
#include <stdint.h>

/* The level's model table entry lent to a loaded file for the length of one spawn call, since
 * the spawn routine binds the model by the record's index; a foreign record names this slot. */
#define NPC_FOREIGN_MODEL_SLOT 0

/* Counts the archive's creatures the loaded level did not load, afresh. */
void npc_foreign_count(void);

/* How many are offered, and the kind at `index` among them, with `foreign` set. */
uint32_t npc_foreign_kind_count(void);
bool     npc_foreign_kind(uint32_t index, npc_spawner_kind_t *out);

/* Writes the record a copy of the archive file `file` is raised from: the plainest of the
 * level's own placements copied for its everyday numbers, mass, turn rate, field of view, hit
 * points, and where the level offers none, the values the retail placements carry most often.
 * The model index names the borrowed table slot, the script index the level's first script
 * (the copy's own script replaces it before the first tick), the range is 0 so the copy is
 * never despawned for distance, the fire interval just over the engine's own floor, the move
 * mode the one the retail levels fly the model with, the first weapon the one they arm it with
 * or the blaster, and the name is
 * `stem`, cut to the eleven characters the engine's strcpy into the actor leaves room for. */
void npc_foreign_write_record(const uint8_t *level, uint8_t *record, const char *stem,
                              const char *file);

#endif /* DEV_OVERLAY_NPC_FOREIGN_H */
