/* mp_signatures_foot.h: the byte patterns for the engine's footstep tick.
 *
 * Patterns only. The two rows that use them live in mp_signatures_puppet.c with every other row,
 * because a table split costs a merge and mp_signatures.c has no room for one: it stood seven
 * lines under the hard limit before these sites existed.
 *
 * The arrays are extern with an explicit size rather than static with an implicit one, which is
 * what moving a pattern out of its table costs. mp_cell_sites.c records why that matters: the
 * offline verifier once recognised only the static form and thirty four patterns were outside
 * verification for weeks with nothing saying so. Both of these are checked by name against all
 * five shipped images.
 */
#ifndef MULTIPLAYER_MP_SIGNATURES_FOOT_H
#define MULTIPLAYER_MP_SIGNATURES_FOOT_H

#include <stdint.h>

extern const uint8_t SIG_MP_FOOTSTEP_TICK[29];
extern const uint8_t MSK_MP_FOOTSTEP_TICK[29];
extern const uint8_t SIG_MP_FOOT_RUN[31];
extern const uint8_t MSK_MP_FOOT_RUN[31];

/* The first instruction boundary at or past five in the tick's head: push, mov, sub esp,8. The
 * neighbouring foot_run takes six as well, which is a coincidence and not a reason. This is the one
 * definition: the hull asks the table for it, and the name carries the feature's prefix so that a
 * diagnostic hull of the same tick, with a prologue of its own, cannot be taken for it. */
#define MP_FOOTSTEP_TICK_PROLOGUE 6u

#endif /* MULTIPLAYER_MP_SIGNATURES_FOOT_H */
