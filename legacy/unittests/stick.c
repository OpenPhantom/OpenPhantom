/* stick.c: the shared thumbstick arithmetic, checked without a pad.
 *
 * The case that matters most here is the square corner. XInput reports each axis independently, so
 * a stick held to a true diagonal can arrive as 32767,32767, which is 1.41 long. Every consumer of
 * this function treats what comes back as a speed, so a vector longer than 1 is a diagonal that
 * moves and turns faster than a straight push. The check below states that as a length rather than
 * as a pair of components, because the direction is allowed to be anything and only the length is
 * the claim.
 */
#include "unittest.h"

#include "common/stick.h"

#include <limits.h>
#include <math.h>

#define DEADZONE 0.24f

static float length(float x, float y)
{
    return (float)sqrt((double)(x * x + y * y));
}

/* Full deflection on one axis, as XInput reports it. */
#define FULL ((short)32767)

static void test_refusals(void)
{
    float x = -99.0f;
    float y = -99.0f;

    ut_section("what the deadzone refuses");

    ut_check(!stick_apply_radial_deadzone(0, 0, DEADZONE, &x, &y),
             "a centred stick is inside the deadzone");
    ut_check(x == -99.0f && y == -99.0f,
             "a refused stick leaves both outputs untouched, so a caller can tell centred from "
             "nearly nothing without an epsilon of its own");

    ut_check(!stick_apply_radial_deadzone((short)(FULL / 10), 0, DEADZONE, &x, &y),
             "a tenth of full deflection is inside a 0.24 deadzone");
    ut_check(stick_apply_radial_deadzone((short)(FULL / 2), 0, DEADZONE, &x, &y),
             "half of full deflection is outside it");

    /* A per-axis deadzone would refuse this on both axes. That is why this one is radial: the
     * corner of a square dead region is 1.41 times its edge. */
    ut_check(stick_apply_radial_deadzone((short)(FULL / 5), (short)(FULL / 5), DEADZONE, &x, &y),
             "a diagonal push of a fifth on each axis is live, because 0.28 clears the boundary "
             "that neither axis clears alone");

    ut_check(!stick_apply_radial_deadzone(0, 0, 0.0f, &x, &y),
             "a centred stick with no deadzone at all is still refused, rather than dividing by "
             "its own zero magnitude");

    ut_check(!stick_apply_radial_deadzone(FULL, 0, DEADZONE, NULL, &y),
             "a null output is refused");
    ut_check(!stick_apply_radial_deadzone(FULL, 0, DEADZONE, &x, NULL),
             "a null output is refused whichever one it is");
}

static void test_speed(void)
{
    float x = 0.0f;
    float y = 0.0f;

    ut_section("how fast the result says to go");

    ut_check(stick_apply_radial_deadzone(FULL, 0, DEADZONE, &x, &y), "a full push right is live");
    ut_near((double)length(x, y), 1.0, 0.0005, "a full push on one axis is exactly full speed");
    ut_near((double)y, 0.0, 0.0005, "a push along one axis puts nothing on the other");

    /* THE REGRESSION. This came back 1.414 while the magnitude was clamped before being used as
     * the divisor, so a diagonal ran 41 percent faster than a straight push on any pad reporting a
     * square range, which includes Steam Input. */
    ut_check(stick_apply_radial_deadzone(FULL, FULL, DEADZONE, &x, &y),
             "a full push into the corner is live");
    ut_near((double)length(x, y), 1.0, 0.0005,
            "a full diagonal is full speed and no faster, so the corner of a square range cannot "
            "outrun a straight push");
    ut_near((double)x, (double)y, 0.0005, "a 45 degree push stays at 45 degrees");

    ut_check(stick_apply_radial_deadzone((short)-FULL, (short)-FULL, DEADZONE, &x, &y),
             "the opposite corner is live too");
    ut_near((double)length(x, y), 1.0, 0.0005, "and is the same speed");
    ut_check(x < 0.0f && y < 0.0f, "with both signs carried through");

    /* The one raw value with no positive twin. A short runs to -32768 and XInput reports it, so
     * a stick held hard left arrives one count past what 32767 divides to and comes out 1.00003
     * long before the cap. It must not come out longer than a push to the right. */
    ut_check(stick_apply_radial_deadzone(SHRT_MIN, 0, DEADZONE, &x, &y),
             "a full push left, the raw -32768, is live");
    ut_near((double)x, -1.0, 0.00001,
            "and is exactly full speed leftwards, not a count past it");
    ut_near((double)y, 0.0, 0.0005, "with nothing on the other axis");
    ut_check(stick_apply_radial_deadzone(SHRT_MIN, SHRT_MIN, DEADZONE, &x, &y),
             "the corner both axes reach at -32768 is live");
    ut_near((double)length(x, y), 1.0, 0.0005,
            "and is full speed and no faster, the same cap the positive corner gets");
    ut_near((double)x, (double)y, 0.0005, "and still on the diagonal");

    /* Exactly on the boundary. The deadzone is built from the same division the module does, so
     * the magnitude it computes for this raw value is the deadzone bit for bit, and which side
     * of the comparison equality lands on is the claim: the live side, at no speed at all. */
    {
        const short raw   = (short)8192;
        const float exact = stick_magnitude((float)raw / 32767.0f, 0.0f);

        ut_check(stick_apply_radial_deadzone(raw, 0, exact, &x, &y),
                 "a magnitude exactly at the deadzone is on the live side of it");
        ut_near((double)length(x, y), 0.0, 0.0f,
                "and comes out at exactly no speed, so the boundary is where motion begins, "
                "not a step onto it");
        ut_check(!stick_apply_radial_deadzone((short)(raw - 1), 0, exact, &x, &y),
                 "one raw count inside it is refused");
    }

    /* Just outside the boundary. Without the rescale the first live sample would be the deadzone's
     * own value, so the character would leave a standstill at a quarter speed. */
    ut_check(stick_apply_radial_deadzone((short)((int)(DEADZONE * FULL) + 100), 0,
                                         DEADZONE, &x, &y),
             "a push barely past the boundary is live");
    ut_check(length(x, y) < 0.02f,
             "and starts from nearly nothing, because the range left after the deadzone is "
             "rescaled back onto 0 to 1");
}

static void test_direction(void)
{
    float x = 0.0f;
    float y = 0.0f;

    ut_section("which way the result points");

    ut_check(stick_apply_radial_deadzone(FULL, (short)(FULL / 2), DEADZONE, &x, &y),
             "a push twice as far right as up is live");
    ut_near((double)(y / x), 0.5, 0.001,
            "and keeps that two to one ratio, so removing the deadzone does not bend the "
            "direction the player pushed");

    ut_check(stick_apply_radial_deadzone(0, FULL, DEADZONE, &x, &y), "a full push up is live");
    ut_check(y > 0.0f,
             "and Y comes back positive, as XInput reports it, so a caller wanting screen "
             "coordinates negates it itself");
}

static void test_nonsense_deadzone(void)
{
    float x = 0.0f;
    float y = 0.0f;
    float not_a_number = (float)NAN;

    ut_section("a deadzone the ini should never have held");

    ut_check(stick_apply_radial_deadzone((short)(FULL / 100), 0, not_a_number, &x, &y),
             "a NaN deadzone is treated as no deadzone rather than refusing every sample");
    ut_check(stick_apply_radial_deadzone((short)(FULL / 100), 0, 1.0f, &x, &y),
             "a deadzone of 1 is treated as no deadzone rather than dividing by zero");
    ut_check(stick_apply_radial_deadzone((short)(FULL / 100), 0, -3.0f, &x, &y),
             "a negative deadzone is treated as no deadzone");
    ut_check(length(x, y) <= 1.0f, "and none of those can produce a speed above full");
}

static void test_magnitude(void)
{
    ut_section("the magnitude helper");

    ut_near((double)stick_magnitude(1.0f, 0.0f), 1.0, 0.0005, "a full axis measures full");
    ut_near((double)stick_magnitude(0.0f, 0.0f), 0.0, 0.0005, "a centred pair measures nothing");
    ut_near((double)stick_magnitude(0.6f, 0.8f), 1.0, 0.0005, "a 3-4-5 pair measures full");
    ut_near((double)stick_magnitude(1.0f, 1.0f), 1.0, 0.0005,
            "a raw square corner is capped at full rather than reported as 1.41");
}

int main(void)
{
    test_refusals();
    test_speed();
    test_direction();
    test_nonsense_deadzone();
    test_magnitude();

    return ut_summary("thumbstick deadzone");
}
