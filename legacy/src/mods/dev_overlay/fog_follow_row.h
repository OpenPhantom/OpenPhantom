/* fog_follow_row.h: whether the fog band tracks the draw distance, as the overlay's own row sees
 * it.
 *
 * A level's authored fog band is a fixed pair of distances, and leaving it alone gives the fog the
 * level asked for. Measuring the eleven shipped levels showed nine are already between 42 and 100
 * per cent opaque where their geometry stops, so on those the scaling only brings the fog nearer
 * than the level wanted. The other two are the reason the scaling exists: Coruscant covers none of
 * its own edge and the Federation ship five per cent, and both show the world ending in clear air.
 * Raising the draw distance moves that edge out without moving an authored band, so the taller the
 * draw distance the more there is to cover.
 *
 * The row asks the opposite question to the key it writes. AuthoredFogBand asks whether the band
 * is left alone; the row asks whether it follows, because following is the behaviour a player is
 * looking for after setting a draw distance and finding the world stops short of the fog.
 *
 * It goes through the ini for the same reason the rows beside it do: the setting belongs to
 * view_distance_fix.dll, feature DLLs here never depend on each other at run time, and either can
 * be deleted from mods\ without breaking the other.
 */
#ifndef DEV_OVERLAY_FOG_FOLLOW_ROW_H
#define DEV_OVERLAY_FOG_FOLLOW_ROW_H

#include <stdbool.h>

/* True when the band is scaled against the draw distance and the field of view. Always true while
 * the fog is drawn per vertex: see fog_follow_row_available(). */
bool fog_follow_row_get(void);

/* False while the fog is drawn per vertex, where the band is the only thing hiding the edge the
 * world stops at and an authored band leaves two of the levels bare. That combination is the one
 * this pair does not offer, and view_distance_fix reaches the same answer for itself, so the row
 * and the game agree about what is running. */
bool fog_follow_row_available(void);

/* Writes it. False when the row is unavailable or the file could not be written, which the caller
 * shows by leaving the row where it was rather than reporting a state the game is not in. */
bool fog_follow_row_set(bool follow);

#endif /* DEV_OVERLAY_FOG_FOLLOW_ROW_H */
