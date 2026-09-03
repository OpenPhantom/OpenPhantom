/* overlay_utilities.h: the panel's second OpenPhantom group, the one that is not cheats.
 *
 * It is a group of its own because the tab began as five cheats with a settings row appended, and
 * settings kept arriving until the settings outnumbered the cheats and a reader scrolled past
 * invincibility to reach the draw distance. The retail half of the panel already splits its own
 * rows into two groups by what they are, so this follows it: things that change the game sit under
 * Cheats, things that configure this patch sit under Utilities.
 *
 * It is a file of its own because overlay_model.c was over its size limit before this split and its
 * own SIZE NOTE names a seam. This is a better seam than the one it named: seven rows, each
 * reading and writing one setting, with no share of the panel's navigation, search, folding or
 * typing state. What stays behind in overlay_model.c is the part that has to know which row is
 * being typed into, because that is the panel's state rather than any row's.
 *
 * The rows here are DESCRIBED and ACTED ON here, and NUMBERED by the caller. That split is what
 * keeps this file free of the id arithmetic the cheats group needs, where rows are positioned
 * relative to a cheat enum; a slot here is just its position in the list.
 */
#ifndef DEV_OVERLAY_OVERLAY_UTILITIES_H
#define DEV_OVERLAY_OVERLAY_UTILITIES_H

#include "overlay_model.h"

#include <stdbool.h>
#include <stdint.h>

/* Seven, in the order they are drawn. The draw distance first, because it is the setting a player
 * came looking for; the key binding last, because it is the one nobody needs twice. */
#define OVERLAY_UTILITIES_ROW_COUNT 7u

/* Fills everything about one row except `group` and `id`, which belong to the caller's numbering.
 *
 * `editing_text` is what has been typed so far when this row is the one being typed into, and NULL
 * otherwise. `capturing` is the same question for the key binding row. A row that is neither a
 * typed value nor a key binding ignores both. */
void overlay_utilities_row(uint32_t slot, const char *editing_text, bool capturing,
                           overlay_row_t *out);

/* True when this slot is a typed value, which is what tells the caller to start an edit rather
 * than act on a press. */
bool overlay_utilities_row_is_value(uint32_t slot);

/* True when this slot binds a key, the same question for the other kind of edit. */
bool overlay_utilities_row_is_key(uint32_t slot);

/* Flips a switch. False when the slot is not a switch, the row is unavailable, or the file could
 * not be written, and in every one of those the caller leaves the row where it was. */
bool overlay_utilities_toggle(uint32_t slot);

/* Commits typed text to a value row. False when the slot is not a value row or the text is not a
 * number, which leaves the setting alone rather than writing a zero. */
bool overlay_utilities_commit(uint32_t slot, const char *text);

/* Binds a key. False when the slot is not the key row or the key was refused. */
bool overlay_utilities_bind(uint32_t slot, int32_t virtual_key);

#endif /* DEV_OVERLAY_OVERLAY_UTILITIES_H */
