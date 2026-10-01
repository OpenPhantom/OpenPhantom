/* npc_spawner.h: raise another of the level's own actors, in front of the player.
 *
 * A level's actors are authored as placements, one record each, in a directory the world record
 * carries; the engine walks that directory a few times a second and, for every placement the
 * player has come near, calls the one routine that turns a record into a live actor. The level's
 * scripts call the same routine for their own spawns. This asks it for one more: it copies a
 * placement the level already has, moves the copy to a point just ahead of the player, turns it
 * to face them, and hands it to the engine, which allocates the actor from its own pool, binds
 * the model, starts the stand clip and runs one of this project's own scripts from then on.
 * The copy carries an engine index of its own, 256 + k, past every placement, and a savegame
 * keeps it in the panel's own block rather than in the enemy block. Three heads are hulled for
 * that and nothing else: enemy_saveBlock and save_saveGame (npc_spawn_save.c) and sys_startup
 * (npc_spawn_node.c).
 *
 * The choice is offered by MODEL, one row per actor file, the level's own first and then the
 * archive's, because "a battle droid" is what somebody wants and "placement 31" is not; the
 * copy of a level kind is taken from the plainest placement that uses the model.
 *
 * Internal to dev_overlay; the panel's NPC spawner group draws it.
 */
#ifndef DEV_OVERLAY_NPC_SPAWNER_H
#define DEV_OVERLAY_NPC_SPAWNER_H

#include "npc_spawn_block.h"
#include "npc_spawn_desc.h"
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

/* The ring of records the copies are raised from: one per actor the engine's pool can hold, so
 * every copy a savegame or a session brings has a record. The cap above is a separate number. */
#define NPC_SPAWNER_RING NPC_SPAWN_COPIES_MAX

/* How many different actor files one level is offered for. Mos Espa, the widest retail level,
 * places 122; a level with more is listed up to here and the log says how many were left off. */
#define NPC_SPAWNER_KINDS_MAX 128u

typedef struct npc_spawner_kind {
    char     name[NPC_SPAWNER_NAME_MAX];   /* the actor file's stem, "ddroid" for ddroid.baf */
    char     file[NPC_SPAWNER_FILE_MAX];   /* the name as the header spells it, "ddroid.baf" */
    uint32_t placements;                    /* how many of the level's placements use it; 0 for
                                             * a file the level did not load */
    bool     foreign;                       /* from the archive, not the level: loaded on demand */
    uint8_t  section;                       /* entity_section_t, the shelf the list shows it on;
                                             * 0, the level's own, for a census kind */
} npc_spawner_kind_t;

/* How many of the archive's actor files are offered beyond the level's own: every file the offer
 * rule passes (entity_offer.h) that the level did not load, up to this. Of the retail archive's 303
 * files the rule offers 204. */
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

/* Raises the copy `desc` describes, where it says and facing as it says, under a key of its own:
 * what the placement mode's click asks for outside a session. The same checks as a spawn, the
 * player and the cap, and no spot is looked for, since the one who asks chose it. False when
 * nothing was raised; the log says why. */
bool npc_spawner_raise_described(const npc_spawn_desc_t *desc);

/* Raises a copy a savegame held, where it stood, facing as it faced, with its health. False
 * when it could not be raised; the log says why. The cap does not apply: a savegame holds only
 * what was alive. */
bool npc_spawner_raise_saved(const npc_spawn_saved_t *saved);

/* A copy a multiplayer session granted, under the key its host chose: the builder with no cap,
 * since the host's cap decided, and without looking at the player, since a client builds what its
 * host announced. `saved`, when given, is how a restored copy stood. False when nothing was raised;
 * the log says why. */
bool npc_spawner_raise_granted(const npc_spawn_desc_t *desc, uint32_t key,
                               const npc_spawn_saved_t *saved);

/* Removes `actor`, one of this group's copies or its corpse, through the engine's own delete. False
 * when there is no delete, a save window is open, or the actor is not this group's. */
bool npc_spawner_delete(uintptr_t actor);

/* Whether `actor` was raised from one of this group's ring records, and what from. */
bool npc_spawner_owns(uintptr_t actor);
bool npc_spawner_description(uintptr_t actor, npc_spawn_desc_t *out);

/* The live actor on ring record `slot`, or NULL. */
const uint8_t *npc_spawner_ring_actor(uint32_t slot);

/* Whether any actor names one of the ring's records, a corpse included: whether a save has copies
 * to keep out of its enemy block. */
bool npc_spawner_names_any(void);

/* Whether a copy can be raised at all: the routine, both savegame hulls and the module node. */
bool npc_spawner_can_raise(void);

#endif /* DEV_OVERLAY_NPC_SPAWNER_H */
