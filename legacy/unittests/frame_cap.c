/* frame_cap.c: which cap to use, given a setting, a display and what the machine can hold.
 *
 * The decision is three lines and it took an evening to learn why it matters. Nothing in the
 * shipped stack ties produced frames to shown ones: the DirectDraw flip the game presents through
 * used to be scheduled for the next vertical retrace, and the wrapper that translates it now uses
 * an immediate presentation interval. So a cap below the refresh rate means the display repeats
 * frames on an irregular pattern, and the motion judders however correct the interpolation is.
 *
 * Measured, on the machine that reported the jitter: a cap of 100 on a 144 Hz screen leaves 44
 * refreshes a second repeating a frame, a cap of 144 leaves none, and a cap of 60 on a 90 Hz Deck
 * OLED leaves 30.
 *
 * The case worth its own name is the one in the middle: asked to match a display that will not
 * say. Uncapping or capping at nothing are both worse than the number the player already gave.
 *
 * The second half is the step policy. The refresh is only the right cap while the machine holds
 * it, and the fractions of the refresh are the only other caps that are even, so the policy walks
 * between them on a second's evidence. Every way it can be wrong is a feel: stepping on a hiccup,
 * stepping on a menu or a level load, flapping through a doorway, or stepping up onto a cap the
 * next second cannot hold.
 */
#include "unittest.h"

#include "frame_cap.h"

/* One second at 144 Hz with the given long frames, the rest comfortably short. */
static frame_cap_second_t second_of(unsigned frames, unsigned over_budget, unsigned over_faster,
                                    float longest_ms)
{
    frame_cap_second_t s;

    s.frames       = frames;
    s.over_budget  = over_budget;
    s.over_faster  = over_faster;
    s.longest_work = longest_ms / 1000.0f;
    return s;
}

int main(void)
{
    ut_section("not matching, so the setting stands");

    ut_check(frame_cap_effective(100, false, 144, 1) == 100,
             "the configured cap is used whatever the display reports");
    ut_check(frame_cap_effective(0, false, 144, 1) == 0,
             "and uncapped stays uncapped, which is a real choice rather than an absent one");
    ut_check(frame_cap_effective(100, false, 144, 3) == 100,
             "the divisor means nothing without matching");

    ut_section("matching a display that answers");

    ut_check(frame_cap_effective(100, true, 144, 1) == 144,
             "144 Hz replaces the 100 that would repeat 44 frames a second");
    ut_check(frame_cap_effective(60, true, 90, 1) == 90,
             "and 90 replaces the 60 that made a Deck OLED judder");
    ut_check(frame_cap_effective(0, true, 144, 1) == 144,
             "matching beats uncapped as well, which is the point: smooth without a loaded core");

    ut_section("matching a display that will not say");

    /* VREFRESH answers 0 or 1 for a driver reporting a hardware default rather than a rate. The
     * configured number has to stand: uncapping the game or capping it at nothing are both worse
     * than the answer the player already gave. */
    ut_check(frame_cap_effective(100, true, 0, 1) == 100,
             "an unknown refresh leaves the configured cap alone rather than uncapping");
    ut_check(frame_cap_effective(100, true, 1, 1) == 100,
             "and so does the 1 a driver returns for a hardware default");
    ut_check(frame_cap_effective(0, true, 0, 1) == 0,
             "with nothing configured either, uncapped is still what was asked for");
    ut_check(frame_cap_effective(100, true, 0, 2) == 100,
             "a divisor cannot divide a refresh nobody reported");

    ut_section("rates that are not rates");

    ut_check(frame_cap_effective(100, true, 19, 1) == 100,
             "a reported rate below anything a display runs at is refused");
    ut_check(frame_cap_effective(100, true, 1001, 1) == 100, "and so is one above it");
    ut_check(frame_cap_effective(-5, false, 0, 1) == 0,
             "a negative setting reads as uncapped rather than as a negative frame time");
    ut_check(frame_cap_effective(5000, false, 0, 1) == 1000,
             "and an absurd one is clamped to the range the setting documents");

    ut_section("fractions of the refresh");

    ut_check(frame_cap_effective(0, true, 144, 2) == 72, "half of 144 is 72");
    ut_check(frame_cap_effective(0, true, 144, 3) == 48, "a third is 48");
    ut_check(frame_cap_effective(0, true, 144, 4) == 36, "a quarter is 36");
    ut_check(frame_cap_effective(0, true, 240, 4) == 60, "and a quarter of 240 is 60");
    ut_check(frame_cap_effective(0, true, 75, 2) == 38,
             "an odd refresh rounds to the nearest whole frame rather than truncating");
    ut_check(frame_cap_effective(0, true, 144, 0) == 144, "a divisor of 0 reads as 1");
    ut_check(frame_cap_effective(0, true, 144, 9) == 36,
             "and one past the maximum is held at the maximum");

    ut_section("the floor");

    ut_check(frame_cap_divisor_limit(144) == 4, "144 may go down to a quarter, 36");
    ut_check(frame_cap_divisor_limit(90) == 3, "90 may go to a third, 30, but not a quarter");
    ut_check(frame_cap_divisor_limit(60) == 2, "60 may go to a half, 30, and no further");
    ut_check(frame_cap_divisor_limit(50) == 1, "50 cannot step at all: 25 is under the floor");
    ut_check(frame_cap_effective(0, true, 60, 4) == 30,
             "so asking 60 Hz for a quarter gets the half instead");

    ut_section("stepping down on an overrun second");
    {
        unsigned            clean = 0;
        frame_cap_second_t  s;

        s = second_of(144, 0, 0, 6.5f);
        ut_check(frame_cap_step(1, &clean, &s, 144) == 1, "a clean second at the refresh holds");

        s = second_of(144, 2, 0, 18.0f);
        ut_check(frame_cap_step(1, &clean, &s, 144) == 1,
                 "two long frames in a second are a hiccup, not a reason to step");

        s = second_of(144, 14, 0, 9.0f);
        ut_check(frame_cap_step(1, &clean, &s, 144) == 1,
                 "a tenth exactly still holds; the step needs more than a tenth");

        s = second_of(115, 115, 0, 8.6f);
        ut_check(frame_cap_step(1, &clean, &s, 144) == 2,
                 "the cutscene second: every frame overran, so the cap halves");

        s = second_of(72, 72, 0, 15.0f);
        ut_check(frame_cap_step(2, &clean, &s, 144) == 3, "and halves again when 72 will not do");

        s = second_of(36, 36, 0, 40.0f);
        ut_check(frame_cap_step(4, &clean, &s, 144) == 4,
                 "at the last fraction it stays there rather than inventing a fifth");

        s = second_of(45, 45, 0, 40.0f);
        ut_check(frame_cap_step(3, &clean, &s, 90) == 3,
                 "and on a 90 Hz screen a third is the last fraction, because a quarter is 22");

        s = second_of(0, 0, 0, 0.0f);
        ut_check(frame_cap_step(2, &clean, &s, 144) == 2,
                 "a second with no frames decides nothing");

        /* The first run in the field stepped down twice for nothing: once on a level load, whose
         * frames all overran with an 86 ms one among them, and once on a second of eight frames
         * with one long one, which is a menu. Neither is the machine failing to hold the cap. */
        s = second_of(91, 91, 0, 85.8f);
        ut_check(frame_cap_step(1, &clean, &s, 144) == 1,
                 "a second holding a stall is a level load and decides nothing");
        s = second_of(8, 1, 0, 14.0f);
        ut_check(frame_cap_step(1, &clean, &s, 144) == 1,
                 "and so does a second of eight frames, however long one of them was");
        s = second_of(20, 20, 0, 45.0f);
        ut_check(frame_cap_step(1, &clean, &s, 144) == 2,
                 "twenty frames of 45 ms is a machine that cannot hold the cap, and it steps");
    }

    ut_section("stepping back up needs several clear seconds");
    {
        unsigned            clean = 0;
        frame_cap_second_t  s;
        unsigned            i;
        int                 divisor = 2;

        s = second_of(72, 0, 0, 4.0f);
        for (i = 1; i < FRAME_CAP_CLEAN_SECONDS; ++i) {
            divisor = frame_cap_step(divisor, &clean, &s, 144);
        }
        ut_check(divisor == 2, "four clear seconds are not yet enough");
        divisor = frame_cap_step(divisor, &clean, &s, 144);
        ut_check(divisor == 1, "the fifth takes it back to the refresh");
        ut_check(clean == 0, "and the count starts again from there");

        /* A crowded second in the run resets it. */
        divisor = 2;
        clean   = 0;
        s = second_of(72, 0, 0, 4.0f);
        divisor = frame_cap_step(divisor, &clean, &s, 144);
        divisor = frame_cap_step(divisor, &clean, &s, 144);
        s = second_of(72, 0, 30, 6.5f);
        divisor = frame_cap_step(divisor, &clean, &s, 144);
        ut_check(divisor == 2 && clean == 0,
                 "a second whose frames would crowd the faster cap starts the count over");

        /* But a hiccup inside a clear second does not. */
        s = second_of(72, 1, 1, 12.0f);
        for (i = 0; i < FRAME_CAP_CLEAN_SECONDS; ++i) {
            divisor = frame_cap_step(divisor, &clean, &s, 144);
        }
        ut_check(divisor == 1, "one long frame a second is within the allowance, so it climbs");

        /* At the refresh there is nothing above to climb to. */
        clean = 0;
        s = second_of(144, 0, 0, 3.0f);
        for (i = 0; i < 2 * FRAME_CAP_CLEAN_SECONDS; ++i) {
            divisor = frame_cap_step(1, &clean, &s, 144);
        }
        ut_check(divisor == 1, "and at the refresh it stays at the refresh");

        /* A second that decides nothing does not count as clear either. */
        divisor = 2;
        clean   = 0;
        s = second_of(72, 0, 0, 4.0f);
        for (i = 1; i < FRAME_CAP_CLEAN_SECONDS; ++i) {
            divisor = frame_cap_step(divisor, &clean, &s, 144);
        }
        s = second_of(5, 0, 0, 4.0f);
        divisor = frame_cap_step(divisor, &clean, &s, 144);
        ut_check(divisor == 2, "a paused second is not the fifth clear one");
    }

    ut_section("the two directions do not fight");
    {
        unsigned            clean = 0;
        frame_cap_second_t  s;

        s = second_of(72, 0, 0, 4.0f);
        (void)frame_cap_step(2, &clean, &s, 144);
        (void)frame_cap_step(2, &clean, &s, 144);
        s = second_of(72, 60, 60, 20.0f);
        ut_check(frame_cap_step(2, &clean, &s, 144) == 3 && clean == 0,
                 "an overrun steps down and forgets the clear seconds before it");
    }

    return ut_summary("frame cap");
}
