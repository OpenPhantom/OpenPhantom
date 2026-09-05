/* auto_range_row.h: the draw distance's frame-rate automation, as the overlay's own row sees it.
 *
 * view_distance_fix can lower the draw distance on its own when a scene costs more frame time than
 * it is worth, and give it back when the scene gets cheaper. That is useful and it is also a thing
 * a player may simply not want: it moves the fog with it, so the world visibly opens and closes as
 * the frame rate wanders. Whether that is a feature or a distraction is a matter of taste, and
 * taste belongs on a switch.
 *
 * It goes through the ini for the same reason the draw distance row beside it does: the setting
 * belongs to view_distance_fix.dll, feature DLLs here never depend on each other at run time, and
 * either can be deleted from mods\ without breaking the other. The overlay writes the key and
 * view_distance_fix picks it up on its own schedule, which costs a fraction of a second of latency
 * and buys the value surviving a restart for nothing.
 */
#ifndef DEV_OVERLAY_AUTO_RANGE_ROW_H
#define DEV_OVERLAY_AUTO_RANGE_ROW_H

#include <stdbool.h>

/* Reads the key back from the ini. Absent reads as OFF, matching the shipped default, so a fresh
 * installation shows the row in the state the game is actually in rather than the opposite. */
bool auto_range_row_get(void);

/* Writes it. Returns false when the file could not be written, which the caller shows by leaving
 * the row where it was: a switch that appears to move while the file says otherwise would be worse
 * than one that refuses. */
bool auto_range_row_set(bool enabled);

#endif /* DEV_OVERLAY_AUTO_RANGE_ROW_H */
