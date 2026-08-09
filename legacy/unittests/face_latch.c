/* face_latch.c: the one decision that can be wrong in both directions without saying so.
 *
 * The hook turns the engine's "finished" into "still turning" on some simulation steps. Both
 * failure modes are silent and neither produces a crash or a log line:
 *
 *   yields too rarely   the scripted scene never takes its in-progress branch, the actor stalls in
 *                       its state, and in the swamp the player is never given control back. This is
 *                       the bug the module exists to fix, so a predicate that never fires ships a
 *                       feature that installs and does nothing, which reads exactly like success.
 *   yields too often    a completion is delayed that should not have been, in particular for a clip
 *                       that is still moving, and whatever the script does next is held up.
 *
 * So the predicate is checked against every combination that decides it, not against a sample.
 */
#include "unittest.h"

#include "face_latch.h"

#include <stdbool.h>
#include <stdint.h>

#define ENGINE_FINISHED 1
#define ENGINE_RUNNING  0
#define HOLDS           true
#define FREE_RUNNING    false

/* The shipped period. 32 simulation steps per second divided by 16 is 2 Hz, which is the rate the
 * engine produced by itself at the shipped 30 fps. */
#define SHIPPED_PERIOD 16u

static void engine_still_running_is_never_touched(void)
{
    uint32_t step;

    /* The hook may only ever turn a finished answer into an unfinished one. If it could do the
     * reverse it would invent a completion, and the facing command would report a turn that had
     * not happened. */
    for (step = 0; step < 64u; ++step) {
        ut_checkf(!face_latch_should_yield(ENGINE_RUNNING, step, SHIPPED_PERIOD, HOLDS),
                  "a running clip must be passed through at step %u", (unsigned)step);
        ut_checkf(!face_latch_should_yield(ENGINE_RUNNING, step, SHIPPED_PERIOD, FREE_RUNNING),
                  "a running clip must be passed through at step %u even when free running",
                  (unsigned)step);
    }
}

static void a_clip_that_still_moves_is_never_delayed(void)
{
    uint32_t step;

    /* Withholding is only harmless because a clamped clip has already stopped. A clip that loops
     * or free runs has not, so delaying its completion delays the script. */
    for (step = 0; step < 64u; ++step) {
        ut_checkf(!face_latch_should_yield(ENGINE_FINISHED, step, SHIPPED_PERIOD, FREE_RUNNING),
                  "a free running clip must never be delayed, step %u", (unsigned)step);
    }
}

static void zero_period_switches_the_feature_off(void)
{
    uint32_t step;

    /* This is the switch the user is told to set to prove the module and not something else fixed
     * their scene, so it has to be a true no-op rather than a smaller period. */
    for (step = 0; step < 64u; ++step) {
        ut_checkf(!face_latch_should_yield(ENGINE_FINISHED, step, 0u, HOLDS),
                  "period 0 must never yield, step %u", (unsigned)step);
    }
}

static void it_yields_exactly_once_per_period(void)
{
    uint32_t period;

    for (period = 2u; period <= 64u; ++period) {
        uint32_t yields = 0;
        uint32_t step;

        for (step = 0; step < period * 4u; ++step) {
            if (face_latch_should_yield(ENGINE_FINISHED, step, period, HOLDS)) {
                ++yields;
            }
        }
        ut_checkf(yields == 4u,
                  "period %u should yield 4 times over 4 periods, yielded %u",
                  (unsigned)period, (unsigned)yields);
    }
}

static void the_shipped_period_restores_the_thirty_fps_rate(void)
{
    /* At 30 fps the engine took the in-progress branch about twice a second, because that is how
     * often a frame ran two simulation steps back to back. 32 steps per second over a period of 16
     * reproduces that rate, and the point of the fix is to reproduce it at ANY frame rate. */
    uint32_t yields = 0;
    uint32_t step;

    for (step = 0; step < 32u; ++step) {
        if (face_latch_should_yield(ENGINE_FINISHED, step, SHIPPED_PERIOD, HOLDS)) {
            ++yields;
        }
    }
    ut_checkf(yields == 2u, "one second of simulation should yield twice, yielded %u",
              (unsigned)yields);
}

static void the_counter_wrapping_does_not_break_the_cadence(void)
{
    /* The substep counter is a live engine cell that starts at a value the game chose and runs for
     * the life of the process, so the predicate must not assume it starts at zero or stays small. */
    uint32_t yields = 0;
    uint32_t step;

    for (step = 0xFFFFFFF0u; step != 0x00000010u; ++step) {
        if (face_latch_should_yield(ENGINE_FINISHED, step, SHIPPED_PERIOD, HOLDS)) {
            ++yields;
        }
    }
    ut_checkf(yields == 2u, "32 steps across the wrap should yield twice, yielded %u",
              (unsigned)yields);
}

int main(void)
{
    engine_still_running_is_never_touched();
    a_clip_that_still_moves_is_never_delayed();
    zero_period_switches_the_feature_off();
    it_yields_exactly_once_per_period();
    the_shipped_period_restores_the_thirty_fps_rate();
    the_counter_wrapping_does_not_break_the_cadence();

    return ut_summary("face_latch");
}
