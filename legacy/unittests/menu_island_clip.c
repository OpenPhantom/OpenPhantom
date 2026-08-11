/* menu_island_clip.c: the island clamp, checked without the game.
 *
 * The claims worth writing down in English:
 *   * a sprite that fits the island passes through BIT-IDENTICAL, because that is every authored
 *     widget, and "the repair costs nothing when nothing is wrong" is the property the default-on
 *     setting stands on;
 *   * the reported defect's own numbers come out right: at 3840x2160 the island sits at
 *     (1600,840), and a halo poking past its left border is cut exactly at 1600;
 *   * at real 640x480 the island IS the screen, so the clamp agrees with what the retail
 *     rasterizer did at the screen edge;
 *   * bounds the clamp has no business judging (reversed, NaN) pass through untouched.
 */
#include "unittest.h"

#include "menu_island_clip.h"

#include <stdlib.h>

/* The island origin the engine computes at 3840x2160: ((3840-640)/2, (2160-480)/2). */
#define UHD_LEFT 1600.0f
#define UHD_TOP   840.0f

static void test_identity_inside(void)
{
    float left = UHD_LEFT + 7.0f, right = UHD_LEFT + 206.0f;
    float top = UHD_TOP + 175.0f, bottom = UHD_TOP + 228.0f;

    ut_section("a sprite inside the island is untouched");

    /* The authored button rectangle 7,175,199,53, placed on the 4K island. */
    ut_check(menu_island_clip_rect(&left, &right, &top, &bottom, UHD_LEFT, UHD_TOP),
             "an authored widget is drawn");
    ut_near(left,  UHD_LEFT + 7.0f,   0.0, "its left bound is bit-identical");
    ut_near(right, UHD_LEFT + 206.0f, 0.0, "its right bound is bit-identical");
    ut_near(top,   UHD_TOP + 175.0f,  0.0, "its top bound is bit-identical");
    ut_near(bottom,UHD_TOP + 228.0f,  0.0, "its bottom bound is bit-identical");

    /* The island's own full extent, edge to edge, is also inside. */
    left = UHD_LEFT; right = UHD_LEFT + 640.0f; top = UHD_TOP; bottom = UHD_TOP + 480.0f;
    ut_check(menu_island_clip_rect(&left, &right, &top, &bottom, UHD_LEFT, UHD_TOP),
             "the full-island backdrop is drawn");
    ut_near(left, UHD_LEFT, 0.0, "the backdrop's left bound is untouched");
    ut_near(right, UHD_LEFT + 640.0f, 0.0, "the backdrop's right bound is untouched");
}

static void test_reported_defect(void)
{
    /* The hovered button's halo: wider than its plate, so it starts left of canvas x=0. These
     * are the shape of the reported smear: some twenty canvas pixels past the border. */
    float left = UHD_LEFT - 22.0f, right = UHD_LEFT + 228.0f;
    float top = UHD_TOP + 170.0f, bottom = UHD_TOP + 233.0f;

    ut_section("the reported defect's own numbers");

    ut_check(menu_island_clip_rect(&left, &right, &top, &bottom, UHD_LEFT, UHD_TOP),
             "the poking halo is still drawn");
    ut_near(left, UHD_LEFT, 0.0, "and is cut exactly at the island's left border");
    ut_near(right, UHD_LEFT + 228.0f, 0.0, "while its inside part keeps its bound");
    ut_near(top, UHD_TOP + 170.0f, 0.0, "the vertical bounds were inside and stay");
    ut_near(bottom, UHD_TOP + 233.0f, 0.0, "the vertical bounds were inside and stay");
}

static void test_all_four_borders(void)
{
    float left = UHD_LEFT - 10.0f, right = UHD_LEFT + 650.0f;
    float top = UHD_TOP - 5.0f, bottom = UHD_TOP + 500.0f;

    ut_section("a sprite past all four borders");

    ut_check(menu_island_clip_rect(&left, &right, &top, &bottom, UHD_LEFT, UHD_TOP),
             "an oversized sprite is still drawn");
    ut_near(left, UHD_LEFT, 0.0, "left lands on the border");
    ut_near(right, UHD_LEFT + 640.0f, 0.0, "right lands on the border");
    ut_near(top, UHD_TOP, 0.0, "top lands on the border");
    ut_near(bottom, UHD_TOP + 480.0f, 0.0, "bottom lands on the border");
}

static void test_outside_is_skipped(void)
{
    float left, right, top, bottom;

    ut_section("entirely outside means not drawn at all");

    left = UHD_LEFT - 100.0f; right = UHD_LEFT - 40.0f;
    top = UHD_TOP + 10.0f; bottom = UHD_TOP + 42.0f;
    ut_check(!menu_island_clip_rect(&left, &right, &top, &bottom, UHD_LEFT, UHD_TOP),
             "a sprite wholly left of the island is skipped");

    /* Edge-touching: a zero-width remainder is not a draw. */
    left = UHD_LEFT - 32.0f; right = UHD_LEFT;
    ut_check(!menu_island_clip_rect(&left, &right, &top, &bottom, UHD_LEFT, UHD_TOP),
             "a sprite ending exactly on the border is skipped");

    left = UHD_LEFT + 10.0f; right = UHD_LEFT + 42.0f;
    top = UHD_TOP + 480.0f; bottom = UHD_TOP + 512.0f;
    ut_check(!menu_island_clip_rect(&left, &right, &top, &bottom, UHD_LEFT, UHD_TOP),
             "a sprite starting exactly on the bottom border is skipped");
}

static void test_640x480_is_the_screen(void)
{
    /* At the retail arrangement the island origin is (0,0): the clamp then cuts where the
     * rasterizer's screen clip cut, which is the arrangement that never showed the defect. */
    float left = -22.0f, right = 228.0f, top = 170.0f, bottom = 233.0f;

    ut_section("at 640x480 the island is the screen");

    ut_check(menu_island_clip_rect(&left, &right, &top, &bottom, 0.0f, 0.0f),
             "the same halo is drawn at 640x480");
    ut_near(left, 0.0, 0.0, "and is cut at screen x=0, where the rasterizer cut it");
    ut_near(right, 228.0, 0.0, "with the rest untouched");
}

static void test_not_ours_to_judge(void)
{
    float left, right, top, bottom;

    ut_section("bounds the clamp has no business judging");

    left = 300.0f; right = 200.0f; top = 10.0f; bottom = 40.0f;
    ut_check(menu_island_clip_rect(&left, &right, &top, &bottom, 0.0f, 0.0f),
             "reversed bounds pass through");
    ut_near(left, 300.0, 0.0, "with the left bound untouched");
    ut_near(right, 200.0, 0.0, "and the right bound untouched");

    {
        float not_a_number = (float)atof("nan");
        left = not_a_number; right = 100.0f; top = 10.0f; bottom = 40.0f;
        ut_check(menu_island_clip_rect(&left, &right, &top, &bottom, 0.0f, 0.0f),
                 "a NaN bound passes through rather than being invented around");
        ut_near(right, 100.0, 0.0, "and the finite bounds are untouched");
    }
}

int main(void)
{
    test_identity_inside();
    test_reported_defect();
    test_all_four_borders();
    test_outside_is_skipped();
    test_640x480_is_the_screen();
    test_not_ours_to_judge();

    return ut_summary("menu island clip");
}
