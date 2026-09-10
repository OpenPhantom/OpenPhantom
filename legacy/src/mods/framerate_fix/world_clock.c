/* world_clock.c: see world_clock.h. */
#include "world_clock.h"

#include <math.h>

float world_clock_substep_time(float requested, float previous, bool have_previous, float step)
{
    float unclamped;

    if (!isfinite(requested)) {
        return requested;      /* not ours to invent a clock out of */
    }
    if (!have_previous || !isfinite(previous) || !isfinite(step) || !(step > 0.0f)) {
        return requested;
    }

    /* A clock that has gone backwards is a level opening, and it is the one event that has to be
     * passed straight through: un-clamping against the previous level's last value would start
     * the new level a whole level's duration in the future, and every mover would swallow that
     * gap on its first tick. */
    if (requested < previous) {
        return requested;
    }

    /* A whole step past the last value, and the requested one is not consulted beyond the
     * backwards test above. Every call to this is one substep of the loop, which advances the
     * simulation by exactly one step, so a step past the last value is exactly right every time.
     *
     * An earlier version passed the requested value through whenever it was already a step or
     * more ahead, on the theory that such a call was on the lattice already and should be left
     * alone. It is on the SIMULATION's lattice, which sits a fraction of a step away from this
     * one, so any frame running two or more substeps snapped across to it: the offset was not
     * constant as the header claimed, and the frame it moved on advanced a mover by 1.4 steps.
     * Measured, not argued. */
    unclamped = previous + step;
    return unclamped;
}
