/* overlay_levels.h: the panel's OpenPhantom group for choosing a level.
 *
 * Two rows under a heading of their own, both about which level is played and neither a cheat
 * in the sense the Cheats group means: the skip to the next level, and the level a new game
 * starts at. The second opens a list, the same shape as the window group's size list: one row
 * per level in the game's own order, each named by its file in the LEVEL folder, the chosen
 * one lit, and picking one closes the list. Typing a number was tried first and works, and a
 * list of eleven names is quicker than remembering that Espa is six.
 */
#ifndef DEV_OVERLAY_OVERLAY_LEVELS_H
#define DEV_OVERLAY_OVERLAY_LEVELS_H

#include "overlay_model.h"

#include <stdbool.h>
#include <stdint.h>

/* The slots: the skip, the start level, then the list's entries while it is open. */
#define OVERLAY_LEVELS_SKIP_SLOT   0u
#define OVERLAY_LEVELS_START_SLOT  1u
#define OVERLAY_LEVELS_ENTRY_FIRST 2u
#define OVERLAY_LEVELS_ENTRY_COUNT 11u
#define OVERLAY_LEVELS_ROWS_MAX    (OVERLAY_LEVELS_ENTRY_FIRST + OVERLAY_LEVELS_ENTRY_COUNT)

/* How many rows the group draws right now: two, or two and the list. */
uint32_t overlay_levels_row_count(void);

/* Closes the list, so the panel opens the way the groups do: folded. */
void overlay_levels_reset(void);

/* Fills everything about one row except `group` and `id`, which belong to the caller's
 * numbering. */
void overlay_levels_row(uint32_t slot, overlay_row_t *out);

/* Runs the skip, opens and closes the list, or picks a level from it. False for a slot that is
 * nothing, or a refusal. */
bool overlay_levels_toggle(uint32_t slot);

#endif /* DEV_OVERLAY_OVERLAY_LEVELS_H */
