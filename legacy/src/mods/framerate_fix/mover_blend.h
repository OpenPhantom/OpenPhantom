/* mover_blend.h: the arithmetic that draws a mover between two simulation steps.
 *
 * Separated from the engine facing half because it is pure. No addresses, no detours, no host
 * image, so it can be driven from a console test with the awkward cases spelled out one at a time.
 */
#ifndef MOVER_BLEND_H
#define MOVER_BLEND_H

#include <stdbool.h>

/* The engine's subnode pose is twelve floats: three rows of the 3x3 at subnode+0x14, then the
 * translation vec3 at subnode+0x38. The split is proven by the engine's own previous pose copy at
 * 0x00409AC0, which reads [+0x38] into prevWorldT at [+0x7C], and 0x38 is 0x14 plus nine floats.
 * Anything using this has to keep that order; it is the engine's layout, not ours. */
#define MOVER_WORLD_FLOATS 12
#define MOVER_ROW_COUNT     3
#define MOVER_TRANSLATION   9

/* Refuse the rotation when a row turns further than this in one simulation step. 0.707 is the
 * cosine of 45 degrees, which is the threshold the Quake family uses for the same decision. The
 * fastest continuous mover in the authored data turns 29.8 degrees per step, so this leaves half
 * again as much room, and what it really catches is a discontinuity rather than a fast turn. */
#define MOVER_ROTATION_COS_MINIMUM 0.707f

/* A row shorter than this is not a basis any more and nothing can be recovered from its
 * direction. */
#define MOVER_ROW_LENGTH_MINIMUM 1.0e-6f

/* Blends `previous` toward `current` by `alpha` into `out`, all three in the layout above.
 *
 * `translation_limit` is the furthest the translation may legitimately move in one simulation
 * step; a larger jump is a teleport rather than motion and is not smoothed. Pass a value of zero
 * or less to disable that test.
 *
 * Returns false when the pair must not be blended, in which case `out` holds `current` unchanged
 * and the caller already has the right answer to draw. It never returns a partly written result.
 */
bool mover_blend_world(float *out,
                       const float *previous,
                       const float *current,
                       float alpha,
                       float translation_limit);

#endif /* MOVER_BLEND_H */
