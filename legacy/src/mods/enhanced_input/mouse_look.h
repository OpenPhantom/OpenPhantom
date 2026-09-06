#ifndef MOUSE_LOOK_H
#define MOUSE_LOOK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Turning a stream of mouse reports into one view step per consumed interval.
 *
 * The engine polls the mouse once per rendered frame and does it AFTER that frame's substeps have
 * already run, so a substep can only ever see the previous frame's sample, and every frame that
 * runs no substep is a sample nobody reads. At 144 frames per second roughly four fifths of the
 * hand's movement was polled and overwritten unread.
 *
 * So the samples are collected once per rendered frame and banked, and the bank is drained by
 * whoever consumes, at that consumer's own cadence. The arithmetic of the drain is in mouse_rate.c
 * and it has one property this file depends on completely: a drain removes what it delivers, and
 * it can neither deliver what has not arrived nor deliver against what has. That is what lets two
 * consumers at different cadences share one bank.
 */

/* The engine's own axis reader, resolved out of the call site inside Plr_Steer. Answers the axis
 * already scaled by the binding, not raw device counts. */
typedef float (__cdecl *input_axis_fn_t)(int axis);

/* control_applyMouseDefaults at 0x0046586C registers one binding on function 0, source 6, with
 * flags 0x0C, which is INVERT together with RELATIVE, and scale 0x3E99999A, which is 0.3f. So
 * input_axis(0) answers 0.3 * -lX. The axis itself is registered by
 * stdControl_registerAxis(6, -250, +250, 0.0f) at 0x0048DB64, so its centre is provably 0 and its
 * dead zone is 0.
 *
 * The reader's unit is therefore 0.3 device counts, and a setting expressed in degrees per COUNT,
 * the unit a mouse is actually specified in, has to be divided by it once rather than folded
 * silently into the number the user types.
 *
 * It lives in the header because a second reader now has to answer in the same units to be a
 * drop-in for the first, and a load-bearing constant duplicated across two files is one that
 * rots. */
#define ENGINE_AXIS_SCALE 0.3f

/* ==============================================================================================
 * The pure half: no engine, no configuration, unit-testable on its own.
 * ============================================================================================ */

/* The spike rejector, as the DEGRADED path still uses it. `span_seconds` is the wall time the
 * returned units were collected over, so a sweep across three frames gets three frames' allowance
 * while a single-frame spike gets one frame's. A span that is not positive falls back to
 * `hard_cap_degrees` alone, because a limit measured against no time at all would silently swallow
 * every input.
 *
 * It THROWS AWAY what it clips, which is why the primary path does not use it. A drain there
 * happens once per rendered frame, and the mouse counts arriving in a frame are not proportional
 * to that frame's duration: frame times on real hardware swing by a factor of four or more, so
 * identical hand movement would be clipped on a short frame and passed on a long one, and the
 * difference deleted rather than delayed. That is frame-time jitter converted directly into aim
 * jitter. On the degraded path there is no bank to hold anything back in, so the choice does not
 * arise and the cap is set low enough that the loss is a bound rather than a filter. */
float mouse_look_clamp_step(float degrees, float span_seconds,
                            float max_rate_deg_per_second, float hard_cap_degrees);


/* ==============================================================================================
 * The engine half.
 * ============================================================================================ */

/* Reads the configuration and reports the migration when an old key is still present. */
void mouse_look_load_config(void);

/* Installs the per-frame collector. `reader` is the resolved axis reader and may not be NULL.
 * Never fails outright: without the per-frame hook the degraded path below is used instead, and
 * the log says which one is live. */
void mouse_look_install(input_axis_fn_t reader);

/* The two consumers, and the rule that keeps them apart.
 *
 * Both drain the same bank; what differs is the interval each covers. Phase 2 passes the
 * simulation step and calls it on EVERY run of the thunk, gate open or not, discarding the value
 * when the gate is closed. Free look covers a rendered frame instead and takes the interval from
 * the engine's own frame time.
 *
 * Neither can double-count on the accumulating path, because a take removes what it hands over and
 * cannot reach past what is there. On the DEGRADED path there is no bank and both would answer a
 * live, unzeroed read, so two callers in one frame would each receive the same sample and the turn
 * would be doubled: free look tests mouse_look_is_accumulating() and does not take a second time
 * there. Any future third consumer has to answer the same question. */
float mouse_look_take_substep_degrees(float substep_seconds);
float mouse_look_take_frame_degrees(void);


/* Raise the smoothing ceiling to this build's default for the per-frame view path, unless the
 * player wrote the key themselves, in which case their value stands.
 *
 * It exists because the right ceiling is not a property of the mouse alone but of what consumes it.
 * A simulation step is three times as long as a frame, so the same device puts three times as many
 * reports into it and the boundary matters three times less; a feature that draws the view per
 * frame therefore needs the filter that the per-step path can do without. The measurement that
 * settles it is at the implementation. Called once, by whatever arms that path, and only after it
 * knows it is really running. */
void mouse_look_use_frame_clock_smoothing(void);

/* True when the per-frame collector is live. False means the degraded path: the axis is read once
 * per substep, so motion between substeps is dropped and the effective sensitivity follows the
 * frame rate. */
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
