/* fov_row.h: the field of view, as the overlay's own row sees it.
 *
 * The same setting the video options screen offers, reachable without leaving the game to find it.
 *
 * IT SHOWS DEGREES AND WRITES AN OFFSET, and that is the only awkward thing here. variable_fov
 * stores ExtraDegrees, a signed offset from a base that depends on the canvas, the aspect mode and
 * the engine's own projection. None of those exist in this DLL, so an offset is a number this row
 * could show and nobody could read. variable_fov therefore publishes BaseFov, and the width of the
 * picture is BaseFov plus ExtraDegrees, both of them in the file.
 *
 * THE BASE IS PUBLISHED RATHER THAN THE WIDTH, and the first version of this got that wrong. The
 * width moves every time the offset does, which is every frame of a drag, so working the base out
 * as "width minus offset" pairs a width written a moment ago with an offset written just now. The
 * base then drifts by exactly the amount of the last drag step, every step is measured from a
 * wrong origin, and the value runs away past both ends of the slider. The base itself moves only
 * when the canvas or the aspect mode changes, so publishing that half has nothing to go stale.
 *
 * Reading the offset back also means a drag shows up here immediately, rather than waiting for the
 * game to publish a new width.
 *
 * Without variable_fov installed there is no BaseFov to read, and the row says so rather than
 * inventing a number, because every degree it could invent would be wrong on some canvas.
 *
 * It goes through the ini for the same reason the rows beside it do: the setting belongs to
 * variable_fov.dll, feature DLLs here never depend on each other at run time, and either can be
 * deleted from mods\ without breaking the other.
 */
#ifndef DEV_OVERLAY_FOV_ROW_H
#define DEV_OVERLAY_FOV_ROW_H

#include <stdbool.h>
#include <stddef.h>

/* The range the video options slider offers, and the defaults variable_fov uses for its own ends.
 * Read from the file rather than assumed, since somebody who widened the slider there should not
 * find this row refusing what that one accepts. */
#define FOV_ROW_MIN_DEFAULT 60.0f
#define FOV_ROW_MAX_DEFAULT 120.0f

/* One press of the row's own step, in degrees. A whole degree is below what the eye picks up on a
 * single press and ten would cross the useful range in six. */
#define FOV_ROW_STEP 2.0f

float fov_row_min(void);
float fov_row_max(void);

/* Clamps to the range above. A value that is not a number comes back as the minimum, which is the
 * narrowest picture and the one closest to what the game shipped with. */
float fov_row_clamp(float degrees);

/* Parses what was typed: a bare number, with a trailing "deg" accepted so the chip can be typed
 * back in as displayed. False, leaving `out` untouched, for anything else. */
bool fov_row_parse(const char *text, float *out);

/* "97 deg" into `out`. */
void fov_row_format(float degrees, char *out, size_t size);

/* The width of the picture in degrees, or false when variable_fov has not published one yet, which
 * is the case with that DLL absent and for the first second of a session. */
bool fov_row_get(float *degrees);

/* Writes the offset that produces `degrees`. False when the file could not be written, and false
 * when there is no published width to measure the offset against, since the alternative is writing
 * an offset computed from a guessed base. */
bool fov_row_set(float degrees);

#endif /* DEV_OVERLAY_FOV_ROW_H */
