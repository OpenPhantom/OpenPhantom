/* overlay_rows.h: where each row of the OpenPhantom tab sits on screen, for the two model tests.
 *
 * Shared by overlay_model.c and overlay_groups.c, which walk the same tab from different ends and
 * would otherwise each carry a copy of this arithmetic, and one of them would eventually get it
 * wrong. */
#ifndef UNITTESTS_OVERLAY_ROWS_H
#define UNITTESTS_OVERLAY_ROWS_H

#include "overlay_row_ids.h"
#include "overlay_spawn.h"

#include <stdint.h>

/* Rows by screen position with every group above them open, each heading one past the previous
   group's last row. The level selection and NPC spawner groups' rows are counted with their
   lists shut and the NPC spawner's and the free camera group's with their folds shut, which every
   section but their own keeps true. Written once because every check below would otherwise
   repeat the same arithmetic and one of them would eventually get it wrong.

   The chain is the DRAWN order, which is the order a player meets: the three used while standing
   in a level first, then what is chosen once, then the engine's settings and the rest. Two of the
   sources have no heading of their own and their rows simply continue the group above them, which
   is why the fog and the game's own screens step by no heading at all. */
#define CHEAT_ROW(n) (1u + (uint32_t)(n))
#define FC_ROW(n)    (CHEAT_ROW(OVERLAY_CHEATS_ROW_COUNT) + 1u + (uint32_t)(n))
#define SPAWN_ROW(n) (FC_ROW(OVERLAY_FREECAM_LINE_FIRST) + 1u + (uint32_t)(n))
/* The appearance group sits between them. Its own rows are in the count only while it is
 * unfolded, and no test that walks this chain unfolds it, so the chain steps over its body
 * and lands on the heading after it. */
#define MSWAP_ROW(n) (SPAWN_ROW(OVERLAY_SPAWN_FIXED_ROWS) + 1u + (uint32_t)(n))
#define LVL_ROW(n)   (MSWAP_ROW(0u) + 1u + (uint32_t)(n))
#define PIC_ROW(n)   (LVL_ROW(OVERLAY_LEVELS_ENTRY_FIRST) + 1u + (uint32_t)(n))
/* The fog is drawn under the picture's heading, so its rows follow the picture's with no heading
 * between them. */
#define FOG_ROW(n)   (PIC_ROW(OVERLAY_PICTURE_ROW_COUNT) + (uint32_t)(n))
#define CTRL_ROW(n)  (FOG_ROW(OVERLAY_FOG_ROW_COUNT) + 1u + (uint32_t)(n))
/* The multiplayer's heading comes straight after the controls' last row. */
#define MP_ROW(n)    (CTRL_ROW(OVERLAY_CONTROLS_FIXED_ROWS) + 1u + (uint32_t)(n))
/* With the controls open and the multiplayer, window and frame rate groups folded, four headings
 * stand between the controls' last row and the first row of the menus: the multiplayer's, the
 * window's, the frame rate's and the menus' own. */
#define UTIL_ROW(n)  (CTRL_ROW(OVERLAY_CONTROLS_FIXED_ROWS) + 4u + (uint32_t)(n))
/* The game's own screens are drawn under the menus heading, the fog's arrangement again. */
#define MENU_ROW(n)  (UTIL_ROW(OVERLAY_UTILITIES_ROW_COUNT) + (uint32_t)(n))
#define DIS_ROW(n)   (MENU_ROW(OVERLAY_MENU_EXTRAS_LINE_FIRST) + 1u + (uint32_t)(n))

/* The twelve headings of the OpenPhantom tab. Two sources more than that draw rows there, under
 * the heading they belong to rather than under one of their own. */
#define HEADINGS 12u
/* The rows on screen with the cheats and the free camera open, its fold shut. */
#define FC_OPEN_ROWS (HEADINGS + OVERLAY_CHEATS_ROW_COUNT + OVERLAY_FREECAM_LINE_FIRST)
/* And with the entity spawner open as well, its lists and fold shut: the groups below it count
 * from here. */
#define OPEN_ABOVE_ROWS (FC_OPEN_ROWS + OVERLAY_SPAWN_FIXED_ROWS)

#endif /* UNITTESTS_OVERLAY_ROWS_H */
