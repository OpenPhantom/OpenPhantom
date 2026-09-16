/* pad_input.c: the pad's shaping, checked without a pad.
 *
 * Two pieces stand between the raw report and what the panel and the free camera see: the
 * radial deadzone with its rescale, where a quiet mistake would be felt as a pointer that jumps
 * or a camera that drifts at rest, and the reading of the opening buttons' names from the
 * settings, where one would be felt as a panel that cannot be opened from the pad at all.
 */
#include "unittest.h"

#include "pad_input.h"

#include <math.h>

static int near_to(float a, float b)
{
    return fabsf(a - b) < 0.001f;
}

int main(void)
{
    float x;
    float y;

    ut_section("the radial deadzone");
    pad_shape_stick(0, 0, 0.24f, &x, &y);
    ut_check(x == 0.0f && y == 0.0f, "a stick at rest answers nothing");
    pad_shape_stick(7000, 0, 0.24f, &x, &y);
    ut_check(x == 0.0f && y == 0.0f, "inside the deadzone answers nothing, on one axis");
    pad_shape_stick(5000, 5000, 0.24f, &x, &y);
    ut_check(x == 0.0f && y == 0.0f, "inside the deadzone answers nothing, diagonally");
    pad_shape_stick(8000, 0, 0.24f, &x, &y);
    ut_check(x > 0.0f && x < 0.02f && y == 0.0f,
             "a hair past the deadzone is a hair, not a jump to a quarter");
    pad_shape_stick(32767, 0, 0.24f, &x, &y);
    ut_check(near_to(x, 1.0f) && y == 0.0f, "fully over on one axis is one");
    pad_shape_stick(-32767, 0, 0.24f, &x, &y);
    ut_check(near_to(x, -1.0f), "and minus one the other way");
    pad_shape_stick(32767, 32767, 0.24f, &x, &y);
    ut_check(near_to(sqrtf(x * x + y * y), 1.0f) && near_to(x, y),
             "the corner of the square is capped at the circle, on the diagonal");
    pad_shape_stick(-32768, -32768, 0.24f, &x, &y);
    ut_check(x < 0.0f && y < 0.0f && sqrtf(x * x + y * y) <= 1.0001f,
             "the most negative report stays inside the circle as well");
    pad_shape_stick(16384, 0, 0.0f, &x, &y);
    ut_check(near_to(x, 0.5f), "with no deadzone the report is passed through as it is");
    pad_shape_stick(32767, 0, 1.0f, &x, &y);
    ut_check(x == 0.0f, "a deadzone of one swallows everything and divides by nothing");

    ut_section("the opening buttons by name");
    ut_check(pad_buttons_named("View") == PAD_BUTTON_BACK &&
             pad_buttons_named("back") == PAD_BUTTON_BACK,
             "View and Back are the same button, case aside");
    ut_check(pad_buttons_named("LS RS") == (PAD_BUTTON_LEFT_THUMB | PAD_BUTTON_RIGHT_THUMB) &&
             pad_buttons_named("lb+rb") ==
                 (PAD_BUTTON_LEFT_SHOULDER | PAD_BUTTON_RIGHT_SHOULDER),
             "two words are a chord, split on a space or a plus");
    ut_check(pad_buttons_named("Menu") == PAD_BUTTON_START &&
             pad_buttons_named("Y") == PAD_BUTTON_Y,
             "Menu is Start, and a face button is itself");
    ut_check(pad_buttons_named("") == 0u && pad_buttons_named("Home") == 0u &&
             pad_buttons_named(NULL) == 0u,
             "nothing, a name this reader does not know, and no string at all are no button");
    ut_check(pad_buttons_named("A Home") == PAD_BUTTON_A,
             "an unknown word in a list is dropped and the rest kept");

    ut_section("with no pad read");
    ut_check(!pad_input_state()->present, "the frame's state says no pad until one is polled");

    return ut_summary("pad input");
}
