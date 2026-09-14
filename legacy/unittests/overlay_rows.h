/* overlay_rows.h: where each row of the OpenPhantom tab sits on screen, for the two model tests.
 *
 * Shared by overlay_model.c and overlay_groups.c, which walk the same tab from different ends and
 * would otherwise each carry a copy of this arithmetic, and one of them would eventually get it
 * wrong. */
#ifndef UNITTESTS_OVERLAY_ROWS_H
#define UNITTESTS_OVERLAY_ROWS_H

#include "overlay_row_ids.h"

#include <stdint.h>

/* Rows by screen position with every group above them open, each heading one past the previous
   group's last row. The free camera group's rows are counted with its fold shut, which every
   section but the fold's own keeps true. Written once because every check below would otherwise
   repeat the same arithmetic and one of them would eventually get it wrong. */
#define FC_ROW(n)   (1u + OVERLAY_CHEATS_ROW_COUNT + 1u + (uint32_t)(n))
#define DIS_ROW(n)  (FC_ROW(OVERLAY_FREECAM_LINE_FIRST) + 1u + (uint32_t)(n))
#define UTIL_ROW(n) (DIS_ROW(OVERLAY_DISMEMBER_ROW_COUNT) + 1u + (uint32_t)(n))
#define MENU_ROW(n) (UTIL_ROW(OVERLAY_UTILITIES_ROW_COUNT) + 1u + (uint32_t)(n))
#define PIC_ROW(n)  (MENU_ROW(OVERLAY_MENU_EXTRAS_LINE_FIRST) + 1u + (uint32_t)(n))
#define FOG_ROW(n)  (PIC_ROW(OVERLAY_PICTURE_ROW_COUNT) + 1u + (uint32_t)(n))
#define CTRL_ROW(n) (FOG_ROW(OVERLAY_FOG_ROW_COUNT) + 1u + (uint32_t)(n))

/* The ten headings of the OpenPhantom tab. */
#define HEADINGS 10u

#endif /* UNITTESTS_OVERLAY_ROWS_H */
