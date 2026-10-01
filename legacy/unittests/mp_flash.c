/* mp_flash.c: whether a detonation is one the player would see. */
#include "unittest.h"

#include "mp_flash_rule.h"

#include <stdbool.h>

#define NEAR 20.0f

/* The engine's heading is degrees, and the forward axis that goes with it is (-sin, cos, 0):
 * heading 0 faces +Y, heading 90 faces -X. The cases below are written against that and not
 * against a guess, because getting the axis backwards would pass every symmetric test. */
static bool seen(float hx, float hy, float hz, float heading, float ax, float ay, float az)
{
    float eye[3];
    float at[3];

    eye[0] = hx;
    eye[1] = hy;
    eye[2] = hz;
    at[0]  = ax;
    at[1]  = ay;
    at[2]  = az;
    return mp_flash_is_seen(eye, heading, at, NEAR);
}

static void test_near_beats_everything(void)
{
    ut_check(seen(0, 0, 0, 0, 0, -5, 0),
             "a detonation five units straight behind him still flashes: near is near");
    ut_check(seen(0, 0, 0, 0, 0, 0, -19),
             "and one nineteen units below him, through the floor he stands on");
    ut_check(!seen(0, 0, 0, 0, 0, -21, 0),
             "one unit past the threshold and behind him, it does not");
    ut_check(seen(0, 0, 0, 0, 0, 20, 0),
             "exactly at the threshold counts as near, so the boundary is not a gap");
}

static void test_in_front_flashes_however_far(void)
{
    ut_check(seen(0, 0, 0, 0, 0, 400, 0),
             "four hundred units down a corridor he is looking along: this is the case the rule "
             "must not take away");
    ut_check(seen(0, 0, 0, 90, -400, 0, 0),
             "the same at heading 90, which faces -X: the forward axis is not guessed");
    ut_check(seen(0, 0, 0, 180, 0, -400, 0), "and at 180, facing -Y");
    ut_check(seen(0, 0, 0, 270, 400, 0, 0), "and at 270, facing +X");
    ut_check(seen(0, 0, 0, 0, 200, 200, 0),
             "forty five degrees off his heading is inside the cone");
    ut_check(seen(0, 0, 0, 0, 0, 100, 900),
             "height does not narrow the cone: the test is flat, and a detonation high above the "
             "corridor he faces is still in his picture");
}

static void test_behind_and_far_is_the_only_no(void)
{
    ut_check(!seen(0, 0, 0, 0, 0, -400, 0), "straight behind him and far away: no flash");
    ut_check(!seen(0, 0, 0, 0, 400, 0, 0),
             "ninety degrees off his heading is outside the cone of sixty degrees to each side");
    ut_check(!seen(0, 0, 0, 90, 400, 0, 0), "the same rotated, so the axis is checked twice");
    ut_check(seen(100, 100, 0, 0, 100, 600, 0),
             "the rule is about the difference and not about the origin");
}

static void test_the_degenerate_cases(void)
{
    ut_check(seen(0, 0, 0, 0, 0, 0, 0), "a detonation exactly on him flashes");
    ut_check(seen(0, 0, 0, 0, 0, 0, 400),
             "one straight above him, beyond the threshold, has no flat direction at all and "
             "flashes rather than being refused on a division that has no answer");
    ut_check(mp_flash_is_seen(NULL, 0.0f, NULL, NEAR),
             "nothing to measure lets the flash through: the engine's behaviour is what this "
             "falls back to");
    {
        float eye[3] = { 0, 0, 0 };
        float at[3]  = { 0, -400, 0 };

        ut_check(!mp_flash_is_seen(eye, 0.0f, at, 0.0f),
                 "a threshold of zero turns the near half off and leaves the view test");
    }
}

int main(void)
{
    test_near_beats_everything();
    test_in_front_flashes_however_far();
    test_behind_and_far_is_the_only_no();
    test_the_degenerate_cases();

    return ut_summary("mp_flash");
}
