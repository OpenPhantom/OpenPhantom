/* numeric.c: the clamp and the finite test every DLL used to carry a copy of. */
#include "unittest.h"

#include "common/numeric.h"

#include <math.h>

int main(void)
{
    const float nan      = (float)NAN;
    const float infinity = (float)INFINITY;

    ut_section("the clamp");
    ut_near(numeric_clamp(0.5f, 0.0f, 1.0f), 0.5, 0.0, "a value inside the bounds is unchanged");
    ut_near(numeric_clamp(-1.0f, 0.0f, 1.0f), 0.0, 0.0, "below the minimum reads as the minimum");
    ut_near(numeric_clamp(2.0f, 0.0f, 1.0f), 1.0, 0.0, "above the maximum reads as the maximum");
    ut_near(numeric_clamp(nan, 0.0f, 1.0f), 0.0, 0.0, "NaN reads as the minimum");
    ut_near(numeric_clamp(infinity, 0.0f, 1.0f), 1.0, 0.0, "infinity reads as the maximum");
    ut_near(numeric_clamp(-infinity, 0.0f, 1.0f), 0.0, 0.0, "minus infinity as the minimum");
    ut_near(numeric_clamp(0.5f, 1.0f, 0.0f), 0.0, 0.0,
            "where the bounds cross the maximum wins, so a written ceiling below the floor holds");

    ut_section("the finite test");
    ut_check(numeric_is_finite(0.0f), "zero is finite");
    ut_check(numeric_is_finite(-0.0f), "and so is minus zero");
    ut_check(numeric_is_finite(3.4e38f), "the largest float is finite");
    ut_check(numeric_is_finite(1.0e-40f), "a denormal is finite");
    ut_check(!numeric_is_finite(infinity), "infinity is not");
    ut_check(!numeric_is_finite(-infinity), "nor is minus infinity");
    ut_check(!numeric_is_finite(nan), "nor is NaN");

    return ut_summary("numeric");
}
