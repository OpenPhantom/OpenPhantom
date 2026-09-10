/* mover_weight.c: see mover_weight.h. */
#include "mover_weight.h"

#include "mover_blend.h"

#include <math.h>

bool mover_weight_for(int mode, float elapsed, float interval, float *out_weight)
{
    float phase;

    if (out_weight == NULL) {
        return false;
    }
    if (!isfinite(elapsed) || !isfinite(interval)) {
        return false;
    }
    /* Not a division that can be allowed to happen. A mover whose last move covered no world time
     * has no scale to measure the phase against, and the engine's own short-circuit means that is
     * exactly the case of a mover which has not moved at all. */
    if (!(interval > 0.0f)) {
        return false;
    }

    phase = elapsed / interval;
    if (phase < 0.0f) {
        phase = 0.0f;
    }
    if (phase > MOVER_WEIGHT_PHASE_LIMIT) {
        phase = MOVER_WEIGHT_PHASE_LIMIT;
    }

    switch (mode) {
    case MOVER_WEIGHT_LAG:
        *out_weight = phase;
        return true;
    case MOVER_WEIGHT_LEAD:
        *out_weight = 1.0f + phase;
        return true;
    default:
        /* MOVER_WEIGHT_ALPHA and anything unrecognised. The caller supplies the alpha itself in
         * that mode, so answering false here keeps the one decision in one place rather than
         * having this function hand back a number it was never given. */
        return false;
    }
}
