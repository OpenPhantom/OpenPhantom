/* menu_art_resample.h: make a menu bitmap the size of the canvas, while the game loads it.
 *
 * ==============================================================================================
 * Why this exists
 *
 * The engine's menu blitter copies one source pixel to one destination pixel. It has no scale term
 * anywhere, so a picture only fills a bigger canvas if the file on disk is bigger. That is what the
 * artwork converter does, and it is why the menus are welded to the one resolution it was run for:
 * below that resolution the whole menu scale stands down, because a canvas larger than the back
 * buffer writes past the end of it.
 *
 * Doing the same replication as the picture is loaded removes the weld. The engine reads a bitmap,
 * converts it to 16 bits, and compresses it into a run length stream. Between the second and third
 * of those the pixels are raw, in the display's own format, and about to be thrown away. Replacing
 * them there with a larger copy costs one pass and nothing downstream needs telling: the compressor
 * leaves the surface header alone, and swpic_draw writes the surface's size into the widget's
 * rectangle on every draw, so the layout and the hit box follow on their own.
 *
 * ==============================================================================================
 * Whole pixel replication, and why nothing else is allowed
 *
 * A 16 bit pixel of exactly zero is TRANSPARENT to this engine. Not a colour key, not a threshold:
 * swrle_compressVBuffer tests `*pixel == 0` and emits a skip run. And the zeros are not only the
 * black an artist drew, because stdBitmap_Convert24to16 truncates, so in 565 any 24 bit pixel with
 * red under 8, green under 4 and blue under 8 collapses onto zero.
 *
 * So a smoothing filter fails in both directions, and the arithmetic was traced rather than
 * assumed. Blending a channel value of 1 against an adjacent transparent 0 at half weight gives
 * 128, and the vertical pass then gives (128 * 256) >> 16, which is 0: a dark but OPAQUE pixel
 * becomes transparent, punching holes in dark artwork. Interpolating the other way, from a
 * transparent pixel toward a visible one, produces near black opaque values across a band one less
 * than the ratio wide, haloing every transparent edge.
 *
 * Whole pixel replication cannot do either, by construction rather than by care: every destination
 * pixel is a bit exact copy of some source pixel, so zero maps to zero and non zero maps to non
 * zero at every ratio. The retired artwork converter used the same rule for the same reason.
 *
 * ==============================================================================================
 * Upward only
 *
 * Below a ratio of 1 the source interval for a destination pixel is shorter than one pixel and may
 * contain no source pixel at all, so source pixels are dropped. A one pixel border does not become
 * thinner, it becomes DASHED: present on some rows and absent on others, which reads as a broken
 * frame. Measured at 3840 to 1920, half the source columns are never sampled.
 *
 * Above a ratio of 1 nothing is ever lost: each source pixel owns an interval at least one pixel
 * long, which always contains an integer, so the map is onto. That is the whole of the difference
 * and it is why this refuses to shrink a picture.
 */
#ifndef MENU_ART_RESAMPLE_H
#define MENU_ART_RESAMPLE_H

#include <stdbool.h>
#include <stdint.h>

/* Which source pixel a destination pixel is a copy of.
 *
 * The exact integer form of "the source pixel under the centre of this destination pixel", written
 * so it never leaves integer arithmetic. The obvious fixed point version is what menu_preview.c
 * does and it is wrong twice: its step is truncated, so at six times it uses 42 where the exact
 * step is 42.667 and the last ten source columns are never reached, and its two axes disagree about
 * where a pixel's centre is by half a source pixel.
 *
 * It agrees with the retired artwork converter on every whole ratio and DIFFERS where the size is
 * rounded, which was measured rather than assumed: 60 columns of 1067 at 640 by 1.667, and 239 rows
 * of 2156 for the one artwork file whose height is odd. The converter divides by the ratio it was
 * asked for, this divides by the size it actually produced, and the two are only the same map when
 * the product is already whole. Both reach every source pixel and neither is wrong; the difference
 * moves a boundary by one destination pixel. This one is preferred because it never leaves integer
 * arithmetic and cannot disagree with its own output size.
 *
 * The largest intermediate is about 31 million for a 3840 wide source at the 4095 ceiling, so the
 * multiply cannot overflow a signed 32 bit value. */
int32_t menu_art_resample_source_index(int32_t dest_index, int32_t dest_extent,
                                       int32_t source_extent);

/* The size a ratio gives a dimension, rounded the way the retired artwork converter rounded it,
 * int(v * ratio + 0.5). Keeping the two identical stops a resampled picture and a converted one
 * differing by a pixel and putting the layout half a pixel out. */
int32_t menu_art_resample_scaled(int32_t value, float ratio);

/* Replicates a 16 bit picture into a caller provided buffer.
 *
 * Pitches are in PIXELS, matching the surface header's own field rather than its byte count. False
 * when anything is not positive, when either axis would shrink, or when a pointer is missing; the
 * destination is untouched in every one of those.
 *
 * Each distinct source row is expanded once and its repeats are copied, which is most of why this
 * is cheap: at six times, five rows in six are a memcpy of a row that is already correct. */
bool menu_art_resample_16(const uint16_t *source, int32_t source_width, int32_t source_height,
                          int32_t source_pitch_pixels,
                          uint16_t *dest, int32_t dest_width, int32_t dest_height,
                          int32_t dest_pitch_pixels);

#endif /* MENU_ART_RESAMPLE_H */
