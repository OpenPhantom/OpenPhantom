/* frame_wait.c: when the frame wait may sleep ahead of the engine's spin, for how long, and how the
 * margin it leaves to the spin is learned.
 *
 * Every way this can be wrong is invisible in review and costs something in the game. A sleep that
 * runs into the deadline delivers the frame late, and on a synchronised display a late frame misses
 * its refresh. A rule that never sleeps leaves the core spinning, which is the state the feature
 * exists to leave. And a margin that wraps round when it adds its padding turns the largest
 * overshoot there is into the smallest margin there is.
 *
 * The cap rule is its own section: at a cap above 200 frames a second the budget is five
 * milliseconds or less, under the timer's measured worst overshoot, so nothing may sleep there.
 */
#include "unittest.h"

#include "frame_wait.h"

#include <stdint.h>

static void test_the_cap_rule(void)
{
    ut_section("the cap rule");

    ut_check(frame_wait_sleep_us(0, 0u, 2000u) == 0u,
             "uncapped, or a cap nothing applied, never sleeps: there is no "
             "deadline to sleep toward");
    ut_check(frame_wait_sleep_us(-5, 0u, 2000u) == 0u, "a negative cap is not a cap");
    ut_check(frame_wait_sleep_us(201, 0u, 250u) == 0u,
             "above 200 frames a second nothing sleeps, however small the margin");
    ut_check(frame_wait_sleep_us(240, 0u, 250u) == 0u, "240 is above it");
    ut_check(frame_wait_sleep_us(1000, 0u, 0u) == 0u,
             "and so is the highest cap the settings take");
    ut_check(frame_wait_sleep_us(200, 0u, 2000u) == 3000u,
             "at exactly 200 the 5000 us budget sleeps down to the 2000 us margin, 3000 us");
    ut_check(frame_wait_sleep_us(144, 0u, 2000u) == 4944u,
             "at 144 the budget is 6944 us, so a frame that has used none of it sleeps 4944");
    ut_check(frame_wait_sleep_us(60, 0u, 0u) == 16666u,
             "at 60 with no margin at all the whole 16666 us budget is slept");
}

static void test_the_rest_and_the_margin(void)
{
    ut_section("what is left of the budget, against the margin");

    ut_check(frame_wait_sleep_us(144, 2000u, 2000u) == 2944u,
             "a frame that worked 2000 us of 6944 sleeps what is left above the margin");
    ut_check(frame_wait_sleep_us(144, 3943u, 2000u) == 1001u,
             "3001 us left is one microsecond more than the margin plus a "
             "millisecond, so it sleeps");
    ut_check(frame_wait_sleep_us(144, 3944u, 2000u) == 0u,
             "3000 us left is exactly the margin plus a millisecond, and a sleep that short is not "
             "worth waking for, so it spins");
    ut_check(frame_wait_sleep_us(144, 6944u, 2000u) == 0u,
             "a frame that has used its whole budget does not sleep");
    ut_check(frame_wait_sleep_us(144, 9000u, 2000u) == 0u,
             "and neither does one that overran it, which is the host at its limit");
    ut_check(frame_wait_sleep_us(144, 0u, 7550u) == 0u,
             "a margin learned from a 7.3 ms overshoot leaves nothing to sleep at 144");
    ut_check(frame_wait_sleep_us(30, 0u, 0xFFFFFFFFu) == 0u,
             "the largest margin there is does not wrap round when the millisecond is added to it");
    ut_check(frame_wait_sleep_us(30, 0xFFFFFFFFu, 0u) == 0u,
             "and the largest elapsed time there is does not wrap round against the budget");
}

static void fill(frame_wait_margin_t *margin, unsigned count, uint32_t overshoot_us)
{
    unsigned index;

    for (index = 0u; index < count; ++index) {
        frame_wait_margin_note(margin, overshoot_us);
    }
}

static void test_the_margin_is_learned(void)
{
    frame_wait_margin_t margin;

    ut_section("the margin is learned from the timer's overshoot");

    frame_wait_margin_reset(&margin);
    ut_check(frame_wait_margin_us(&margin) == FRAME_WAIT_MARGIN_START_US,
             "a fresh margin starts at the two milliseconds nobody has measured against yet");
    ut_check(FRAME_WAIT_MARGIN_START_US == 2000u, "and that start is 2000 us");

    fill(&margin, FRAME_WAIT_WINDOW - 1u, 100u);
    ut_check(frame_wait_margin_us(&margin) == 2000u,
             "one sample short of a full window changes nothing");
    frame_wait_margin_note(&margin, 100u);
    ut_check(frame_wait_margin_us(&margin) == 350u,
             "a full window of 100 us overshoots gives 100 plus the 250 us pad");
    ut_check(frame_wait_margin_windows(&margin) == 1u, "and counts as one window learned");

    fill(&margin, FRAME_WAIT_WINDOW, 400u);
    ut_check(frame_wait_margin_us(&margin) == 650u,
             "the next window is judged on its own, not mixed with the one before");

    frame_wait_margin_reset(&margin);
    fill(&margin, FRAME_WAIT_WINDOW - 3u, 100u);
    fill(&margin, 3u, 5000u);
    ut_check(frame_wait_margin_us(&margin) == 5250u,
             "three late wakes in 256 reach the 99th percentile, so the margin covers them");

    frame_wait_margin_reset(&margin);
    fill(&margin, FRAME_WAIT_WINDOW - 2u, 100u);
    fill(&margin, 2u, 5000u);
    ut_check(frame_wait_margin_us(&margin) == 350u,
             "two in 256 do not: the percentile lets them through, and the late count shows them");

    frame_wait_margin_reset(&margin);
    fill(&margin, FRAME_WAIT_WINDOW - 3u, 100u);
    frame_wait_margin_note(&margin, 5000u);
    frame_wait_margin_note(&margin, 90u);
    frame_wait_margin_note(&margin, 5000u);
    ut_check(frame_wait_margin_us(&margin) == 350u,
             "the order the samples came in does not matter, only how many were late");

    frame_wait_margin_reset(&margin);
    fill(&margin, FRAME_WAIT_WINDOW, 0xFFFFFFFFu);
    ut_check(frame_wait_margin_us(&margin) == 0xFFFFFFFFu,
             "the pad saturates rather than wrapping the largest overshoot into a tiny margin");

    frame_wait_margin_reset(&margin);
    fill(&margin, FRAME_WAIT_WINDOW, 0u);
    ut_check(frame_wait_margin_us(&margin) == FRAME_WAIT_MARGIN_PAD_US,
             "a perfect timer still leaves the pad to the spin");
    frame_wait_margin_reset(&margin);
    ut_check(frame_wait_margin_us(&margin) == 2000u && frame_wait_margin_windows(&margin) == 0u,
             "a reset starts over from two milliseconds and no windows");
}

static void test_the_histogram(void)
{
    uint32_t bins[8] = { 0u };

    ut_section("the minute's overshoot histogram");

    ut_check(frame_wait_histogram_bin(0u, 10u, 8u) == 0u, "0 us lands in the first bin");
    ut_check(frame_wait_histogram_bin(9u, 10u, 8u) == 0u, "9 us still does");
    ut_check(frame_wait_histogram_bin(10u, 10u, 8u) == 1u, "10 us is the second bin");
    ut_check(frame_wait_histogram_bin(69u, 10u, 8u) == 6u, "69 us is the seventh");
    ut_check(frame_wait_histogram_bin(70u, 10u, 8u) == 7u, "70 us is the last");
    ut_check(frame_wait_histogram_bin(0xFFFFFFFFu, 10u, 8u) == 7u,
             "and everything beyond the range is kept in the last bin rather than dropped");

    ut_check(frame_wait_histogram_percentile(bins, 8u, 10u, 0u, 50u) == 0u,
             "a minute with no samples answers 0 rather than a bin edge");

    bins[0] = 50u;
    bins[5] = 49u;
    bins[6] = 1u;
    ut_check(frame_wait_histogram_percentile(bins, 8u, 10u, 100u, 50u) == 10u,
             "the median of 50 at under 10 us and 50 above is the first bin's upper edge");
    ut_check(frame_wait_histogram_percentile(bins, 8u, 10u, 100u, 51u) == 60u,
             "one sample past the half is in the sixth bin, answered by its upper edge");
    ut_check(frame_wait_histogram_percentile(bins, 8u, 10u, 100u, 99u) == 60u,
             "the 99th of 100 is still in the sixth bin");
    ut_check(frame_wait_histogram_percentile(bins, 8u, 10u, 100u, 100u) == 70u,
             "the 100th is the single sample in the seventh");

    bins[0] = 0u;
    bins[5] = 0u;
    bins[6] = 0u;
    bins[7] = 3u;
    ut_check(frame_wait_histogram_percentile(bins, 8u, 10u, 3u, 50u) == 0xFFFFFFFFu,
             "a percentile in the last bin has no upper edge and says so; the caller caps it with "
             "the worst it saw");
}

int main(void)
{
    test_the_cap_rule();
    test_the_rest_and_the_margin();
    test_the_margin_is_learned();
    test_the_histogram();

    return ut_summary("frame wait");
}
