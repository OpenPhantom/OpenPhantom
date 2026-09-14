/* overlay_menu_extras.h: the panel's OpenPhantom group for the game's own screens, "In game
 * options extras".
 *
 * One switch and a fold under a heading of its own: whether this project's widgets, the free
 * look and sideways walking check boxes, the mouse sensitivity slider and the field of view
 * slider, appear on the game's own video and controls screens. Both ship off, so those screens
 * look as they did in 1999 and every one of those settings lives in this panel instead; the
 * switch is for the player who wants them in the game's menus as well, and the fold says what
 * they would get, the same shape the free camera's "how to fly" row has.
 *
 * The row writes [variable_fov] MenuSlider and [enhanced_input] MenuWidgets together and reads on
 * only when both are on, since a half state can only be reached by editing the file by hand and
 * reporting that as on would claim a screen is changed when it is half changed. It says "restart
 * the game" on the row because it means it: both screens are patched by repointing the engine's
 * own widget table once while the game starts, and nothing here can put such a table back.
 */
#ifndef DEV_OVERLAY_OVERLAY_MENU_EXTRAS_H
#define DEV_OVERLAY_OVERLAY_MENU_EXTRAS_H

#include "overlay_model.h"

#include <stdbool.h>
#include <stdint.h>

/* The slots: the switch, the fold's summary, then its lines only while it is open. */
#define OVERLAY_MENU_EXTRAS_SWITCH_SLOT  0u
#define OVERLAY_MENU_EXTRAS_SUMMARY_SLOT 1u
#define OVERLAY_MENU_EXTRAS_LINE_FIRST   2u

/* Each line fits the panel on its own; the two that would not are a line plus an indented
 * continuation, the way the free camera's are. */
#define OVERLAY_MENU_EXTRAS_LINE_COUNT 7u
#define OVERLAY_MENU_EXTRAS_ROWS_MAX \
    (OVERLAY_MENU_EXTRAS_LINE_FIRST + OVERLAY_MENU_EXTRAS_LINE_COUNT)

/* How many rows the group draws right now: two, or two and the lines. */
uint32_t overlay_menu_extras_row_count(void);

/* Folds the lines, so the panel opens the way the groups do: folded. */
void overlay_menu_extras_reset(void);

/* Fills everything about one row except `group` and `id`, which belong to the caller's
 * numbering. */
void overlay_menu_extras_row(uint32_t slot, overlay_row_t *out);

/* Flips the switch, or opens and shuts the fold. False for a line, which is a note and never
 * acts, and for a failed write. */
bool overlay_menu_extras_toggle(uint32_t slot);

#endif /* DEV_OVERLAY_OVERLAY_MENU_EXTRAS_H */
