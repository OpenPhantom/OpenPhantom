/* camera_target.c: see camera_target.h. */
#include "camera_target.h"

#include "substep_counter.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"
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
 *
 * The prologue is the first two instructions, eight bytes of whole instructions. The two operands
 * read below are the previous anchor's x (the three floats are contiguous) and the previous
 * heading, so this file carries no address of its own. */
static const uint8_t SIG_SET_CAM_TARGET[] = {
    0x55, 0x8B, 0xEC, 0xA1, 0xD8, 0xB4, 0x5B, 0x00, 0xA3, 0xC8, 0xB4, 0x5B, 0x00,
    0x8B, 0x0D, 0xDC, 0xB4, 0x5B, 0x00, 0x89, 0x0D, 0xCC, 0xB4, 0x5B, 0x00
};
#define SET_CAM_TARGET_PROLOGUE   8u
#define OFFSET_PREV_ANCHOR_OPERAND 0x09u
#define OFFSET_PREV_HEADING_MOV    0x47u     /* 89 0D <headPrev> */
#define OFFSET_PREV_HEADING_OPERAND (OFFSET_PREV_HEADING_MOV + 2u)

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

/* One absolute operand, believed only when the instruction in front of it is the one expected. */
static bool read_operand(uintptr_t site, uintptr_t operand_offset, const uint8_t *opcode,
                         size_t opcode_size, uint32_t *out)
{
    if (!patch_validate_bytes(site + operand_offset - opcode_size, opcode, opcode_size)) {
        return false;
    }
    return memory_read_u32(site + operand_offset, out) &&
           memory_is_inside_image(*out, sizeof(float));
}

void camera_target_install(bool enabled)
{
    static const uint8_t MOV_EAX_TO_ABS[1] = { 0xA3 };
    static const uint8_t MOV_ECX_TO_ABS[2] = { 0x89, 0x0D };
    uintptr_t site;
    uint32_t  prev_anchor;
    uint32_t  prev_heading;

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

    site = signature_find_detour_target(SIG_SET_CAM_TARGET, NULL, sizeof SIG_SET_CAM_TARGET,
                                        SET_CAM_TARGET_PROLOGUE);
    if (site == 0) {
        log_warning("bapview_setCamTarget did not resolve, so the camera's target pair is left "
                    "alone and a ride on a mover keeps its 32 Hz camera");
        return;
    }
    if (!read_operand(site, OFFSET_PREV_ANCHOR_OPERAND, MOV_EAX_TO_ABS, sizeof MOV_EAX_TO_ABS,
                      &prev_anchor) ||
        !read_operand(site, OFFSET_PREV_HEADING_OPERAND, MOV_ECX_TO_ABS, sizeof MOV_ECX_TO_ABS,
                      &prev_heading)) {
        log_warning("bapview_setCamTarget at %08X does not carry its operands where the retail "
                    "build has them, so the camera's target pair is left alone", (unsigned)site);
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
