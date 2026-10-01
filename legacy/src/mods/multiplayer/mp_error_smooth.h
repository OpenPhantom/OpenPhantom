/* mp_error_smooth.h: hiding a prediction correction so a body glides instead of snapping.
 *
 * Layer 1, pure logic. A client predicts its own body forward and the host corrects it; without
 * smoothing, the moment a correction arrives the body teleports by the error. The modern answer,
 * from the same consensus the rest of this feature follows, is to never move the simulation by the
 * error: the corrected position jumps, but a separate visual offset absorbs the jump so the drawn
 * position stays continuous, and that offset decays toward zero over the following frames. The
 * drawn body is the corrected position plus the offset.
 *
 * The decay is faster the larger the error, so a small nudge glides gently and a large one is
 * caught up quickly, and an error past a snap threshold is not hidden at all, because sliding a
 * body a long way looks worse than a clean cut. This is a render-side effect: it never touches the
 * authoritative state the correction carries, so it cannot desync anything, only how it looks. It
 * sits above the engine's own interpolation: the engine interpolates the object between substeps,
 * and this offsets where that interpolation is drawn.
 */
#ifndef MULTIPLAYER_MP_ERROR_SMOOTH_H
#define MULTIPLAYER_MP_ERROR_SMOOTH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct mp_error_smooth {
    float offset[3];      /* the current visual offset, decaying toward zero */

    /* Below small_error the offset decays by slow_decay per frame; above large_error by fast_decay;
     * between them the factor is blended. Past snap_error a correction is applied with no offset at
     * all, a clean cut. All in the engine's own position units, chosen by the caller. */
    float small_error;
    float large_error;
    float snap_error;
    float slow_decay;     /* per frame, near 1, a gentle glide */
    float fast_decay;     /* per frame, lower, a quick catch up */
    float settle;         /* an offset shorter than this is set to zero, so it reaches home */
} mp_error_smooth_t;

/* Sets sensible defaults scaled to `unit`, the size of one engine position unit that the caller
 * treats as "small". small_error = unit, large_error = 4*unit, snap_error = 40*unit,
 * slow_decay = 0.90, fast_decay = 0.80, settle = unit/64. The caller may then tune any field. */
void mp_error_smooth_init(mp_error_smooth_t *smooth, float unit);

/* A correction moves the body by `delta` = corrected minus old position. The offset takes the
 * opposite, so the drawn position (corrected + offset) is exactly where it was drawn before, and
 * the jump is invisible. A delta longer than snap_error is not absorbed: the offset is cleared so
 * the body cuts to the corrected position. Returns true when it smoothed, false when it snapped. */
bool mp_error_smooth_correct(mp_error_smooth_t *smooth, const float delta[3]);

/* Decays the offset one frame toward zero, faster the larger it is, and zeroes it once it is
 * within settle, so it reaches home rather than creeping forever. */
void mp_error_smooth_decay(mp_error_smooth_t *smooth);

/* Writes the drawn position, the authoritative position plus the current offset. */
void mp_error_smooth_render(const mp_error_smooth_t *smooth, const float position[3],
                            float out[3]);

/* The current offset's length, for a caller that wants to report or gate on it. */
float mp_error_smooth_magnitude(const mp_error_smooth_t *smooth);

#endif /* MULTIPLAYER_MP_ERROR_SMOOTH_H */
