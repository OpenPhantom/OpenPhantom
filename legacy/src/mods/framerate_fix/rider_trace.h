/* rider_trace.h: what the rider blend was handed and what it drew, frame by frame, for one model.
 *
 * The blade drawn out of a hand (object_interpolation.c, and the README's account of it) came
 * back in Mos Espa: every time the player stands beside Obi-Wan and presses a key, with
 * InterpolateRiders on and not with it off. Its cause was never established, and nothing in
 * the DLLs logs anything when it happens, so this is the instrument: with RiderTrace=1 every
 * blend of every object whose model file starts with RiderTraceModel is recorded, the inputs,
 * the two previous positions, the weight, what was drawn, whether the tracker answered or the
 * engine's pair was used, whether the limit refused, and the x87 status and control words at
 * entry; every key going down or up is recorded too, with the frame it happened on, so the two
 * can be laid side by side. Any object's blend that is refused, not finite, or entered with the
 * x87 stack not empty is recorded whatever its model. And once a frame, at the frame's end,
 * the status word is sampled on its own and written whenever the stack pointer in it has moved
 * since the last frame's end: that sample needs no blend hook, so it stands with
 * InterpolateRiders off and every other switch in this DLL off, which is the run that decides
 * whether the drift belongs to this DLL at all.
 *
 * Nothing is formatted inside the blend. The hook may not run a CRT floating-point routine, so
 * a record is a copy of the numbers into a ring, and the frame callback writes the ring out
 * once the draw is over. Off as shipped; a measurement, not a feature.
 */
#ifndef RIDER_TRACE_H
#define RIDER_TRACE_H

#include <stdbool.h>
#include <stdint.h>

/* Reads [framerate_fix] RiderTrace and RiderTraceModel. */
void rider_trace_install(void);

bool rider_trace_on(void);

/* One blend, from inside the hook: copies and integer tests only. `object` is the bapObj. */
void rider_trace_blend(const char *object, uint32_t step, uint32_t gap, float alpha, float weight,
                       const float *mine, bool from_tracker, const float *engine,
                       const float *current, const float *drawn, bool refused);

/* Once a frame, after the draw: writes the ring out and notes the keys. */
void rider_trace_frame(void);

#endif /* RIDER_TRACE_H */
