/* controls_layout.h: where the controls screen's added group goes, and the drawn footprints the
 * numbers are checked against. Shared by input_menu.c, which lays the group out from these, and
 * by the menu_patcher unit test, which checks the patcher lays it out here: one set of numbers,
 * so a change in either place is a change in both.
 *
 * ---- where the space is -----------------------------------------------------------------------
 *
 * Authored rectangles:
 *     title    382, 19,235, 67   buttons  7,175,199,53 and 7,277,199,53
 *     BACK     484,400,106, 67   hidden 170,140,300,200  (PIC 5 and TEXT 6, both visible = 0)
 *
 * and `controls.bmp` is opaque over rows 19..85, 116..374 and 399..465 within its two column
 * bands.
 * The one genuinely empty region, verified transparent over the whole rectangle, is
 * x 281..639, y 86..398, which is 359x313.
 *
 * The hidden pair is the one thing this group overlaps, and it is named rather than designed
 * around: it is the screen's own message plate, it ships invisible, and the two buttons here open
 * sub-screens instead of raising a box. If some build does raise it, it draws over these widgets
 * for as long as it is up and nothing is damaged.
 *
 * ---- what each widget really occupies, which is not what its rectangle says --------------------
 *
 * This difference is the whole reason the previous compile-time checks were worthless:
 *
 *   SLIDER    exactly slgauge.bmp, 250x50, from its own (x,y). Rewritten from that bitmap on every
 *             screen open, so the width passed below is a declaration of intent and nothing more.
 *   CHECKBOX  a 34x34 box at (x,y), chkbxoff.bmp, plus a caption at x + 34 + 4 that is ALWAYS
 *             200 wide. So a box's right edge is x + 238, and ONLY the 34x34 square is clickable:
 *             the words beside it are not a hit target however wide the rectangle claims.
 *   TEXT      the rectangle is used as given. */
#ifndef ENHANCED_INPUT_CONTROLS_LAYOUT_H
#define ENHANCED_INPUT_CONTROLS_LAYOUT_H

#define FREE_REGION_X       281
#define FREE_REGION_Y        86
#define FREE_REGION_RIGHT   639
#define FREE_REGION_BOTTOM  398

#define GROUP_X             330
#define SLIDER_X            330
#define SLIDER_Y             96
#define SLIDER_LABEL_Y      148
#define SLIDER_LABEL_HEIGHT  40
#define CHECKBOX_Y_STRAFE   192
#define CHECKBOX_Y_FREE_LOOK 246

/* Declared intent. The engine rewrites a slider's pair from slgauge.bmp and a check box's from
 * chkbxoff.bmp; of a check box's four rect fields only HEIGHT is read, and only by its caption. */
#define CHECKBOX_WIDTH      255
#define CHECKBOX_HEIGHT      50
#define SLIDER_WIDTH        255
#define SLIDER_HEIGHT        50

/* The drawn footprints, the only thing the assertions below are allowed to reason about. */
#define GAUGE_WIDTH         250   /* slgauge.bmp  */
#define GAUGE_HEIGHT         50
#define CHECKBOX_BOX_SIZE    34   /* chkbxoff.bmp */
#define CHECKBOX_LABEL_GAP    4
#define CHECKBOX_LABEL_WIDTH 200  /* forced by the toolkit, whatever the rectangle says */
#define CHECKBOX_DRAWN_WIDTH (CHECKBOX_BOX_SIZE + CHECKBOX_LABEL_GAP + CHECKBOX_LABEL_WIDTH)

/* These assert the real footprint. The previous set checked CHECKBOX_WIDTH (255) and SLIDER_WIDTH
 * (255) against a plate rectangle; two numbers the engine discards, against a plate that was
 * never drawn where it was asked for. An assertion that is neither the constraint nor an
 * approximation of it is worse than none, because it reads as a guarantee. */
_Static_assert(SLIDER_X >= FREE_REGION_X && SLIDER_X + GAUGE_WIDTH - 1 <= FREE_REGION_RIGHT,
               "the mouse-speed slider leaves the empty region horizontally");
_Static_assert(GROUP_X >= FREE_REGION_X &&
               GROUP_X + CHECKBOX_DRAWN_WIDTH - 1 <= FREE_REGION_RIGHT,
               "a check box and its 200-wide caption leave the empty region horizontally");
_Static_assert(SLIDER_Y >= FREE_REGION_Y &&
               CHECKBOX_Y_FREE_LOOK + CHECKBOX_BOX_SIZE - 1 <= FREE_REGION_BOTTOM,
               "the controls group leaves the empty region vertically");
/* The rows must not overlap: the slider's own 50 pixels, its caption, then two boxes whose
 * captions are as tall as the rectangle they are given. */
_Static_assert(SLIDER_Y + GAUGE_HEIGHT <= SLIDER_LABEL_Y &&
               SLIDER_LABEL_Y + SLIDER_LABEL_HEIGHT <= CHECKBOX_Y_STRAFE &&
               CHECKBOX_Y_STRAFE + CHECKBOX_HEIGHT <= CHECKBOX_Y_FREE_LOOK,
               "the controls group's rows overlap each other");
/* The authored BACK button is the one widget the group could still reach. */
_Static_assert(CHECKBOX_Y_FREE_LOOK + CHECKBOX_HEIGHT <= 400,
               "the controls group reaches BACK's row");

#endif /* ENHANCED_INPUT_CONTROLS_LAYOUT_H */
