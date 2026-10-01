/* mp_signatures_blade.h: the byte patterns for the two heads a far Jedi's blade is drawn through.
 *
 * Patterns only. The two rows that use them live in mp_signatures_puppet.c with every other row,
 * because a table of their own would need a merge and neither pattern file has room for the bytes:
 * mp_signatures.c stands at the hard limit and mp_signatures_puppet.c close to it.
 *
 * The arrays are extern with an explicit size, which is what moving a pattern out of its table
 * costs and what the offline verification reads by name. The names are this feature's own: the
 * diagnostics match the same two heads under names of their own, and a shared name would fold the
 * two into one wherever patterns are compared by name.
 */
#ifndef MULTIPLAYER_MP_SIGNATURES_BLADE_H
#define MULTIPLAYER_MP_SIGNATURES_BLADE_H

#include <stdint.h>

extern const uint8_t SIG_MP_THING_DISPATCH[22];
extern const uint8_t MSK_MP_THING_DISPATCH[22];
extern const uint8_t SIG_MP_HALO_DRAW_FOR_THING[22];

/* The first instruction boundary at or past five in each head: push ebp, mov ebp,esp, push ecx,
 * and then a compare of seven bytes against an absolute cell in the dispatcher and of four bytes
 * against the first argument in the halo pass. */
#define MP_THING_DISPATCH_PROLOGUE      11u
#define MP_HALO_DRAW_FOR_THING_PROLOGUE 8u

#endif /* MULTIPLAYER_MP_SIGNATURES_BLADE_H */
