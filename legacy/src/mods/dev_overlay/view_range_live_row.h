/* view_range_live_row.h: the draw distance the game is ACTUALLY running, as the panel's note sees
 * it.
 *
 * The row above it writes ViewRangeScale and then has no idea what becomes of that number. Two
 * guards lower it afterwards and neither says so on screen: the frame governor when a scene costs
 * more frame time than it is worth, and the cell watchdog when the draw table or the vertex cache
 * is close to overflowing. The watchdog is the one that surprises people, because on a heavy level
 * it can pin the scale at 1.00 for the whole level, and the panel went on displaying the number
 * that had been typed while the game ran something else entirely. That was reported as the dev
 * menu doing nothing, which is exactly what it looks like.
 *
 * So view_distance_fix publishes what is in force and this reads it back. The direction is the
 * unusual part: every other row here writes a setting for that DLL to pick up, and this one only
 * ever reads a value that DLL wrote. Nothing here can change it, which is why the row that shows
 * it is a note and not a control.
 */
#ifndef DEV_OVERLAY_VIEW_RANGE_LIVE_ROW_H
#define DEV_OVERLAY_VIEW_RANGE_LIVE_ROW_H

#include <stdbool.h>
#include <stddef.h>

/* Formats the scale in force as "1.00x" into `out`. False when the key is absent, which is what a
 * fresh installation reads before the first frame and what a machine without view_distance_fix
 * reads for ever; the caller says so rather than inventing a number. */
bool view_range_live_row_get(char *out, size_t size);

#endif /* DEV_OVERLAY_VIEW_RANGE_LIVE_ROW_H */
