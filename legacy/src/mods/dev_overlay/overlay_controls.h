/* overlay_controls.h: the panel's OpenPhantom group for the control scheme, "Enhanced input".
 *
 * A group of its own for the reason Window mode and Frame rate became one: these rows answer
 * one question a player arrives with, how to play this on a pad or a mouse, and among two dozen
 * settings in Utilities they read as unrelated switches with the game's own names on them.
 * Together under one heading, with the one-click switch at the top, they read as the scheme they
 * are.
 *
 * Every row writes the [enhanced_input] section of the settings file and none calls the DLL that
 * reads it, so the group works with enhanced_input.dll deleted and a choice takes effect on that
 * DLL's next once-a-second read. The rows themselves live in their own files, one setting each;
 * this file is only the order they are drawn in and the ways one is acted on.
 */
#ifndef DEV_OVERLAY_OVERLAY_CONTROLS_H
#define DEV_OVERLAY_OVERLAY_CONTROLS_H

#include "overlay_model.h"

#include <stdbool.h>
#include <stdint.h>

/* The one-click switch, free look, strafe, the two built on free look, the mouse speed with its
 * slider, and a fold that says what each of them does, in the slot after the slider so that its
 * lines are the last rows of the group and a slot stays its id whether or not it is open. */
#define OVERLAY_CONTROLS_FIXED_ROWS  8u
#define OVERLAY_CONTROLS_SUMMARY_SLOT 7u

/* Each row of the scheme gets a line and one or two continuations, since what a row does is a
 * sentence wider than the panel, and the mouse speed gets one line. */
#define OVERLAY_CONTROLS_LINE_COUNT 13u
#define OVERLAY_CONTROLS_ROWS_MAX   (OVERLAY_CONTROLS_FIXED_ROWS + OVERLAY_CONTROLS_LINE_COUNT)

/* How many rows the group draws right now: the eight, or the eight and the lines. */
uint32_t overlay_controls_row_count(void);

/* Folds the lines, so the panel opens the way the groups do: folded. */
void overlay_controls_reset(void);

/* Fills everything about one row except `group` and `id`, which belong to the caller's numbering.
 * `editing_text` is what has been typed so far when this row is the one being typed into, and
 * NULL otherwise. */
void overlay_controls_row(uint32_t slot, const char *editing_text, overlay_row_t *out);

/* Flips a switch, or opens and shuts the fold. False when the slot is neither, the row is
 * unavailable, or the file could not be written, and in every one of those the caller leaves the
 * row where it was. */
bool overlay_controls_toggle(uint32_t slot);

/* Commits typed text to the mouse speed. False when the slot is not that row or the text is not a
 * number, which leaves the setting alone instead of writing a zero. */
bool overlay_controls_commit(uint32_t slot, const char *text);

/* Drags the mouse speed's slider to `fraction`, 0 to 1. False for any other slot or a failed
 * write. Never at the full rate: its effect is felt on the next turn of the mouse, not seen
 * under the hand the way the field of view is. */
bool overlay_controls_slider_set(uint32_t slot, float fraction);

#endif /* DEV_OVERLAY_OVERLAY_CONTROLS_H */
