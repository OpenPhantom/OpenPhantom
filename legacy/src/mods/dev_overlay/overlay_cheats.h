/* overlay_cheats.h: the panel's first OpenPhantom group, the cheats, as rows.
 *
 * The rows that line up with cheats_own_id_t, and the one that does not: the jump boost scale in
 * the slot free camera's id would have had. The level skip was the tail of this group until the
 * Level selection group took it. Their ids and the
 * reasoning behind each are in overlay_row_ids.h; this file is what each id looks like on screen
 * and what a click on it does. Free camera itself, its key and its fold are a group of their own,
 * overlay_freecam.c.
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

/* Fills one row by its id, which is also its position. `editing_text` is what has been typed
 * into the jump boost scale so far, or NULL. */
void overlay_cheats_row(uint32_t id, const char *editing_text, overlay_row_t *out);

/* Acts on a row by its id: flips a cheat. The typed row is the model's own business and never
 * reaches here. */
bool overlay_cheats_toggle(uint32_t id);

#endif /* DEV_OVERLAY_OVERLAY_CHEATS_H */
