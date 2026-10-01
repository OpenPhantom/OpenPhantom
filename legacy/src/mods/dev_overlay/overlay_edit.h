/* overlay_edit.h: the one row of the panel that is being edited, and nothing else.
 *
 * Two rows of this panel can be mid-something: a key row waiting for the next keypress, and a
 * number being typed into. At most one at a time, which is the whole reason they are one module:
 * a click that starts either of them ends the other, and the panel's keyboard handling redirects
 * the next key to whichever is running instead of doing its usual thing with it.
 *
 * It sits below the model rather than inside it. The model asks it what is being typed while it
 * builds a row, and the rest of the panel reaches it through the model's own published names,
 * which are declared in overlay_model.h and defined here beside the state they act on. That is
 * the seam overlay_model.c's own size note named years before it was taken: every group goes
 * through this, and none of the model's navigation, search or folding does.
 *
 * Which group a finished edit belongs to is decided here too, by the id the row carries, because
 * an id is the only thing either of these remembers a row by. The bases are in overlay_row_ids.h
 * and the tests are what hold a group to its own block of them.
 */
#ifndef DEV_OVERLAY_OVERLAY_EDIT_H
#define DEV_OVERLAY_OVERLAY_EDIT_H

#include <stdbool.h>
#include <stdint.h>

/* Ends whatever is running, keeping nothing. What the panel does when it closes: a capture that
 * survived would swallow the next key pressed in the game, and a half typed number would come
 * back later with digits in it that belong to a minute ago. */
void overlay_edit_forget(void);

/* Arms a key row, or a typed row, by the id of the row that was clicked. Starting either ends
 * the other, and starting a typed row discards anything left in the field, which is the "click
 * it again to start over" shape both rows have. */
void overlay_edit_start_capture(uint32_t row_id);
void overlay_edit_start_value(uint32_t row_id);

/* What is being typed into `row_id`, or NULL when that is not the row. The model hands this to
 * the group building the row, which shows it with a cursor instead of the stored number. */
const char *overlay_edit_text_for(uint32_t row_id);

/* Whether `row_id` is the key row waiting for a press, which that row draws as "...". */
bool overlay_edit_capturing_row(uint32_t row_id);

#endif /* DEV_OVERLAY_OVERLAY_EDIT_H */
