#ifndef CAMERA_SITES_H
#define CAMERA_SITES_H

#include <stdbool.h>
#include <stdint.h>

/* Everything the horizontal free look has to FIND in the follow camera, and nothing it does with
 * what it finds. Split off from the feature for the same reason the player sites are: it is one
 * job with one answer, "is the camera in this build the one we know, and where are its cells",
 * and the byte evidence for seven patterns is long enough to bury the code that uses it.
 *
 * Not one address is written down. Every cell below is read out of an operand at a site whose
 * surrounding instructions prove what the operand is for, and several of them are read twice at
 * two different sites and cross-checked against each other. Three builds of this engine ship in
 * one installation and one of them is a recompile, so an address table would have written into a
 * different function.
 *
 * Why the write site is a detour on updateCam and not a per-FRAME CALLBACK. The camera consumes
 * the yaw offset one frame after a frame-end callback could write it, and by then the substeps
 * have already advanced the three globals the offset was computed against, so the camera would
 * still inherit the frame's heading change, which is exactly the wobble the offset exists to
 * remove. The only instant at which the interpolated heading updateCam is about to use already
 * exists is between the substeps and updateCam itself, so the feature writes from a chained
 * detour on updateCam's own prologue, immediately before the original runs.
 */

/* push ebp / mov ebp,esp / sub esp,0x88; nine bytes, one instruction boundary, no relative
 * operand, which is what the trampoline needs to be able to copy it. */
#define CAMERA_UPDATE_PROLOGUE_SIZE 9u

/* push ebp / mov ebp,esp / sub esp,0x44; six bytes, same reasoning. */
#define PLAYER_AUTO_AIM_PROLOGUE_SIZE 6u

/* `push ebp / mov ebp,esp / sub esp,0x14`; six bytes, so the five a jmp needs are covered
 * without splitting an instruction. */
#define PLAYER_FIRE_SHOT_PROLOGUE_SIZE 6u

/* --- the camera object's own fields, none of them written by this feature -------------------- *
 *
 * Read out of the two functions that build the view, and listed here because the transition log
 * quotes the last two: the pitch and the eye height are the two numbers a horizontal free look must
 * never move, and printing them either side of every transition is what turns that from a promise
 * into something a player can check.
 *
 *   +0x00  state    0 follow, 1 fixed look-at or fixed heading, 2 world-fixed
 *   +0x2C  eye Z    composed as  eyeZ = anchorZ (+0x1C) + offsetZ (+0x0C)
 *                   The offset's X and Y are rotated by the camera's own euler and its Z is
 *                   forced to zero before that rotation, then the rotated Z is DISCARDED and the
 *                   unrotated one added instead. That is why turning the camera cannot raise or
 *                   lower the eye by a hair: the yaw is not on the path at all.
 *   +0x34  euler.x  the camera PITCH in degrees, built by a lerp whose rate is a pushed immediate
 *                   rather than the recentre-rate cell this feature freezes
 *   +0x38  euler.y  the camera YAW in degrees, = wrap360(interpolatedHeading + the yaw offset) */
#define BAPVIEW_EYE_Z_OFFSET       0x2Cu
#define BAPVIEW_EULER_PITCH_OFFSET 0x34u
#define BAPVIEW_EULER_YAW_OFFSET   0x38u
#define BAPVIEW_READ_SIZE          0x3Cu

typedef struct camera_sites {
    /* The two chained-detour targets. `auto_aim` is optional and is 0 when it did not resolve. */
    uintptr_t update_cam;
    uintptr_t auto_aim;
    uintptr_t fire_shot;        /* the action handler that spawns the bolt; 0 = not resolved */

    /* The three cells the feature writes. All live in the zero-filled tail of .data, which the
     * engine itself rewrites every frame, so they are ordinary writable data and need no page
     * unprotection. `volatile` because the engine reads them between our stores. */
    volatile float *camera_yaw_offset;
    volatile float *yaw_lag;

    /* The heading the camera update compared against LAST frame. Writing this frame's heading into
     * it makes the engine measure a zero change, which is what selects the simple yaw arm, the
     * only one whose result is what this feature computed. NULL when the site did not resolve, in
     * which case free look still runs and the engine still picks the arm itself. */
    volatile float *last_interp;

    /* Read-only inputs to the offset arithmetic. */
    const float *region_yaw;      /* the authored yaw of the camera region under the player */
    const float *head_previous;   /* the two-deep heading history the substeps maintain        */
    const float *head_current;
    const float *substep_alpha;   /* how far this frame sits between the last two substeps     */

    /* Read-only exemption signals. */
    const int32_t        *camera_override;  /* non-zero: a script has forced a camera region   */
    const int32_t        *snap_countdown;   /* non-zero: the camera is snapping, not blending  */
    void *const          *view;             /* the camera object; its first field is the state */
    const uint8_t *const *current_region;   /* NULL when the optional site did not resolve     */
} camera_sites_t;

/* Resolves every site by signature and logs one line each.
 *
 * Returns false when anything the feature cannot run without is missing, in which case `out` must
 * not be read: there is no honest degraded mode for the two cells. Writing the offset without
 * freezing the recentre gives a camera that slides home under the player's hand, and freezing the
 * recentre without the exemption signals leaves a scripted camera permanently rotated.
 *
 * `current_region`, `last_interp` and `auto_aim` are optional. Each is left NULL/0 with a warning
 * naming exactly what is lost, and the feature still installs.
 *
 * `expected_player_pointer` is the cell the steering already resolved out of Plr_Steer. The
 * auto-aim site is only believed when it reads the player out of that same cell, which is what
 * separates "the auto-aim of the player we steer" from "a function shaped like it". */
bool camera_sites_resolve(camera_sites_t *out, const void *expected_player_pointer);

#endif /* CAMERA_SITES_H */
