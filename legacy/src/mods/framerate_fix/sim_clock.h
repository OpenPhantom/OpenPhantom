/* sim_clock.h: keep the interpolation phase from precessing as a level runs.
 *
 * The simulation target is a float32 accumulating level time, so the rounding of its per frame
 * addition grows with the level: one unit in the last place is about 0.95 microseconds ten seconds
 * in, 15.3 at two hundred seconds and 61 at six hundred. The interpolation alpha is the fraction
 * between that clock and the simulation time, so as the rounding grows the alpha ladder precesses,
 * and periodically one substep is drawn across four frames instead of five. Everything drawn
 * between simulation steps then moves about 25 per cent too fast for one frame.
 *
 * Alpha depends only on the difference of the two clocks, and subtracting the same representable
 * amount from both leaves that difference bit for bit. The world clock, which every mover compares
 * for exact equality, keeps its absolute value.
 *
 * On by default (RebaseSimClock=1), played and accepted by the maintainer.
 */
#ifndef SIM_CLOCK_H
#define SIM_CLOCK_H

#include <stdbool.h>

void sim_clock_install(bool enabled);

/* How much to take off both clocks, or 0 when it is not time yet. Split out and declared here
 * because it is the one claim this feature stands on and it is pure arithmetic: the amount must be
 * a power of two no greater than the live value, which is what makes both subtractions exact and
 * therefore leaves the difference the interpolation depends on untouched. Driven by the unit test.
 */
double sim_clock_rebase_step(float live);

/* Called once per rendered frame, from outside the substep loop. */
void sim_clock_sample(void);

#endif /* SIM_CLOCK_H */
