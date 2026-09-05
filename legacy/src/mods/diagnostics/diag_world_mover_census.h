/* diag_world_mover_census.h: the mover integrator call-site census, trigger level 3.
 *
 * The hook that feeds it stays in diag_world.c, on the detour level 2 already installed, and the
 * census is reached only through the three functions below.
 */
#ifndef DIAG_WORLD_MOVER_CENSUS_H
#define DIAG_WORLD_MOVER_CENSUS_H

#include <stdint.h>

/* Finds the integrator's call sites, arms the census and writes what it found into the install
 * record. A `tick_address` of 0 is how an unresolved site arrives, and nothing is armed. */
void mover_census_install(uintptr_t tick_address);

/* Buckets one call to the integrator against the site it came from, and counts whether it had
 * anything to do. */
void mover_census_record(const void *return_address, const uint8_t *mover, float now);

/* One frame of the census. Called from diag_world_census_tick, never registered on its own. */
void mover_census_report(void);

#endif /* DIAG_WORLD_MOVER_CENSUS_H */
