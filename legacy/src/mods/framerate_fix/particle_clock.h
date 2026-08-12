/* particle_clock.h: draw particles at the instant the frame is actually showing.
 *
 * A particle's position is a closed form in the clock, evaluated from four constants written once
 * at emission, so handing that evaluation a retarded clock is not an interpolation between two
 * samples. It is the exact position at that instant. Nothing accumulates and nothing is stored,
 * which is why this module needs no side table and no per level state.
 *
 * Off unless InterpolateParticles is set, and it says so in the log either way.
 */
#ifndef PARTICLE_CLOCK_H
#define PARTICLE_CLOCK_H

#include <stdbool.h>

void particle_clock_install(bool enabled);

#endif /* PARTICLE_CLOCK_H */
