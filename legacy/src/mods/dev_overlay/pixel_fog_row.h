/* pixel_fog_row.h: how the fog is drawn, as the overlay's own row sees it.
 *
 * The engine computes a fog factor at each vertex and interpolates it across the triangle. Where a
 * triangle is large, which here is most of the ground and most of a skyline, the factor between
 * the corners is whatever the interpolation makes it rather than what the distance says, and it
 * moves as the camera does. Handing the band to the device instead has it measure eye-space depth
 * at every pixel, which is steady and needs no band conversion.
 *
 * The row reports what was asked for rather than what is running. A device that reports neither
 * table fog nor w fog cannot do this, and view_distance_fix logs that and stays on the engine's
 * ramp; the row still reads on, because it is the setting that survives to the next machine.
 *
 * It goes through the ini for the same reason the rows beside it do: the setting belongs to
 * view_distance_fix.dll, feature DLLs here never depend on each other at run time, and either can
 * be deleted from mods\ without breaking the other.
 */
#ifndef DEV_OVERLAY_PIXEL_FOG_ROW_H
#define DEV_OVERLAY_PIXEL_FOG_ROW_H

#include <stdbool.h>

/* Reads the key back from the ini. Absent reads as on, matching the shipped default, so a fresh
 * installation shows the row in the state the game is actually in rather than the opposite. */
bool pixel_fog_row_get(void);

/* Writes it. Returns false when the file could not be written, which the caller shows by leaving
 * the row where it was: a switch that appears to move while the file says otherwise would be worse
 * than one that refuses. */
bool pixel_fog_row_set(bool per_pixel);

#endif /* DEV_OVERLAY_PIXEL_FOG_ROW_H */
