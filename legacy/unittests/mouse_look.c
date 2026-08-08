/* mouse_look.c: the mouse step arithmetic, checked without the game.
 *
 * The three properties under test are the three the reported defect came from, and each of them is
 * invisible in a reading of the code that produced it:
 *
 *   1. CONSERVATION. Whatever the bolt holds back must come out later. The old limiter deleted it,
 *      which is why a fast flick under-turned and why frame-time jitter, measured at 4.6x between
 *      the shortest and longest frame on the reporting machine, was cut straight into the aim.
 *   2. THE ZERO-span drain changes nothing. Two consumers share one reservoir, and within a single
 *      frame the second finds the bank already emptied by the first. If that call released a share
 *      anyway, the smoothing would be defeated on exactly the frames that run a substep.
 *   3. the slider round trip. A notch that does not survive being converted to a setting and back
 *      is a slider that jumps under the hand the first time the screen is opened.
 */
#include "unittest.h"

#include "mouse_look.h"

#include <math.h>

/* A frame at 90 frames per second, which is what the reporting machine actually runs. */
#define FRAME 0.0111f

static void test_smoothing_weight(void)
{
    ut_near(mouse_look_smoothing_weight(FRAME, 0.0f), 1.0f, 0.0f,
                "a time constant of zero delivers everything at once");
    ut_near(mouse_look_smoothing_weight(0.0f, 0.010f), 1.0f, 0.0f,
                "a span of zero cannot be smoothed and asks for everything");
    ut_near(mouse_look_smoothing_weight(0.010f, 0.010f), 0.63212f, 0.0005f,
                "one time constant of elapsed time releases 1 - 1/e");
    ut_check(mouse_look_smoothing_weight(1.0f, 0.010f) <= 1.0f,
          "a span far past the time constant never releases more than everything");
    ut_check(mouse_look_smoothing_weight(FRAME, 0.010f) > 0.0f &&
          mouse_look_smoothing_weight(FRAME, 0.010f) < 1.0f,
          "a normal frame releases part of the reservoir");

    /* Both arguments are read from the engine's own clock, so neither is trusted to be a number. */
    ut_near(mouse_look_smoothing_weight((float)sqrt(-1.0), 0.010f), 1.0f, 0.0f,
                "a span that is not a number asks for everything rather than poisoning the share");
    ut_near(mouse_look_smoothing_weight(FRAME, (float)sqrt(-1.0)), 1.0f, 0.0f,
                "a time constant that is not a number switches the smoothing off");
}

static void test_release_conserves(void)
{
    float pending = 10.0f;
    float step;

    /* No smoothing, well inside the bolt: the whole reservoir goes out in one drain. */
    step = mouse_look_release(&pending, FRAME, 0.0f, 3000.0f, 90.0f);
    ut_near(step, 10.0f, 0.0001f, "an unsmoothed step inside the limit is delivered whole");
    ut_near(pending, 0.0f, 0.0001f, "and the reservoir is then empty");

    /* Past the bolt: 3000 deg/s over 0.0111 s is 33.3 degrees, and the rest must SURVIVE. */
    pending = 100.0f;
    step = mouse_look_release(&pending, FRAME, 0.0f, 3000.0f, 90.0f);
    ut_near(step, 33.3f, 0.05f, "the bolt limits one drain to rate times elapsed time");
    ut_near(step + pending, 100.0f, 0.01f, "and what it held back is still in the reservoir");

    /* The hard cap is the last bolt and is what keeps a step below the 180-degree ambiguity. */
    pending = 500.0f;
    step = mouse_look_release(&pending, 1.0f, 0.0f, 3000.0f, 90.0f);
    ut_near(step, 90.0f, 0.0001f, "the hard cap outranks the rate over a long span");
    ut_near(step + pending, 500.0f, 0.01f, "and it too delays rather than deletes");

    /* Sign is carried through both bolts. */
    pending = -100.0f;
    step = mouse_look_release(&pending, FRAME, 0.0f, 3000.0f, 90.0f);
    ut_check(step < 0.0f, "a leftward turn is limited leftward");
    ut_near(step + pending, -100.0f, 0.01f, "and is conserved with the same arithmetic");
}

/* The property the whole repair rests on: over a run of drains the delivered total is the banked
 * total, whatever the frames did in between. The spans below are the measured min, avg and max of a
 * real 60-frame window, 5.5, 11.3 and 25.1 ms, in the order that hurts most. */
static void test_release_total_over_jittery_frames(void)
{
    const float spans[9] = { 0.0055f, 0.0251f, 0.0055f, 0.0113f, 0.0055f,
                             0.0251f, 0.0113f, 0.0055f, 0.0251f };
    float pending = 0.0f;
    float banked  = 0.0f;
    float total   = 0.0f;
    int   i;

    for (i = 0; i < 9; ++i) {
        pending += 12.0f;            /* the same hand movement every frame, whatever its length */
        banked  += 12.0f;
        total   += mouse_look_release(&pending, spans[i], 0.010f, 3000.0f, 90.0f);
    }

    ut_near(total + pending, banked, 0.01f,
                "nothing is lost across nine frames of 4.6x jitter");
    ut_check(total > banked * 0.75f,
          "and most of it has already been delivered rather than sitting in the reservoir");
}

static void test_release_ignores_a_zero_span_drain(void)
{
    float pending = 40.0f;
    float step;

    step = mouse_look_release(&pending, 0.0f, 0.010f, 3000.0f, 90.0f);
    ut_near(step, 0.0f, 0.0f, "a drain that collected no time delivers nothing");
    ut_near(pending, 40.0f, 0.0f, "and leaves the reservoir exactly as it was");

    step = mouse_look_release(&pending, -1.0f, 0.010f, 3000.0f, 90.0f);
    ut_near(step, 0.0f, 0.0f, "a negative span is treated the same way");
    ut_near(pending, 40.0f, 0.0f, "and is equally harmless");
}

static void test_release_drops_a_poisoned_reservoir(void)
{
    float pending = (float)sqrt(-1.0);
    float step    = mouse_look_release(&pending, FRAME, 0.010f, 3000.0f, 90.0f);

    ut_near(step, 0.0f, 0.0f, "a reservoir that is not a number delivers nothing");
    ut_near(pending, 0.0f, 0.0f,
                "and is emptied, because it could never be spent down again");
}

static void test_slider_notches(void)
{
    int notch;

    ut_near(mouse_look_degrees_per_count_from_notch(0), 0.001f, 0.00001f,
                "the slider's left end is 0.001 degrees per count, which the caption shows as 1");
    ut_near(mouse_look_degrees_per_count_from_notch(MOUSE_SLIDER_NOTCH_COUNT - 1), 0.100f,
                0.00001f, "the slider's right end is 0.100, shown as 100");
    ut_near(mouse_look_degrees_per_count_from_notch(29), 0.030f, 0.00001f,
                "the default 0.030 lands exactly on notch 29 rather than between two");

    /* The caption is the setting with the decimal point moved, so every notch has to land on a
     * round thousandth, otherwise two adjacent notches show the same number. */
    for (notch = 0; notch < MOUSE_SLIDER_NOTCH_COUNT; ++notch) {
        float degrees = mouse_look_degrees_per_count_from_notch(notch);
        float shown   = degrees * 1000.0f;

        if (fabsf(shown - (float)(notch + 1)) > 0.001f) {
            ut_checkf(0, "notch %d shows as %.4f, not %d", notch, (double)shown, notch + 1);
        }
    }
    ut_checkf(1, "every notch shows as a whole number, 1 through %d", MOUSE_SLIDER_NOTCH_COUNT);

    ut_check(mouse_look_notch_from_degrees_per_count(0.030f) == 29,
          "and converts back to notch 29");

    for (notch = 0; notch < MOUSE_SLIDER_NOTCH_COUNT; ++notch) {
        float degrees = mouse_look_degrees_per_count_from_notch(notch);

        if (mouse_look_notch_from_degrees_per_count(degrees) != notch) {
            ut_checkf(0, "notch %d does not survive the round trip (%.5f came back as %d)",
                   notch, (double)degrees, mouse_look_notch_from_degrees_per_count(degrees));
        }
    }
    ut_check(1, "every notch survives the round trip");

    /* A setting typed into the ini between two notches shows as the CLOSER one. */
    ut_check(mouse_look_notch_from_degrees_per_count(0.0296f) == 29,
          "a setting just under a notch rounds to it rather than down");
    ut_check(mouse_look_notch_from_degrees_per_count(0.0306f) == 30,
          "and one just over the next notch rounds up to that one");

    /* Both ends of the slider are bolts against a setting from outside its band. The ini accepts
     * up to 1.0, which is ten times the slider's right-hand end. */
    ut_check(mouse_look_notch_from_degrees_per_count(0.0001f) == 0,
          "a setting below the slider's band sits at its left end");
    ut_check(mouse_look_notch_from_degrees_per_count(1.0f) == MOUSE_SLIDER_NOTCH_COUNT - 1,
          "a setting above it sits at the right end");
    ut_check(mouse_look_notch_from_degrees_per_count((float)sqrt(-1.0)) == 29,
          "a setting that is not a number falls on the default's notch, not on an end");

    ut_near(mouse_look_degrees_per_count_from_notch(-5), 0.001f, 0.00001f,
                "a notch below the slider is clamped to its left end");
    ut_near(mouse_look_degrees_per_count_from_notch(9999), 0.100f, 0.00001f,
                "and one past its right end to that end");
}

/* The degraded path still uses the old clamp, so it still has to behave. */
static void test_clamp_step(void)
{
    ut_near(mouse_look_clamp_step(10.0f, 0.0f, 3000.0f, 25.0f), 10.0f, 0.0001f,
                "with no span the hard cap alone applies");
    ut_near(mouse_look_clamp_step(100.0f, 0.0f, 3000.0f, 25.0f), 25.0f, 0.0001f,
                "and it bounds the step");
    ut_near(mouse_look_clamp_step(100.0f, 0.001f, 3000.0f, 25.0f), 3.0f, 0.0001f,
                "a short span limits harder than the cap");
}

int main(void)
{
    test_smoothing_weight();
    test_release_conserves();
    test_release_total_over_jittery_frames();
    test_release_ignores_a_zero_span_drain();
    test_release_drops_a_poisoned_reservoir();
    test_slider_notches();
    test_clamp_step();

    return ut_summary("mouse_look");
}
