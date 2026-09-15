/* overlay_cheats.h: the panel's first OpenPhantom group, the cheats, as rows.
 *
 * The toggles, whose ids are their cheats_own_id_t, and the three rows that are not toggles:
 * super run's speed with its slider track, and the jump boost scale. The drawn order is a slot
 * table here, so each typed row sits directly under its toggle. The level skip was the tail of
 * this group until the Level selection group took it. The ids and the reasoning behind each are
 * in overlay_row_ids.h; this file is what each id looks like on screen and what a click, a typed
 * number or a drag on it does. Free camera itself, its key and its fold are a group of their
 * own, overlay_freecam.c.
 *
 * It came out of overlay_model.c when a fifth settings group put that file within a few lines of
 * its hard limit. The cut is the same one every other group already had: the model keeps the
 * state (which row is being typed into) and hands it in as an argument, and this file knows
 * nothing about the panel beyond the row it is filling.
 */
#ifndef DEV_OVERLAY_OVERLAY_CHEATS_H
#define DEV_OVERLAY_OVERLAY_CHEATS_H

#include "overlay_model.h"

#include <stdbool.h>
#include <stdint.h>

/* The id drawn at a slot, in the group's own order; OVERLAY_CHEATS_ROW_COUNT past the end. */
uint32_t overlay_cheats_id_at(uint32_t slot);

/* Fills one row by its id. `editing_text` is what has been typed into this row so far when it
 * is the one being typed into, or NULL. */
void overlay_cheats_row(uint32_t id, const char *editing_text, overlay_row_t *out);

/* Acts on a row by its id: flips a cheat. False for a row that is not a switch. */
bool overlay_cheats_toggle(uint32_t id);

/* Commits typed text to one of the two value rows. False when the id is not one of them or the
 * text is not a positive number, which leaves the value alone instead of writing a floor. */
bool overlay_cheats_commit(uint32_t id, const char *text);

/* Drags super run's track to `fraction`, 0 to 1, on a hundredths grid. False for any other id
 * or when the write did not land. */
bool overlay_cheats_slider_set(uint32_t id, float fraction);

#endif /* DEV_OVERLAY_OVERLAY_CHEATS_H */
