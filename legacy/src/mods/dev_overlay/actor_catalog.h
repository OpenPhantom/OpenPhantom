/* actor_catalog.h: every actor file in the game's archive, and what each one is.
 *
 * The NPC spawner offers the archive's creatures beyond the ones the level loaded, and writes
 * each copy's script with the model's own clip numbers. The level's own model table only says
 * which files this level placed; a player who wants a Tusken on Naboo needs the archive itself.
 * So this reads big.lab once, the way the engine's own loader lays it out, and for each .baf
 * keeps the name, the clip count, whether it has a body, a head or a chest node, which tells a
 * creature from a ship, a pickup or a door, and what it holds, a sabre node or the weapon mount
 * the hero code hangs a blaster on, which sets how close an attacking copy comes before it
 * swings. Nothing is loaded through the engine and nothing is kept but the catalogue; the
 * spawner loads a file when it first raises one.
 *
 * Internal to dev_overlay.
 */
#ifndef DEV_OVERLAY_ACTOR_CATALOG_H
#define DEV_OVERLAY_ACTOR_CATALOG_H

#include <stdbool.h>
#include <stdint.h>

#define ACTOR_CATALOG_NAME_MAX 25u    /* "obiwan.baf" and room; the archive names are short */
#define ACTOR_CATALOG_MAX      384u   /* the retail archive holds 303 .baf files */

/* What the model holds, by its nodes: a node whose name begins "sabre" is a blade; "weapon" is
 * the mount the hero code hangs a blaster on, a staff or a club on everyone else. */
typedef enum actor_weapon {
    ACTOR_WEAPON_NONE = 0,
    ACTOR_WEAPON_MOUNT,
    ACTOR_WEAPON_SABRE
} actor_weapon_t;

typedef struct actor_catalog_entry {
    char           name[ACTOR_CATALOG_NAME_MAX];
    uint32_t       clips;      /* the header's clip count, +0xC8 */
    bool           has_body;   /* a node named "head" or "chest": a creature, not a prop */
    actor_weapon_t weapon;
} actor_catalog_entry_t;

/* Reads the archive on the first call and answers from memory after that. False, with a log
 * line, when big.lab could not be found or read; the catalogue is then empty. */
bool actor_catalog_load(void);

uint32_t                     actor_catalog_count(void);
const actor_catalog_entry_t *actor_catalog_at(uint32_t index);

/* The entry for a file name as the archive spells it, case aside; NULL when it has none. */
const actor_catalog_entry_t *actor_catalog_find(const char *file);

/* One clip of an actor file: the name of the key it was built from, "runnowp2" without the
 * extension and in lower case, its track flags (bit 2 is loop) and its usage mark (1 death,
 * 2 hit react, 3 knockback, 4 falling, 5 landing, 6 twitching, 7 jumping, 8 turning, 9 aiming,
 * 0 none), the mark bapobj_findClipByUsage searches by. */
#define ACTOR_CLIP_NAME_MAX 0x24u
typedef struct actor_clip {
    char     name[ACTOR_CLIP_NAME_MAX];
    uint32_t flags;
    uint32_t usage;
} actor_clip_t;

/* Reads the clip list of one file from the archive, up to `max` clips, in the file's own order,
 * which is the order the engine numbers them. False when the file is not in the archive or its
 * animation block does not walk; then `*count` is 0. */
bool actor_catalog_clips(const char *file, actor_clip_t *out, uint32_t max, uint32_t *count);

#endif /* DEV_OVERLAY_ACTOR_CATALOG_H */
