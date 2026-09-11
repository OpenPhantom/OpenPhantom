/* frame_cap.c: which cap to use, given a setting and a display.
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
 */
#include "unittest.h"

#include "frame_cap.h"

int main(void)
{
    ut_section("not matching, so the setting stands");

    ut_check(frame_cap_effective(100, false, 144) == 100,
             "the configured cap is used whatever the display reports");
    ut_check(frame_cap_effective(0, false, 144) == 0,
             "and uncapped stays uncapped, which is a real choice rather than an absent one");

    ut_section("matching a display that answers");

    ut_check(frame_cap_effective(100, true, 144) == 144,
             "144 Hz replaces the 100 that would repeat 44 frames a second");
    ut_check(frame_cap_effective(60, true, 90) == 90,
             "and 90 replaces the 60 that made a Deck OLED judder");
    ut_check(frame_cap_effective(0, true, 144) == 144,
             "matching beats uncapped as well, which is the point: smooth without a loaded core");

    ut_section("matching a display that will not say");

    /* VREFRESH answers 0 or 1 for a driver reporting a hardware default rather than a rate. The
     * configured number has to stand: uncapping the game or capping it at nothing are both worse
     * than the answer the player already gave. */
    ut_check(frame_cap_effective(100, true, 0) == 100,
             "an unknown refresh leaves the configured cap alone rather than uncapping");
    ut_check(frame_cap_effective(100, true, 1) == 100,
             "and so does the 1 a driver returns for a hardware default");
    ut_check(frame_cap_effective(0, true, 0) == 0,
             "with nothing configured either, uncapped is still what was asked for");

    ut_section("rates that are not rates");

    ut_check(frame_cap_effective(100, true, 19) == 100,
             "a reported rate below anything a display runs at is refused");
    ut_check(frame_cap_effective(100, true, 1001) == 100, "and so is one above it");
    ut_check(frame_cap_effective(-5, false, 0) == 0,
             "a negative setting reads as uncapped rather than as a negative frame time");
    ut_check(frame_cap_effective(5000, false, 0) == 1000,
             "and an absurd one is clamped to the range the setting documents");

    return ut_summary("frame cap");
}
