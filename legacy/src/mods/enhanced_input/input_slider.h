#ifndef INPUT_SLIDER_H
#define INPUT_SLIDER_H

#include "common/engine_types.h"
#include "common/menu_patcher.h"

#include <stdbool.h>
#include <stdint.h>

/* The mouse sensitivity slider on the controls screen, and the caption flush beneath it.
 *
 * Split out of input_menu.c when that file reached the 900-line limit, along the seam that file had
 * already named: a slider carries a NOTCH rather than a bool, its caption changes as it moves, and
 * it needs a second widget of a different class to show that caption. None of that is shared with a
 * check box beyond the patch context both append into.
 *
 * The LAYOUT is deliberately NOT owned here. It is passed in, so that the screen's group, plate,
 * two check boxes, slider, caption, has exactly one owner and one place where a rectangle can be
 * checked against the plate it has to fit on.
 */

typedef struct input_slider_layout {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    int32_t label_y;              /* flush beneath the slider, as the authored gamma pair is */
    int32_t label_height;
    int32_t slider_id;
    int32_t label_id;
    int32_t knob_bitmap;          /* indices into the SCREEN's own bitmap-name table */
    int32_t gauge_bitmap;
    int32_t font_index;
} input_slider_layout_t;

/* The localised caption, as a format string with exactly one `%d`, the setting in thousandths of
 * a degree per count. Copied, so the caller's buffer need not outlive the call. Call it before
 * appending: the caption buffer has to hold text by the time the label widget points at it. */
void input_slider_set_caption_format(const char *format);

/* Appends the slider and its caption to `patch`, recording where each landed in `widgets`.
 *
 * Returns false only when the SLIDER could not be appended, a caption without its control would
 * be a line of text stating a number nobody can change. The reverse is survivable and is reported
 * as a warning: a slider whose caption failed still moves, saves and takes effect. */
bool input_slider_append(menu_patch_context_t *patch, sw_widget_t *widgets,
                         const input_slider_layout_t *layout);

/* Call after menu_patcher_commit has succeeded. Until then the engine has not seen the array, so a
 * notch read back from it would be read from a widget that is not on any screen. */
void input_slider_commit(void);

/* On screen open: put the widget in step with the setting that is actually in force. */
void input_slider_seed(void);

/* The single apply path, used by the per-frame poll and by the read-back when the screen closes, so
 * the two can never disagree. `force` skips the "did the notch actually move" test. */
void input_slider_apply(bool force);

bool input_slider_is_present(void);
int  input_slider_notch(void);
int  input_slider_seed_notch(void);

/* For the installation log: where it landed and what it reads. */
const char *input_slider_caption(void);

#endif /* INPUT_SLIDER_H */
