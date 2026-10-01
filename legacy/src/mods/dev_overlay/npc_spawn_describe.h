/* npc_spawn_describe.h: what a spawn asks for, as a description.
 *
 * Taken out of npc_spawner.c at the seam its size note named: what the one who asks decides shares
 * nothing with the raising. A description is all of it: the kind, the behaviour in force, where
 * the copy stands and which way it faces. The builder raises a copy from it without looking at
 * the player at all, so the same description raises the same copy wherever it is handed over.
 *
 * Internal to dev_overlay.
 */
#ifndef DEV_OVERLAY_NPC_SPAWN_DESCRIBE_H
#define DEV_OVERLAY_NPC_SPAWN_DESCRIBE_H

#include "npc_spawn_desc.h"

#include <stdbool.h>
#include <stdint.h>

/* Describes a copy of the chosen kind standing at `position`, at its feet, turned `facing` degrees:
 * what the placement mode's click asks for, whose place and turn the player chose and the mode's
 * probes passed. No spot is looked for. False, with a log line where it is not obvious, when
 * nothing is chosen or the kind cannot be carried in a description: a file name longer than
 * twelve characters, or a placement index past a byte. */
bool npc_spawn_describe_at(const float *position, float facing, npc_spawn_desc_t *out);

/* Where a live copy is and what state it is in, for the log of a removal: the actor's own position,
 * its body's, its state and flags. A body whose position still reads 0 0 0 was never ticked, the
 * shape of a copy removed with the menu never closed. */
void npc_spawn_describe_where(const uint8_t *actor, char *out, uint32_t out_size);

#endif /* DEV_OVERLAY_NPC_SPAWN_DESCRIBE_H */
