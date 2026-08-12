/* frame_delta.h: give the engine a frame time whose resolution does not decay with process uptime.
 *
 * The engine derives its frame delta by subtracting the previous timestamp from the current one,
 * and it keeps that timestamp in a float32 holding seconds since the process started, with no reset
 * anywhere in the image. The subtraction is therefore exact minus rounded, and the rounding of the
 * older sample grows as the process stays open: fed a perfectly regular 6.25 ms, the reported delta
 * spreads by 48.8 microseconds after ten minutes of uptime, 195.3 after an hour and 781.3 after two
 * and a quarter hours, and that last figure is 12.5 per cent of the frame.
 *
 * Everything downstream consumes that delta, the simulation accumulator included, so the whole
 * picture inherits the noise. It is continuous rather than periodic, it gets worse the longer the
 * game has been open, and reloading a level does nothing for it.
 *
 * The repair is to compute the delta in double from a counter of our own and write the result into
 * the cell the engine just filled. The engine's own timestamp is left alone.
 *
 * On by default (PreciseFrameTime=1), played and accepted by the maintainer.
 */
#ifndef FRAME_DELTA_H
#define FRAME_DELTA_H

#include <stdbool.h>

/* Installs the frame time repair. Safe to call more than once; the second call does nothing. */
void frame_delta_install(bool enabled);

#endif /* FRAME_DELTA_H */
