/* air_control_row.h: the developer menu's row for steering a jump.
 *
 * A key on its own was not enough. The setting only ever mattered to a pad player, and a pad player
 * on a handheld has no comfortable way to open an ini, so the one control that needed reaching most
 * easily was the one that could not be reached at all.
 */
#ifndef DEV_OVERLAY_AIR_CONTROL_ROW_H
#define DEV_OVERLAY_AIR_CONTROL_ROW_H

#include <stdbool.h>

bool air_control_row_get(void);

/* Writes free look on with it, the same one-way dependency the passive camera carries: the body
 * only holds its heading in the air because free look holds it, so without free look there is
 * nothing for this to release. Switching it off leaves free look alone. */
bool air_control_row_set(bool enabled);

/* Available while EITHER the sideways walk or free look is on, because either one is enough to
 * take the engine's own jump steering away, and unavailable while both are off because the shipped
 * game already steers a jump perfectly well on its own. See the site comment for the evidence. */
bool air_control_row_available(void);

#endif /* DEV_OVERLAY_AIR_CONTROL_ROW_H */
