/* overlay_modelswap.h: the Model swap group.
 *
 * One row per model the roster offers, marked ON where the player is wearing it. Wearing one is
 * the whole of the interaction: the swap decides for itself whether a row means "put this on" or
 * "go back", because going back is a model and not a separate row (character_model_select).
 *
 * It sits next to the NPC spawner because both are about the bodies in the level rather than
 * about the picture or the controls, and because a player looking for "play as somebody else"
 * looks at the two of them in turn.
 */
#ifndef OVERLAY_MODELSWAP_H
#define OVERLAY_MODELSWAP_H

#include "overlay_model.h"

#include <stdbool.h>
#include <stdint.h>

/* The roster's own bound, which is what the group can draw at most. */
#define OVERLAY_MODELSWAP_ROWS_MAX 320u

uint32_t overlay_modelswap_row_count(void);
void     overlay_modelswap_row(uint32_t slot, overlay_row_t *out);
bool     overlay_modelswap_toggle(uint32_t slot);

#endif /* OVERLAY_MODELSWAP_H */
