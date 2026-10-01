/* npc_spawn_desc.h: what the one who asks for a spawned copy decides, and nothing else.
 *
 * A copy is raised from this and from the level: the kind, the script it runs, where it stands
 * and where it looks. Everything else in its record, the class, the hit points, the weapon, the
 * reload and whether it flies, the builder derives from these and from the level, the same way on
 * every machine that holds the same level and archive. That is what lets a saved copy come back,
 * and a copy travel, as a description rather than as the record the engine wrote into.
 *
 * Internal to dev_overlay.
 */
#ifndef DEV_OVERLAY_NPC_SPAWN_DESC_H
#define DEV_OVERLAY_NPC_SPAWN_DESC_H

#include "common/npc_spawn_note.h"

#include <stdbool.h>
#include <stdint.h>

/* The longest file name, "no placement" and a copy's key, 256 + k below the actor pool, are the
 * multiplayer's as much as this panel's, so they come from the note both of them compile:
 * NPC_SPAWN_FILE_MAX, NPC_SPAWN_NO_SOURCE, NPC_SPAWN_COPIES_MAX and NPC_SPAWN_KEY_FIRST/END. */

/* The one kind the player can mount, and the one a save must know about. */
#define NPC_SPAWN_TRIPOD_FILE "tripod.baf"

typedef struct npc_spawn_desc {
    uint8_t source;                        /* a level kind's placement; an archive kind's donor */
    uint8_t behaviour;                     /* spawn_behaviour_t */
    bool    archive;                       /* the file is the archive's, not the level's */
    char    file[NPC_SPAWN_FILE_MAX + 1u]; /* the actor file, NUL ended */
    float   position[3];                   /* at the feet; a flyer's height is the builder's */
    float   facing;                        /* degrees, as a placement's start yaw */
} npc_spawn_desc_t;

#endif /* DEV_OVERLAY_NPC_SPAWN_DESC_H */
