/* overlay_choice.h: a choice drawn as a choice.
 *
 * Both of the panel's choice kinds are served from here. OVERLAY_ROW_CHOICE is one entry of a
 * list that is open on screen; it was drawn as a switch, so five shapes of window read as five
 * switches with one of them on rather than as one shape picked out of five, and the green chip
 * said "this is switched on" about a row nobody can switch off. OVERLAY_ROW_SEGMENT is the whole
 * choice on one row, for a choice short enough to show entire.
 *
 * What is here and what is not. The words of a segment row belong to the source that built it,
 * not to the row: `value` holds sixteen characters and the four behaviours of a spawned entity
 * need twenty four. So the row carries the chosen INDEX and this file asks the source for the
 * words. Nothing here draws and nothing here measures a font; the drawing hands its own
 * measurements in and gets the arithmetic back, the same shape overlay_kit.c has for a track,
 * and for the same reason: the paint and the hit test have to agree about where a box ends and
 * they are in two files.
 */
#ifndef DEV_OVERLAY_OVERLAY_CHOICE_H
#define DEV_OVERLAY_OVERLAY_CHOICE_H

#include "overlay_model.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Four, which is what a row can show beside its name and still be read at 640x480 with the dev
 * menu size turned up. A choice with more entries is a list and gets OVERLAY_ROW_CHOICE rows. */
#define OVERLAY_CHOICE_SEGMENTS_MAX 4u

/* The words a SEGMENT row is drawn with, in order, and how many there are. 0 for any other row,
 * and for a segment row whose source has nothing to offer. The strings belong to the source and
 * outlive the call. */
uint32_t overlay_choice_segments(const overlay_row_t *row, const char **out, uint32_t max);

/* The strip as ONE string, spelled the way it is drawn: every word with a space on each side and
 * the boxes abutting. Both measurements read it, the panel's width and the check that every row
 * fits in it, so what is measured is what is painted. False for a row with no segments.
 *
 * It exists because the width check measured `value`, which a segment row leaves empty, and would
 * therefore have counted the widest row of the spawner group as costing nothing but its name. */
bool overlay_choice_strip(const overlay_row_t *row, char *out, size_t size);

/* What a SEGMENT row costs the panel in characters: its name and its strip side by side. 0 for a
 * row with no segments, which is what tells the width check to measure the row the ordinary
 * way. */
uint32_t overlay_choice_row_width(const overlay_row_t *row);

/* Whether `index` is the chosen segment of that row. False for a row with no segments and for an
 * index past the end, so nothing outside a list can read as chosen. */
bool overlay_choice_is_chosen(const overlay_row_t *row, uint32_t index);

/* Whether the row is DRAWN as a strip of words rather than with a chip: a segment row that can be
 * used. One that cannot reads its reason where the words would stand, so there are no words to
 * paint or to make room for. The paint and the panel's width both ask here; they were the same
 * conditional written twice.
 *
 * The width CHECK in unittests/overlay_model.c deliberately does not ask: it measures a segment
 * row by its words whether or not the row can be used, which is the safe direction to be wrong in
 * and which keeps that check from falling silent in a process where nothing resolves. */
bool overlay_choice_is_strip(const overlay_row_t *row);

/* The name of the entry a choice has settled on, as a band word: a list entry's label without the
 * indent it carries on screen, or the chosen word of a row of segments. Cut with the panel's own
 * two dots when it is longer than `out`, because what it has to fit is a row's `value` and nothing
 * measures a font where this is read.
 *
 * Both choice kinds answer here so that a heading's word is one rule and not one rule per kind.
 * False for a row that is no choice, for an entry that is not the chosen one, and for an empty
 * name, so a list with nothing chosen contributes nothing.
 *
 * A row of words answers only while it can be USED, through overlay_choice_is_strip() above. A
 * strip nobody can act on shows a reason where its words would be, and the band read the word
 * regardless: the heading of the entity spawner said `Stand` over a behaviour row the panel had
 * greyed out because a pickup was chosen, which ignores the behaviour entirely. */
bool overlay_choice_chosen_name(const overlay_row_t *row, char *out, size_t size);

/* Picks segment `index`. False for a row with no segments, a row that cannot be used, an index
 * past the end, or a write that did not land, and in each of those the caller leaves the row
 * where it was. */
bool overlay_choice_pick(const overlay_row_t *row, uint32_t index);

/* The next segment along from the chosen one, `by` steps, clamped at both ends. -1 for a row with
 * no segments and for a step that would leave the strip, which is what lets the caller treat the
 * end of the strip as "this key means something else now". */
int32_t overlay_choice_step(const overlay_row_t *row, int32_t by);

/* Where the boxes of a strip of `count` segments sit, right aligned to end at `x1`: `count + 1`
 * edges, so box i runs from out[i] to out[i + 1]. Each box is its own word plus `pad` at each
 * side. Answers how many edges were written, 0 for a count past `max - 1` or a NULL.
 *
 * The widths are the caller's, because only the drawing has a font; the arithmetic is here,
 * because the paint and the hit test below both need it and a second copy of it is how a click
 * lands on the box beside the one under the pointer. */
uint32_t overlay_choice_edges(const float *word_widths, uint32_t count, float pad, float x1,
                              float *out, uint32_t max);

/* Which box of that strip `x` falls in. -1 for a point past either end, and for fewer than two
 * edges. */
int32_t overlay_choice_hit(const float *edges, uint32_t count, float x);

#endif /* DEV_OVERLAY_OVERLAY_CHOICE_H */
