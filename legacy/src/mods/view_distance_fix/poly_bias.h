/* poly_bias.h: the per-polygon depth bias, and why it can make the fog flicker.
 *
 * Every world polygon carries a signed byte at poly+0x36. The transform pass scales it by 1/64, so
 * up to plus or minus two world units, and ADDS IT TO THE CAMERA-SPACE DEPTH immediately before the
 * projection, which is also where the fog alpha is baked into the vertex's specular.
 *
 * The trouble is the vertex cache. A world vertex is transformed once per frame and its slot is
 * shared by every polygon that references it, so the bias applied is whichever polygon reached that
 * vertex FIRST. On a cache hit the second polygon's bias is never applied at all. Which polygon
 * gets there first depends on the order cells were gathered, and that order changes as the camera
 * moves.
 *
 * So a vertex shared between two polygons with different bias bytes has a fog alpha that flips
 * between two stable values as the camera moves. Two world units against a band a dozen units wide
 * is a large fraction of an eight-bit alpha. It is invisible where the fog is flat, and worst where
 * the gradient is steep, which is why it only shows when the band's end lands inside the draw cut:
 * a band that saturates beyond the cut has no steep gradient anywhere on screen.
 *
 * This switch exists to test that, and it is the whole of the mechanism in one number: the scale
 * has exactly one reader in the image, so writing zero over it makes every polygon's bias zero and
 * changes nothing else. What it costs is what the bias is for, which is depth sorting between
 * coplanar surfaces, so expect flimmer between overlapping faces with it off. It is a diagnostic
 * and a question, not a shipped answer.
 */
#ifndef VIEW_DISTANCE_FIX_POLY_BIAS_H
#define VIEW_DISTANCE_FIX_POLY_BIAS_H

#include <stdbool.h>

/* `enabled` true leaves the engine exactly as it is. False resolves the scale and writes zero over
 * it. Logs which it did either way, and refuses rather than guessing if the site or the operand
 * does not check out. */
void poly_bias_install(bool enabled);

#endif /* VIEW_DISTANCE_FIX_POLY_BIAS_H */
