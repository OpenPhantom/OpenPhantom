/* view_range_row.h: the draw distance setting, as the overlay's own row sees it.
 *
 * WHY THIS GOES THROUGH THE INI RATHER THAN A FUNCTION CALL. The scale this row edits belongs to
 * view_distance_fix.dll, and feature DLLs in this project never depend on each other at run time:
 * any one of them can be deleted from mods\ without breaking the others, and a direct call would
 * end that. The overlay therefore writes the value to engine_fixes.ini and view_distance_fix reads
 * it back on its own schedule, which is a channel both already have and neither owns.
 *
 * What that costs is a fraction of a second of latency between letting go of the row and the world
 * changing, and it is worth naming rather than hiding. What it buys, besides the rule, is that the
 * value survives a restart for free: it is written where the setting already lived.
 *
 * The overlay does not read the scale back from the game. It reads the ini, which is the same thing
 * unless something else has written the key since, and there is nothing else that does.
 */
#ifndef DEV_OVERLAY_VIEW_RANGE_ROW_H
#define DEV_OVERLAY_VIEW_RANGE_ROW_H

#include <stdbool.h>
#include <stddef.h>

/* The range view_distance_fix itself accepts. Its own clamp is the authority; these exist so the
 * row refuses a number before writing it rather than writing something that will be silently
 * corrected later, which would leave the ini and the world disagreeing. Kept in step with the
 * clamp in view_distance_fix.c by the comment at both ends, since a header cannot be shared
 * between two DLLs without making one depend on the other. */
#define VIEW_RANGE_MIN 1.0f
#define VIEW_RANGE_MAX 2.5f

/* One press of the row's own step, in scale units. */
#define VIEW_RANGE_STEP 0.1f

/* Clamps to the accepted range. A value that is not a number comes back as the minimum, because
 * the alternative is writing a NaN into a file the game reads on every start. */
float view_range_row_clamp(float scale);

/* Parses what was typed. Accepts a bare number with an optional decimal point, and ignores a
 * trailing "x" so the chip can be typed back in exactly as it is displayed. Returns false, leaving
 * `out` untouched, for empty text, text that is not a number, or trailing rubbish after one.
 * A parsed number is NOT clamped here: the caller decides whether to clamp or refuse. */
bool view_range_row_parse(const char *text, float *out);

/* Formats for the row's chip, the same "2.50x" shape the jump-boost scale row uses. Always
 * terminates when `size` is at least one. */
void view_range_row_format(float scale, char *out, size_t size);

/* The current setting, read from the ini, clamped. Falls back to the minimum when the key is
 * absent or unreadable, which is what an untouched installation reads as. */
float view_range_row_get(void);

/* Writes the setting to the ini, clamped first. False when the file could not be written, which
 * the row reports rather than showing a value the game will never see. */
bool view_range_row_set(float scale);

#endif /* DEV_OVERLAY_VIEW_RANGE_ROW_H */
