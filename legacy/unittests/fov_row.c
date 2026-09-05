/* fov_row: the field of view row's own arithmetic, which is where a slider goes wrong.
 *
 * A slider is a fraction turned into a number, and every fault a slider has lives at one of the
 * two ends or in the rounding between them: a handle that cannot reach the maximum, a handle that
 * reaches past it, or a value that displays as one number and is written as another. None of that
 * is visible in a screenshot, so it is pinned here.
 *
 * The clamp matters for the same reason the draw distance row's does: whatever it returns is
 * written into a file the game reads on every start, so a value that escapes it is not a bad frame,
 * it is a bad installation.
 *
 * The row's ends come out of the settings file, and with no file present they are the defaults,
 * which is the state these run in.
 */
#include "fov_row.h"

#include "unittest.h"

#include <math.h>
#include <string.h>

static void test_the_ends(void)
{
    ut_section("the range, with no settings file to widen it");
    ut_check(fov_row_min() == FOV_ROW_MIN_DEFAULT,
             "the narrow end is the one variable_fov's own slider offers");
    ut_check(fov_row_max() == FOV_ROW_MAX_DEFAULT,
             "and so is the wide end, so the two sliders accept the same numbers");
}

static void test_the_clamp(void)
{
    ut_section("the clamp, whose answer is written into the file the game reads at every start");
    ut_check(fov_row_clamp(FOV_ROW_MIN_DEFAULT) == FOV_ROW_MIN_DEFAULT, "the minimum is accepted");
    ut_check(fov_row_clamp(FOV_ROW_MAX_DEFAULT) == FOV_ROW_MAX_DEFAULT, "so is the maximum");
    ut_check(fov_row_clamp(90.0f) == 90.0f, "so is a number between them, unchanged");

    ut_check(fov_row_clamp(FOV_ROW_MIN_DEFAULT - 0.1f) == FOV_ROW_MIN_DEFAULT,
             "one step below the minimum comes back as the minimum rather than as a picture "
             "narrower than the slider can express");
    ut_check(fov_row_clamp(FOV_ROW_MAX_DEFAULT + 0.1f) == FOV_ROW_MAX_DEFAULT,
             "and one step above the maximum comes back as the maximum");
    ut_check(fov_row_clamp(-400.0f) == FOV_ROW_MIN_DEFAULT, "so does a long way below");
    ut_check(fov_row_clamp(4000.0f) == FOV_ROW_MAX_DEFAULT, "and a long way above");

    ut_check(fov_row_clamp((float)NAN) == FOV_ROW_MIN_DEFAULT,
             "a NaN is caught rather than falling past both bounds, because it compares false "
             "against each of them and would otherwise reach the file");
    ut_check(fov_row_clamp((float)INFINITY) == FOV_ROW_MAX_DEFAULT, "an infinity clamps");
    ut_check(fov_row_clamp(-(float)INFINITY) == FOV_ROW_MIN_DEFAULT, "so does a negative one");
}

static void test_parsing_what_was_typed(void)
{
    float value = -1.0f;

    ut_section("typing a number back into the chip");
    ut_check(fov_row_parse("90", &value) && value == 90.0f, "a bare number");
    ut_check(fov_row_parse("97.5", &value) && value == 97.5f,
             "a decimal point, read as a decimal point rather than as whatever the machine's "
             "language calls one");

    ut_check(fov_row_parse("90 deg", &value) && value == 90.0f,
             "the chip's own text, typed back in exactly as it is shown, which is the whole reason "
             "the suffix is stripped");
    ut_check(fov_row_parse("90deg", &value) && value == 90.0f, "with no space either");
    ut_check(fov_row_parse("90 DEG", &value) && value == 90.0f, "and in capitals");

    value = -1.0f;
    ut_check(!fov_row_parse("", &value) && value == -1.0f,
             "empty text is refused and leaves the caller's value alone");
    ut_check(!fov_row_parse("wide", &value) && value == -1.0f, "so is a word");
    ut_check(!fov_row_parse("90x", &value) && value == -1.0f,
             "and so is the WRONG suffix: an x belongs to the draw distance row, and accepting it "
             "here would take a number meant for another row");
    ut_check(!fov_row_parse(NULL, &value) && value == -1.0f, "a missing string is refused");
    ut_check(!fov_row_parse("120", NULL), "so is a missing destination");
}

static void test_the_chip_text(void)
{
    char text[16];

    ut_section("what the chip shows");
    fov_row_format(90.0f, text, sizeof text);
    ut_check(strcmp(text, "90 deg") == 0, "whole degrees and a unit, because a field of view with "
                                          "decimals in it is a number nobody chose");
    fov_row_format(97.5f, text, sizeof text);
    ut_check(strcmp(text, "98 deg") == 0, "and a half rounds rather than truncating");

    fov_row_format(90.0f, text, 1u);
    ut_check(text[0] == '\0', "a buffer with room for nothing comes back terminated and empty "
                              "rather than written past");
    fov_row_format(90.0f, NULL, sizeof text);
    ut_check(true, "and a missing buffer is survived rather than written through");
}

int main(void)
{
    test_the_ends();
    test_the_clamp();
    test_parsing_what_was_typed();
    test_the_chip_text();

    return ut_summary("fov_row");
}
