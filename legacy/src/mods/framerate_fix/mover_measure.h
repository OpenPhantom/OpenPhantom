/* mover_measure.h: what the mover blend's own measurement really produces in a running game.
 *
 * Two ways of deciding when a mover should be drawn were derived from the engine's clock
 * arithmetic, built, and played. Both were worse than the substep alpha they replaced: the first
 * roughly twice as jittery, the second worse again. The arithmetic says neither should be, so one
 * of the two quantities underneath it is not what it is assumed to be, and deriving a third frame
 * lattice by hand would be the same mistake a third time.
 *
 * So this reads the quantities out of the game instead. It is arranged as ranges rather than
 * averages, because a mean is exactly what would hide the case that matters, and it holds no
 * opinion about what the numbers ought to be; the caller's log line says that, next to what it
 * measured.
 *
 * The ranges are pure and driven by a test: a range that reports nothing when it has seen nothing,
 * and one that survives the values a float can carry, are both properties worth pinning down
 * before trusting an instrument to settle an argument. The report itself writes to the log and is
 * here rather than beside the hooks so that the numbers and the note of what each one ought to be
 * sit together.
 */
#ifndef MOVER_MEASURE_H
#define MOVER_MEASURE_H

#include <stdbool.h>
#include <stdint.h>

/* Jitter is a frame that disagrees with its neighbours, not a window whose steps span a range.
 *
 * The first version of this took the largest and smallest consecutive-frame step a subnode made
 * and divided them. That is a speed range, and it condemned perfectly smooth motion: a lift easing
 * into its stop, a door decelerating, a platform reversing all produce ratios of three, six or
 * twenty while moving smoothly, because the smallest step in the window is the last one before the
 * mover stopped. Worse, its two bounds lived in the subnode rather than in the window and were
 * never reset, so a single slowdown tainted that subnode for the rest of the session. It reported
 * 3.222 and then 6.5, and both numbers were noise; a mode that provably drew identical output was
 * judged an improvement on the strength of them.
 *
 * So the test is local. Each frame's step is compared against the mean of the step before it and
 * the step after it, and a frame counts as uneven when it differs from that mean by more than a
 * few per cent. Smooth motion at any speed, accelerating or decelerating, passes; a frame that
 * jumps and a frame that holds both fail, the pair a stepped mover produces.
 * All three steps have to clear the floor, so a mover coming to rest is not judged on the frame it
 * stops.
 */
typedef struct mover_drawn {
    float    last[3];
    uint32_t stamp;      /* the frame that translation was drawn on */
    float    step_before;
    float    step_middle;
    bool     have_before;
    bool     have_middle;
    bool     seen;
} mover_drawn_t;

/* One observed quantity over one reporting window. */
typedef struct mover_range {
    float low;
    float high;
    bool  seen;
} mover_range_t;

typedef struct mover_measurement {
    /* Every call to the mover tick, whether it integrated or not. One per mover per frame, so
     * this divided by the frame count IS the number of movers in the level, which the first
     * window did not measure and could not do without: 41.7 integrating ticks a frame is 42
     * movers integrating on every frame or 78 integrating on half of them, and those two say
     * opposite things about the engine. */
    uint32_t      calls;
    /* How many of those calls actually integrated. Against `calls` rather than against the frame
     * count, so the answer does not depend on knowing the mover count. */
    uint32_t      ticks;
    uint32_t      draws;
    /* How many frames old the pose being drawn is, in frames between the tick that produced it
     * and the draw that reads it. The mover modules install before the object module, but the
     * world geometry modules install before both, so a mover may be DRAWN before it is TICKED on
     * every frame, in which case everything the rejected arithmetic reasoned about was one frame
     * older than it assumed. Zero means drawn in the same frame it moved. */
    mover_range_t pose_age;
    /* How much world time one move covered. */
    mover_range_t interval;
    /* The render time since a mover last moved, which is the quantity both rejected modes were
     * built on. Near zero would mean the alpha is not changing between the two reads. */
    mover_range_t elapsed;
    /* elapsed over interval, which should sweep the whole way from 0 to 1. */
    mover_range_t phase;
    /* The substep alpha itself, for comparison: whatever else is true, this one is known to
     * sweep, because the mode that uses it directly is the one that looks best. */
    mover_range_t alpha;
    /* How much the alpha moves in one rendered frame, sampled once per frame rather than once per
     * pose. This is the residual jitter, measured directly. While the sample pair is stable the
     * drawn advance is proportional to this, so at 60 frames a second against a 32 Hz simulation
     * it should sit at 32/60, which is 0.533, and the width of the range is the size of whatever
     * unevenness is left. A tight range means the mover is smooth and anything still visible is
     * elsewhere; a wide one means the lattice is still not right. The frame where the alpha wraps
     * is skipped, because a negative step there is the pair rolling forward rather than a
     * measurement. */
    mover_range_t alpha_advance;
    /* How OFTEN the advance is uneven, which a range cannot say and which is the whole question
     * once the range is known. Six hundred frames at 0.5333 with two at 0.5840 is smooth motion
     * and a hiccup; a hundred frames high is the cause of what the eye sees. The thresholds are
     * fractions above the exact 32/60: a frame that advances more than one per cent past it has
     * moved everything drawn that much further than the display did. */
    uint32_t      advance_over_1pc;
    uint32_t      advance_over_5pc;
    uint32_t      advance_frames;
    /* The other half of the frames, and the half where a fault would live. On roughly every
     * second frame at 60 fps the alpha wraps, because a new simulation step has begun and the
     * sample pair rolls on to the next one. An earlier version of this skipped those as "the pair
     * rolling rather than an uneven frame", which measured only the easy 47 per cent and then
     * reported the motion as uniform.
     *
     * Across a wrap the drawn position advances by `1 + alpha_new - alpha_old` steps, which comes
     * to the same 32/60 when the pair rolls on exactly the frame the alpha wraps. When it does
     * not, the object jumps by up to a whole step of travel, and the pose age already says the
     * roll does not land on the same frame for every mover. */
    /* Frames judged against their own neighbours, and how many disagreed. This is the one
     * measurement here that is not a proxy for something else, and the worst disagreement says
     * how big the jump was as a fraction of what the neighbours expected. */
    uint32_t      steps_judged;
    uint32_t      steps_uneven;
    float         worst_unevenness;

    mover_range_t wrap_advance;
    uint32_t      wrap_over_1pc;
    uint32_t      wrap_over_5pc;
    uint32_t      wrap_frames;
} mover_measurement_t;

/* Forgets the window. Also the way to initialise one, so a caller never has to know that an
 * unseen range is spelled with its bounds crossed. */
void mover_measure_reset(mover_measurement_t *measurement);

/* The exact per-frame alpha advance at 60 frames a second against a 32 Hz simulation, and the two
 * thresholds counted against it. Named here so the report and the counting cannot disagree. */
#define MOVER_ADVANCE_EXACT_60 0.533333f
#define MOVER_ADVANCE_OVER_1PC (MOVER_ADVANCE_EXACT_60 * 1.01f)
#define MOVER_ADVANCE_OVER_5PC (MOVER_ADVANCE_EXACT_60 * 1.05f)

/* Records one observation. A value that is not finite is ignored rather than allowed to swallow
 * the range: one NaN compared against would otherwise take both bounds with it and the window
 * would report nothing usable for the whole 600 frames it took to gather. */
void mover_measure_note(mover_range_t *range, float value);

/* The bounds, or zero and zero when nothing was seen. A caller printing a range it never filled
 * would otherwise print whichever sentinel this file happens to use, which reads as data. */
void mover_measure_bounds(const mover_range_t *range, float *out_low, float *out_high);

/* Writes one window to the log, with `frames` the number of rendered frames it covered. */
void mover_measure_report(const mover_measurement_t *measurement, uint32_t frames);

/* The three points a window is gathered from, so the hooks carry a call rather than the
 * arithmetic. They live here rather than beside the hooks because the numbers and the note of
 * what each one ought to be belong together, and because the hook file is at its size limit.
 *
 * `mover_measure_frame` is once per rendered frame and owns the alpha's own history, so the
 * caller does not keep it. `mover_measure_tick` is once per integrating tick of one mover.
 * `mover_measure_draw` is once per drawn pose, and `pose_age` is how many whole frames separate
 * the tick that produced that pose from this draw. */
void mover_measure_frame(mover_measurement_t *measurement, float alpha);
void mover_measure_tick(mover_measurement_t *measurement, float interval);
void mover_measure_draw(mover_measurement_t *measurement, float alpha, float elapsed,
                        float interval, uint32_t pose_age);

/* One drawn pose, for the measurement above. `translation` is the blended translation about to be
 * handed to the engine, and `drawn` is that subnode's own history of it. Only consecutive frames
 * are compared, so a subnode that goes off screen and comes back starts again rather than
 * reporting the gap as a step. */
void mover_measure_drawn(mover_measurement_t *measurement, mover_drawn_t *drawn,
                         const float *translation, uint32_t frame_stamp);

#endif /* MOVER_MEASURE_H */
