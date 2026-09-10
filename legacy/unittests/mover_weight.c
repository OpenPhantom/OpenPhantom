/* mover_weight.c: when a mover should be drawn, as opposed to how far it may move.
 *
 * A mover is not on the clock the rest of the draw is on. It moves once per rendered frame and
 * integrates against the world clock, which only the substep loop writes, so its newest pose sits
 * at the moment of the last frame that ran a substep: never in the future, and above 32 frames a
 * second usually in the past. What shipped applied the substep alpha to it anyway, which is a
 * measure of the simulation clock and describes neither end of the mover's own move.
 *
 * The lattice section below is the point of the file. It walks a 60 fps frame pattern with the
 * substep pattern the accumulator really produces and asks the one question that matters: does
 * the drawn moment advance by the same amount every frame. A constant lag is smooth and a varying
 * one is judder, whatever its size.
 */
#include "unittest.h"

#include "mover_weight.h"

#include <math.h>

/* 60 fps against a 32 Hz simulation. Worked through by hand from the accumulator: the target
 * advances 1/60 a frame, the simulation only ever moves in whole 1/32 steps, and a frame runs a
 * substep only when the target has caught up. The interval a mover's move covers settles at two
 * frames, 1/30 s, and the phase alternates with it. */
#define FRAME_SECONDS    (1.0 / 60.0)
#define INTERVAL_SECONDS (1.0 / 30.0)

int main(void)
{
    float weight;
    bool  answered;
    int   n;
    bool  advance_is_even;
    float previous_drawn = 0.0f;
    float worst_uneven = 0.0f;

    ut_section("a mover whose last move covered nothing");

    answered = mover_weight_for(MOVER_WEIGHT_LAG, 0.0f, 0.0f, &weight);
    ut_check(!answered, "an interval of zero has no phase and is refused rather than divided by");

    answered = mover_weight_for(MOVER_WEIGHT_LAG, 0.01f, -1.0f, &weight);
    ut_check(!answered, "and so is an interval that runs backwards");

    ut_section("values a division cannot rank");

    ut_check(!mover_weight_for(MOVER_WEIGHT_LAG, (float)NAN, 0.03f, &weight),
             "an elapsed time that is not a number is refused");
    ut_check(!mover_weight_for(MOVER_WEIGHT_LAG, 0.01f, (float)NAN, &weight),
             "and so is an interval that is not one");
    ut_check(!mover_weight_for(MOVER_WEIGHT_LAG, (float)INFINITY, 0.03f, &weight),
             "an endless elapsed time is refused as well");

    ut_section("the two answers differ by exactly one whole move");

    /* Halfway through the interval since the mover last moved. The lag answer draws it halfway
     * along the move it has already made, one whole move behind; the lead answer draws it half a
     * move past the newest pose the engine has produced. */
    (void)mover_weight_for(MOVER_WEIGHT_LAG, 0.5f, 1.0f, &weight);
    ut_near(weight, 0.5f, 0.0001, "lag mode is the phase itself");
    (void)mover_weight_for(MOVER_WEIGHT_LEAD, 0.5f, 1.0f, &weight);
    ut_near(weight, 1.5f, 0.0001, "and lead mode is one more than the phase");

    (void)mover_weight_for(MOVER_WEIGHT_LAG, 0.0f, 1.0f, &weight);
    ut_near(weight, 0.0f, 0.0001,
            "on the frame the mover moved on, lag mode draws the pose it moved FROM");
    (void)mover_weight_for(MOVER_WEIGHT_LEAD, 0.0f, 1.0f, &weight);
    ut_near(weight, 1.0f, 0.0001,
            "and lead mode draws the one it moved to, which is where that frame wants it");

    ut_section("a phase past the end of the move");

    /* More render time has passed than the last move covered, so the mover has stopped being
     * ticked, or the frame took a hitch, or a level boundary went by. There is no evidence about
     * where it would have gone next and extrapolating would invent some. */
    (void)mover_weight_for(MOVER_WEIGHT_LEAD, 5.0f, 1.0f, &weight);
    ut_near(weight, 2.0f, 0.0001,
            "lead mode stops one whole move past the newest pose and no further");
    (void)mover_weight_for(MOVER_WEIGHT_LAG, 5.0f, 1.0f, &weight);
    ut_near(weight, 1.0f, 0.0001, "and lag mode stops at the newest pose");

    /* The alpha is not guaranteed to stay inside its interval right after a rate change, so a
     * small negative phase is a real measurement rather than a broken one. */
    (void)mover_weight_for(MOVER_WEIGHT_LAG, -0.25f, 1.0f, &weight);
    ut_near(weight, 0.0f, 0.0001, "a phase before the move began is held at its start");

    ut_section("the mode that reproduces what shipped");

    ut_check(!mover_weight_for(MOVER_WEIGHT_ALPHA, 0.5f, 1.0f, &weight),
             "mode 0 answers nothing here, because the caller applies the alpha itself");
    ut_check(!mover_weight_for(99, 0.5f, 1.0f, &weight),
             "and a mode nobody recognises answers nothing rather than guessing");

    ut_section("a 60 fps lattice, which is where judder would show");

    /* The frame pattern, worked out from the accumulator: at 60 fps against 32 Hz a substep runs
     * on some frames and not others, and the mover moves only on the frames that ran one. What is
     * checked is that the drawn moment still advances by one frame of time on every frame. */
    advance_is_even = true;
    for (n = 0; n < 240; ++n) {
        double target = (double)n * FRAME_SECONDS;
        double moved_at;
        double elapsed;
        double drawn;

        /* The mover last moved on the most recent frame whose target had caught the simulation,
         * which lands on a two frame cadence at this rate. */
        moved_at = floor(target / INTERVAL_SECONDS) * INTERVAL_SECONDS;
        elapsed  = target - moved_at;

        if (!mover_weight_for(MOVER_WEIGHT_LAG, (float)elapsed, (float)INTERVAL_SECONDS,
                              &weight)) {
            advance_is_even = false;
            break;
        }

        /* Where that weight actually puts it in time: the start of the move it has made, plus the
         * weight of that move. Lag mode should sit one whole move behind the frame's own target,
         * on every single frame. */
        drawn = (moved_at - INTERVAL_SECONDS) + (double)weight * INTERVAL_SECONDS;

        if (n > 2) {
            float uneven = (float)fabs((drawn - previous_drawn) - FRAME_SECONDS);

            if (uneven > worst_uneven) {
                worst_uneven = uneven;
            }
        }
        previous_drawn = (float)drawn;
    }
    ut_check(advance_is_even, "every frame of the lattice had a weight");
    ut_checkf(worst_uneven < 1.0e-6f,
              "the drawn moment advances by one frame of time on every frame, worst error %.2e s",
              (double)worst_uneven);

    return ut_summary("mover weight");
}
