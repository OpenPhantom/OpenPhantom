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
/* `scripted_camera` is true while a script places the camera rather than the player driving it.
 * The anchor weight then goes to ZERO, which is not the engine's value and is deliberate.
 *
 * The anchor is not only what the camera is drawn from. bapview_buildCamView composes the eye
 * from it, and bapdraw_drawWorld collects the cells to draw in a circle centred on that eye. So
 * the anchor is the ORIGIN OF THE WORLD GATHER, and any lag in it is a gather that trails the
 * camera: cells ahead of the eye fall outside the radius and are never collected, which is world
 * geometry silently not drawn. The count stays the same because the radius does; it is the
 * membership that is wrong, which is why measuring the cell count never showed it.
 *
 * A followed camera wants the damping, and 30 fps behaviour restored is the right answer for it.
 * A placed camera wants none: the target it is given is already interpolated smoothly between
 * substeps, so a weight of zero puts the anchor exactly on it and the gather exactly under the
 * camera. That is also what the free camera does, by writing the anchor directly, and is why the
 * free camera has never shown this fault. */
void camera_compensation_update(float smoothed_seconds_scale, bool scripted_camera);

#endif /* CAMERA_COMPENSATION_H */
