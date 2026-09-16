/* npc_census.h: what a loaded level offers the NPC spawner, counted out of its own directory.
 *
 * Taken out of npc_spawner.c at the seam its size note named: what a level offers is a
 * different question from how one of it is raised. This reads the level's placement directory
 * and its model table, decides which placements may be copied and from which the copy of each
 * kind is taken. The spawner asks
 * it for the kinds, the chosen one and its source, and reads a placement back through it.
 *
 * Internal to dev_overlay.
 */
#ifndef DEV_OVERLAY_NPC_CENSUS_H
#define DEV_OVERLAY_NPC_CENSUS_H

#include "npc_spawner.h"

#include <stdbool.h>
#include <stdint.h>

/* The world record's four fields this reads, from the bytes quoted above. */
#define WORLD_MODEL_COUNT     0x1E8u
#define WORLD_MODELS          0x1F4u
#define WORLD_PLACEMENT_COUNT 0x204u
#define WORLD_PLACEMENTS      0x20Cu

/* The placement record. */
#define PLACE_FLAGS        0x00u
#define PLACE_START_MODE   0x04u   /* the record's starting AI mode */
#define PLACE_MOVE_MODE    0x08u   /* 0 walks; odd modes skip collision; 2 hovers, 4 to 6 fly */
#define PLACE_HIT_POINTS   0x0Cu
#define PLACE_MOVE_SPEED   0x10u
#define PLACE_MASS         0x14u
#define PLACE_TURN_RATE    0x18u
#define PLACE_FOV          0x1Cu
#define PLACE_START_YAW    0x24u
#define PLACE_RANGE        0x28u   /* 0 is never despawned for distance */
#define PLACE_CLASS        0x34u
#define PLACE_FIRE_INTERVAL 0x38u  /* f32 seconds; a shooter rearms after a random part of it */
#define PLACE_MIN_DIFFICULTY 0x3Cu
#define PLACE_DETAIL       0x40u
#define PLACE_NAME         0xB8u
#define PLACE_NAME_MAX     11u     /* strcpy into the actor's twelve byte name */
#define PLACE_HIT_POINTS_LEAST 10  /* what a copy gets when its source has none */
#define PLACE_SHOT_KIND_BLASTER 1  /* the bolt the retail guard scripts fire outright; what an
                                    * empty first weapon slot becomes */
#define PLACE_WEAPON_KINDS 0x8Cu   /* u16[4], the shot kinds a script's attack kinds 8 to 11
                                    * fire (op_shoot's remap, ENMY_WEAPON_KIND) */
#define PLACE_SCRIPT_INDEX 0xA4u
#define PLACE_MODEL_INDEX  0xA8u
#define PLACE_POSITION     0xACu
#define PLACE_SPAWN_STATE  0xC8u
#define PLACE_LIVE_ACTOR   0xD0u
#define PLACE_REVEAL_COUNT 0xD4u
#define PLACE_SIZE         0xD8u
#define PLACE_FLAG_HOSTED  0x2000u   /* parked on the player's body, never given one of its own */
#define PLACE_FLAG_SCANNED 0x0001u   /* the activation scan's own filter */

/* The placement's class, +0x34, is the engine's own sorting of what a placement is, and the
 * copy it into the actor and the body (spawn_actor: shooterClass, objClass). The levels use it
 * as: 0 for props, ships and pods; 1 for the story's principals, the Obi-Wan, Padme, Panaka and
 * Jar Jar stand-ins whose scripts run the cutscenes and the plot, and which the player's own
 * body is too; 2 and 3 for everybody else, the crowds, the guards, the creatures and the
 * droids; 4 for a tripod gun; 13 upward for the power-ups, health, ammunition and the rest. A
 * copy that ran its source's script ran it from the top, so a class 1 copy started its cutscene
 * over again with the real one already past it and hung the level, and a boss walked off to the
 * marks his script names. So a copy runs none of them: spawn_scripts.c gives it a script of
 * this project's own, and classes 1 to 3 are all offered. Giving a copy another placement's
 * script was tried as well and taken out: the borrowed script greets the player in the
 * townsman's voice and follows them about. */
#define CLASS_OFFERED_LOW  1
#define CLASS_OFFERED_HIGH 3

/* The actor file header the model table points at: its name at +8, 0x18 bytes. */
#define ACTOR_FILE_NAME        0x08u
#define ACTOR_FILE_NAME_LENGTH 0x18u

/* The engine's own assert bounds the directory at 256 (0x0043790E); a count past it is not a
 * level this file understands. */
#define PLACEMENTS_MAX 256u

typedef struct npc_census {
    const void        *level;                        /* the world record counted, or NULL */
    uint32_t           count;
    npc_spawner_kind_t kind[NPC_SPAWNER_KINDS_MAX];
    uint32_t           source[NPC_SPAWNER_KINDS_MAX];   /* the placement a kind is copied from */
    uint32_t           plain[NPC_SPAWNER_KINDS_MAX];    /* how plain that placement is */
    int32_t            model[NPC_SPAWNER_KINDS_MAX];    /* its model index, the key */
    int32_t            chosen;
} npc_census_t;

/* One placement's record and the name of its actor file, read without trusting either pointer.
 * False for a record the spawner will not offer: unreadable, hosted, not a class 1 to 3, a model
 * index past the table, no model there, or the script anchor's inviso.baf, which has no body to
 * see. `file` may be NULL. */
bool npc_census_read_placement(const uint8_t *level, uint32_t index, uint8_t *record,
                               char *stem, uint32_t stem_size, char *file, uint32_t file_size);

/* Counts `level` afresh, or empties the census for NULL. */
void npc_census_count(const uint8_t *level);

/* The census as it stands. Never NULL. */
npc_census_t *npc_census(void);

#endif /* DEV_OVERLAY_NPC_CENSUS_H */
