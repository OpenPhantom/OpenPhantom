/* variable_fov.h: an adjustable, aspect-correct field of view.
 *
 * Produces: variable_fov.dll
 *
 * The public surface is the install entry plus what fov_menu.c needs to drive and to caption the
 * slider. Nothing outside this DLL uses any of it.
 */
#ifndef VARIABLE_FOV_H
#define VARIABLE_FOV_H

#include "fov_math.h"

#include <stdbool.h>

void variable_fov_install(void);

/* True once the camera hook stands. The slider is only added when this is true, because a slider
 * that drives nothing is worse than no slider. */
bool variable_fov_is_active(void);

/* The user offset added on top of the computed horizontal field of view. It may be NEGATIVE:
 * the aspect correction widens the view beyond the authored 60 deg on any frame wider than 4:3,
 * so reaching a field of view of 60 there means subtracting rather than adding. */
float variable_fov_extra_degrees(void);

/* The horizontal field of view this canvas would have with NO offset at all, the pure result of
 * the aspect mode. The slider needs it to turn an absolute angle into an offset, and taking it
 * from here rather than subtracting the offset from the live value keeps the arithmetic honest
 * when the live value has been clamped. 0 = nothing has been computed yet, or STRETCH. */
float variable_fov_base_horizontal_degrees(void);

/* Sets the offset, rebuilds the projection immediately and writes the value back to the ini.
 * Rebuilding immediately is REQUIRED, not a nicety: rdCamera_BuildProjection is not a per-frame
 * function, so without it a new value would sit unused until the next level load. */
void variable_fov_set_extra_degrees(float degrees);

/* The angles currently in force, for the caption. 0 = no projection has been built yet. */
float variable_fov_horizontal_degrees(void);
float variable_fov_vertical_degrees(void);

/* The slider's range, as ABSOLUTE horizontal degrees rather than as an offset. The bottom end is
 * what the leftmost notch reads on screen. */
int variable_fov_slider_min_fov_degrees(void);
int variable_fov_slider_max_fov_degrees(void);

#endif /* VARIABLE_FOV_H */
