/* overlay_freecam.h: the panel's OpenPhantom group for the free camera.
 *
 * Three rows and a fold: the teleport key, the cheat itself, and "how to fly", which opens into
 * the lines that say what the keys do. They were the tail of the cheats group, with the most
 * involved id arithmetic in the panel to keep the fold's lines drawn under their summary while the
 * rows below kept fixed ids. Under a heading of their own the lines are the last rows there are,
 * so a slot is its id and nothing is remapped.
 *
 * Read top to bottom the rows tell the story on their own: set a teleport key, then the toggle
 * right below it stops reading unavailable, then the fold says how to fly it. The fold opens
 * itself when the camera goes on and shuts when it goes off, because the mouse is claimed for as
 * long as the camera flies and no click could reach it; see overlay_freecam_sync().
 */
#ifndef DEV_OVERLAY_OVERLAY_FREECAM_H
#define DEV_OVERLAY_OVERLAY_FREECAM_H

#include "overlay_model.h"

#include <stdbool.h>
#include <stdint.h>

/* The slots: the key, the cheat, the fold's summary, then its lines only while it is open. */
#define OVERLAY_FREECAM_HOTKEY_SLOT  0u
#define OVERLAY_FREECAM_TOGGLE_SLOT  1u
#define OVERLAY_FREECAM_SUMMARY_SLOT 2u
#define OVERLAY_FREECAM_LINE_FIRST   3u

/* Eleven lines, not seven, because three of them describe hiding the panel and the two ways out,
 * and each is a sentence wider than the panel. A line that does not fit is drawn clipped, running
 * off the edge of the box with no ellipsis and no wrap, so the reader loses the end of exactly
 * the sentence that tells them how to get out. Each is written as a line plus a continuation
 * indented two spaces instead. */
#define OVERLAY_FREECAM_LINE_COUNT 11u
#define OVERLAY_FREECAM_ROWS_MAX   (OVERLAY_FREECAM_LINE_FIRST + OVERLAY_FREECAM_LINE_COUNT)

/* How many rows the group draws right now: three, or three and the lines. */
uint32_t overlay_freecam_row_count(void);

/* Folds the lines, so the panel opens the way the groups do: folded. */
void overlay_freecam_reset(void);

/* Called once per rebuild. Free camera changes state by the panel's own toggle or, more often, by
 * its hotkey, which flips it inside cheats_openphantom.c without ever going through the panel;
 * this is what sees the hotkey path. The fold opens when the camera goes on and shuts when it
 * goes off, on the edge only, so a click in between still wins. */
void overlay_freecam_sync(void);

/* Fills everything about one row except `group` and `id`, which belong to the caller's numbering.
 * `capturing` is whether the teleport key row is waiting for a key. */
void overlay_freecam_row(uint32_t slot, bool capturing, overlay_row_t *out);

/* True for the teleport key row. */
bool overlay_freecam_row_is_key(uint32_t slot);

/* Flips the cheat, or opens and shuts the fold. False for a line, which is a note and never
 * acts, and for a cheat that refused. */
bool overlay_freecam_toggle(uint32_t slot);

#endif /* DEV_OVERLAY_OVERLAY_FREECAM_H */
