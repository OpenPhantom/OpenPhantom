/* mp_signatures_world.h: the third site table, the three probes that answer a question about a
 * point in the world.
 *
 * Which point is walkable, which point has room over it, and where the floor under a point is.
 * The engine already knows all three and answers better than any estimate a mod could make, so a
 * re-entry that has to find a free spot beside a living body asks these rather than guessing.
 *
 * The enumeration is mp_world_site_t in mp_signatures.h, which also says why it is a second
 * enumeration and not a third block of mp_site_t. Everything else follows the rules of the other
 * two pattern files: an absolute address is never a required byte, it is wildcarded and read out
 * of the matched operand; a relative displacement is wildcarded too; and every site is declared as
 * a detour target so the two stage resolver survives another module writing a branch over its
 * head.
 */
#ifndef MULTIPLAYER_MP_SIGNATURES_WORLD_H
#define MULTIPLAYER_MP_SIGNATURES_WORLD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mp_signatures.h"

#include "common/signature.h"

/* Resolves the three probes. Returns how many resolved. Safe to call again; the second call
 * redoes the work and reports the same thing. Every site is optional and a caller has to check:
 * one that did not resolve disables the search that needed it rather than letting it guess. */
size_t mp_signatures_world_resolve(void);

/* 0 when the site did not resolve. */
uintptr_t mp_signatures_world_address(mp_world_site_t site);

/* The whole table, MP_WORLD_SITE_COUNT long and indexed by mp_world_site_t, so that a test can
 * check its shape without a game. */
const signature_t *mp_signatures_world_sites(size_t *count);

#endif /* MULTIPLAYER_MP_SIGNATURES_WORLD_H */
