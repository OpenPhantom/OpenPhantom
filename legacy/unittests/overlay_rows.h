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
   repeat the same arithmetic and one of them would eventually get it wrong. */
#define LVL_ROW(n)  (1u + OVERLAY_CHEATS_ROW_COUNT + 1u + (uint32_t)(n))
#define SPAWN_ROW(n) (LVL_ROW(OVERLAY_LEVELS_ENTRY_FIRST) + 1u + (uint32_t)(n))
#define FC_ROW(n)   (SPAWN_ROW(OVERLAY_SPAWN_FIXED_ROWS) + 1u + (uint32_t)(n))
#define DIS_ROW(n)  (FC_ROW(OVERLAY_FREECAM_LINE_FIRST) + 1u + (uint32_t)(n))
#define UTIL_ROW(n) (DIS_ROW(OVERLAY_DISMEMBER_ROW_COUNT) + 1u + (uint32_t)(n))
#define MENU_ROW(n) (UTIL_ROW(OVERLAY_UTILITIES_ROW_COUNT) + 1u + (uint32_t)(n))
#define PIC_ROW(n)  (MENU_ROW(OVERLAY_MENU_EXTRAS_LINE_FIRST) + 1u + (uint32_t)(n))
#define FOG_ROW(n)  (PIC_ROW(OVERLAY_PICTURE_ROW_COUNT) + 1u + (uint32_t)(n))
#define CTRL_ROW(n) (FOG_ROW(OVERLAY_FOG_ROW_COUNT) + 1u + (uint32_t)(n))

/* The twelve headings of the OpenPhantom tab. */
#define HEADINGS 12u

#endif /* UNITTESTS_OVERLAY_ROWS_H */
