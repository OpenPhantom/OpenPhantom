/* frame_clock.h: the engine's own per-frame delta, in seconds.
 *
 * Lifted out of mouse_look.c, which had resolved it because the mouse bank needed to age a sample,
 * and then found itself lending it to the camera. The clock is not the mouse's: it is the interval
 * the engine last drew a frame over, and anything that moves on the RENDER clock rather than the
 * 32 Hz simulation clock wants it. The passive camera is the second such reader and would otherwise
 * have reached into the mouse for something that has nothing to do with a mouse.
 *
 * Read-only. Nothing here writes an engine cell.
 */
#ifndef ENHANCED_INPUT_FRAME_CLOCK_H
#define ENHANCED_INPUT_FRAME_CLOCK_H

#include <stdbool.h>

/* Resolves g_frameDelta out of the frame hook's own site operand. Optional: a failure is a named
 * degraded mode rather than a refusal, and every reader below then gets 0. */
bool frame_clock_install(void);

/* The engine's frame delta in seconds, or 0 when the cell did not resolve. Zero is deliberately the
 * same answer a stopped clock gives, because every caller already has to refuse a non-positive
 * interval rather than divide by it. */
float frame_clock_seconds(void);

#endif /* ENHANCED_INPUT_FRAME_CLOCK_H */
