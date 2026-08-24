/* activation_race_fix.h: refuse exactly one destroy call, for exactly five placements.
 *
 * FUN_00437161 (the activation scan) creates an actor from a placement's STATIC position and an
 * ACTIVATION radius. FUN_00432bf2's own per-tick tail separately destroys it (reason 0) from the
 * actor's LIVE position and a different DEACTIVATION radius. On level geometry that never moves
 * the two never disagree; on the two lift platforms this fix is for, which spawn low and carry
 * their riders upward once the player is close, they can disagree fast enough underfoot to tear an
 * actor down that FUN_00437850's own reason-0 branch immediately re-arms for creation, forever, for
 * as long as the disagreement holds. See lift_droid_fix.h for the full field investigation and
 * activation_race_fix.c for the mechanism, this fix's own field confirmation, and a general,
 * mover-based version that was tried, shipped, and reverted after a played session showed it
 * refusing zero of over two thousand reason-0 destroys it should have caught.
 *
 * While enabled, all five placements still spawn and fight completely normally. Only the specific
 * reason-0 destroy is refused for these five; every other reason, for every placement, known or
 * not, reaches the engine exactly as it always did, and a known placement genuinely killed still
 * leaves the same way (reason 1) it always has. */
#ifndef ACTIVATION_RACE_FIX_H
#define ACTIVATION_RACE_FIX_H

#include <stdbool.h>

void activation_race_fix_install(bool enabled);

#endif /* ACTIVATION_RACE_FIX_H */
