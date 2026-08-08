/* fov_math.c: the field-of-view arithmetic, checked without the game.
 *
 * Two of these cases are cross-checks against retail data rather than against my own algebra:
 *   * a 4:3 canvas with the authored 46.826 degree vertical must come out at 60.000 horizontal,
 *     which is the `push 0x42700000` in bapview_newView;
 *   * a 640x480 canvas at 60 degrees must yield a focal length of 554.256, which is the
 *     hard-coded 3-D menu constant at [0x4A8888].
 * If either drifts, the arithmetic has stopped matching the engine.
 */
#include "unittest.h"

#include "fov_math.h"

#include <math.h>
#include <stdlib.h>

static void test_clamp(void)
{
    ut_near(fov_clamp_float(5.0f, 0.0f, 10.0f), 5.0f, 0.0f, "clamp passes a value through");
    ut_near(fov_clamp_float(-1.0f, 0.0f, 10.0f), 0.0f, 0.0f, "clamp holds the minimum");
    ut_near(fov_clamp_float(99.0f, 0.0f, 10.0f), 10.0f, 0.0f, "clamp holds the maximum");
    ut_near(fov_clamp_float(0.0f, 0.0f, 10.0f), 0.0f, 0.0f, "clamp accepts the minimum");
    ut_near(fov_clamp_float(10.0f, 0.0f, 10.0f), 10.0f, 0.0f, "clamp accepts the maximum");

    /* NaN must land on the minimum rather than propagate into the projection. */
    {
        float not_a_number = (float)atof("nan");
        ut_near(fov_clamp_float(not_a_number, 1.0f, 2.0f), 1.0f, 0.0f,
                    "clamp turns NaN into the minimum");
    }
    {
        float infinity = (float)atof("inf");
        ut_near(fov_clamp_float(infinity, 1.0f, 2.0f), 2.0f, 0.0f,
                    "clamp turns +inf into the maximum");
        ut_near(fov_clamp_float(-infinity, 1.0f, 2.0f), 1.0f, 0.0f,
                    "clamp turns -inf into the minimum");
    }
}

static void test_horizontal(void)
{
    /* THE RETAIL CROSS-CHECK: 4:3 with the authored vertical is the shipped 60 degrees. */
    ut_near(fov_horizontal_degrees(ASPECT_MODE_HOR_PLUS, 46.826f, 0.0f, 640, 480),
                60.0f, 0.01f, "Hor+ at 4:3 reproduces the shipped 60 degrees");

    /* 16:9 widens, and by a known amount. */
    ut_near(fov_horizontal_degrees(ASPECT_MODE_HOR_PLUS, 46.826f, 0.0f, 1920, 1080),
                75.0f, 0.6f, "Hor+ at 16:9 widens to about 75 degrees");

    /* Vert- keeps the horizontal wherever the canvas goes. */
    ut_near(fov_horizontal_degrees(ASPECT_MODE_VERT_MINUS, 46.826f, 0.0f, 1920, 1080),
                fov_horizontal_degrees(ASPECT_MODE_VERT_MINUS, 46.826f, 0.0f, 640, 480),
                0.001f, "Vert- gives the same horizontal at any aspect");

    ut_near(fov_horizontal_degrees(ASPECT_MODE_STRETCH, 46.826f, 0.0f, 1920, 1080),
                0.0f, 0.0f, "Stretch signals 'leave the field alone'");

    /* The extra degrees are added on top. */
    ut_near(fov_horizontal_degrees(ASPECT_MODE_HOR_PLUS, 46.826f, 10.0f, 640, 480),
                70.0f, 0.01f, "the offset is added to the computed angle");

    /* The upper clamp is 170, deliberately below the engine's own 179 (see fov_math.h). */
    ut_near(fov_horizontal_degrees(ASPECT_MODE_HOR_PLUS, 46.826f, 500.0f, 640, 480),
                FOV_MAX_DEGREES, 0.0f, "an absurd offset is clamped to the maximum");
    ut_near(fov_horizontal_degrees(ASPECT_MODE_HOR_PLUS, 30.0f, -500.0f, 640, 480),
                FOV_MIN_DEGREES, 0.0f, "a negative offset is clamped to the minimum");

    /* A canvas that is not there yet must not produce a projection. */
    ut_near(fov_horizontal_degrees(ASPECT_MODE_HOR_PLUS, 46.826f, 0.0f, 0, 0),
                60.0f, 0.0f, "a zero canvas falls back to the shipped angle");
    ut_near(fov_horizontal_degrees(ASPECT_MODE_HOR_PLUS, 46.826f, 0.0f, -100, 480),
                60.0f, 0.0f, "a negative canvas falls back to the shipped angle");
}

static void test_focal_and_vertical(void)
{
    /* The second retail cross-CHECK: 640x480 at 60 degrees is the 3-D menu's own constant. */
    ut_near(fov_focal_pixels(60.0f, 320.0f), 554.256f, 0.01f,
                "focal at 640x480 / 60 deg is the hard-coded 554.256");

    ut_near(fov_focal_pixels(60.0f, 0.0f), 0.0f, 0.0f, "focal refuses a zero half-width");
    ut_near(fov_focal_pixels(0.0f, 320.0f), 0.0f, 0.0f, "focal refuses a zero angle");

    /* The vertical is a RESULT: at 4:3 it must give the authored value back. */
    ut_near(fov_vertical_degrees(60.0f, 320.0f, 240.0f), 46.826f, 0.01f,
                "the vertical at 4:3 is the authored 46.826");

    /* At 16:9 the same horizontal yields a smaller vertical; that IS Vert-. */
    ut_check(fov_vertical_degrees(60.0f, 960.0f, 540.0f) < 46.826f,
          "the vertical shrinks at 16:9 for a fixed horizontal");

    ut_near(fov_vertical_degrees(60.0f, 320.0f, 0.0f), 0.0f, 0.0f,
                "the vertical refuses a zero half-height");
}

static void test_menu_focal(void)
{
    /* Native mode at the design resolution must reproduce the retail cell exactly. */
    ut_near(fov_menu_focal_cell(MENU_3D_MODE_NATIVE, 640, 480, 554.256f), 554.256f, 0.01f,
                "native mode at 640x480 reproduces the retail cell");

    /* Stretch at the design resolution is the same thing, because 640/640 = 1. */
    ut_near(fov_menu_focal_cell(MENU_3D_MODE_STRETCH, 640, 480, 554.256f), 554.256f, 0.01f,
                "stretch mode at 640x480 reproduces the retail cell");

    ut_near(fov_menu_focal_cell(MENU_3D_MODE_STRETCH, 0, 0, 554.256f), 0.0f, 0.0f,
                "an unknown canvas yields no cell");
    ut_near(fov_menu_focal_cell(MENU_3D_MODE_STRETCH, 1920, 1080, 0.0f), 0.0f, 0.0f,
                "a zero focal yields no cell");
}

static void test_notches(void)
{
    const float step = 2.0f;
    const int   count = 21;                        /* 0 .. 40 degrees */

    ut_check(fov_notch_from_degrees(0.0f, step, count) == 0, "0 degrees is notch 0");
    ut_check(fov_notch_from_degrees(2.0f, step, count) == 1, "2 degrees is notch 1");
    ut_check(fov_notch_from_degrees(40.0f, step, count) == 20, "40 degrees is the last notch");

    /* Rounding boundary: 0.999 of a step rounds up, 0.4 of a step rounds down. */
    ut_check(fov_notch_from_degrees(2.9f, step, count) == 1, "2.9 degrees rounds down to notch 1");
    ut_check(fov_notch_from_degrees(3.1f, step, count) == 2, "3.1 degrees rounds up to notch 2");

    ut_check(fov_notch_from_degrees(-5.0f, step, count) == 0, "a negative angle clamps to notch 0");
    ut_check(fov_notch_from_degrees(999.0f, step, count) == count - 1,
          "an absurd angle clamps to the last notch");
    ut_check(fov_notch_from_degrees((float)atof("nan"), step, count) == 0,
          "NaN clamps to notch 0");

    ut_near(fov_degrees_from_notch(0, step, count), 0.0f, 0.0f, "notch 0 is 0 degrees");
    ut_near(fov_degrees_from_notch(20, step, count), 40.0f, 0.0f, "notch 20 is 40 degrees");
    ut_near(fov_degrees_from_notch(-3, step, count), 0.0f, 0.0f, "a negative notch clamps");
    ut_near(fov_degrees_from_notch(999, step, count), 40.0f, 0.0f, "a huge notch clamps");

    /* Round trip: every notch must survive degrees -> notch. */
    {
        int notch;
        int round_trip_ok = 1;

        for (notch = 0; notch < count; ++notch) {
            if (fov_notch_from_degrees(fov_degrees_from_notch(notch, step, count), step, count)
                != notch) {
                round_trip_ok = 0;
            }
        }
        ut_check(round_trip_ok, "every notch survives the degrees round trip");
    }

    /* A degenerate notch count must not divide by anything. */
    ut_check(fov_notch_from_degrees(10.0f, step, 0) == 0, "a zero notch count yields 0");
    ut_near(fov_degrees_from_notch(5, step, 0), 0.0f, 0.0f,
                "a zero notch count yields 0 degrees");
    ut_check(fov_notch_from_degrees(10.0f, 0.0f, count) == 0, "a zero step yields notch 0");
}

int main(void)
{
    test_clamp();
    test_horizontal();
    test_focal_and_vertical();
    test_menu_focal();
    test_notches();

    return ut_summary("fov_math");
}
