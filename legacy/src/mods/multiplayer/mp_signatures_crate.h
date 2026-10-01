/* mp_signatures_crate.h: the eight calls a push block's life goes through, as byte patterns.
 *
 * Layer 2. Every one is a CALL SITE, found by the instructions that set up its arguments and ended
 * on the call's own E8 with the four operand bytes behind it masked. Four of them are repointed
 * while a session's transport stands, which is why their operands are masked: the pattern has to
 * find its site again after the repointing, and so does every other module's. The other four are
 * only read, for the address of the function they call: the engine's own sink, floor probe,
 * attach and cell occupancy, each vouched for by the call that reaches it rather than by a
 * pattern of its head.
 */
#ifndef MULTIPLAYER_MP_SIGNATURES_CRATE_H
#define MULTIPLAYER_MP_SIGNATURES_CRATE_H

#include "common/signature.h"

#include <stddef.h>
#include <stdint.h>

typedef enum mp_crate_call {
    MP_CRATE_CALL_PUSH,          /* 0x0044EBCD  the player's push phase calls the block's push */
    MP_CRATE_CALL_LAND_SINK,     /* 0x00409921  a landing block with the sink bit is sunk */
    MP_CRATE_CALL_DROP,          /* 0x0040B71C  the push drops a block over an edge */
    MP_CRATE_CALL_CRUSH,         /* 0x0040A28C  the sink crushes whoever stands in its cylinder */
    MP_CRATE_CALL_RESTORE_SINK,  /* 0x0040AEF1  a savegame sinks a block it restores; read only */
    MP_CRATE_CALL_PROBE_FLOOR,   /* 0x0040B6B8  the push asks the floor under the target; read */
    MP_CRATE_CALL_ATTACH,        /* 0x0040B753  the push binds the block to what it stands on */
    MP_CRATE_CALL_NODE_POS,      /* 0x0040B774  the push moves the block's cell occupancy */
    MP_CRATE_CALL_COUNT
} mp_crate_call_t;

/* Resolves the eight sites once and answers the same afterwards; the first call logs one line per
 * site and how long the search took. Answers how many resolved. */
size_t mp_signatures_crate_resolve(void);

/* The address of the E8, or 0 when its site did not resolve or the byte there is not an E8. */
uintptr_t mp_signatures_crate_call(mp_crate_call_t call);

/* The table, for the line that names a site that did not resolve and for the unit test that
 * holds its shape. Every pattern ends on its call's E8 and the four operand bytes behind it. */
const signature_t *mp_signatures_crate_sites(size_t *count);

#endif /* MULTIPLAYER_MP_SIGNATURES_CRATE_H */
