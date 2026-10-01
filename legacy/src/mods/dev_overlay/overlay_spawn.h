/* overlay_spawn.h: the panel's OpenPhantom group for the entity spawner.
 *
 * The spawner is called "Entity spawner" in the panel. In the source and on the wire it is still
 * npc_spawn*, because those names live in common/ and the multiplayer shares them, and its log
 * lines keep the prefix "npc spawner:", because a renamed line would break every comparison against
 * an older field log. The [multiplayer] report lines keep their heads for the same reason.
 *
 * A copy goes down through the placement mode and nowhere else. The row that put one three steps
 * ahead of the player is gone.
 *
 * Eight rows and a fold under a heading of their own: a note counting the spawns alive against the
 * cap, the row that hides the panel and places with the mouse, the two keys of that mode, the row
 * that says what is spawned, the row that says what it does, a note under that, the row that
 * removes everything spawned, and "about spawned entities", which opens into the lines that say
 * what to expect. The kind row opens the list of what can be spawned, shelf by shelf under a
 * heading each, the level's own first; the chosen one is marked and a pick closes the list. The
 * behaviour is not a list at all: its four words stand on the row itself, the chosen one filled,
 * with a note under them for the one thing the four words do not say. The rows read unavailable
 * with no level loaded. On the host of
 * a multiplayer session that runs the copies, a row under the remove takes every player's copies,

 * and the remove itself takes this player's; a row under the keys says why the last wish, or the
 * last spawn here, was refused, until the next one. Above that row, while anything in the group
 * is out of reach, stands the placement mode's own sentence for why, so that a greyed row is
 * never a row without a reason.
 */
#ifndef DEV_OVERLAY_OVERLAY_SPAWN_H
#define DEV_OVERLAY_OVERLAY_SPAWN_H

#include "entity_offer.h"
#include "npc_spawner.h"
#include "overlay_model.h"

#include <stdbool.h>
#include <stdint.h>

/* The slots with the lists and the fold shut and nothing refused: the count of the copies alive,
 * the placement, its two keys, the kind, the behaviour, the remove, the fold's summary. The first
 * four never move; the refusal row, the lists and the fold's lines push only what is below them. */
#define OVERLAY_SPAWN_ALIVE_SLOT     0u
#define OVERLAY_SPAWN_PLACE_SLOT     1u
#define OVERLAY_SPAWN_PLACE_KEY_SLOT 2u
#define OVERLAY_SPAWN_FACE_KEY_SLOT  3u
#define OVERLAY_SPAWN_KIND_SLOT      4u
#define OVERLAY_SPAWN_BEHAVIOUR_SLOT 5u
#define OVERLAY_SPAWN_BEHAVIOUR_NOTE 6u
#define OVERLAY_SPAWN_REMOVE_SLOT    7u
#define OVERLAY_SPAWN_SUMMARY_SLOT   8u
#define OVERLAY_SPAWN_FIXED_ROWS     9u
#define OVERLAY_SPAWN_HOST_ROWS      1u   /* the host's remove of every player's copies */
#define OVERLAY_SPAWN_WHY_ROWS       1u   /* why a row of this group cannot be used now */
#define OVERLAY_SPAWN_REFUSAL_ROWS   1u   /* why the last wish or spawn was refused */
#define OVERLAY_SPAWN_LINE_COUNT    16u
#define OVERLAY_SPAWN_ENTRY_COUNT  (NPC_SPAWNER_KINDS_MAX + NPC_SPAWNER_FOREIGN_MAX)
/* Every kind and a heading for every shelf. */
#define OVERLAY_SPAWN_LIST_MAX     (OVERLAY_SPAWN_ENTRY_COUNT + ENTITY_SECTION_COUNT)
/* Every row the group can build at once. The behaviour's four rows used to be in it and were left
 * out for a while: the budget the asserts in overlay_row_ids.h guard was four rows smaller than
 * what the group could put on screen. Nothing overflowed, because both ceilings have room to
 * spare, and a number that is wrong with room to spare is the kind somebody else finds later.
 * They are gone from both now, because the behaviour is one row and not a list. */
#define OVERLAY_SPAWN_ROWS_MAX     (OVERLAY_SPAWN_FIXED_ROWS + OVERLAY_SPAWN_HOST_ROWS + \
                                    OVERLAY_SPAWN_WHY_ROWS + OVERLAY_SPAWN_REFUSAL_ROWS + \
                                    OVERLAY_SPAWN_LINE_COUNT + OVERLAY_SPAWN_LIST_MAX)

/* How many rows the group draws right now: nine, ten on the host of a session, one more while a
 * refusal stands, and the fold's lines and the kind list while either is open. */
uint32_t overlay_spawn_row_count(void);

/* Closes the lists and the fold, so the panel opens the way the groups do: folded. */
void overlay_spawn_reset(void);

/* Fills everything about one row except `group` and `id`, which belong to the caller's
 * numbering. `capturing` says a key row is waiting for its key, which its chip shows. */
void overlay_spawn_row(uint32_t slot, bool capturing, overlay_row_t *out);

/* Removes, asks for the placement mode, opens and closes the kind list or the fold, or picks a
 * kind. False for a slot that is nothing, or a refusal. The behaviour row is picked by segment,
 * below, and answers false here. */
bool overlay_spawn_toggle(uint32_t slot);

/* The words on the behaviour row, in the order they stand, and 0 for any other slot. The strings
 * are the behaviour table's own and outlive the call; see overlay_choice.h for why the row cannot
 * carry them itself. */
uint32_t overlay_spawn_segments(uint32_t slot, const char **out, uint32_t max);

/* Picks one of those words. False for any other slot, an index past the end, or a behaviour this
 * kind of entity would ignore. */
bool overlay_spawn_choose(uint32_t slot, uint32_t index);

/* Binds the key a captured key row is for. False for a slot that is no key row, or a key that row
 * refuses (spawn_keys.h). */
bool overlay_spawn_bind(uint32_t slot, int32_t virtual_key);

#endif /* DEV_OVERLAY_OVERLAY_SPAWN_H */
