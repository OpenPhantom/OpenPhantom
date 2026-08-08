#ifndef MOUSE_LOOK_H
#define MOUSE_LOOK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Turning a stream of per-frame mouse samples into one per-substep view step.
 *
 * The engine polls the mouse once per rendered frame and does it AFTER that frame's substeps have
 * already run, so a substep can only ever see the previous frame's sample, and every frame that
 * runs no substep is a sample nobody reads. At 144 frames per second roughly four fifths of the
 * hand's movement is polled and overwritten unread, and what survives is modulated by where the
 * frame boundaries happened to fall. That is the jitter; it was never the hand.
 *
 * The samples are therefore banked once per rendered frame and consumed whole, once per substep.
 */

/* The engine's own axis reader, resolved out of the call site inside Plr_Steer. Answers degrees
 * already scaled by the binding, not raw device counts, see ENGINE_AXIS_SCALE below. */
typedef float (__cdecl *input_axis_fn_t)(int axis);

/* ==============================================================================================
 * The pure half: no engine, no configuration, unit-testable on its own.
 * ============================================================================================ */

typedef struct mouse_bank {
    float axis_sum;       /* banked axis units since the last consume            */
    float time_sum;       /* wall time those units were collected over, seconds  */
    float idle_seconds;   /* wall time since the last consume                    */
    bool  armed;          /* false while nothing is consuming: see below         */
} mouse_bank_t;

/* Empties the bank and disarms it. */
void mouse_bank_reset(mouse_bank_t *bank);

/* One rendered frame's worth. Does nothing but count the time while the bank is disarmed, and
 * disarms it when `dormant_after_seconds` have passed with nobody consuming.
 *
 * That rule is the pause guard, and it is not a refinement. While the game is paused the engine
 * skips the poll entirely, so the axis reader keeps answering the SAME non-zero number it last
 * saw; a bank that kept adding it would integrate a frozen sample once per frame for the whole
 * length of the pause and dump the total into the first substep afterwards. Going dormant costs
 * exactly one substep of input at the resume, the one that re-arms the bank, and it covers
 * the cutscene case (polled, never consumed) with the same line of code. */
void mouse_bank_frame(mouse_bank_t *bank, float axis_sample, float frame_seconds,
                      float dormant_after_seconds);

/* Takes everything banked, empties the bank and ARMS it. `out_span_seconds` receives the wall
 * time the returned units were collected over, which is what the rate limit below is measured
 * against. Never NULL. */
float mouse_bank_take(mouse_bank_t *bank, float *out_span_seconds);

/* The spike rejector, as the DEGRADED path still uses it. `span_seconds` is the bank's own measured
 * wall time, so a sweep across three frames gets three frames' allowance while a single-frame spike
 * gets one frame's. A span that is not positive falls back to `hard_cap_degrees` alone, because a
 * limit measured against no time at all would silently swallow every input.
 *
 * What this function must not be used for, because it was and it is the defect this file was
 * rewritten to remove: it THROWS AWAY what it clips. On the banked path a drain happens once per
 * rendered frame, so the allowance it computes is one frame's, and the mouse counts arriving in a
 * frame are not proportional to that frame's duration. Frame times on real hardware swing by a
 * factor of four or more, so identical hand movement was clipped on a short frame and passed on a
 * long one, and the difference was deleted rather than delayed. That is frame-time jitter converted
 * directly into aim jitter. The banked path therefore uses mouse_look_release() below. */
float mouse_look_clamp_step(float degrees, float span_seconds,
                            float max_rate_deg_per_second, float hard_cap_degrees);

/* The share of a pending turn that one drain may deliver: `1, exp(-dt/tau)`, so the same fraction
 * of real time always produces the same fraction of the turn no matter how the frames fall. Answers
 * 1.0 for a time constant of zero, i.e. "deliver everything now". */
float mouse_look_smoothing_weight(float span_seconds, float time_constant_seconds);

/* Delivers this drain's share of `*pending` and leaves the rest there. Nothing is ever discarded:
 * a step held back by the bolt is a step delayed by a frame, not a step deleted.
 *
 * In order: the smoothing weight spreads the turn over real time, the rate bolt bounds what one
 * drain may deliver, and the hard cap is the last bolt; it is what keeps a single step below the
 * 180-degree ambiguity, and it is the only one of the three that exists for safety rather than for
 * feel. A drain with no elapsed time delivers NOTHING and leaves `*pending` untouched, which is
 * what keeps the second consumer (free look, once per rendered frame) from emptying the reservoir
 * that the first (phase 2, once per substep) has just filled. */
float mouse_look_release(float *pending, float span_seconds, float time_constant_seconds,
                         float max_rate_deg_per_second, float hard_cap_degrees);

/* How many notches the mouse speed slider offers, and the two conversions between a notch and the
 * setting behind it. The slider reads 1 to 100 in its caption, which is 0.001 to 0.100 degrees per
 * count in steps of exactly 0.001, so the caption is the setting with the decimal point moved and
 * every notch is a round number in that unit. The ini still accepts up to 1.0 for anyone who wants
 * to go past the right-hand end. */
#define MOUSE_SLIDER_NOTCH_COUNT 100

float mouse_look_degrees_per_count_from_notch(int notch);
int   mouse_look_notch_from_degrees_per_count(float degrees_per_count);

/* ==============================================================================================
 * The engine half.
 * ============================================================================================ */

/* Reads the configuration and reports the migration when an old key is still present. */
void mouse_look_load_config(void);

/* Installs the per-frame bank. `reader` is the resolved axis reader and may not be NULL.
 * Never fails outright: without the per-frame hook the degraded path below is used instead, and
 * the log says which one is live. */
void mouse_look_install(input_axis_fn_t reader);

/* One substep's view step in degrees, already limited. Call it on EVERY run of the phase-2 thunk,
 * gate open or not, and discard the value when the gate is closed, a step left in the bank
 * would be applied later, in a state that did not earn it.
 *
 * There is a second consumer, and the rule that keeps the two apart is worth stating here rather
 * than only where it is used. Free look drains the bank again once per rendered frame, at the
 * camera update, so that the camera advances on frames that ran no substep at all. It cannot
 * double-count on the accumulating path, because a take ZEROES and the bank is only refilled at
 * the frame end, i.e. after both drains. On the DEGRADED path below there is no bank and this
 * function answers a live, unzeroed read, so two callers in one frame would each receive the same
 * sample and the turn would be doubled: free look tests mouse_look_is_accumulating() and does not
 * take a second time there. Any future third consumer has to answer the same question. */
float mouse_look_take_step_degrees(void);

/* True when the per-frame bank is live. False means the degraded path: the axis is read once per
 * substep, so motion between substeps is dropped and the effective sensitivity follows the frame
 * rate. */
bool mouse_look_is_accumulating(void);

/* Degrees of view turn per mouse count, as configured. For the installation log and for seeding
 * the slider on the controls screen. */
float mouse_look_degrees_per_count(void);

/* Applies a new sensitivity and saves it. Clamped to the accepted range; a value that is not a
 * number is refused. Returns false when the setting could not be written to the ini, in which case
 * it IS in force for this session but will be back to the old value on the next launch, the
 * caller is expected to say so rather than to claim it was saved. */
bool mouse_look_set_degrees_per_count(float degrees_per_count);

#endif /* MOUSE_LOOK_H */
