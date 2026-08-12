/* mouse_look.c: the arithmetic this module still owns once the delivery moved to mouse_rate.c.
 *
 * Two things are left here and both are the kind that break silently:
 *
 *   1. the slider round trip. A notch that does not survive being converted to a setting and back
 *      is a slider that jumps under the hand the first time the screen is opened;
 *   2. the degraded path's clamp, which is the only limit left on a build where the per-frame hook
 *      could not be installed.
 *
 * Conservation, the property the whole repair rests on, is tested against the delivery itself in
 * mouse_rate.c, because that is where it now lives.
 */
#include "unittest.h"

#include "input_slider.h"
#include "mouse_look.h"

#include <math.h>

static void test_slider_notches(void)
{
    int notch;

    ut_near(input_slider_degrees_from_notch(0), 0.001f, 0.00001f,
                "the slider's left end is 0.001 degrees per count, which the caption shows as 1");
    ut_near(input_slider_degrees_from_notch(MOUSE_SLIDER_NOTCH_COUNT - 1), 0.100f,
                0.00001f, "the slider's right end is 0.100, shown as 100");
    ut_near(input_slider_degrees_from_notch(29), 0.030f, 0.00001f,
                "the default 0.030 lands exactly on notch 29 rather than between two");

    /* The caption is the setting with the decimal point moved, so every notch has to land on a
     * round thousandth, otherwise two adjacent notches show the same number. */
    for (notch = 0; notch < MOUSE_SLIDER_NOTCH_COUNT; ++notch) {
        float degrees = input_slider_degrees_from_notch(notch);
        float shown   = degrees * 1000.0f;

        if (fabsf(shown - (float)(notch + 1)) > 0.001f) {
            ut_checkf(0, "notch %d shows as %.4f, not %d", notch, (double)shown, notch + 1);
        }
    }
    ut_checkf(1, "every notch shows as a whole number, 1 through %d", MOUSE_SLIDER_NOTCH_COUNT);

    ut_check(input_slider_notch_from_degrees(0.030f) == 29,
          "and converts back to notch 29");

    for (notch = 0; notch < MOUSE_SLIDER_NOTCH_COUNT; ++notch) {
        float degrees = input_slider_degrees_from_notch(notch);

        if (input_slider_notch_from_degrees(degrees) != notch) {
            ut_checkf(0, "notch %d does not survive the round trip (%.5f came back as %d)",
                   notch, (double)degrees, input_slider_notch_from_degrees(degrees));
        }
    }
    ut_check(1, "every notch survives the round trip");

    /* A setting typed into the ini between two notches shows as the CLOSER one. */
    ut_check(input_slider_notch_from_degrees(0.0296f) == 29,
          "a setting just under a notch rounds to it rather than down");
    ut_check(input_slider_notch_from_degrees(0.0306f) == 30,
          "and one just over the next notch rounds up to that one");

    /* Both ends of the slider are bolts against a setting from outside its band. The ini accepts
     * up to 1.0, which is ten times the slider's right-hand end. */
    ut_check(input_slider_notch_from_degrees(0.0001f) == 0,
          "a setting below the slider's band sits at its left end");
    ut_check(input_slider_notch_from_degrees(1.0f) == MOUSE_SLIDER_NOTCH_COUNT - 1,
          "a setting above it sits at the right end");
    ut_check(input_slider_notch_from_degrees((float)sqrt(-1.0)) == 29,
          "a setting that is not a number falls on the default's notch, not on an end");

    ut_near(input_slider_degrees_from_notch(-5), 0.001f, 0.00001f,
                "a notch below the slider is clamped to its left end");
    ut_near(input_slider_degrees_from_notch(9999), 0.100f, 0.00001f,
                "and one past its right end to that end");
}

/* The degraded path still uses this clamp, so it still has to behave. */
static void test_clamp_step(void)
{
    ut_near(mouse_look_clamp_step(10.0f, 0.0f, 3000.0f, 25.0f), 10.0f, 0.0001f,
                "with no span the hard cap alone applies");
    ut_near(mouse_look_clamp_step(100.0f, 0.0f, 3000.0f, 25.0f), 25.0f, 0.0001f,
                "and it bounds the step");
    ut_near(mouse_look_clamp_step(100.0f, 0.001f, 3000.0f, 25.0f), 3.0f, 0.0001f,
                "a short span limits harder than the cap");
    ut_near(mouse_look_clamp_step(-100.0f, 0.001f, 3000.0f, 25.0f), -3.0f, 0.0001f,
                "and a leftward step is limited leftward");
}

int main(void)
{
    test_slider_notches();
    test_clamp_step();

    return ut_summary("mouse_look");
}
