/* effect_clock.h: put the effects that re-roll themselves back on the engine's own fixed clock.
 *
 * Produces: effect_clock.dll
 *
 * Three places in the drawing path draw a fresh random number every time they are drawn: the
 * lightning arcs, the per object flicker flag and the halo brightness jitter. The drawing path
 * runs at the display's rate, so all three speed up with the frame rate. This DLL makes each of
 * them answer from the simulation's substep counter instead.
 *
 * At the frame rate the game was written for this changes nothing. It only takes effect above it,
 * and what it restores is the authored appearance rather than a new one.
 */
#ifndef EFFECT_CLOCK_H
#define EFFECT_CLOCK_H

void effect_clock_install(void);

#endif /* EFFECT_CLOCK_H */
