/* mover_wraps.c: a wrap is a pose question, a reset is a geometry question, and the second is
 * answered by comparing the mover with itself.
 *
 * The numbers here are the ones the census read off Coruscant on 2026-09-11: eight movers on a
 * 29 unit track, each wrapping four times a second. Three of them moved across the wrap exactly as
 * they moved on an ordinary tick, and were being held raw for a substep every loop; one turned
 * three times its ordinary angle, and was a reset.
 */
#include "unittest.h"

#include "mover_wraps.h"

#include <math.h>

int main(void)
{
    ut_section("the pose wrap");

    ut_check(mover_wraps_is_wrap(28.279f, 3.029f, 29.0f), "28.3 to 3.0 on a 29 track is a wrap");
    ut_check(!mover_wraps_is_wrap(3.029f, 6.779f, 29.0f), "and the next ordinary step is not");
    ut_check(!mover_wraps_is_wrap(10.0f, 9.0f, 29.0f),
             "a reversal drops by a step, not by a track, so it is not a wrap");
    ut_check(!mover_wraps_is_wrap(28.0f, 3.0f, 0.0f), "no track, nothing to wrap");

    ut_section("the angle from a cosine");

    ut_check(mover_wraps_angle_degrees(1.0f) == 0.0f, "a cosine of one is no turn");
    ut_check(mover_wraps_angle_degrees(1.0001f) == 0.0f,
             "and a hair over one, from rounding, too");
    ut_check(mover_wraps_angle_degrees(-1.0f) == 180.0f, "minus one is a half turn");
    {
        float ninety = mover_wraps_angle_degrees(0.0f);
        ut_check(ninety > 89.99f && ninety < 90.01f, "zero is a right angle");
    }

    ut_section("a loop moves across the wrap as on any tick");

    ut_check(!mover_wraps_is_reset(9.354f, 0.0f, 9.877f, 0.0f, true),
             "the belt: 9.35 across the wrap against 9.88 ordinarily, no turn either way");
    ut_check(!mover_wraps_is_reset(9.766f, 32.7f, 10.311f, 34.6f, true),
             "the second belt: step and turn both within half of the ordinary");
    ut_check(!mover_wraps_is_reset(0.0f, 84.2f, 0.0f, 88.9f, true),
             "the rotor: 84 degrees across the wrap against 89 ordinarily");
    ut_check(!mover_wraps_is_reset(0.0f, 0.0f, 0.0f, 0.0f, true),
             "a subnode the track never moves is not a reset for wrapping");
    ut_check(!mover_wraps_is_reset(0.3f, 4.0f, 0.0f, 0.0f, true),
             "and one that barely moves is judged against the floor, not against nothing");

    ut_section("a reset moves it unlike any tick");

    ut_check(mover_wraps_is_reset(0.0f, 55.1f, 0.0f, 18.7f, true),
             "the one genuine reset in the census: 55 degrees against 19");
    ut_check(mover_wraps_is_reset(29.0f, 0.0f, 3.75f, 0.0f, true),
             "a piston snapping back a whole track against a 3.75 step");
    ut_check(mover_wraps_is_reset(1.0f, 0.0f, 0.0f, 0.0f, true),
             "a unit of travel on a mover that ordinarily does not move");
    ut_check(mover_wraps_is_reset(0.0f, 20.0f, 0.0f, 0.0f, true),
             "twenty degrees on one that ordinarily does not turn");

    ut_section("no yardstick yet");

    ut_check(mover_wraps_is_reset(9.354f, 0.0f, 0.0f, 0.0f, false),
             "with no ordinary tick seen the wrap is a reset, the safe side");

    ut_section("values that are not numbers");

    {
        float nan = (float)NAN;
        ut_check(mover_wraps_is_reset(nan, 0.0f, 9.0f, 0.0f, true),
                 "a step that is not a number reads as a reset rather than as a match");
        ut_check(mover_wraps_angle_degrees(nan) == 0.0f,
                 "and a cosine that is not a number is no turn rather than a NaN angle");
    }

    return ut_summary("mover wraps");
}
