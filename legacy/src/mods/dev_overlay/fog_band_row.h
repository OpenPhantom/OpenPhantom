/* fog_band_row.h: how near the fog sits, as the overlay's own row sees it.
 *
 * Every other fog setting decides where the fog HAS to be: the level's own numbers, the correction
 * for a widened picture, the floor that keeps the band from being pulled in past the edge the
 * world stops at. None of them answers "I would like more fog than that", because that is taste
 * rather than correctness. This row is that answer, and it is the only one that can bring the band
 * nearer than the rest of them put it.
 *
 * Below 1 the fog arrives sooner and is denser at a given distance, since the band is shorter and
 * the ramp across it steeper. Both ends move together, so a level's authored proportions survive.
 *
 * view_distance_fix polls this key roughly once a second and the band eases to the new target, so
 * a player watching the world while typing sees the change a moment later without a reload. That
 * is the reason it is worth having here at all: the right number is a matter of looking at it.
 *
 * It goes through the ini for the same reason the rows beside it do: the setting belongs to
 * view_distance_fix.dll, feature DLLs here never depend on each other at run time, and either can
 * be deleted from mods\ without breaking the other.
 */
#ifndef DEV_OVERLAY_FOG_BAND_ROW_H
#define DEV_OVERLAY_FOG_BAND_ROW_H

#include <stdbool.h>
#include <stddef.h>

/* The range view_distance_fix itself accepts. Its own clamp is the authority; these exist so the
 * row refuses a number before writing it rather than writing one that will be silently corrected
 * later, which would leave the ini and the world disagreeing. Kept in step with the clamp in
 * view_distance_fix.c by the comment at both ends, since a header cannot be shared between two
 * DLLs without making one depend on the other. */
#define FOG_BAND_MIN 0.25f
#define FOG_BAND_MAX 1.0f

/* What the game runs at with the key absent, which is what the row has to show then. The same
 * number as the maximum, and kept as its own name anyway: they mean different things, and 0.6 was
 * the shipped default for long enough to prove that reusing one for the other reads as a mistake
 * the moment they part again. */
#define FOG_BAND_DEFAULT 1.0f

/* One press of the row's own step. Smaller than the draw distance row's, because the whole useful
 * range here is narrower than that row's single step. */
#define FOG_BAND_STEP 0.05f

/* Clamps to the accepted range. A value that is not a number comes back as the shipped default,
 * not as either end: an unreadable file should leave the game where a fresh installation would be,
 * whereas falling back to 0.25 would wrap the player in fog because a file could not be parsed and
 * falling back to 1.0 would quietly ship a picture nobody chose. A number that is merely out of
 * range clamps to the end it was heading for, since that says which way it was going. */
float fog_band_row_clamp(float scale);

/* Parses what was typed. A bare number with an optional decimal point, with a trailing "x"
 * accepted so the chip can be typed back in exactly as displayed. False, leaving `out` untouched,
 * for empty text, text that is not a number, or rubbish after one. Not clamped here: the caller
 * decides whether to clamp or refuse. */
bool fog_band_row_parse(const char *text, float *out);

/* Formats for the row's chip. Always terminates when `size` is at least one. */
void fog_band_row_format(float scale, char *out, size_t size);

/* The current setting, read from the ini and clamped. An absent key reads as 1.0, which is what an
 * untouched installation runs at. */
float fog_band_row_get(void);

/* Writes it, clamped first. False when the file could not be written, which the row reports rather
 * than showing a value the game will never see. */
bool fog_band_row_set(float scale);

#endif /* DEV_OVERLAY_FOG_BAND_ROW_H */
