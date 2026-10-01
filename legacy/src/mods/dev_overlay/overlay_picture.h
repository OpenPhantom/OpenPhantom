/* overlay_picture.h: the panel's OpenPhantom group for the picture, drawn under the heading Engine.
 *
 * A group of its own for the reason Window mode and Frame rate became one: these rows answer one
 * question a player arrives with, how the engine draws the world, and they were the front half of
 * Utilities until the settings there outnumbered everything else. The draw distance and its two
 * gates, the field of view and the subtitle size are one subject under one heading; the fog is
 * drawn under the same heading after them, and Utilities keeps what is left: the panel's own
 * size, its key and the game's menus. The heading says Engine and the group is still the picture
 * inside, because its slots and its row ids are written against that name.
 *
 * Every row writes a settings file and none calls the DLL that reads it: view_distance_fix owns
 * the draw distance, variable_fov the field of view, enhanced_resolution the subtitle size, and
 * each re-reads its keys about once a second. The rows themselves live in their own files, one
 * setting each; this file is the order they are drawn in and the four ways one is acted on.
 */
#ifndef DEV_OVERLAY_OVERLAY_PICTURE_H
#define DEV_OVERLAY_OVERLAY_PICTURE_H

#include "overlay_model.h"
#include "overlay_number.h"

#include <stdbool.h>
#include <stdint.h>

/* The draw distance, its slider, the number in force, its two gates; the field of view and its
 * slider; the subtitle size and its slider. */
#define OVERLAY_PICTURE_ROW_COUNT 9u

/* The drawn slots, in order, and the order the table in overlay_picture.c builds them in.
 *
 * It is here rather than in the .c because session_lock.c addresses four of them, and it did
 * that with the bare numbers 0, 1, 3 and 4. That left three descriptions of one order, the
 * table, this enum and those literals, and the only thing holding any two of them together
 * was their LENGTH: the _Static_assert beside the table counts the enum against the row
 * count and says nothing about which slot is which. A row swapped with the one below it
 * keeps every count in the build correct and hands a player, mid-session, a row the session
 * takes. unittests/overlay_groups.c names each of these against the row it draws.
 *
 * The draw distance first, because it is the setting a player came looking for, with a note
 * under it saying what the game is actually running and the two gates that decide who else
 * may lower it; then the field of view; then the subtitle size, the one row here that is not
 * the 3-D picture. The fog has a group of its own, drawn under this one. */
typedef enum overlay_picture_slot {
    OVERLAY_PICTURE_VIEW_RANGE = 0,
    OVERLAY_PICTURE_VIEW_RANGE_TRACK,
    OVERLAY_PICTURE_VIEW_RANGE_LIVE,
    OVERLAY_PICTURE_AUTO_RANGE,
    OVERLAY_PICTURE_STRICT_RANGE,
    OVERLAY_PICTURE_FOV,
    OVERLAY_PICTURE_FOV_TRACK,
    OVERLAY_PICTURE_SUBTITLE_SIZE,
    OVERLAY_PICTURE_SUBTITLE_SIZE_TRACK
} overlay_picture_slot_t;

_Static_assert((uint32_t)OVERLAY_PICTURE_SUBTITLE_SIZE_TRACK + 1u == OVERLAY_PICTURE_ROW_COUNT,
               "the slot enum and OVERLAY_PICTURE_ROW_COUNT have to end together, or the last "
               "row is either built and never drawn or drawn and never built");

/* What the group's table draws, so the constant above can be checked against it instead of
 * believed. The rows are a table now (overlay_kit.h), a number row is two of them, and a table's
 * length is exactly the kind of thing that stops matching a number written beside it. */
uint32_t overlay_picture_row_count(void);

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

/* What the row above that track reads with the handle at `fraction`; see overlay_kit.h. */
bool overlay_picture_slider_value(uint32_t slot, float fraction, char *out, size_t size);

/* The numbers behind that track: its ends, the two press sizes and the standard, as the
 * group's table wrote them. False for a slot that is no track. */
bool overlay_picture_slider_limits(uint32_t slot, overlay_number_t *out);

/* Whether a drag on this slider should reach the file at the full rate the panel allows. The
 * field of view is the one that does: its whole effect is the picture zooming under the hand, and
 * at a few writes a second that zoom is a series of steps. */
bool overlay_picture_slider_wants_full_rate(uint32_t slot);

#endif /* DEV_OVERLAY_OVERLAY_PICTURE_H */
