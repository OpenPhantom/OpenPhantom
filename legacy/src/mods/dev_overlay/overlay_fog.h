/* overlay_fog.h: the panel's OpenPhantom group for the fog.
 *
 * Four rows: no fog, the thickness with its slider, and what the band is measured against. They
 * sat at the middle of the picture group, and before that among the utilities, and the argument
 * for their own heading is the one that put them together in the first place: a player looking
 * for one of them is looking at the fog, and the others are the rest of that answer. This one
 * removes it, the second says how thick it is, the third says what it is a share of.
 *
 * No fog is a cheat by origin, still the code in cheats_no_fog.c and still the NoFog key; the
 * other two are [view_distance_fix] keys that DLL re-reads about once a second, and nothing here
 * calls it.
 */
#ifndef DEV_OVERLAY_OVERLAY_FOG_H
#define DEV_OVERLAY_OVERLAY_FOG_H

#include "overlay_model.h"

#include <stdbool.h>
#include <stdint.h>

/* No fog, the thickness, its slider, what it follows. */
#define OVERLAY_FOG_ROW_COUNT 4u

/* Fills everything about one row except `group` and `id`, which belong to the caller's numbering.
 * `editing_text` is what has been typed so far when the thickness is being typed into, or NULL. */
void overlay_fog_row(uint32_t slot, const char *editing_text, overlay_row_t *out);

/* Flips a switch. False when the slot is not one, the row is unavailable, or the write failed. */
bool overlay_fog_toggle(uint32_t slot);

/* Commits typed text to the thickness. False when the slot is not that row or the text is not a
 * number, which leaves the setting alone instead of writing a zero. */
bool overlay_fog_commit(uint32_t slot, const char *text);

/* Drags the thickness's slider to `fraction`, 0 to 1, on the hundredth grid the row's own
 * formatter shows. False for any other slot or a failed write. */
bool overlay_fog_slider_set(uint32_t slot, float fraction);

#endif /* DEV_OVERLAY_OVERLAY_FOG_H */
