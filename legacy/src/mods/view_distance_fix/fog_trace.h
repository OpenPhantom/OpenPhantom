/* fog_trace.h: a capture of what the fog band was computed from, frame by frame, after a level
 * load.
 *
 * Testers report the fog flashing over the first seconds of some levels. Three configuration runs
 * narrowed it: it happens on both fog implementations, and it stops when AuthoredFogBand=1 takes
 * the cut and the field of view out of the target. So the target is moving and something feeding
 * it is unstable, and the candidates are all numbers this module already has in its hand once a
 * frame. Reading them off a log is the difference between knowing which one and guessing.
 *
 * It captures rather than logs as it goes. Writing a line per frame at a hundred frames a second
 * is a file write inside the thing being measured, and a stall there changes the easing that is
 * under suspicion. So the samples go into a fixed array and the whole run is written out once,
 * when the band settles or the array fills.
 *
 * Off unless LogFogBand=1. Nothing here runs, and nothing is captured, in an ordinary session.
 */
#ifndef VIEW_DISTANCE_FIX_FOG_TRACE_H
#define VIEW_DISTANCE_FIX_FOG_TRACE_H

#include "fog_regime.h"

#include <stdbool.h>

/* From the ini, once, at install. */
void fog_trace_configure(bool enabled);

/* A level has been adopted: throw away any previous capture and start again. */
void fog_trace_begin(void);

/* One fog tick. `wrote` is whether this tick went on to write the band, so a run can be read for
 * the write cadence as well as for the values. */
void fog_trace_sample(float horizontal_fov_degrees, float reference_cut, float live_cut,
                      float settled_cut, bool cut_observed,
                      const fog_regime_band_t *target, const fog_regime_band_t *current,
                      bool wrote);

/* A frame the tick left early, and the band it found in the record when it did. `branch` is one
 * character naming which return it took. These are the frames the first capture could not see, and
 * a run where they outnumber the sampled ones says the tick is standing aside every other frame
 * rather than easing anything. */
void fog_trace_aside(char branch, float record_start, float record_end);

/* The engine's gather counts for this frame, recorded alongside whatever else the frame did. If
 * these two oscillate while the band does not, the picture is changing because the geometry is,
 * which is the thing the fog has been getting the blame for. */
void fog_trace_counts(uint32_t cells, uint32_t vertices, float frame_seconds);

/* Resolves the engine's own draw-path counters so the capture can record them. Each one comes out
 * of an operand at a unique instruction pattern, with its own address and every other image
 * address in the window wildcarded, so nothing here is written down as an address and the set
 * survives a forced base as well as the three builds that ship at this file size. Every resolved
 * value is range-checked against the loaded image and the whole set is abandoned if any of the
 * thirteen fails. Nothing is read unless LogFogBand is on. */
void fog_trace_counters_install(void);

/* Write the capture out and stop. Safe to call when nothing was captured. */
void fog_trace_flush(const char *why);

#endif /* VIEW_DISTANCE_FIX_FOG_TRACE_H */
