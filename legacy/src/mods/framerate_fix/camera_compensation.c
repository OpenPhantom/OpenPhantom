#include "camera_compensation.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"
#include "common/signature.h"

#include <windows.h>

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* --- 0x00418FDD  bapview_updateCam tail: the four camera-lag immediates ---------------------- *
 *   C7 05 04018A00 CDCCCC3E   gPosLag = 0.4          (CAMREGION_FAST_LAG)  imm at +0x06
 *   C7 05 00018A00 CDCCCC3E   gYawLag = 0.4                                imm at +0x10
 *   EB 14
 *   C7 05 04018A00 ABAA6A3F   gPosLag = 0.9166667                          imm at +0x1C
 *   C7 05 00018A00 8FC2753F   gYawLag = 0.96                               imm at +0x26
 *
 * These are absolute-address writes, and the pattern finds nothing at all in obi.exe, the
 * recompile. That is the reason the anchor patch further down is not gated on this site. */
static const uint8_t SIG_CAMERA_LAG_CONSTANTS[] = {
    0xC7, 0x05, 0x04, 0x01, 0x8A, 0x00, 0xCD, 0xCC, 0xCC, 0x3E,
    0xC7, 0x05, 0x00, 0x01, 0x8A, 0x00, 0xCD, 0xCC, 0xCC, 0x3E,
    0xEB, 0x14,
    0xC7, 0x05, 0x04, 0x01, 0x8A, 0x00, 0xAB, 0xAA, 0x6A, 0x3F,
    0xC7, 0x05, 0x00, 0x01, 0x8A, 0x00, 0x8F, 0xC2, 0x75, 0x3F
};
#define OFFSET_POSITION_LAG_FAST   0x06
#define OFFSET_YAW_LAG_FAST        0x10
#define OFFSET_POSITION_LAG_NORMAL 0x1C
#define OFFSET_YAW_LAG_NORMAL      0x26
#define CAMERA_LAG_PAGE_BYTES        48

/* --- 0x0041868A  bapview_lerpPitch: push 0x3F75C28F (0.96) into bapview_lerpAngle ------------ */
static const uint8_t SIG_CAMERA_PITCH_LAG[] = {
    0x50, 0x8B, 0x4D, 0xF0, 0x51, 0x68, 0x8F, 0xC2, 0x75, 0x3F, 0xE8
};
#define OFFSET_PITCH_LAG_IMMEDIATE 0x06
#define PITCH_LAG_PAGE_BYTES         16

/* --- 0x00418F6D  bapview_followYaw: two `fld dword [0x4A8170]` (0.5) ------------------------- *
 *   0x418F6D  fld [0x4A8170] / fmul [ebp-0x20]   the camera angle, which gets k
 *   0x418F76  fld [0x4A8170] / fmul [ebp-0x70]   the target angle, which gets 1-k
 *
 * The two operand fields are nine bytes apart, and repointing one without the other is the worst
 * outcome available here, which is why the pair is written transactionally further down. */
static const uint8_t SIG_CAMERA_FOLLOW_YAW[] = {
    0xD9, 0x05, 0x70, 0x81, 0x4A, 0x00, 0xD8, 0x4D, 0xE0,
    0xD9, 0x05, 0x70, 0x81, 0x4A, 0x00, 0xD8, 0x4D, 0x90, 0xDE, 0xC1
};
static const int OFFSET_FOLLOW_YAW_OPERAND[2] = { 0x02, 0x0B };

/* --- 0x00418623  bapview_updateCam: the anchor mean, 63 bytes rewritten in place ------------- *
 *
 *   D9 45 C8  D8 45 E4  D9 5D C8      anchor.x += at.x        (3x, one axis per line)
 *   D9 45 C8  D8 0D <0.5>  D9 5D C8   anchor.x *= [0x4A8170]  (3x)
 *
 * that is, anchor = (anchor + at) * 0.5, an arithmetic mean evaluated once per rendered frame.
 * [ebp-0x38/-0x34/-0x30] is the live gView->anchor, read from [0x8A011C]+0x14 at 0x418609 and
 * written back at 0x418737; [ebp-0x1C/-0x18/-0x14] is the alpha-interpolated look-at target,
 * finished at 0x4185E2 and never written inside the run.
 *
 * The three absolute operands are what make this unique: the first 27 bytes alone also match a
 * plain vector add at 0x433AC1. All 63 together match exactly once, in all three builds, including
 * obi.exe, where the lag-constant pattern above finds nothing.
 *
 * The run also carries three .reloc HIGHLOW entries, the three disp32. The images declare no
 * DYNAMICBASE, so they are never applied; under a forced-ASLR policy they would rewrite the
 * operands, the pattern would find zero matches and this patch would decline. Fail closed. */
static const uint8_t SIG_CAMERA_ANCHOR_MEAN[] = {
    0xD9, 0x45, 0xC8, 0xD8, 0x45, 0xE4, 0xD9, 0x5D, 0xC8,
    0xD9, 0x45, 0xCC, 0xD8, 0x45, 0xE8, 0xD9, 0x5D, 0xCC,
    0xD9, 0x45, 0xD0, 0xD8, 0x45, 0xEC, 0xD9, 0x5D, 0xD0,
    0xD9, 0x45, 0xC8, 0xD8, 0x0D, 0x70, 0x81, 0x4A, 0x00, 0xD9, 0x5D, 0xC8,
    0xD9, 0x45, 0xCC, 0xD8, 0x0D, 0x70, 0x81, 0x4A, 0x00, 0xD9, 0x5D, 0xCC,
    0xD9, 0x45, 0xD0, 0xD8, 0x0D, 0x70, 0x81, 0x4A, 0x00, 0xD9, 0x5D, 0xD0
};
#define ANCHOR_RUN_BYTES  CAMERA_ANCHOR_RUN_BYTES
#define ANCHOR_AXIS_BYTES 18u

/* ebp-relative slot displacements, as they appear in the ModRM disp8 field. */
static const uint8_t ANCHOR_SLOT[3] = { 0xC8, 0xCC, 0xD0 };   /* gView->anchor  x, y, z */
static const uint8_t TARGET_SLOT[3] = { 0xE4, 0xE8, 0xEC };   /* the look-at target     */

/* --- 0x00418715  bapview_updateCam: the yaw deadband, and it is a per-frame threshold -------- *
 *   D9 45 8C           fld  [ebp-0x74]            ; |turn|, this frame's heading change
 *   D8 1D 78814A00     fcomp[0x4A8178] = 0.05     ; <- operand at +0x05, one reader in the image
 *
 * A turn of zero is what makes bapview_followYaw take its rigid snap arm instead of easing. The
 * threshold is compared against a per-frame angle, so it scales with the frame rate: a 15 deg/s
 * turn is 0.5 deg/frame at 30 fps, which eases, and 0.03 deg/frame at 500 fps, which snaps. Around
 * that boundary the camera flips between hard locked and eased many times a second, which is
 * exactly what rubber-banding looks like. Scaled by dt*30 it means the same angular rate at any
 * frame rate. */
static const uint8_t SIG_CAMERA_YAW_DEADBAND[] = {
    0xD9, 0x45, 0x8C, 0xD8, 0x1D, 0x78, 0x81, 0x4A, 0x00, 0xDF, 0xE0,
    0xF6, 0xC4, 0x01, 0x74, 0x0A
};
#define OFFSET_YAW_DEADBAND_OPERAND 0x05

enum {
    SITE_CAMERA_LAG_CONSTANTS,
    SITE_CAMERA_PITCH_LAG,
    SITE_CAMERA_FOLLOW_YAW,
    SITE_CAMERA_YAW_DEADBAND,
    SITE_CAMERA_ANCHOR_MEAN,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY("camera_lag_constants", SIG_CAMERA_LAG_CONSTANTS),
    SIGNATURE_ENTRY("camera_pitch_lag",     SIG_CAMERA_PITCH_LAG),
    SIGNATURE_ENTRY("camera_follow_yaw",    SIG_CAMERA_FOLLOW_YAW),
    SIGNATURE_ENTRY("camera_yaw_deadband",  SIG_CAMERA_YAW_DEADBAND),
    SIGNATURE_ENTRY("camera_anchor_mean",   SIG_CAMERA_ANCHOR_MEAN)
};

_Static_assert(sizeof(SIG_CAMERA_ANCHOR_MEAN) == ANCHOR_RUN_BYTES,
               "the anchor pattern must span the whole run it replaces, 0x418623..0x418661");
_Static_assert(3u * ANCHOR_AXIS_BYTES <= ANCHOR_RUN_BYTES,
               "the replacement must fit inside the run it overwrites");

/* The retail values, as bit patterns turned into floats. */
#define LAG_POSITION_NORMAL 0.91666669f   /* 0x3F6AAAAB = 11/12 */
#define LAG_YAW_NORMAL      0.95999998f   /* 0x3F75C28F */
#define LAG_FAST            0.40000001f   /* 0x3ECCCCCD */
#define LAG_PITCH           0.95999998f   /* 0x3F75C28F, pushed at 0x41868F */
#define BLEND_HALF          0.50000000f   /* [0x4A8170] */
#define YAW_DEADBAND        0.05f

/* The two cells that replace [0x4A8170] in bapview_followYaw, and why there are two of them.
 *
 * followYaw computes  cam = 0.5*cam + 0.5*target  as two separate instructions, each with its own
 * operand field. That is a weighted blend, so it compensates correctly, but only if the two
 * weights are k and 1-k. Feeding both the same k gives weights summing to 2k, which diverges.
 *
 * Neither cell has a file-scope initialiser, unlike anchor_k below, which invites the question of
 * what the first drawn frame reads if the per-frame hook never runs. It is not a hazard:
 * camera_compensation_install seeds both with 0.5 before it resolves the table, so an operand
 * cannot end up pointing at a cell holding zero. Written down because the asymmetry with anchor_k
 * raises the question every time somebody reads this file. */
static volatile float follow_yaw_k;
static volatile float follow_yaw_one_minus_k;
static volatile float yaw_deadband_cell = YAW_DEADBAND;

/* The blend weight the rewritten anchor run multiplies by, read by three `fmul` in host .text.
 * It is seeded with the retail 0.5 at file scope as well as before the write, because the very
 * first frame after the patch reads it whether or not the per-frame hook ever runs.
 *
 * The operand is unconditional: this DLL must never be unloaded. That is the project's rule for
 * every feature DLL, but here it decides whether the camera reads a float or freed memory. */
static volatile float anchor_k = BLEND_HALF;

typedef struct camera_compensation_state {
    bool      patchable;
    bool      anchor_patched;
    uintptr_t position_lag_fast;
    uintptr_t yaw_lag_fast;
    uintptr_t position_lag_normal;
    uintptr_t yaw_lag_normal;
    uintptr_t pitch_lag;
    float     last_scale;
    bool      last_scripted;
} camera_compensation_state_t;

static camera_compensation_state_t camera_state = { false, false, 0, 0, 0, 0, 0, -1.0f };

/* ============================================================================================
 * Both operands or neither, because one of the two is worse than none of them.
 *
 * The blend is cam = w1*cam + w2*target, written as two instructions with an operand each, and it
 * stays a blend only while the two weights sum to one. Its fixed point is at
 *
 *     cam* = target * w2 / (1 - w1)
 *
 * With both operands repointed, w1 = k and w2 = 1-k, so cam* is exactly target for every k and the
 * arm can only ever lag. With only the first repointed, w1 = k while w2 stays on the engine's 0.5.
 * At 90 frames per second k = 0.5^(30/90) = 0.7937, the weights sum to 1.294, and
 *
 *     cam* = 0.5 / (1 - 0.7937) = 2.42 * target
 *
 * so the camera settles at 2.42 times the angle it is following, and the quadrant unwrap
 * downstream then sees angles it was never built for. The error is exact at 30 frames per second,
 * where k is 0.5, and grows with every frame above that. Leaving both alone merely leaves the
 * camera feeling rigid, so half the work is far worse than none of it.
 *
 * That outcome is not reachable on any shipped build. The pattern is unmasked and carries the
 * target cell at both slots, so a successful resolve already guarantees both verifications pass,
 * and the two operands are nine bytes apart, so it would take a write that failed on one of them
 * and succeeded on its neighbour. It is defended against anyway because of how much runs through
 * here, not because the failure is likely: MouseLookRigidCamera=1 forces the plain arm only on
 * armed frames, so this blend runs on every released frame, which means every cutscene, every
 * authored camera region, and every frame at all when free look is off or enhanced_input is not
 * installed. A partial repoint would surface during cutscenes, which is the worst place to debug.
 *
 * So the pair is transactional. A failure on the second write rolls the first one back, and if
 * even the rollback fails the log says so at error level, because that is the one path that leaves
 * the engine with mismatched weights and nothing later can repair it.
 *
 * Returns the number of operands left repointed, which is only ever 0 or 2. */
static int repoint_follow_yaw(void)
{
    uintptr_t site = sites[SITE_CAMERA_FOLLOW_YAW].address;
    uintptr_t first;
    uintptr_t second;
    uint32_t  expected;

    if (site == 0) {
        return 0;
    }
    first  = site + (uintptr_t)OFFSET_FOLLOW_YAW_OPERAND[0];
    second = site + (uintptr_t)OFFSET_FOLLOW_YAW_OPERAND[1];

    if (!memory_read_u32(first, &expected)) {
        return 0;
    }

    /* patch_repoint_operand verifies the current operand before writing, so a build whose
     * constant sits elsewhere degrades to "not compensated" rather than to a wild write. */
    if (patch_repoint_operand(first, expected, (uint32_t)(uintptr_t)&follow_yaw_k)
        != PATCH_RESULT_OK) {
        log_warning("the first followYaw weight at %08X could not be repointed, so neither was: "
                    "the camera's yaw arm keeps its per-frame 0.5 and feels rigid above 30 fps",
                    (unsigned)first);
        return 0;
    }

    if (patch_repoint_operand(second, expected, (uint32_t)(uintptr_t)&follow_yaw_one_minus_k)
        == PATCH_RESULT_OK) {
        return 2;
    }

    if (patch_repoint_operand(first, (uint32_t)(uintptr_t)&follow_yaw_k, expected)
        == PATCH_RESULT_OK) {
        log_warning("the second followYaw weight at %08X could not be repointed, so the first at "
                    "%08X was put back. The yaw arm is uncompensated, which is the same behaviour "
                    "as CompensateCamera=0.",
                    (unsigned)second, (unsigned)first);
    } else {
        log_error("the second followYaw weight at %08X could not be repointed and the first at "
                  "%08X could not be restored. The two blend weights no longer sum to one, so the "
                  "camera will overshoot its target rather than lag it. Set CompensateCamera=0.",
                  (unsigned)second, (unsigned)first);
    }
    return 0;
}

static void repoint_yaw_deadband(void)
{
    uintptr_t site = sites[SITE_CAMERA_YAW_DEADBAND].address;
    uintptr_t operand;
    uint32_t  expected;

    if (site == 0) {
        log_warning("camera_yaw_deadband did not resolve, above about 200 fps the camera will "
                    "alternate between snapping and easing (the rubber-band symptom)");
        return;
    }

    operand = site + OFFSET_YAW_DEADBAND_OPERAND;
    if (!memory_read_u32(operand, &expected)) {
        return;
    }
    if (patch_repoint_operand(operand, expected, (uint32_t)(uintptr_t)&yaw_deadband_cell)
        == PATCH_RESULT_OK) {
        log_info("yaw deadband operand at %08X repointed, the camera no longer flips into its "
                 "rigid-snap arm just because the frame got shorter", (unsigned)operand);
    }
}

/* ============================================================================================
 * The anchor: a mean cannot be compensated, so it is replaced by a blend.
 *
 * The run computes anchor = (anchor + at) * 0.5. Substituting a frame-rate-corrected k for that
 * 0.5 is the one thing that must NOT be done: it yields k*anchor + k*at, whose weights sum to 2k,
 * so at a high frame rate (k -> 1) the anchor roughly doubles every frame and the eye leaves the
 * world within a fraction of a second. An early build shipped exactly that and drew nothing but
 * the fog clear and the 2-D HUD.
 *
 * One multiply cannot express a weighted blend. Five instructions can, and there is room for
 * three of them inside the 63 bytes the mean occupies, so the arithmetic is replaced outright:
 *
 *     D9 45 <a>        fld  [ebp-<a>]     anchor[i]
 *     D8 65 <t>        fsub [ebp-<t>]     - at[i]
 *     D8 0D <&k>       fmul [k]           * k
 *     D8 45 <t>        fadd [ebp-<t>]     + at[i]
 *     D9 5D <a>        fstp [ebp-<a>]     -> anchor[i]
 *
 * The weights are k and 1-k by construction, so the result is always between the two inputs and
 * cannot diverge for any k. 18 bytes per axis, 54 in total, 9 NOP fill the rest. At 30 fps
 * k = 0.5 and the result is the original mean.
 * ============================================================================================ */
bool camera_compensation_build_anchor_blend(uint8_t *out, size_t out_size, const void *k_cell)
{
    uint32_t cell = (uint32_t)(uintptr_t)k_cell;
    size_t   at   = 0;
    int      axis;

    if (out == NULL || out_size != ANCHOR_RUN_BYTES) {
        return false;
    }

    for (axis = 0; axis < 3; ++axis) {
        out[at++] = 0xD9; out[at++] = 0x45; out[at++] = ANCHOR_SLOT[axis];
        out[at++] = 0xD8; out[at++] = 0x65; out[at++] = TARGET_SLOT[axis];
        out[at++] = 0xD8; out[at++] = 0x0D;
        memcpy(out + at, &cell, sizeof(cell));
        at += sizeof(cell);
        out[at++] = 0xD8; out[at++] = 0x45; out[at++] = TARGET_SLOT[axis];
        out[at++] = 0xD9; out[at++] = 0x5D; out[at++] = ANCHOR_SLOT[axis];
    }
    while (at < ANCHOR_RUN_BYTES) {
        out[at++] = 0x90;
    }
    return true;
}

static bool install_anchor_blend(void)
{
    uintptr_t      site = sites[SITE_CAMERA_ANCHOR_MEAN].address;
    uint8_t        replacement[ANCHOR_RUN_BYTES];
    patch_result_t result;

    if (site == 0) {
        log_warning("camera_anchor_mean did not resolve, the camera anchor keeps converging "
                    "about three times too fast at 90 fps and drags on everything the camera "
                    "carries. Every other camera damper is unaffected.");
        return false;
    }

    /* The cell before the bytes. The instant the run is rewritten the engine reads this operand
     * on its next frame, whether or not the per-frame hook is ever installed. */
    anchor_k = BLEND_HALF;
    if (!camera_compensation_build_anchor_blend(replacement, sizeof(replacement),
                                                (const void *)&anchor_k)) {
        log_error("the anchor replacement could not be encoded, the run at %08X is NOT patched",
                  (unsigned)site);
        return false;
    }

    /* A short or misaligned write here lands mid-x87-sequence and corrupts the camera silently
     * instead of crashing, so the bytes are re-read immediately before the write even though the
     * resolver already matched them, and all 63 go in one call, never three of 21. */
    if (!patch_validate_bytes(site, SIG_CAMERA_ANCHOR_MEAN, ANCHOR_RUN_BYTES)) {
        log_error("the bytes at %08X changed between resolving and writing, the anchor run is "
                  "NOT patched", (unsigned)site);
        return false;
    }
    result = patch_write_bytes(site, replacement, ANCHOR_RUN_BYTES);
    if (result != PATCH_RESULT_OK) {
        log_error("the anchor run at %08X could not be written (%s), it keeps its 30 Hz mean",
                  (unsigned)site, patch_result_text(result));
        return false;
    }

    log_info("anchor mean at %08X replaced by a blend against a live weight, the camera anchor "
             "now converges at its authored rate instead of once per frame", (unsigned)site);
    return true;
}

/* ============================================================================================ */
bool camera_compensation_install(bool compensate_anchor)
{
    uintptr_t lag;
    uintptr_t pitch;
    int       repointed;

    follow_yaw_k           = BLEND_HALF;
    follow_yaw_one_minus_k = BLEND_HALF;

    signature_resolve_table(sites, SITE_COUNT);

    /* The anchor stands on its own site and must not be gated on the lag pair. The lag constants
     * are absolute-address writes and their pattern finds nothing in obi.exe, while the anchor run
     * is present and unique in all three builds. Behind the early return below the anchor would
     * silently never be patched on the recompile. */
    if (compensate_anchor) {
        camera_state.anchor_patched = install_anchor_blend();
    } else {
        log_info("CompensateCameraAnchor=0, the camera anchor keeps its per-frame mean");
    }

    lag   = sites[SITE_CAMERA_LAG_CONSTANTS].address;
    pitch = sites[SITE_CAMERA_PITCH_LAG].address;
    if (lag == 0 || pitch == 0) {
        log_warning("camera lag sites unresolved (lag=%08X pitch=%08X), the four remaining "
                    "dampers are NOT compensated and will feel rigid above 30 fps",
                    (unsigned)lag, (unsigned)pitch);
        return camera_state.anchor_patched;
    }

    camera_state.position_lag_fast   = lag   + OFFSET_POSITION_LAG_FAST;
    camera_state.yaw_lag_fast        = lag   + OFFSET_YAW_LAG_FAST;
    camera_state.position_lag_normal = lag   + OFFSET_POSITION_LAG_NORMAL;
    camera_state.yaw_lag_normal      = lag   + OFFSET_YAW_LAG_NORMAL;
    camera_state.pitch_lag           = pitch + OFFSET_PITCH_LAG_IMMEDIATE;

    /* These five dwords are rewritten at run time, so the pages stay writable. This is the one
     * place in the project that keeps a page permanently RWX, and it is why memory_make_writable
     * exists separately from patch_write_*. */
    if (!memory_make_writable(lag, CAMERA_LAG_PAGE_BYTES) ||
        !memory_make_writable(pitch, PITCH_LAG_PAGE_BYTES)) {
        log_error("the camera lag pages could not be unprotected, the four immediate-operand "
                  "dampers are NOT compensated");
        return camera_state.anchor_patched;
    }
    camera_state.patchable = true;

    repointed = repoint_follow_yaw();
    repoint_yaw_deadband();

    log_info("camera compensated, 5 immediates at %08X/%08X, the followYaw yaw arm is %s",
             (unsigned)lag, (unsigned)pitch,
             (repointed == 2) ? "rate corrected, both weights repointed"
                              : "left on the engine's per-frame 0.5, see the warning above");
    return true;
}

void camera_compensation_update(float smoothed_seconds_scale, bool scripted_camera)
{
    float k;

    if (!camera_state.patchable && !camera_state.anchor_patched) {
        return;
    }
    /* Not a clamp, a rejection, and the zero case is the load-bearing half: powf(0.5, 0) is 1.0,
     * which would freeze the anchor on the spot for the rest of the level. */
    if (!(smoothed_seconds_scale > 0.0f)) {
        return;
    }
    if (camera_state.last_scale == smoothed_seconds_scale &&
        camera_state.last_scripted == scripted_camera) {
        return;                                    /* the common case: nothing to do at all */
    }
    camera_state.last_scale    = smoothed_seconds_scale;
    camera_state.last_scripted = scripted_camera;

    /* The anchor weight is a plain data cell, so it needs neither a writable code page nor a
     * cache flush, and it is written even when the four immediate-operand dampers are absent. */
    if (camera_state.anchor_patched) {
        /* Zero for a placed camera: see the header. The lag cells below are left compensated,
         * because they damp the rig and the euler rather than the gather origin. */
        anchor_k = scripted_camera ? 0.0f : powf(BLEND_HALF, smoothed_seconds_scale);
    }
    if (!camera_state.patchable) {
        return;
    }

    *(float *)camera_state.position_lag_normal = powf(LAG_POSITION_NORMAL,
                                                      smoothed_seconds_scale);
    *(float *)camera_state.yaw_lag_normal      = powf(LAG_YAW_NORMAL, smoothed_seconds_scale);
    *(float *)camera_state.position_lag_fast   = powf(LAG_FAST,       smoothed_seconds_scale);
    *(float *)camera_state.yaw_lag_fast        = powf(LAG_FAST,       smoothed_seconds_scale);
    *(float *)camera_state.pitch_lag           = powf(LAG_PITCH,      smoothed_seconds_scale);

    k = powf(BLEND_HALF, smoothed_seconds_scale);
    follow_yaw_k           = k;
    follow_yaw_one_minus_k = 1.0f - k;

    yaw_deadband_cell = YAW_DEADBAND * smoothed_seconds_scale;

    /* These five dwords are immediate operands inside live instructions. x86 keeps its own caches
     * coherent for self-modifying code, but the API contract asks for this and it costs nothing
     * at the rate it actually runs, only when the frame time really moves. */
    FlushInstructionCache(GetCurrentProcess(), (LPCVOID)camera_state.position_lag_fast,
                          CAMERA_LAG_PAGE_BYTES);
    FlushInstructionCache(GetCurrentProcess(), (LPCVOID)camera_state.pitch_lag, 8);
}
