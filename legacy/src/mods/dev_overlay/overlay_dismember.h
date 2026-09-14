/* overlay_dismember.h: the panel's OpenPhantom group for lightsaber dismemberment.
 *
 * One row under a heading of its own. It has lived with the settings, on the argument that its
 * choice survives the session, and with the cheats, on the argument that a player looking for it
 * looks there first; a heading that says the word is where that player looks first of all, and
 * the row it holds is the one thing this project changes about what a lightsaber does to a body.
 *
 * The row writes [dismemberment] Mode, 2 or 0, and never reaches into dismemberment.dll; that DLL
 * re-reads the key about once a second, so a press changes the game within that second. The key
 * also takes 1, which corrects which limb the engine's own seven authored severings take without
 * adding any; nobody wants that on purpose, so the row writes 2 or 0 and a reader who has set 1
 * by hand sees the row lit and keeps their setting until they press it.
 */
#ifndef DEV_OVERLAY_OVERLAY_DISMEMBER_H
#define DEV_OVERLAY_OVERLAY_DISMEMBER_H

#include "overlay_model.h"

#include <stdbool.h>
#include <stdint.h>

#define OVERLAY_DISMEMBER_ROW_COUNT 1u

/* Fills everything about the row except `group` and `id`, which belong to the caller's
 * numbering. */
void overlay_dismember_row(uint32_t slot, overlay_row_t *out);

/* Flips it. False for any other slot or a failed write. */
bool overlay_dismember_toggle(uint32_t slot);

#endif /* DEV_OVERLAY_OVERLAY_DISMEMBER_H */
