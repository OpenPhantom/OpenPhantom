/* camera_compensation.h: keep the camera's five per-frame dampers meaning the same thing at
 * any frame rate.
 *
 * bapview_updateCam 0x418544 answers message 0x0D, once per rendered FRAME, and ignores the
 * dt it is handed. It carries five exponential dampers with per-frame factors:
 *
 *     anchor mean        anchor = (anchor + target) * 0.5      (0x418623, 3x, [0x4A8170])
 *     lerpPitch          0.96                                  (push imm, 0x41868F)
 *     lerpYaw            gYawLag [0x8A0100] = 0.96
 *     settleOffset       gPosLag [0x8A0104] = 0.9166667 = 11/12
 *     followYaw          cam = 0.5*cam + 0.5*target            (2x, [0x4A8170])
 *
 * and the two globals are rewritten at the tail of updateCam (0x418FDD..0x419006), with a 0.4
 * pair for CAMREGION_FAST_LAG regions. At 144 fps each damper runs 4.8x as often, so the camera
 * turns rigid.
 *
 * The correction is exact: k' = k^(dt*30). At dt = 1/30 it is the identity, so 30 fps stays
 * byte-identical to the original.
 *
 * Four of the five are operands and are corrected in place. The anchor is not: it is a mean, and
 * no single factor can turn a mean into a rate-correct blend, so its 63 bytes are rewritten as a
 * true lerp instead. That patch stands on its OWN site deliberately, the lag-constant pattern
 * finds nothing in obi.exe, and gating the anchor behind it would have disabled the anchor there
 * without a word in the log.
 *
 * The anchor also feeds three consumers outside the camera: the 3-D sound listener via [0x8A0148],
 * the world render camera via [0x8A0060]+0x18, and an enemy distance gate. All three see the
 * unchanged value at 30 fps and a value closer to the authored one above it.
 */
#ifndef CAMERA_COMPENSATION_H
#define CAMERA_COMPENSATION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 0x00418623..0x00418661 inclusive: the six x87 triples that form the mean. */
#define CAMERA_ANCHOR_RUN_BYTES 63u

/* Resolves the sites, rewrites the anchor run when `compensate_anchor` is set, and prepares the
 * writable pages for the four operand dampers. Returns false only when NOTHING was compensated;
 * each half says in the log what it did and did not get. */
bool camera_compensation_install(bool compensate_anchor);

/* Encodes the replacement for the anchor run: three 18-byte blends against `k_cell`, then NOP to
 * the end of the buffer. Pure, so it is checked without the game, a single wrong ModRM byte
 * here lands mid-x87-sequence and corrupts the camera SILENTLY instead of crashing.
 * `out_size` must be exactly CAMERA_ANCHOR_RUN_BYTES; anything else is refused. */
bool camera_compensation_build_anchor_blend(uint8_t *out, size_t out_size, const void *k_cell);

/* `smoothed_seconds_scale` is dt*30, already low-passed and quantised by the caller.
 * Cheap and idempotent: it writes nothing when the value has not really moved. */
void camera_compensation_update(float smoothed_seconds_scale);

#endif /* CAMERA_COMPENSATION_H */
