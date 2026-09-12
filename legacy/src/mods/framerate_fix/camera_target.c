/* camera_target.c: see camera_target.h. */
#include "camera_target.h"

#include "substep_counter.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <stdint.h>
#include <string.h>

/* --- 0x004184CC  bapview_setCamTarget(ground, anchor, heading, turnRate), cdecl ------------- *
 *   +0x00  55 8B EC              push ebp / mov ebp,esp
 *   +0x03  A1 <cur.x>            mov eax,[gTargetAnchor.x]
 *   +0x08  A3 <prev.x>           mov [gTargetAnchorPrev.x],eax      the pair rotates, x
 *   +0x0D  8B 0D <cur.y>  89 0D <prev.y>                              y
 *   +0x19  8B 15 <cur.z>  89 15 <prev.z>                              z
 *   +0x25  8B 45 0C / 8B 08 / 89 0D <cur.x> ...                       the new anchor lands
 *   +0x41  8B 0D <head>   89 0D <headPrev>                            the heading pair rotates
 *   +0x4D  8B 55 10 / 89 15 <head>                                    [ebp+0x10], the heading
 *   +0x56  8B 45 14 / A3 <rate>                                       [ebp+0x14], the turn rate
 *   +0x5E  8B 4D 08 / 89 0D <ground>                                  [ebp+8], the ground block
 *   +0x67  8B 15 <calls> / 83 C2 01 / 89 15 <calls>                   the call counter
 *   +0x76  5D C3                 pop ebp / ret
 *
 * The whole function, every absolute operand masked, so the pattern is the shape and the operands
 * are read out of the match rather than copied from it. Four arguments at [ebp+8] to [ebp+0x14]
 * and a plain ret with no immediate: the caller cleans, which is the cdecl below. The prologue
 * is the first two instructions, eight bytes of whole instructions.
 *
 * The previous anchor's three floats are read from their three operands and required to sit
 * four bytes apart, which is the layout the code below writes them back through. */
static const uint8_t SIG_SET_CAM_TARGET[] = {
    0x55, 0x8B, 0xEC,
    0xA1, 0x00, 0x00, 0x00, 0x00, 0xA3, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x89, 0x0D, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x15, 0x00, 0x00, 0x00, 0x00, 0x89, 0x15, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x45, 0x0C, 0x8B, 0x08, 0x89, 0x0D, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x50, 0x04, 0x89, 0x15, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x40, 0x08, 0xA3, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x89, 0x0D, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x55, 0x10, 0x89, 0x15, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x45, 0x14, 0xA3, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x4D, 0x08, 0x89, 0x0D, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x15, 0x00, 0x00, 0x00, 0x00, 0x83, 0xC2, 0x01, 0x89, 0x15, 0x00, 0x00, 0x00, 0x00,
    0x5D, 0xC3
};
static const uint8_t MSK_SET_CAM_TARGET[] = {
    0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF
};
_Static_assert(sizeof SIG_SET_CAM_TARGET == sizeof MSK_SET_CAM_TARGET,
               "the setCamTarget pattern and its mask are different lengths");
#define SET_CAM_TARGET_PROLOGUE        8u
#define OFFSET_PREV_ANCHOR_X_OPERAND   0x09u  /* A3 <prev.x>      */
#define OFFSET_PREV_ANCHOR_Y_OPERAND   0x15u  /* 89 0D <prev.y>   */
#define OFFSET_PREV_ANCHOR_Z_OPERAND   0x21u  /* 89 15 <prev.z>   */
#define OFFSET_PREV_HEADING_OPERAND    0x49u  /* 89 0D <headPrev> */

typedef void (__cdecl *set_cam_target_fn_t)(void *ground, const float *anchor, float heading,
                                            float turn_rate);

static struct {
    detour_t        setter;
    volatile float *previous_anchor;     /* three floats */
    volatile float *previous_heading;
    uint32_t        last_step;
    bool            have_last;
    uint32_t        folded;              /* second calls whose rotation was undone */
    bool            reported;
} target_state;

static void __cdecl hook_set_cam_target(void *ground, const float *anchor, float heading,
                                        float turn_rate)
{
    set_cam_target_fn_t original = (set_cam_target_fn_t)target_state.setter.original;
    uint32_t            step;
    bool                same_step;

    same_step = substep_counter_read(&step) && target_state.have_last &&
                step == target_state.last_step;

    if (!same_step) {
        original(ground, anchor, heading, turn_rate);
        if (substep_counter_read(&step)) {
            target_state.last_step = step;
            target_state.have_last = true;
        }
        return;
    }

    /* The second call of this substep. The engine's body still runs whole, so the current sample,
     * the heading, the turn rate, the ground block and the call counter are all as it left them;
     * the previous sample and heading are then put back to what the first call made them, and
     * the pair is two substeps again. */
    {
        float saved_anchor[3];
        float saved_heading = target_state.previous_heading[0];

        saved_anchor[0] = target_state.previous_anchor[0];
        saved_anchor[1] = target_state.previous_anchor[1];
        saved_anchor[2] = target_state.previous_anchor[2];

        original(ground, anchor, heading, turn_rate);

        target_state.previous_anchor[0]  = saved_anchor[0];
        target_state.previous_anchor[1]  = saved_anchor[1];
        target_state.previous_anchor[2]  = saved_anchor[2];
        target_state.previous_heading[0] = saved_heading;
    }

    if (++target_state.folded == 1u && !target_state.reported) {
        target_state.reported = true;
        log_info("the camera's target was fed twice in substep %u, which is the player standing "
                 "on a mover, and the second feed no longer collapses the pair the camera "
                 "interpolates between. The ride is drawn between two substeps from here on",
                 (unsigned)step);
    }
}

/* One absolute operand of the match, checked to name a cell inside the image. */
static bool read_operand(uintptr_t site, uintptr_t operand_offset, uint32_t *out)
{
    return memory_read_u32(site + operand_offset, out) &&
           memory_is_inside_image(*out, sizeof(float));
}

void camera_target_install(bool enabled)
{
    uintptr_t site;
    uint32_t  prev_anchor   = 0;
    uint32_t  prev_anchor_y = 0;
    uint32_t  prev_anchor_z = 0;
    uint32_t  prev_heading  = 0;

    if (!enabled) {
        log_info("CameraTargetPair=0, the camera's two samples collapse into one while the player "
                 "rides a mover, as the game shipped");
        return;
    }
    if (!substep_counter_resolve()) {
        log_warning("the substep counter did not resolve, so the camera's target pair is left "
                    "alone: without it one substep cannot be told from the next");
        return;
    }

    site = signature_find_detour_target(SIG_SET_CAM_TARGET, MSK_SET_CAM_TARGET,
                                        sizeof SIG_SET_CAM_TARGET, SET_CAM_TARGET_PROLOGUE);
    if (site == 0) {
        log_warning("bapview_setCamTarget did not resolve, so the camera's target pair is left "
                    "alone and a ride on a mover keeps its 32 Hz camera");
        return;
    }
    if (!read_operand(site, OFFSET_PREV_ANCHOR_X_OPERAND, &prev_anchor) ||
        !read_operand(site, OFFSET_PREV_ANCHOR_Y_OPERAND, &prev_anchor_y) ||
        !read_operand(site, OFFSET_PREV_ANCHOR_Z_OPERAND, &prev_anchor_z) ||
        !read_operand(site, OFFSET_PREV_HEADING_OPERAND, &prev_heading) ||
        prev_anchor_y != prev_anchor + sizeof(float) ||
        prev_anchor_z != prev_anchor + 2u * sizeof(float)) {
        log_warning("bapview_setCamTarget at %08X keeps the previous anchor at %08X, %08X and "
                    "%08X, which is not three floats in a row, so the camera's target pair is "
                    "left alone", (unsigned)site, (unsigned)prev_anchor, (unsigned)prev_anchor_y,
                    (unsigned)prev_anchor_z);
        return;
    }
    target_state.previous_anchor  = (volatile float *)(uintptr_t)prev_anchor;
    target_state.previous_heading = (volatile float *)(uintptr_t)prev_heading;

    if (!detour_install(&target_state.setter, site, (const void *)hook_set_cam_target,
                        SET_CAM_TARGET_PROLOGUE)) {
        log_warning("bapview_setCamTarget at %08X could not be detoured, so the camera's target "
                    "pair is left alone", (unsigned)site);
        return;
    }
    log_info("the camera's target pair rotates once per substep (bapview_setCamTarget %08X, "
             "previous anchor %08X, previous heading %08X). While the player rides a mover the "
             "player tick fed it twice a substep and the two samples the camera interpolates "
             "between were the same one, so the camera stepped at 32 Hz for the whole ride",
             (unsigned)site, (unsigned)prev_anchor, (unsigned)prev_heading);
}
