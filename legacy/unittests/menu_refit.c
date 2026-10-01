/* menu_refit.c: why a screen is refitted from the rectangle it was AUTHORED with.
 *
 * The refit itself is engine writes and cannot be run without a game. What can be run is the
 * rounding it goes through, and the question that decides the design: given a screen laid out for
 * one canvas, can the canvas it was authored for be recovered by dividing, or does it have to be
 * kept?
 *
 * The answer differs by widget, which is the whole finding. For a rectangle nothing has touched
 * since it was scaled, dividing recovers it exactly. For a list box it does not, because the engine
 * rewrites the box's own height when the screen is opened, and what it rewrites is not the scaled
 * authored height. So the value dividing recovers is not the authored one, and every further change
 * of resolution divides a number that has already moved.
 */
#include "unittest.h"

#include "menu_art_load.h"
#include "menu_scale.h"
#include "menu_scale_internal.h"
#include "menu_scale_sites.h"
#include "pointer_cage.h"

#include <stdint.h>

/* The ratios a reader actually walks through: 1440p, 4K vertically, a 1080 tall window, the
 * converted set's own 4.5, and 1280x960. */
static const float RATIOS[] = { 2.25f, 3.0f, 1.6875f, 4.5f, 2.0f };
#define RATIO_COUNT (sizeof RATIOS / sizeof RATIOS[0])

/* ==============================================================================================
 * The engine's own arithmetic, from swlistbx_input's SWMSG_RESET arm at 0x0045C9A7:
 *
 *     pLBox->lineHeight = (font < 17) ? 16 : font3d_queryFont();
 *     pLBox->numLines   = (pWidget->rect.height - 3) / pLBox->lineHeight;
 *     pWidget->rect.height = pLBox->numLines * pLBox->lineHeight + 6;
 *
 * The floor and the font are both scaled by the canvas, so the row height is the authored 16 times
 * the ratio. The height the engine leaves behind is a whole number of rows plus six, so it is not
 * the scaled authored height and cannot be divided back into one. */
static int32_t engine_row_height(float ratio)
{
    return scaled_coordinate(16, ratio);
}

static int32_t engine_reset_height(int32_t box_height, float ratio, int32_t *out_rows)
{
    int32_t row_height = engine_row_height(ratio);
    int32_t rows       = (box_height - 3) / row_height;

    if (out_rows != NULL) {
        *out_rows = rows;
    }
    return rows * row_height + 6;
}

/* ============================================================================================ */
static void test_a_plain_rectangle_survives_dividing(void)
{
    bool    every_one = true;
    int32_t value;
    size_t  index;

    for (index = 0; index < RATIO_COUNT; ++index) {
        for (value = 0; value <= MENU_SCALE_CANVAS_WIDTH; ++value) {
            int32_t there = scaled_coordinate(value, RATIOS[index]);

            if (unscaled_coordinate(there, RATIOS[index]) != value) {
                every_one = false;
            }
        }
    }

    ut_check(every_one,
          "a rectangle scaled up and divided back comes out exactly where it started, for every "
          "authored coordinate on the canvas and every ratio above. Past a ratio of 1 the map from "
          "authored to scaled never puts two authored values on one scaled value, so nothing is "
          "lost on the way and the fallback for a menu with no shadow is sound as far as it goes");
}

static void test_the_authored_size_walks_away_from_a_list_box(void)
{
    const int32_t authored = 100;
    int32_t       believed = authored;
    size_t        index;
    bool          moved = false;

    for (index = 0; index < RATIO_COUNT; ++index) {
        int32_t after = engine_reset_height(scaled_coordinate(believed, RATIOS[index]),
                                            RATIOS[index], NULL);

        believed = unscaled_coordinate(after, RATIOS[index]);
        if (believed != authored) {
            moved = true;
        }
    }

    ut_check(moved && believed == 83,
          "a list box authored 100 tall, refitted through those five canvases and dividing its way "
          "back each time, ends up believing it was authored 83. The engine's reset leaves the box "
          "a whole number of rows plus six pixels, which is not the scaled authored height, so "
          "dividing recovers a number that was never the authored one and the error is carried "
          "into the next change. It is not even monotonic: the third canvas lands back on 100 "
          "exactly, which is the kind of accident that makes this look fine in a short test");
}

static void test_the_drift_eventually_costs_a_row(void)
{
    const int32_t authored = 100;
    int32_t       believed = authored;
    int32_t       kept_rows = 0;
    int32_t       divided_rows = 0;
    size_t        index;

    for (index = 0; index < RATIO_COUNT; ++index) {
        int32_t divided_height;

        (void)engine_reset_height(scaled_coordinate(authored, RATIOS[index]), RATIOS[index],
                                  &kept_rows);
        divided_height = engine_reset_height(scaled_coordinate(believed, RATIOS[index]),
                                             RATIOS[index], &divided_rows);
        believed = unscaled_coordinate(divided_height, RATIOS[index]);
    }

    ut_check(kept_rows == 6 && divided_rows == 5,
          "and by the fifth change that is a whole row: the box holds six names when the refit "
          "scales from the height the screen was authored with, and five when it divides its way "
          "back to one. A resolution list that quietly loses a row per few changes is the reason "
          "the shadow keeps the authored rectangle rather than only the last one written");
}

static void test_the_canvas_rounds_the_same_way_the_cells_do(void)
{
    bool   every_one = true;
    size_t index;

    for (index = 0; index < RATIO_COUNT; ++index) {
        int32_t width  = (int32_t)((float)MENU_SCALE_CANVAS_WIDTH  * RATIOS[index] + 0.5f);
        int32_t height = (int32_t)((float)MENU_SCALE_CANVAS_HEIGHT * RATIOS[index] + 0.5f);

        if (width != scaled_coordinate(MENU_SCALE_CANVAS_WIDTH, RATIOS[index]) ||
            height != scaled_coordinate(MENU_SCALE_CANVAS_HEIGHT, RATIOS[index])) {
            every_one = false;
        }
    }

    ut_check(every_one,
          "the canvas the clip is written with is the authored canvas through the same rounding "
          "the rectangles go through, at every ratio above. If the two ever parted, the widget at "
          "the far edge of a screen would be clipped by a pixel the layout thinks is inside");
}

static void test_the_ceiling_is_where_the_encoder_gives_out(void)
{
    ut_check(scaled_coordinate(MENU_SCALE_CANVAS_WIDTH, MENU_SCALE_MAX_RATIO) == 4095,
          "the largest canvas allowed is exactly 4095 wide, which is the widest run the length "
          "encoder can carry in twelve bits. A refit takes its ratio from whatever mode the engine "
          "opened, so a display past this has to be clamped rather than refused");
    ut_check(MENU_SCALE_MIN_RATIO == 1.0f,
          "and the floor is 1, so a refit down to the authored canvas is the smallest it can ask "
          "for. Below it the artwork would have to be shrunk, which drops source pixels");
}

/* ==============================================================================================
 * The stand down is a change of canvas too.
 *
 * A canvas that no longer fits the display is given up: the clip, the origin and the glyph scale go
 * back to 640x480. The artwork has to go back with them. The load path replicates every picture to
 * the ratio it was last told, and a stand down that did not tell it would leave, with MenuScale=3
 * on a 1080 line display, every menu picture three times its size and drawn against a 640x480
 * clip: the top left ninth of it, enlarged. The engine cells are pointed at locals here;
 * the patch sites are unresolved in a test process, so the stand down's writes into the image are
 * refused and logged, and what is left to see is its own arithmetic. */
static void test_a_stand_down_takes_the_artwork_back(void)
{
    static volatile float   screen_width  = 1920.0f;
    static volatile float   screen_height = 1080.0f;
    static volatile int32_t origin_x      = 0;
    static volatile int32_t origin_y      = 0;
    static volatile float   menu_scale    = 0.0f;
    static volatile float   text_scale    = 0.0f;
    static volatile float   base_text     = 1.0f;
    float                   art_x         = 0.0f;
    float                   art_y         = 0.0f;

    menu_cells.screen_width    = &screen_width;
    menu_cells.screen_height   = &screen_height;
    menu_cells.origin_x        = &origin_x;
    menu_cells.origin_y        = &origin_y;
    menu_cells.menu_scale      = &menu_scale;
    menu_cells.menu_text_scale = &text_scale;
    menu_cells.base_text       = &base_text;

    scale_state.installed     = true;
    scale_state.stood_down    = false;
    scale_state.ratio_x       = 3.0f;
    scale_state.ratio_y       = 3.0f;
    scale_state.canvas_width  = 1920;
    scale_state.canvas_height = 1440;
    menu_art_load_set_ratio(3.0f, 3.0f);

    ut_check(!canvas_still_fits(),
          "MenuScale=3 at 1920x1080 is a 1920x1440 canvas on a 1080 line display, which does not "
          "fit and is given up");
    menu_art_load_ratio(&art_x, &art_y);
    ut_checkf(art_x == 1.0f && art_y == 1.0f,
              "and the artwork is loaded at its own size again (%.3f by %.3f): a picture "
              "replicated for the canvas just given up would be drawn against the 640x480 clip "
              "as its own top left corner, enlarged", (double)art_x, (double)art_y);
}

/* The cage is given the quad the refit last drew, and the shipped 32 before it drew any. The three
 * callers hand menu_scale_cursor_size() on; what it answers is pinned here. */
_Static_assert(DRAW_CURSOR_SHIPPED == POINTER_CAGE_SHIPPED_CURSOR,
               "the refit and the cage name the same shipped quad");

static void test_the_cage_is_told_the_drawn_cursor(void)
{
    const int32_t kept = scale_state.cursor_size;

    scale_state.cursor_size = 0;
    ut_checkf(menu_scale_cursor_size() == DRAW_CURSOR_SHIPPED,
              "before the refit drew a cursor the shipped quad is answered (%d)",
              (int)menu_scale_cursor_size());
    scale_state.cursor_size = 72;
    ut_checkf(menu_scale_cursor_size() == 72,
              "after it drew a 72 pixel quad at 2.25 the cage is told 72 (%d)",
              (int)menu_scale_cursor_size());
    scale_state.cursor_size = kept;
}

int main(void)
{
    ut_section("recovering the size a screen was authored at");
    test_a_plain_rectangle_survives_dividing();
    test_the_authored_size_walks_away_from_a_list_box();
    test_the_drift_eventually_costs_a_row();

    ut_section("the canvas a ratio gives");
    test_the_canvas_rounds_the_same_way_the_cells_do();
    test_the_ceiling_is_where_the_encoder_gives_out();

    ut_section("the stand down");
    test_a_stand_down_takes_the_artwork_back();

    ut_section("the cursor the cage is told");
    test_the_cage_is_told_the_drawn_cursor();

    return ut_summary("menu_refit");
}
