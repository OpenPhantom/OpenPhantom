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

/* The widest weight this will act on.
 *
 * The guard used to be the open interval (0, 1), which is right for a weight that is the substep
 * alpha and wrong for anything else. It cost two whole play sessions. Two modes were added that
 * computed a weight from the mover's own clock instead, one producing [0, 1] and one [1, 2], and
 * this guard refused every pose either of them ever produced: the caller draws the newest pose on
 * a refusal, so both modes came out as the stepped behaviour they were meant to replace, one of
 * them alternating with a blended frame. The arithmetic they rested on was never executed once,
 * and it was written up as a fault in that arithmetic on the strength of how the game looked.
 *
 * So the range is explicit. Zero is a legitimate weight, meaning draw the earlier sample exactly,
 * and a weight above one extrapolates past the newer one. Those modes and their MoverBlendMode
 * key are gone, since the jitter they chased was the render cap not matching the display, and
 * the one caller left passes the substep alpha, which never leaves [0, 1]. The bound stays at
 * two, one whole interval past the newer sample and the furthest any caller ever asked for, so
 * the test still walks the range a guard once refused without a word. */
#define MOVER_BLEND_WEIGHT_MAX 2.0f

/* WHY A REFUSAL HAS A NAME NOW.
 *
 * Every rejection below draws the newest pose instead, which is a one-frame forward jump for that
 * mover, and they were all counted in one bucket. That bucket read 35 to 84 poses per 600 frames
 * against ten to twenty thousand blended, which looks like nothing and says nothing: a total under
 * one per cent is equally consistent with a handful of movers refusing once and with one subnode
 * of one platform refusing every few frames, and only the second is visible. The same blindness
 * hid the weight guard for two whole play sessions. */
enum {
    MOVER_BLEND_OK = 0,
    /* Outside the weight range, or not a number. */
    MOVER_BLEND_WEIGHT_RANGE,
    /* Exactly 1.0, which draws the newer sample byte for byte and is the identity at 32 fps. */
    MOVER_BLEND_IDENTITY,
    /* A basis row too short to have a direction, so nothing can be recovered from it. */
    MOVER_BLEND_ROW_LENGTH,
    /* A row turning further in one step than motion can account for. */
    MOVER_BLEND_ROTATION,
    /* The translation moving further in one step than a mover legitimately can. */
    MOVER_BLEND_TRANSLATION,
    /* The blended basis could not be made orthonormal again. */
    MOVER_BLEND_BASIS,
    MOVER_BLEND_REASON_COUNT
};

/* Blends `previous` toward `current` by `alpha` into `out`, all three in the layout above.
 *
 * `out_reason` receives one of the values above, and may be NULL. It is written on success as
 * well, so a caller cannot read a stale reason and believe it.
 *
 * `translation_limit` is the furthest the translation may legitimately move in one simulation
 * step; a larger jump is a teleport rather than motion and is not smoothed. Pass a value of zero
 * or less to disable that test.
 *
 * Returns false when the pair must not be blended, in which case `out` holds `current` unchanged
 * and the caller already has the right answer to draw. It never returns a partly written result.
 * A weight below zero, above MOVER_BLEND_WEIGHT_MAX, or not a number is one of those cases.
 */
bool mover_blend_world(float *out,
                       const float *previous,
                       const float *current,
                       float alpha,
                       float translation_limit,
                       int *out_reason);

#endif /* MOVER_BLEND_H */
