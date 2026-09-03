/* authored_fog_row.h: which fog band the game computes, as the overlay's own row sees it.
 *
 * view_distance_fix can either scale each level's authored fog band against the draw distance and
 * the field of view, so the fog is always solid before the geometry stops, or leave the band
 * exactly as the level shipped it. Nine of the eleven levels already hide their own draw edge
 * unaided, so on those the scaling only brings the fog nearer than the level asked for; two of
 * them, Coruscant and the Federation ship, author fog that ends beyond anything the engine can
 * draw and show a hard edge without it. Neither answer is wrong, which is why it is a switch.
 *
 * It goes through the ini for the same reason the two rows beside it do: the setting belongs to
 * view_distance_fix.dll, feature DLLs here never depend on each other at run time, and either can
 * be deleted from mods\ without breaking the other.
 */
#ifndef DEV_OVERLAY_AUTHORED_FOG_ROW_H
#define DEV_OVERLAY_AUTHORED_FOG_ROW_H

#include <stdbool.h>

/* True when the level's own band is being used untouched. Absent reads as false, matching the
 * shipped default, so a fresh installation shows the state the game is actually in. */
bool authored_fog_row_get(void);

/* Writes it. False when the file could not be written, which the caller shows by leaving the row
 * where it was rather than reporting a state the game is not in. */
bool authored_fog_row_set(bool authored);

#endif /* DEV_OVERLAY_AUTHORED_FOG_ROW_H */
