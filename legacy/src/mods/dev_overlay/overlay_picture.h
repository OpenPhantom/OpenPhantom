/* overlay_picture.h: the panel's OpenPhantom group for the picture, "Enhanced resolution".
 *
 * A group of its own for the reason Window mode and Frame rate became one: these rows answer one
 * question a player arrives with, what the picture looks like, and they were the front half of
 * Utilities until the settings there outnumbered everything else. The draw distance and its two
 * gates, the field of view and the subtitle size are one subject under one heading; the fog has
 * a heading of its own directly under this one, and Utilities keeps what is left: the panel's own
 * size, its key and the game's menus.
 *
 * Every row writes a settings file and none calls the DLL that reads it: view_distance_fix owns
 * the draw distance, variable_fov the field of view, enhanced_resolution the subtitle size, and
 * each re-reads its keys about once a second. The rows themselves live in their own files, one
 * setting each; this file is the order they are drawn in and the four ways one is acted on.
 */
#ifndef DEV_OVERLAY_OVERLAY_PICTURE_H
#define DEV_OVERLAY_OVERLAY_PICTURE_H

#include "overlay_model.h"

#include <stdbool.h>
#include <stdint.h>

/* The draw distance, its slider, the number in force, its two gates; the field of view and its
 * slider; the subtitle size and its slider. */
#define OVERLAY_PICTURE_ROW_COUNT 9u

/* Fills everything about one row except `group` and `id`, which belong to the caller's numbering.
 * `editing_text` is what has been typed so far when this row is the one being typed into, and
 * NULL otherwise. */
void overlay_picture_row(uint32_t slot, const char *editing_text, overlay_row_t *out);

/* Flips a switch. False when the slot is not a switch, the row is unavailable, or the file could
 * not be written, and in every one of those the caller leaves the row where it was. */
bool overlay_picture_toggle(uint32_t slot);

/* Commits typed text to a value row. False when the slot is not a value row or the text is not a
 * number, which leaves the setting alone instead of writing a zero. */
bool overlay_picture_commit(uint32_t slot, const char *text);

/* Drags the slot's slider to `fraction`, 0 to 1. False when the slot has no slider or the write
 * failed. Three slots have one, the settings whose whole range is worth sweeping through to find
 * a number. Every one of them but the field of view writes on a hundredth grid, for the reason
 * recorded at the first of them in overlay_picture.c. */
bool overlay_picture_slider_set(uint32_t slot, float fraction);

/* Whether a drag on this slider should reach the file at the full rate the panel allows. The
 * field of view is the one that does: its whole effect is the picture zooming under the hand, and
 * at a few writes a second that zoom is a series of steps. */
bool overlay_picture_slider_wants_full_rate(uint32_t slot);

#endif /* DEV_OVERLAY_OVERLAY_PICTURE_H */
