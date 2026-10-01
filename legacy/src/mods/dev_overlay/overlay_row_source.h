/* overlay_row_source.h: which source builds a row of the panel, and what it is handed.
 *
 * Every group on both tabs offers the same two answers, how many rows it has and what one of them
 * looks like, and nothing else about them is alike: one counts a level's actors, one counts a
 * display's modes, one is a fixed list of switches. This is where that difference is absorbed, so
 * that the model above it can walk a tab without knowing which group it is walking.
 *
 * It holds no state. What a row shows comes from the group that owns it, and the one thing a row
 * cannot work out for itself, whether it is the row being typed into or the key row waiting for a
 * press, is asked of overlay_edit.c here rather than passed down from the model.
 */
#ifndef DEV_OVERLAY_OVERLAY_ROW_SOURCE_H
#define DEV_OVERLAY_OVERLAY_ROW_SOURCE_H

#include "overlay_model.h"
#include "overlay_number.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* How many rows this source offers right now. Asked once per rebuild, because several of them
 * count something of the world to answer. */
uint32_t overlay_row_source_count(overlay_group_t group);

/* Row `id` of that source, filled in whole. An id past the end answers as an empty unavailable
 * row rather than as whatever was in the caller's buffer, so a mistake here reads as a blank line
 * and never as another group's row. */
void overlay_row_source_fill(overlay_group_t group, uint32_t id, overlay_row_t *out);

/* The track on `row`, asked of the group that owns it: dragged to a fraction, read at one,
 * and its own numbers. The caller has already established that the row is a track and can be
 * used; these answer false for a group that offers no track at all.
 *
 * They sit here and not in the model because they are the same per-group switch this file was
 * made of, and the model held no state for them. overlay_model.h is what the panel calls. */
bool overlay_row_source_slider_set(const overlay_row_t *row, float fraction);
bool overlay_row_source_slider_value(const overlay_row_t *row, float fraction, char *out,
                                     size_t size);
bool overlay_row_source_slider_limits(const overlay_row_t *row, overlay_number_t *out);

#endif /* DEV_OVERLAY_OVERLAY_ROW_SOURCE_H */
