/* view_lead.c: the one property the per-frame view lead exists for, and the variant that reverses.
 *
 * The claim is that the drawn view angle advances by exactly one frame's worth of hand movement on
 * every rendered frame, while the body goes on turning once per simulation step. That is a property
 * of a sequence rather than of a single call, so the tests below play a whole substep of frames and
 * measure the drawn angle the way an eye would, increment by increment.
 *
 * Why the body turns only on a step
 *
 * There is one site that advances the view angle, inside Plr_Integrate, phase 7 of the player phase
 * table:
 *
 *     0044a692  fld  [eax + 0x2A4]     turnWheel, degrees per second
 *     0044a69e  fmul [ecx + 0x74]      the substep length
 *     0044a6a7  fadd [edx + 0x2A0]     the heading
 *     0044a6b2  fstp [eax + 0x2A0]
 *     0044a6c5  call 0x004940AB        the wrap
 *
 * So the angle moves 32 times a second and never in between. A held direction key contributes an
 * equal amount to every step, so the interpolation that fills the frames between two steps draws a
 * straight line. The mouse contributes a different amount to every step, so the drawn line is a
 * polyline whose slope changes 32 times a second, and that change is what a player reads as
 * stepping.
 *
 * The engine's own arithmetic is reproduced here rather than called, because it is exactly what is
 * being checked. The camera interpolates the two most recent headings at 0x004185e5:
 *
 *     004185e5  push [0x8a0074]      head
 *     004185ec  push [0x8a0070]      headPrev
 *     004185f2  call 0x4941c0        angle_diff, into (-180, 180]
 *     004185fd  fmul [ebp-0x3c]      times alpha
 *     00418600  fadd [0x8a0070]      plus headPrev
 *
 * and alpha itself is built at the tail of the substep loop:
 *
 *     004757ff  fld  [0x869294]      simTarget
 *     00475805  fsub [0x868728]      minus simTime
 *     0047580b  fadd [0x868714]      plus the step
 *     00475811  fdiv [0x868714]      over the step
 *     00475817  fstp [0x86871c]      into g_substepAlpha
 *
 * The loop exits with an overshoot of at most one step, so alpha lies in (0, 1], and the heading
 * pair is rotated only in bapview_setCamTarget at 0x4184cc, once per step. Within a step the alpha
 * therefore sweeps from 1/n to 1 against a frozen pair, which is what `play` below reproduces.
 *
 * The second test is the regression. Adding only what is banked, which is the obvious way to write
 * this and was the first design, double counts: the interpolation is already paying the previous
 * step's mouse turn out as a ramp while the bank refills with the next one. The test asserts that
 * the naive form really does step backwards at the substep boundary, so that nobody rediscovers it
 * as a simplification.
 */
#include "unittest.h"

#include "view_lead.h"

#include <math.h>
#include <stdbool.h>

#define FRAMES_PER_STEP 5
#define STEPS           6

/* Degrees per frame. Twenty counts at the slider's default of 0.030 degrees per count, which
 * is an ordinary brisk sweep and not an extreme. Five frames of it is one step's 3.0 degrees, so at
 * 32 steps a second the hand is turning at 96 degrees a second. */
#define HAND_PER_FRAME  0.6f

/* One substep of the engine's own drawing, with the lead added.
 *
 * `compensate` selects the shipped form. False is the refuted one: the bank alone, with the
 * interpolation left to pay the same movement out a second time.
 *
 * Returns the drawn angle after the last frame of the run and fills `increments` with the drawn
 * step of every frame, which is the quantity the eye reads. */
static float play(bool compensate, float *increments, int count)
{
    float head_previous = 0.0f;
    float head_current  = 0.0f;
    float pending       = 0.0f;
    float substep_mouse = 0.0f;
    float drawn         = 0.0f;
    float previous      = 0.0f;
    int   index         = 0;
    int   step;
    int   frame;

    for (step = 0; step < STEPS; ++step) {
        for (frame = 0; frame < FRAMES_PER_STEP; ++frame) {
            float alpha = (float)(frame + 1) / (float)FRAMES_PER_STEP;
            float interpolated;
            float lead;

            /* The simulation step runs at the top of the frame it lands on, before the camera
             * update, and it takes everything banked. */
            if (frame == 0) {
                head_previous = head_current;
                head_current += pending;
                substep_mouse = pending;
                pending       = 0.0f;
            }

            /* The frame's own hand movement is banked inside the camera update. */
            pending += HAND_PER_FRAME;

            interpolated = head_previous + (head_current - head_previous) * alpha;
            lead = compensate ? view_lead_degrees(pending, substep_mouse, alpha) : pending;
            drawn = interpolated + lead;

            if (index < count) {
                increments[index] = (step == 0 && frame == 0) ? HAND_PER_FRAME : (drawn - previous);
            }
            ++index;
            previous = drawn;
        }
    }
    return drawn;
}

/* The shipped form, written out. `h` is the hand's movement per frame, five frames to a step, so a
 * step publishes 5h, `H` is the heading after that step from a fixed origin, and the unpaid
 * term is what the interpolation still owes:
 *
 *     frame                 alpha   interp   banked   unpaid   drawn   step
 *     the one that stepped    0.2   H-4h     h        4h       H+h
 *     +1                      0.4   H-3h     2h       3h       H+2h    +h
 *     +2                      0.6   H-2h     3h       2h       H+3h    +h
 *     +3                      0.8   H-1h     4h       1h       H+4h    +h
 *     +4                      1.0   H        5h       0        H+5h    +h
 *     the next step's frame   0.2   H+h      h        4h       H+6h    +h
 *
 * The two terms move in opposite directions by the same amount, so their sum is constant during a
 * steady sweep and every frame advances by one frame of hand movement. That is the table this test
 * checks row by row. */
static void the_drawn_angle_advances_by_one_frame_of_hand_movement(void)
{
    float increments[STEPS * FRAMES_PER_STEP];
    int   i;

    ut_section("the shipped form");
    (void)play(true, increments, STEPS * FRAMES_PER_STEP);

    /* The first substep is the only one that is not in steady state: nothing was banked before the
     * run began, so its own movement is still on its way to the body. From the second on, every
     * frame must draw exactly what the hand did on it. */
    for (i = FRAMES_PER_STEP; i < STEPS * FRAMES_PER_STEP; ++i) {
        ut_checkf(fabs((double)increments[i] - (double)HAND_PER_FRAME) < 1.0e-4,
                  "frame %d draws one frame of hand movement: %.4f", i, (double)increments[i]);
    }
}

static void the_body_receives_every_degree(void)
{
    float drawn;

    ut_section("the body");

    /* Six steps of five frames at a fixed speed. The drawn angle at the end is the whole of the
     * hand movement: the body has had all but the last step's worth and the lead carries the rest,
     * so nothing is invented and nothing is lost. */
    drawn = play(true, NULL, 0);
    ut_near((double)drawn, (double)HAND_PER_FRAME * STEPS * FRAMES_PER_STEP, 1.0e-3,
            "the drawn angle is the total hand movement, no more and no less");
}

/* The refuted form, the same table without the unpaid term:
 *
 *     frame                 alpha   interp   banked   drawn   step
 *     the one that stepped    0.2   H-4h     h        H-3h
 *     +1                      0.4   H-3h     2h       H-h     +2h
 *     +2                      0.6   H-2h     3h       H+h     +2h
 *     +3                      0.8   H-1h     4h       H+3h    +2h
 *     +4                      1.0   H        5h       H+5h    +2h
 *     the next step's frame   0.2   H+h      h        H+2h    -3h
 *
 * The total over the step is right and every frame inside it is wrong: the camera runs at twice the
 * hand's speed for four frames and then falls back by three frames' worth. At 300 degrees a second
 * that is a 5.6 degree backwards jerk 32 times a second, worse than the defect the lead was
 * written to remove. The sequence is asserted rather than described so that the shorter form cannot
 * be rediscovered as a simplification. */
static void the_uncompensated_variant_steps_backwards(void)
{
    float increments[STEPS * FRAMES_PER_STEP];
    int   i;
    int   reversals = 0;

    ut_section("the refuted variant, kept as a regression");
    (void)play(false, increments, STEPS * FRAMES_PER_STEP);

    for (i = FRAMES_PER_STEP; i < STEPS * FRAMES_PER_STEP; ++i) {
        if (increments[i] < 0.0f) {
            ++reversals;
        }
    }
    ut_check(reversals == STEPS - 1,
             "adding only the bank turns the camera backwards once per simulation step");
    ut_check(increments[FRAMES_PER_STEP + 1] > (float)(1.9 * HAND_PER_FRAME),
             "and it runs at nearly twice the hand's speed in between");
}

static void an_alpha_of_one_leaves_only_the_bank(void)
{
    ut_section("the boundaries");

    /* At the last frame of a step the interpolation has paid the whole of the step out, so there is
     * nothing left to cancel and the lead is what the hand has done since. */
    ut_near((double)view_lead_degrees(0.4f, 3.0f, 1.0f), 0.4, 1.0e-6,
            "alpha 1 leaves the bank alone");

    /* And at 32 frames a second, where every frame runs a step, the lead is exactly one frame of
     * movement: the camera leads the body by a frame instead of lagging it by one. */
    ut_near((double)view_lead_degrees(0.6f, 0.6f, 1.0f), 0.6, 1.0e-6,
            "one frame per step leads by exactly that frame");

    /* The engine's own alpha lies in (0, 1], so a value outside that band means something has
     * extrapolated. What is left to pay out is then nothing, never a negative amount, or the lead
     * would turn the camera the wrong way. */
    ut_near((double)view_lead_degrees(0.0f, 5.0f, 1.4f), 0.0, 1.0e-6,
            "an alpha past 1 does not push the camera backwards");
    ut_near((double)view_lead_degrees(0.0f, 5.0f, -0.2f), 5.0, 1.0e-6,
            "an alpha below 0 pays out the whole step");
}

static void nothing_that_is_not_a_number_reaches_the_camera(void)
{
    float nan  = (float)NAN;
    float huge = 1.0e38f;

    ut_section("the bolts");
    ut_check(view_lead_degrees(nan, 1.0f, 0.5f) == 0.0f, "a banked NaN answers zero");
    ut_check(view_lead_degrees(1.0f, nan, 0.5f) == 0.0f, "a NaN step answers zero");
    ut_check(view_lead_degrees(1.0f, 1.0f, nan) == 0.0f, "a NaN alpha answers zero");

    /* The bound is on the drawn lead only, and 120 degrees is a third of a turn. A bank this large
     * means movement was collected while nothing consumed it; the body still receives it all, and
     * what must not happen is the camera being thrown a third of a turn away from the body in one
     * frame. */
    ut_check(view_lead_degrees(huge, huge, 0.5f) <= 120.0f,
             "an impossible bank is held at the bound rather than drawn");
    ut_check(view_lead_degrees(-huge, 0.0f, 0.5f) >= -120.0f,
             "and the same in the other direction");
}

int main(void)
{
    the_drawn_angle_advances_by_one_frame_of_hand_movement();
    the_body_receives_every_degree();
    the_uncompensated_variant_steps_backwards();
    an_alpha_of_one_leaves_only_the_bank();
    nothing_that_is_not_a_number_reaches_the_camera();

    return ut_summary("view_lead");
}
