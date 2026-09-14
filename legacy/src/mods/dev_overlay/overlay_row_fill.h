/* overlay_row_fill.h: the four things every settings group does to a row before it says anything
 * of its own.
 *
 * Written once because a difference between groups in any of them is the kind of thing a reader
 * notices and cannot explain: a typed row that shows its cursor one way here and another way
 * there, or a slider whose handle sits past the end of its track in one group only. Three groups
 * share these, Utilities, Enhanced resolution and Enhanced input, and none of them is the panel's
 * state, so none of them belongs in overlay_model.c. */
#ifndef DEV_OVERLAY_OVERLAY_ROW_FILL_H
#define DEV_OVERLAY_OVERLAY_ROW_FILL_H

#include "overlay_model.h"

#include <stddef.h>

/* A switch that is on nothing and available, with no value and no text: the state every row in a
 * settings group starts from. Every row there edits a settings file and never reaches into the
 * running game, so unlike the cheats group none of them can be unavailable for want of a resolved
 * site; a row that turns out to be unavailable says so itself afterwards. */
void overlay_row_defaults(overlay_row_t *out);

/* Copies a label, truncated to the row's own width and always terminated. NULL reads as empty. */
void overlay_row_label(char *out, const char *text);

/* A typed row shows either what is being typed, with a cursor, or the stored value through the
 * row's own formatter. */
void overlay_row_typed(overlay_row_t *out, const char *editing_text,
                       void (*format)(float, char *, size_t), float value);

/* A handle belongs on its track. A value outside the slider's own ends is left honest on the row
 * above, because a setting typed into the file should read as what it is, but a fraction outside
 * 0 to 1 would draw the handle past the end of the track and read as a broken slider, not as
 * a value off the scale. */
void overlay_row_clamp_fraction(overlay_row_t *out);

#endif /* DEV_OVERLAY_OVERLAY_ROW_FILL_H */
