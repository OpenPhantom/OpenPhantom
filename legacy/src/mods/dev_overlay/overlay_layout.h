/* overlay_layout.h: where every part of the panel sits.
 *
 * Split from the drawing because both the paint and the pointer need the same answers: what was
 * drawn and what can be clicked must never be a frame apart. Nothing here draws.
 *
 * ==============================================================================================
 * EVERYTHING IS A MULTIPLE OF H
 *
 * H is the height of a capital in the font the engine actually has. The engine keeps that at about
 * sixteen pixels whatever the display does, so a panel sized in H is sized in pixels, and that is
 * right: it should be as large as its text needs, not a fraction of a display it knows nothing
 * about. It fills most of a 640 wide screen and a fifth of a 1920 wide one, and both are correct.
 *
 * If the whole panel is ever too tight or too loose, the knob is the single multiplier on the
 * measured height, not thirty constants.
 *
 * ==============================================================================================
 * TWO RULES THAT ARE NOT TASTE
 *
 * SEPARATION IS CONTRAST, NOT DISTANCE. There are no gaps between bands. A few pixels between two
 * regions of the same colour is invisible and costs height for nothing, while a one pixel rule in a
 * distinctly lighter colour reads at once. So every separator is the last pixel row OF the band
 * above it, which also keeps the band arithmetic exact.
 *
 * EVERY ROW IS THE SAME HEIGHT, HEADINGS INCLUDED. The hit test finds a row with one division, so a
 * taller heading would put the paint and the pointer a row apart. A heading takes its weight from a
 * fill and an accent bar instead.
 */
#ifndef OVERLAY_LAYOUT_H
#define OVERLAY_LAYOUT_H

#include <stdbool.h>
#include <stdint.h>

/* The layout stores where each tab ended up, because a tab is as wide as its own word rather than
 * half the panel. A four character word inside a twelve character box has no visible relationship
 * to the word that names it, which is why the tabs did not read as tabs. */
#define OVERLAY_LAYOUT_TABS 2u

typedef struct layout {
    float text_h;          /* H, the measured height of a capital                        */
    float rule;            /* one pixel at the normal font size, never less than one      */
    float left;
    float top;
    float width;
    float height;

    float cap_h;           /* the accent bar that gives the title its weight              */
    float title_h;         /* the bottom of the title band, panel relative                */
    float tabs_top;
    float tab_h;
    float search_top;
    float search_h;
    float rows_top;
    float row_h;

    /* HOW MANY ROWS ARE ACTUALLY DRAWN, which is not how many the tab has. Enough rows, a large
     * DevMenuSize or a short screen and the list is taller than the display; the panel used to be
     * pushed up until its top hit zero and then simply ran off the bottom, with no clipping and
     * nothing to say the rows were there. This is the number that fits, and everything past it is
     * reached by scrolling. */
    uint32_t visible_rows;

    float tab_x0[OVERLAY_LAYOUT_TABS];
    float tab_w[OVERLAY_LAYOUT_TABS];
} layout_t;

/* Rebuilds and remembers.
 *
 * `content_width` is the widest NAME the current tab can ever show, measured over all its rows and
 * ignoring both the search and the folds. That is deliberate: measured over the rows actually
 * visible, the panel would change width on every keystroke and every fold, and a panel that is also
 * positioned would then move the row out from under the pointer while it is being typed at.
 *
 * `tab_widths` is one measured word width per tab; the padding is added here. */
void overlay_layout_build(float text_height, float content_width, uint32_t row_count,
                          const float *tab_widths, float screen_width, float screen_height);

const layout_t *overlay_layout(void);

#endif /* OVERLAY_LAYOUT_H */
