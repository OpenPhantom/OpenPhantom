#include "draw_interpolation.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* --- 0x004112D9  bapobj_drawAll: the euler the renderer uses --------------------------------- *
 *   8B 55 F8              mov edx,[ebp-8]            ; obj
 *   83 C2 3C              add edx,0x3C               ; &obj->rot
 *   8B 02   / 89 85 6CFBFFFF   [ebp-0x494] = rot.x   ; PITCH, raw
 *   8B 4A04 / 89 8D 70FBFFFF   [ebp-0x490] = rot.y
 *   8B 5208 / 89 95 74FBFFFF   [ebp-0x48C] = rot.z   ; ROLL , raw
 *   ...          <- 0x20 bytes, exactly the three raw loads, ending at 0x4112F9
 *   8B 45 F8 / 8B 48 40 / 51 / 8B 55 F8 / 8B 42 64 / 50
 *   E8 <rel32>            call angle_diff            ; <- at +0x2E, its rel32 at +0x2F
 *
 * Only the yaw is interpolated. Position (0x41125B..0x4112D3) and yaw (0x4112F9..0x411321) track
 * the substep alpha at [ebp-0x420]; pitch and roll are taken straight from the CURRENT substep,
 * so they step at 32 Hz while everything around them moves at the render rate. On a
 * slope-following NPC that is visible judder above 30 fps: aiext.c rewrites actualPitch every
 * substep as new*0.25 + old*0.75.
 *
 * The first 0x20 bytes are self-contained; they only fill the three euler locals out of
 * obj+0x3C, so they can be replaced wholesale by a call that fills the same three slots with
 * INTERPOLATED values. obj->rot itself is never touched.
 *
 * Register and stack contract, checked against the bytes:
 *   * eax/ecx/edx are dead across the patched region (0x4112F9 reloads obj from [ebp-8]);
 *   * the x87 stack is EMPTY here (0x4112D3 is an fstp, the next x87 op is inside the angle_diff
 *     call at 0x411307), so a normal C body is safe;
 *   * the caller cleans up (__cdecl), and the replacement is padded with NOPs to exactly 0x20. */
static const uint8_t SIG_OBJECT_DRAW_EULER[] = {
    0x8B, 0x55, 0xF8, 0x83, 0xC2, 0x3C,
    0x8B, 0x02, 0x89, 0x85, 0x6C, 0xFB, 0xFF, 0xFF,
    0x8B, 0x4A, 0x04, 0x89, 0x8D, 0x70, 0xFB, 0xFF, 0xFF,
    0x8B, 0x52, 0x08, 0x89, 0x95, 0x74, 0xFB, 0xFF, 0xFF,
    0x8B, 0x45, 0xF8, 0x8B, 0x48, 0x40, 0x51, 0x8B, 0x55, 0xF8, 0x8B, 0x42, 0x64, 0x50,
    0xE8
};
#define OFFSET_ANGLE_DIFF_REL32 0x2Fu
#define EULER_PATCH_LENGTH      0x20u

/* --- 0x00410019  rdThing_Draw: THE POSE THROTTLE --------------------------------------------- *
 *   8B 3D 60884B00     mov edi,[g_tickCounter]     ; the SUBSTEP counter, not a frame counter
 *   8D 4C CA 24        lea ecx,[edx+ecx*8+0x24]
 *   89 0D E05F5B00     mov [g_thingCurGeoSet],ecx
 *   8B 48 1C           mov ecx,[eax+0x1C]          ; thing->poseStamp
 *   3B CF              cmp ecx,edi
 *   74 19              je  0x410049                <- +0x15: skip the pose rebuild
 *   8D 54 24 24 / 52 / 50 / E8 ...                 ; rdPuppet_buildJointMatrices(thing, &pose)
 *
 * and the stamp is written at the end of the compositor:
 *   0x4846A6  mov [esi+0x1C], edx                  ; poseStamp = g_tickCounter
 *
 * This is why every animation looks like 30 fps ABOVE 32 fps: the joint matrices are rebuilt only
 * when the SUBSTEP counter has moved.
 *   at  30 fps: 1.07 substeps per frame -> the counter changes every frame and the `je` never
 *               fires. the shipped game never executes this branch.
 *   at  60 fps: 0.53 substeps per frame -> about 47 % of frames redraw the previous matrices.
 *   at 144 fps: 78 %.
 * And because rdModel3_concatHierarchy bakes the world matrix into pNodeMatrix, a skipped frame
 * freezes POSE AND PLACEMENT together.
 *
 * g_tickCounter [0x4B8860] has two increment sites in the whole image: sys_runSubsteps (per
 * SUBSTEP) and swmenu_render (per MENU frame), which is why the 3-D inventory model already
 * animated smoothly while the world did not.
 *
 * ONE SHARED-STATE NOTE, and it is a RETURN to the original rather than a new coupling.
 * poseStamp is shared: the draw path builds the pose from the INTERPOLATED transform, the
 * simulation's node queries (hit spheres, muzzle and auto-aim via rdThing_GetNodeMatrix) build it
 * from the RAW substep transform, and whoever runs first stamps it. At 30 fps the draw rebuilt
 * essentially always, so the sim always inherited the draw's pose. Uncapped, that inheritance is
 * intermittent; NOPing the branch makes it constant again. */
static const uint8_t SIG_POSE_THROTTLE[] = {
    0x8B, 0x3D, 0x60, 0x88, 0x4B, 0x00, 0x8D, 0x4C, 0xCA, 0x24,
    0x89, 0x0D, 0xE0, 0x5F, 0x5B, 0x00, 0x8B, 0x48, 0x1C, 0x3B, 0xCF,
    0x74, 0x19, 0x8D, 0x54, 0x24, 0x24, 0x52, 0x50, 0xE8
};
#define OFFSET_POSE_THROTTLE_JE 0x15u

/* bapObj offsets used by the replacement body. */
#define OBJECT_ROTATION      0x3C
#define OBJECT_PREVIOUS_ROT  0x60
/* Frame-pointer-relative locals inside bapobj_drawAll. */
#define LOCAL_SUBSTEP_ALPHA  0x420
#define LOCAL_EULER_PITCH    0x494
#define LOCAL_EULER_YAW      0x490
#define LOCAL_EULER_ROLL     0x48C

typedef float (__cdecl *angle_diff_fn_t)(float from, float to);

static angle_diff_fn_t engine_angle_diff;

/* Writes the SAME three stack slots the original wrote, using the SAME angle_diff the yaw lerp
 * uses, and reads the SAME alpha at [ebp-0x420]. */
static void __cdecl hook_draw_euler(char *object, char *frame_pointer)
{
    const float *rotation      = (const float *)(object + OBJECT_ROTATION);
    const float *previous      = (const float *)(object + OBJECT_PREVIOUS_ROT);
    float        alpha         = *(const float *)(frame_pointer - LOCAL_SUBSTEP_ALPHA);

    *(float *)(frame_pointer - LOCAL_EULER_PITCH) =
        previous[0] + engine_angle_diff(previous[0], rotation[0]) * alpha;

    /* The yaw slot: the engine overwrites it with its own lerp immediately after this call, so
     * the raw value is exactly what the original put there. */
    *(float *)(frame_pointer - LOCAL_EULER_YAW) = rotation[1];

    *(float *)(frame_pointer - LOCAL_EULER_ROLL) =
        previous[2] + engine_angle_diff(previous[2], rotation[2]) * alpha;
}

void draw_interpolation_install_euler(void)
{
    uintptr_t site;
    uint32_t  displacement;
    uintptr_t angle_diff;
    uint8_t   replacement[EULER_PATCH_LENGTH];
    uint32_t  call_displacement;
    size_t    index;

    site = signature_find_unique(SIG_OBJECT_DRAW_EULER, NULL, sizeof(SIG_OBJECT_DRAW_EULER));
    if (site == 0) {
        log_warning("object_draw_euler did not resolve, pitch and roll keep stepping at 32 Hz");
        return;
    }

    if (!memory_read_u32(site + OFFSET_ANGLE_DIFF_REL32, &displacement)) {
        return;
    }
    /* The rel32 begins one byte after the E8, and the E8 sits at OFFSET_ANGLE_DIFF_REL32 - 1. */
    angle_diff = (site + OFFSET_ANGLE_DIFF_REL32 - 1) + 5 + displacement;
    if (!memory_is_inside_image(angle_diff, 1)) {
        log_warning("angle_diff resolved to %08X, outside the image, refused",
                    (unsigned)angle_diff);
        return;
    }
    engine_angle_diff = (angle_diff_fn_t)angle_diff;

    /*  55                push ebp
     *  8B 45 F8          mov  eax,[ebp-8]        ; obj
     *  50                push eax
     *  E8 <rel32>        call hook_draw_euler    ; __cdecl(obj, ebp)
     *  83 C4 08          add  esp,8
     *  90 ...            pad to 0x20                                                            */
    for (index = 0; index < EULER_PATCH_LENGTH; ++index) {
        replacement[index] = 0x90;
    }
    replacement[0] = 0x55;
    replacement[1] = 0x8B; replacement[2] = 0x45; replacement[3] = 0xF8;
    replacement[4] = 0x50;
    replacement[5] = 0xE8;
    call_displacement = (uint32_t)((uintptr_t)hook_draw_euler - (site + 5 + 5));
    replacement[6] = (uint8_t)(call_displacement       & 0xFFu);
    replacement[7] = (uint8_t)((call_displacement >>  8) & 0xFFu);
    replacement[8] = (uint8_t)((call_displacement >> 16) & 0xFFu);
    replacement[9] = (uint8_t)((call_displacement >> 24) & 0xFFu);
    replacement[10] = 0x83; replacement[11] = 0xC4; replacement[12] = 0x08;

    if (patch_write_bytes(site, replacement, EULER_PATCH_LENGTH) == PATCH_RESULT_OK) {
        log_info("pitch and roll are now interpolated like yaw (%u bytes at %08X, "
                 "angle_diff=%08X). obj->rot is untouched, this changes only what is DRAWN.",
                 (unsigned)EULER_PATCH_LENGTH, (unsigned)site, (unsigned)angle_diff);
    } else {
        log_error("the euler patch at %08X could not be written", (unsigned)site);
    }
}

void draw_interpolation_install_pose_throttle(void)
{
    static const uint8_t expected_branch[2] = { 0x74, 0x19 };
    static const uint8_t nop_pair[2]        = { 0x90, 0x90 };
    uintptr_t site;
    uintptr_t branch;

    site = signature_find_unique(SIG_POSE_THROTTLE, NULL, sizeof(SIG_POSE_THROTTLE));
    if (site == 0) {
        log_warning("pose_throttle did not resolve, animation WILL step at 32 Hz above 32 fps");
        return;
    }

    branch = site + OFFSET_POSE_THROTTLE_JE;
    if (!patch_validate_bytes(branch, expected_branch, sizeof(expected_branch))) {
        log_error("expected `74 19` at %08X - refused", (unsigned)branch);
        return;
    }

    if (patch_write_bytes(branch, nop_pair, sizeof(nop_pair)) == PATCH_RESULT_OK) {
        log_info("THROTTLE REMOVED at %08X (74 19 -> 90 90). Joint matrices are rebuilt every "
                 "FRAME instead of only when g_tickCounter moved.", (unsigned)branch);
    } else {
        log_error("the throttle patch at %08X could not be written", (unsigned)branch);
    }
}
