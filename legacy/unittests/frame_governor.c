/* frame_governor.c: the one decision the view distance's frame-time governor makes.
 *
 * Everything else in that module is a clock and a ring buffer. This is the part where getting it
 * wrong is quiet: a governor that lowers a shade too eagerly walks a perfectly good setting down
 * to retail's own draw distance over a couple of minutes and the player only ever notices that
 * the game "looks worse than it used to", and a governor whose two thresholds touch oscillates
 * between two scales for as long as the level lasts, which reads as the fog breathing. Neither
 * crashes, neither logs anything alarming, and neither is visible to somebody who does not
 * already know what the numbers should have been.
 *
 * The dead zone between the thresholds is the property worth pinning down hardest, because it is
 * the one that stops the oscillation, and it is a relationship between two settings rather than
 * anything either of them says on its own.
 */
#include "unittest.h"

#include <stddef.h>

#include "frame_governor.h"

/* The thresholds a 100 fps cap produces: lower below 75 fps (13.33 ms), raise above 86.25 fps
 * (11.59 ms). These are the numbers the field report's own machine would have run with. */
#define LOWER_ABOVE_MS 13.333f
#define RAISE_BELOW_MS 11.594f
#define NEEDED         30u

int main(void)
{
    ut_section("the slow side: one bad second is enough");
    ut_check(frame_governor_decide(15.0f, LOWER_ABOVE_MS, RAISE_BELOW_MS, 0u, NEEDED) ==
                 FRAME_GOVERNOR_LOWER,
             "15 ms a frame, the 66 fps the field report measured, lowers the scale");
    ut_check(frame_governor_decide(15.0f, LOWER_ABOVE_MS, RAISE_BELOW_MS, NEEDED, NEEDED) ==
                 FRAME_GOVERNOR_LOWER,
             "and it lowers however many healthy seconds came before it - pain is not outvoted "
             "by a good history");
    ut_check(frame_governor_decide(13.4f, LOWER_ABOVE_MS, RAISE_BELOW_MS, 0u, NEEDED) ==
                 FRAME_GOVERNOR_LOWER,
             "just past the threshold is past it");

    ut_section("the fast side: a raise has to be earned");
    ut_check(frame_governor_decide(10.0f, LOWER_ABOVE_MS, RAISE_BELOW_MS, NEEDED, NEEDED) ==
                 FRAME_GOVERNOR_RAISE,
             "10 ms a frame, the capped 100 fps, with the full count of healthy seconds behind it");
    ut_check(frame_governor_decide(10.0f, LOWER_ABOVE_MS, RAISE_BELOW_MS, NEEDED - 1u, NEEDED) ==
                 FRAME_GOVERNOR_HOLD,
             "one second short of the count is not yet a raise");
    ut_check(frame_governor_decide(10.0f, LOWER_ABOVE_MS, RAISE_BELOW_MS, NEEDED + 100u, NEEDED) ==
                 FRAME_GOVERNOR_RAISE,
             "and more than enough is still a raise, not an error");

    ut_section("the dead zone, which is what stops it oscillating");
    /* Between the two thresholds the governor must do NOTHING, no matter how long it has been
       healthy. If this band ever closes, a scale that lands inside it is lowered, recovers,
       raised, and lowered again forever. */
    ut_check(frame_governor_decide(12.5f, LOWER_ABOVE_MS, RAISE_BELOW_MS, NEEDED, NEEDED) ==
                 FRAME_GOVERNOR_HOLD,
             "12.5 ms is too fast to lower and too slow to raise, so it holds");
    ut_check(frame_governor_decide(12.5f, LOWER_ABOVE_MS, RAISE_BELOW_MS, 0u, NEEDED) ==
                 FRAME_GOVERNOR_HOLD,
             "the same with no history behind it");
    ut_check(frame_governor_decide(RAISE_BELOW_MS, LOWER_ABOVE_MS, RAISE_BELOW_MS, NEEDED,
                                   NEEDED) == FRAME_GOVERNOR_HOLD,
             "exactly on the raise threshold is not below it, so it holds rather than raising");
    ut_check(frame_governor_decide(LOWER_ABOVE_MS, LOWER_ABOVE_MS, RAISE_BELOW_MS, 0u, NEEDED) ==
                 FRAME_GOVERNOR_HOLD,
             "and exactly on the lower threshold is not past it either: both edges belong to the "
             "dead zone, so a frame time parked on one cannot toggle the scale");

    ut_section("a measurement it cannot believe moves nothing");
    ut_check(frame_governor_decide(0.0f, LOWER_ABOVE_MS, RAISE_BELOW_MS, NEEDED, NEEDED) ==
                 FRAME_GOVERNOR_HOLD,
             "no frames measured yet reads as no opinion, not as infinitely fast");
    ut_check(frame_governor_decide(-1.0f, LOWER_ABOVE_MS, RAISE_BELOW_MS, NEEDED, NEEDED) ==
                 FRAME_GOVERNOR_HOLD,
             "a negative median, which a counter that went backwards would produce, is refused");
    ut_check(frame_governor_decide(15.0f, 0.0f, RAISE_BELOW_MS, 0u, NEEDED) ==
                 FRAME_GOVERNOR_HOLD,
             "an unconfigured lower threshold refuses to lower rather than lowering always");
    ut_check(frame_governor_decide(15.0f, LOWER_ABOVE_MS, 0.0f, 0u, NEEDED) ==
                 FRAME_GOVERNOR_HOLD,
             "an unconfigured raise threshold disables the whole decision, not just the raise: a "
             "governor holding one threshold and not the other is worse than one holding neither");

    ut_section("thresholds the wrong way round are refused, not obeyed");
    /* Inverted thresholds have no dead zone at all, since every frame time is both too slow and fast
       enough - so this is the configuration that would oscillate hardest. It must be rejected. */
    ut_check(frame_governor_decide(12.0f, RAISE_BELOW_MS, LOWER_ABOVE_MS, NEEDED, NEEDED) ==
                 FRAME_GOVERNOR_HOLD,
             "swapped thresholds leave no dead zone, so the decision refuses to act at all");
    ut_check(frame_governor_decide(12.0f, LOWER_ABOVE_MS, LOWER_ABOVE_MS, NEEDED, NEEDED) ==
                 FRAME_GOVERNOR_HOLD,
             "and two equal thresholds are the same defect with the band closed to nothing");

    ut_section("how big a step, from how far off target");
    /* Sizing the step is what replaced an attribution test that could not work; see the header.
       The two ends are what matter: a near miss must not lurch, and a collapse must not crawl. */
    ut_check(frame_governor_step_size(13.2f, 13.333f, 0.15f, 0.10f) == 0.0f,
             "a frame time already inside the target asks for no step at all");
    ut_checkf(frame_governor_step_size(14.6f, 13.333f, 0.15f, 0.10f) > 0.12f,
              "14.6 ms, a 10%% miss, is worth a whole step (%.3f): the measured gain across this "
              "setting's range is about 5 ms, so a 1.3 ms miss is not a rounding error to be "
              "nudged at",
              frame_governor_step_size(14.6f, 13.333f, 0.15f, 0.10f));
    ut_checkf(frame_governor_step_size(13.7f, 13.333f, 0.15f, 0.10f) < 0.06f,
              "but a 3%% miss still only nudges (%.3f), which is what stops it overshooting a "
              "target it is nearly meeting",
              frame_governor_step_size(13.7f, 13.333f, 0.15f, 0.10f));
    ut_checkf(frame_governor_step_size(19.1f, 13.333f, 0.15f, 0.10f) > 0.25f,
              "19.1 ms, the 52 fps the second field run sat at for seven seconds, takes a big one "
              "(%.3f)", frame_governor_step_size(19.1f, 13.333f, 0.15f, 0.10f));
    ut_check(frame_governor_step_size(100.0f, 13.333f, 0.15f, 0.10f) <= 0.15f * 4.0f + 0.0001f,
             "and an absurd frame time is still clamped, to four steps, rather than being a lurch "
             "from the configured scale to the floor in one decision");
    ut_check(frame_governor_step_size(13.4f, 13.333f, 0.15f, 0.10f) >= 0.15f / 3.0f - 0.0001f,
             "a miss too small to measure still moves by the minimum, so it cannot stall just "
             "outside the target");

    ut_section("the two field runs, replayed against the step sizes");
    {
        /* Run one walked to 1.15 at a fixed step. Run two held at 52 fps for seven seconds. Both
           are answered here by how far off target each second was, and the floor is applied the
           way the live governor applies it, so these are scales the game could actually be at. */
        const float run_one[] = { 16.1f, 16.6f, 16.9f, 16.8f, 16.1f, 16.2f, 15.0f, 14.0f, 13.8f };
        const float run_two[] = { 14.7f, 14.8f, 14.8f, 15.2f, 15.7f, 18.9f, 19.1f };
        const float near_target[] = { 13.5f, 13.6f, 13.5f, 13.4f, 13.5f, 13.6f, 13.5f, 13.4f,
                                      13.5f };
        float       scale;
        float       run_one_end;
        size_t      i;

        scale = 2.50f;
        for (i = 0; i < sizeof run_one / sizeof run_one[0]; i++) {
            scale -= frame_governor_step_size(run_one[i], 13.333f, 0.15f, 0.10f);
            if (scale < 1.0f) {
                scale = 1.0f;
            }
        }
        run_one_end = scale;
        /* It still walks a long way down, and that is CORRECT: in that scene the target was not
           reachable at any scale - the field run measured 13.8 ms even at 1.15 - so there was no
           setting the governor could have stopped at and been right. Sizing the step is not a way
           of pretending a scene is cheaper than it is. */
        ut_checkf(run_one_end < 1.40f,
                  "a scene the target cannot be reached in at any scale still walks down, to %.2f",
                  run_one_end);

        scale = 1.90f;
        for (i = 0; i < sizeof run_two / sizeof run_two[0]; i++) {
            scale -= frame_governor_step_size(run_two[i], 13.333f, 0.15f, 0.10f);
            if (scale < 1.0f) {
                scale = 1.0f;
            }
        }
        ut_checkf(scale <= 1.0f,
                  "and run two's collapse to 52 fps is answered rather than held through, "
                  "reaching %.2f where the version before this one sat at 1.90 for seven seconds",
                  scale);

        /* The property that sizing actually buys: nine seconds only just past the target cost far
           less than nine seconds well past it. A fixed step could not tell the two apart. */
        scale = 2.50f;
        for (i = 0; i < sizeof near_target / sizeof near_target[0]; i++) {
            scale -= frame_governor_step_size(near_target[i], 13.333f, 0.15f, 0.10f);
            if (scale < 1.0f) {
                scale = 1.0f;
            }
        }
        ut_checkf(scale > 1.90f,
                  "nine windows only just past the target still cost far less, ending at %.2f, "
                  "where nine seconds well past it ended at %.2f - which is the whole point of "
                  "sizing the step rather than fixing it", scale, run_one_end);
    }

    return ut_summary("the view distance's frame-time governor");
}
