/* frame_governor.h: back the view distance off when it is costing more than it is worth.
 *
 * The cell watchdog beside this one guards correctness. It lowers the scale when the draw table or
 * the vertex cache is about to overflow, because the alternative is torn geometry or a crash. It
 * knows nothing about how fast the game is running, so a scale that is merely expensive walks
 * straight past it. Field report, issue #35:
 *
 *   "The tank at the beginning of the gardens of theed makes the fps drop considerably"
 *
 * Measured on the reporter's own machine, GARDEN, the cutscene the tank drives out of:
 *
 *   ViewRangeScale 2.50 : 14.5-15.4 ms a frame, 64-69 fps, cpu 21-26 ms/frame, gpu 15-27 %
 *   ViewRangeScale 1.00 : 10.00 ms a frame, 100.0 fps flat, cpu 12-13 ms/frame, gpu unchanged
 *
 * The cell watchdog never fired during either. Peak usage never came near the alarm and nothing
 * overflowed; the scene was simply four times the geometry to transform.
 *
 * The unchanged gpu figure is the mechanism. This engine transforms world geometry on the CPU, the
 * way a 1999 title does, so a longer view is more CPU work per frame and no more work for the
 * card. Frame time is the only instrument that can see that cost, because a card 20 % busy at both
 * settings says nothing at all.
 *
 * ==============================================================================================
 * Why it converges instead of oscillating
 *
 * Any governor that lowers when slow and raises when fast can sit in a loop: lower, recover, raise,
 * slow again. Four things keep this one still.
 *
 *   * It acts on the median frame time of a whole window, so one long frame, a level load or a
 *     stall outside the process, cannot move it at all.
 *   * The two thresholds are apart. It lowers below `backoff` and only raises above `backoff` plus
 *     a margin, so the band between them is a dead zone it settles into rather than crosses.
 *   * It is impatient about pain and slow about recovery. One bad window lowers, and it takes
 *     thirty consecutive good seconds to give the first step back, so leaving a heavy scene
 *     restores the setting while a scene merely near the threshold never rings.
 *   * The step is sized by how far off target the window was, which keeps it from overshooting
 *     without having to guess why the frame rate is what it is. Barely off target, barely move;
 *     badly off target, move properly.
 *
 * ==============================================================================================
 * The attribution test that was tried here first, and why it is gone
 *
 * A step whose frame time did not improve looks like proof that the view distance is not the cost,
 * so an earlier version refused to step again until one did. Two field runs later:
 *
 *     14.7 ms (68 fps)  View scale 2.05 -> 1.90.
 *     14.8 ms (67 fps)  ... the last step to 1.90 did not improve it (14.7 ms then)
 *     15.7 ms (64 fps)  ... the last step to 1.90 did not improve it (14.7 ms then)
 *     18.9 ms (53 fps)  ... the last step to 1.90 did not improve it (14.7 ms then)
 *     19.1 ms (52 fps)  ... the last step to 1.90 did not improve it (14.7 ms then)
 *
 * It held at 52 fps for seven consecutive seconds. The comparison was against the frame time at
 * the moment of the step, and in a scene whose own cost is rising that measures the scene rather
 * than the step, so it blamed the step for the scene and refused to act when it was needed most.
 *
 * The confound has no fix, because there is no way to hold a moving scene still while attributing
 * a quarter of a millisecond to one step. The test is gone and the size of the step does its job
 * instead. It is written down because the reasoning is appealing and will be thought of again.
 *
 * It never goes below 1.0, which is retail's own draw distance, and never above what the reader
 * configured. Everything it does is logged with the number that caused it.
 */
#ifndef FRAME_GOVERNOR_H
#define FRAME_GOVERNOR_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    FRAME_GOVERNOR_HOLD = 0,
    FRAME_GOVERNOR_LOWER,
    FRAME_GOVERNOR_RAISE
} frame_governor_action_t;

/* The whole decision, pure, so that it can be tested without a frame or a clock.
 *
 * `median_ms` is the middle frame time of the window just finished. `lower_above_ms` and
 * `raise_below_ms` are the two thresholds, and the caller is responsible for the second being the
 * smaller. The dead zone between them is what keeps this still. `healthy_seconds` counts the
 * consecutive windows already spent under `raise_below_ms`, and a raise needs
 * `healthy_seconds_needed` of them.
 *
 * A median of zero, or thresholds that are not a sensible pair, answer HOLD. A governor that
 * cannot tell what it is looking at must do nothing rather than guess. */
frame_governor_action_t frame_governor_decide(float median_ms,
                                              float lower_above_ms,
                                              float raise_below_ms,
                                              uint32_t healthy_seconds,
                                              uint32_t healthy_seconds_needed);

/* How big a step down, from how far short of target the window was.
 *
 * `median_ms` over `lower_above_ms` is the shortfall: 1.1 is ten per cent past what the frame time
 * is allowed to be, 1.4 is badly off. The answer is `base_step` scaled by that shortfall against
 * `full_step_shortfall`, the shortfall at which a whole step is the right answer, then clamped so
 * that neither end can produce a step of nothing or a lurch.
 *
 * Sizing rather than gating is what makes this converge. The steps shrink as the target is
 * approached, so it settles instead of overshooting, and they grow when the scene turns sharply
 * heavier, so a collapse to 52 fps is answered in a second or two rather than seven. A frame time
 * already inside the target answers zero, since there is nothing to correct. */
float frame_governor_step_size(float median_ms, float lower_above_ms, float base_step,
                               float full_step_shortfall);

/* `backoff_fps` of 0 means "work it out": three quarters of framerate_fix's own TargetFps when
 * there is a frame cap to miss, and a plain 50 when the frame rate is uncapped and there is no
 * target to measure against. `enabled` false installs the whole thing as a no-op that says so
 * once, because a governor that is off and a governor that never had to act must not read the
 * same way in the log. */
void frame_governor_configure(bool enabled, float backoff_fps, float configured_scale);

/* Called once per frame, before the cell watchdog. `cell_ceiling` is the lowest scale the cell
 * watchdog has imposed, which this must never raise above. Correctness outranks comfort, and
 * without that term the two would take turns undoing each other every few seconds. */
void frame_governor_on_frame(float *effective_view_scale, float configured_scale,
                             float cell_ceiling);

/* Forgets everything it has learned. Called when ViewRangeScale changes on disk, for the same
 * reason the cell watchdog is reset there: the reader has just said what they want, and a
 * governor still braked from a setting nobody is asking for any more would silently ignore it. */
void frame_governor_reset(float configured_scale);

/* Switches it on or off while the game runs, which is how the developer overlay's own row reaches
 * it. Turning it OFF hands the configured scale straight back rather than leaving the view where
 * the governor happened to have walked it to: a reader who has just said "stop moving my draw
 * distance" means the one they asked for, not the one they were left with. */
void frame_governor_set_enabled(bool enabled, float configured_scale);

#endif /* FRAME_GOVERNOR_H */
