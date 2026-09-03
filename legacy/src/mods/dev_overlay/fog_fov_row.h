/* fog_fov_row.h: whether the field of view scales the fog band, as the overlay's own row sees it.
 *
 * The levels were authored at 60 degrees. Widen the picture and a cell at the corner of the
 * frustum is nearer the eye than one straight ahead at the same radial distance, so a band that
 * was right at 60 degrees ends too far out to hide that corner. This term brings the band in with
 * the picture, and at the authored 60 degrees it is exactly 1.0 and changes nothing.
 *
 * It has an effect only while the band follows the draw distance. A band left as the level
 * authored it is not scaled by anything, which is what that row means, so this one reads as
 * unavailable there rather than as a switch that does nothing.
 *
 * It goes through the ini for the same reason the rows beside it do: the setting belongs to
 * view_distance_fix.dll, feature DLLs here never depend on each other at run time, and either can
 * be deleted from mods\ without breaking the other.
 */
#ifndef DEV_OVERLAY_FOG_FOV_ROW_H
#define DEV_OVERLAY_FOG_FOV_ROW_H

#include <stdbool.h>

/* Absent reads as on, matching the shipped default, so a fresh installation shows the row in the
 * state the game is actually in rather than the opposite. */
bool fog_fov_row_get(void);

/* False while the band is the level's own, where this term has nothing to scale. */
bool fog_fov_row_available(void);

/* Writes it. False when the row is unavailable or the file could not be written, which the caller
 * shows by leaving the row where it was: a switch that appears to move while the game says
 * otherwise would be worse than one that refuses. */
bool fog_fov_row_set(bool follow);

#endif /* DEV_OVERLAY_FOG_FOV_ROW_H */
