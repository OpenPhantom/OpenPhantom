/* npc_spawner.h: raise another of the level's own actors, in front of the player.
 *
 * A level's actors are authored as placements, one record each, in a directory the world record
 * carries; the engine walks that directory a few times a second and, for every placement the
 * player has come near, calls the one routine that turns a record into a live actor. The level's
 * scripts call the same routine for their own spawns. This asks it for one more: it copies a
 * placement the level already has, moves the copy to a point just ahead of the player, turns it
 * to face them, and hands it to the engine, which allocates the actor from its own pool, binds
 * the model, starts the stand clip and runs a script from then on: the placement's own for the
 * level's fighters, so a spawned droid patrols and fires the way that one was authored to, or
 * one of this project's own, stand or follow, for everyone else and for the archive's creatures,
 * whose scripts are not in the level. Nothing is hooked.
 *
 * The choice is offered by MODEL, one row per actor file, the level's own first and then the
 * archive's, because "a battle droid" is what somebody wants and "placement 31" is not; the
 * copy of a level kind is taken from the plainest placement that uses the model.
 *
 * Internal to dev_overlay; the panel's NPC spawner group draws it.
 */
#ifndef DEV_OVERLAY_NPC_SPAWNER_H
#define DEV_OVERLAY_NPC_SPAWNER_H

#include "spawn_scripts.h"

#include <stdbool.h>
#include <stdint.h>

/* An actor file's name is 0x18 bytes in its header, "ddroid.baf"; the stem is shown, and the
 * whole name kept for the loader, which loads by it. */
#define NPC_SPAWNER_NAME_MAX  16u
#define NPC_SPAWNER_FILE_MAX  25u

/* What a spawned copy does: one of this project's own scripts, see spawn_scripts.h. Stand to
 * start, kept across kinds and levels; the panel's row sets it. A copy running its source's own
 * script was offered for a day (2026-09-16) and taken out: a level's fighter patrols and fights
 * on it, but a boss walks off to the marks his script names, and Attack does the fighting for
 * everyone without them. */
uint32_t npc_spawner_behaviour(void);
void     npc_spawner_set_behaviour(uint32_t behaviour);

/* How many spawned actors may be alive in one level at once. The engine's pool holds 128 actors
 * for the level's own placements and these together, and a full pool culls corpses and then
 * refuses, so a cap well under it leaves the level its own. Sixteen is also about where a
 * machine of the game's day would have felt them. */
#define NPC_SPAWNER_ALIVE_MAX 16u

/* How many different actor files one level is offered for. Mos Espa, the widest retail level,
 * places 122; a level with more is listed up to here and the log says how many were left off. */
#define NPC_SPAWNER_KINDS_MAX 128u

typedef struct npc_spawner_kind {
    char     name[NPC_SPAWNER_NAME_MAX];   /* the actor file's stem, "ddroid" for ddroid.baf */
    char     file[NPC_SPAWNER_FILE_MAX];   /* the name as the header spells it, "ddroid.baf" */
    uint32_t placements;                    /* how many of the level's placements use it; 0 for
                                             * a file the level did not load */
    bool     foreign;                       /* from the archive, not the level: loaded on demand */
} npc_spawner_kind_t;

/* How many of the archive's actor files are offered beyond the level's own: every file with a
 * body the level did not load, up to this. The retail archive has 303 files, about 200 of them
 * creatures. */
#define NPC_SPAWNER_FOREIGN_MAX 256u

/* Resolves the spawn routine and the world pointer, each from two places that have to agree.
 * False, with a log line, when they did not; the group's rows then read unavailable. */
bool npc_spawner_install(void);

/* Installed, a level is loaded and the player is in it. Read on every rebuild, because the level
 * comes and goes under the panel. */
bool npc_spawner_is_available(void);

/* The actor files the loaded level's placements use, counted afresh whenever the level changes
 * and whenever npc_spawner_refresh() asks. Zero with no level. */
void     npc_spawner_refresh(void);
uint32_t npc_spawner_kind_count(void);
bool     npc_spawner_kind(uint32_t index, npc_spawner_kind_t *out);

/* Which kind the next spawn raises: -1 for none, forgotten when the level changes. */
int32_t npc_spawner_chosen(void);
void    npc_spawner_choose(int32_t index);

/* How many of this group's spawns are alive in the loaded level right now. */
uint32_t npc_spawner_alive(void);

/* Once a frame: keeps every live flyer's record over the player's head, which is where its
 * scripts fly it to, and logs every change of a spawned copy's health, which is how a fight
 * between copies is seen. Nothing to do with no level or no player. */
void npc_spawner_tick(void);

/* Deletes every one of them through the engine's own delete, the way a script's remove does:
 * body, effects and pool slot given back, the record's live word cleared. Answers how many
 * went. Nothing to do with the level's own actors. */
uint32_t npc_spawner_remove_all(void);

/* Raises one actor of the chosen kind a few units ahead of the player, facing them, on the
 * first spot of three files ahead, a body's width apart, the middle one first, that none of
 * the earlier spawns still stands on, or on the spot with the most room when they stand on all
 * of them. False when
 * nothing is chosen, no level is loaded, the cap is reached, or the engine's pool had no room
 * even after it culled its corpses; the log says which. */
bool npc_spawner_spawn(void);

#endif /* DEV_OVERLAY_NPC_SPAWNER_H */
