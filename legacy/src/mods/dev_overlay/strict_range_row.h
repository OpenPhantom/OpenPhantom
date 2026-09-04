/* strict_range_row.h: hold the draw distance at exactly what was asked for, as the overlay's own
 * row sees it.
 *
 * Three things move the draw distance after the reader has set it. The frame governor lowers it
 * when a scene costs more frame time than the distance is worth and gives it back when the scene
 * gets cheaper, and the row above this one already turns that off. The level-opening window and a
 * scripted camera can each raise it, both shipped off. And the cell watchdog lowers it, and never
 * raises it again, when the draw table or the vertex cache comes near overflowing.
 *
 * That last one is not taste. cell_watchdog.h documents what happens without it: the cell table
 * ends where the bucket list heads begin, the limit is checked once at function entry and not
 * again, and an overflow was traced to an access violation in the draw function with a list head
 * the renderer had read as a pointer. The vertex cache fails more quietly and more permanently,
 * leaving vertices that are never reset until the level is reloaded, which is the stretched
 * geometry seen after raising the distance.
 *
 * So this row is not "turn off an annoyance". It is "I accept those two outcomes in exchange for
 * the number I set being the number in force". That is a legitimate thing to want from a developer
 * menu and a poor thing to leave on, which is why it ships off and says so in the log the moment
 * it is switched on.
 *
 * The watchdog keeps running underneath it and keeps writing its warnings to engine_fixes.log, so
 * a session that ends badly still says why. What it cannot do while this is on is act.
 *
 * It goes through the ini for the same reason the rows beside it do: the setting belongs to
 * view_distance_fix.dll, feature DLLs here never depend on each other at run time, and either can
 * be deleted from mods\ without breaking the other.
 */
#ifndef DEV_OVERLAY_STRICT_RANGE_ROW_H
#define DEV_OVERLAY_STRICT_RANGE_ROW_H

#include <stdbool.h>

/* Reads the key back from the ini. Absent reads as OFF, matching the shipped default. */
bool strict_range_row_get(void);

/* Writes it. False when the file could not be written, which the caller shows by leaving the row
 * where it was rather than reporting a state the game is not in. */
bool strict_range_row_set(bool strict);

#endif /* DEV_OVERLAY_STRICT_RANGE_ROW_H */
