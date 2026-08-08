/* input_slider.c: the mouse sensitivity slider on the controls screen and the caption under it.
 *
 * ==============================================================================================
 * Why a slider is possible on a screen that ships none
 *
 * A slider names its track and its knob as INDICES into the screen's own bitmap-name table, not by
 * file name, so a screen whose table does not carry them cannot show one however correct the widget
 * record is. The controls screen ships no slider of its own, but it is built with the same table
 * as the video screen, and that table's entries 2 and 3 are slgauge.bmp and slslide.bmp. The gamma
 * slider's artwork is therefore already loaded here. That was checked before this file was written,
 * because the failure mode is a control that installs, logs success and draws nothing.
 *
 * ==============================================================================================
 * The caption is built from the setting, never from the notch
 *
 * The setter clamps, and it refuses a value that is not a number. A caption built from the notch
 * would therefore state a sensitivity that is not the one in force on exactly the occasions where
 * that matters. It is built from what mouse_look really holds, after the setter has run.
 *
 * The displayed number is the setting in thousandths, 0.030 shows as 30, which is the ini's own
 * number with the decimal point moved, so a player who reads the caption and then opens the ini
 * finds what they expect. Every notch is a round thousandth, so no two adjacent notches can display
 * the same value; a unit test walks all hundred of them to prove it.
 */
#include "input_slider.h"

#include "mouse_look.h"

#include "common/logging.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* The caption is not answered through swmenu_getString, a label carries its text pointer directly,
 * so it has no toolkit buffer size to respect and gets room for the longest wording plus its
 * number. */
#define MAX_SLIDER_CAPTION 48

#define DEGREES_PER_COUNT_DISPLAY_SCALE 1000.0f

typedef struct input_slider_state {
    bool         appended;
    bool         present;        /* appended AND committed, so its notch really is read back */
    bool         label_present;
    sw_widget_t *widgets;        /* the patcher's target array, borrowed */
    size_t       widget_index;
    size_t       label_index;
    int          seed_notch;
    int          last_notch;
    char         caption[MAX_SLIDER_CAPTION];
    char         format[MAX_SLIDER_CAPTION];
} input_slider_state_t;

static input_slider_state_t slider_state;

/* ============================================================================================ */
void input_slider_set_caption_format(const char *format)
{
    if (format == NULL) {
        return;
    }
    _snprintf(slider_state.format, sizeof(slider_state.format), "%s", format);
    slider_state.format[sizeof(slider_state.format) - 1] = '\0';
}

static void update_caption(void)
{
    int shown = (int)(mouse_look_degrees_per_count() * DEGREES_PER_COUNT_DISPLAY_SCALE + 0.5f);

    if (slider_state.format[0] == '\0') {
        return;
    }
    _snprintf(slider_state.caption, sizeof(slider_state.caption), slider_state.format, shown);
    slider_state.caption[sizeof(slider_state.caption) - 1] = '\0';
}

static int clamped_notch(void)
{
    int notch;

    if (slider_state.widgets == NULL) {
        return 0;
    }
    notch = (int)slider_state.widgets[slider_state.widget_index].state;
    if (notch < 0) {
        return 0;
    }
    if (notch > MOUSE_SLIDER_NOTCH_COUNT - 1) {
        return MOUSE_SLIDER_NOTCH_COUNT - 1;
    }
    return notch;
}

/* ============================================================================================ */
bool input_slider_append(menu_patch_context_t *patch, sw_widget_t *widgets,
                         const input_slider_layout_t *layout)
{
    if (patch == NULL || widgets == NULL || layout == NULL) {
        return false;
    }

    if (menu_patcher_has_widget_id(patch, layout->slider_id) ||
        menu_patcher_has_widget_id(patch, layout->label_id)) {
        log_error("widget id %d or %d is already taken on the controls screen, the mouse "
                  "sensitivity slider is not added", layout->slider_id, layout->label_id);
        return false;
    }

    slider_state.widgets    = widgets;
    slider_state.seed_notch = mouse_look_notch_from_degrees_per_count(
                                  mouse_look_degrees_per_count());
    slider_state.last_notch = slider_state.seed_notch;
    update_caption();

    slider_state.appended = menu_patcher_append_slider(patch, layout->slider_id,
                                                       MOUSE_SLIDER_NOTCH_COUNT,
                                                       layout->x, layout->y,
                                                       layout->width, layout->height,
                                                       layout->knob_bitmap, layout->gauge_bitmap,
                                                       &slider_state.widget_index);
    if (!slider_state.appended) {
        return false;
    }
    widgets[slider_state.widget_index].state = slider_state.seed_notch;

    slider_state.label_present = menu_patcher_append_label(patch, layout->label_id,
                                                           layout->x, layout->label_y,
                                                           layout->width, layout->label_height,
                                                           layout->font_index,
                                                           slider_state.caption,
                                                           &slider_state.label_index);
    if (!slider_state.label_present) {
        log_warning("the mouse sensitivity slider has no caption, it still moves, saves and takes "
                    "effect, but it no longer names the value it is set to");
    }
    return true;
}

void input_slider_commit(void)
{
    slider_state.present = slider_state.appended;
}

void input_slider_seed(void)
{
    int notch;

    if (!slider_state.present) {
        return;
    }
    notch = mouse_look_notch_from_degrees_per_count(mouse_look_degrees_per_count());

    slider_state.seed_notch = notch;
    slider_state.last_notch = notch;
    slider_state.widgets[slider_state.widget_index].state = notch;
    update_caption();
}

void input_slider_apply(bool force)
{
    int notch;

    if (!slider_state.present) {
        return;
    }

    notch = clamped_notch();
    if (!force && notch == slider_state.last_notch) {
        return;
    }
    slider_state.last_notch = notch;

    /* Saved on every change rather than once on leaving the screen: a crash or a hung graphics
     * wrapper must not swallow a setting the player has just made. The field-of-view slider saves
     * per change for the same reason, and for the same reason it is not measurable work. */
    (void)mouse_look_set_degrees_per_count(mouse_look_degrees_per_count_from_notch(notch));

    /* AFTER the setter, never before: the caption quotes the value the setter really put in force,
     * which is not necessarily the one the notch asked for. */
    update_caption();
}

bool input_slider_is_present(void)
{
    return slider_state.present;
}

int input_slider_notch(void)
{
    return slider_state.last_notch;
}

int input_slider_seed_notch(void)
{
    return slider_state.seed_notch;
}

const char *input_slider_caption(void)
{
    return slider_state.caption;
}
