/* view_range_row: the draw distance row's own arithmetic.
 *
 * The parser here is hand written rather than handed to strtof, and that is the part worth
 * testing. strtof reads a full stop as a decimal point only where the locale agrees it is one, so
 * on a German Windows "2.5" would come back as 2 with ".5" left over as trailing rubbish. That is
 * the kind of fault nobody finds on their own machine, so the cases below pin the behaviour rather
 * than trusting a library call to be locale blind.
 *
 * The clamp matters for a different reason: whatever it returns is written into a file the game
 * reads on every start, so a value that escapes it is not a bad frame, it is a bad installation.
 */
#include "view_range_row.h"

#include "unittest.h"

#include <math.h>
#include <string.h>

static void test_the_clamp(void)
{
    ut_section("what may be written to the file");

    ut_near((double)view_range_row_clamp(1.5f), 1.5, 0.0001,
            "a value inside the range comes back untouched");
    ut_near((double)view_range_row_clamp(VIEW_RANGE_MIN), (double)VIEW_RANGE_MIN, 0.0001,
            "the minimum itself is accepted, not treated as below the floor");
    ut_near((double)view_range_row_clamp(VIEW_RANGE_MAX), (double)VIEW_RANGE_MAX, 0.0001,
            "and so is the maximum");
    ut_near((double)view_range_row_clamp(0.2f), (double)VIEW_RANGE_MIN, 0.0001,
            "below the floor is raised to it: the engine refuses to draw less than it always did");
    ut_near((double)view_range_row_clamp(9.0f), (double)VIEW_RANGE_MAX, 0.0001,
            "above the ceiling is lowered to it rather than written out and silently corrected "
            "later, which would leave the file and the world disagreeing");
    ut_near((double)view_range_row_clamp(-3.0f), (double)VIEW_RANGE_MIN, 0.0001,
            "a negative scale is not a direction, it is a mistake, and reads as the minimum");
    ut_near((double)view_range_row_clamp((float)NAN), (double)VIEW_RANGE_MIN, 0.0001,
            "and a NaN reads as the minimum rather than reaching a file the game parses on every "
            "start");
}

static void test_parsing_what_was_typed(void)
{
    float value = 0.0f;

    ut_section("reading the typed text");

    ut_check(view_range_row_parse("2", &value) && value > 1.99f && value < 2.01f,
             "a bare whole number parses");
    ut_check(view_range_row_parse("2.5", &value) && value > 2.49f && value < 2.51f,
             "and one with a decimal point, which is the case a locale aware parser would get "
             "wrong on a machine that writes 2,5");
    ut_check(view_range_row_parse("1.25", &value) && value > 1.24f && value < 1.26f,
             "two decimal places parse too, not just one");
    ut_check(view_range_row_parse("2.50x", &value) && value > 2.49f && value < 2.51f,
             "the trailing x the chip displays is ignored, so the value can be typed back in "
             "exactly as it is read off the screen");
    ut_check(view_range_row_parse(".5", &value) && value > 0.49f && value < 0.51f,
             "a leading decimal point is a number, and the clamp rather than the parser is what "
             "decides it is too small");
    ut_check(view_range_row_parse("0", &value) && value < 0.01f,
             "zero parses as zero: refusing it is the clamp's job, not the parser's");
}

static void test_text_that_is_not_a_number(void)
{
    float value = 42.0f;

    ut_section("refusing rubbish rather than reading it as zero");

    ut_check(!view_range_row_parse("", &value), "empty text is not a number");
    ut_check(!view_range_row_parse(".", &value),
             "a lone decimal point is not a number, which is exactly the input atof would answer "
             "zero for and a clamp would then turn into the minimum");
    ut_check(!view_range_row_parse("abc", &value), "letters are not a number");
    ut_check(!view_range_row_parse("2.5.1", &value), "a second decimal point is refused");
    ut_check(!view_range_row_parse("2.5q", &value), "trailing rubbish is refused, x aside");
    ut_check(!view_range_row_parse("x", &value), "an x with no digits before it is not a number");
    ut_check(!view_range_row_parse("-2", &value),
             "a minus sign is refused here rather than parsed and clamped, since there is no such "
             "thing as a negative draw distance to have meant");
    ut_check(!view_range_row_parse(NULL, &value), "no text at all is refused");
    ut_check(view_range_row_parse("1.5", NULL) == false,
             "and nowhere to put the answer is refused rather than written through");
    ut_near((double)value, 42.0, 0.0001,
            "every refusal above left the caller's own value untouched, so a failed parse cannot "
            "quietly change the setting");
}

static void test_the_chip_text(void)
{
    char text[8];

    ut_section("what the row shows");

    view_range_row_format(2.5f, text, sizeof text);
    ut_check(strcmp(text, "2.50x") == 0,
             "two decimal places and a trailing x, the same shape the jump-boost scale row uses");

    view_range_row_format(1.0f, text, sizeof text);
    ut_check(strcmp(text, "1.00x") == 0, "the minimum reads as a scale, not as off");

    /* Round trip: what is shown must be what can be typed back in. */
    {
        float parsed = 0.0f;

        view_range_row_format(1.75f, text, sizeof text);
        ut_check(view_range_row_parse(text, &parsed) && parsed > 1.74f && parsed < 1.76f,
                 "and the text it produces parses back to the number it came from, so reading the "
                 "chip and retyping it is not a way to change the value by accident");
    }

    text[0] = 'z';
    view_range_row_format(2.0f, text, 0u);
    ut_check(text[0] == 'z', "a buffer of no length is left alone rather than written past");
}

int main(void)
{
    test_the_clamp();
    test_parsing_what_was_typed();
    test_text_that_is_not_a_number();
    test_the_chip_text();

    return ut_summary("view_range_row");
}
