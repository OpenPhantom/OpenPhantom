/* camera_sites.c: finding the follow camera's own cells in this build, or refusing.
 *
 * Seven patterns and what each of them is worth. About two thirds of this file is disassembly
 * rather than code, and that is deliberate: a pattern without its listing is a magic number, and
 * splitting the two apart would put every proof one file away from the code that depends on it.
 *
 * SIZE NOTE. Well over the 600 line mark, under the 900 hard limit. Only a small part of it is
 * code, the whole of which is "read this operand, range-check it, and cross-check it against its
 * twin". The rest is the seven disassembly listings that say WHY each pattern is the site it
 * claims to be, which is what has to stand at the site, and which deleting to reach a line count
 * would be the wrong trade.
 *
 * There is no seam worth cutting here: every listing belongs to the resolver directly beneath it,
 * and a split would sort patterns into two files by nothing more interesting than where the limit
 * happened to fall. If this file has to grow again, the unit to move out is one WHOLE site,
 * pattern, offsets, listing and resolver together, and the candidate is the auto-aim, which is
 * the only one of the seven that is not part of the camera at all.
 *
 * The claim the whole feature rests on, and where it comes from:
 *
 *     the follow camera's yaw IS  interpolatedPlayerHeading + a persistent global f32,
 *     and that global is pulled back toward the camera region's authored yaw once per rendered
 *     frame by a lerp whose rate is a second global.
 *
 * Both globals are plain data. Writing the first turns the camera; writing 1.0 into the second
 * makes the lerp a no-op for exactly one frame, because the function that reads it rewrites it
 * from an immediate at its own tail before it returns. The release is therefore "stop writing" and
 * there is no restore path that can be got wrong.
 */
#include "camera_sites.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* --- 0x00418544  updateCam: the prologue, the state flags and the substep alpha --------------- *
 *
 * The function names itself. Its very first statement prints its own name and four of its own
 * globals, which is a cheaper instrument than any call graph:
 *
 *   55 8B EC 81 EC 88000000   push ebp / mov ebp,esp / sub esp,0x88   <- the detour prologue
 *   A1 <gr>          / 50     mov eax,[gr]            which region a script has forced
 *   8B 0D <gOver>    / 51     mov ecx,[gOver]         whether a script has forced one at all
 *   8B 15 <reset>    / 52     mov edx,[reset]         the snap-for-N-frames countdown
 *   A1 <frames>      / 50     mov eax,[frames]        setCamTarget calls since the last update
 *   68 <format>               push "updateCam frames %d, reset %d (gOver %d, gr=%d)"
 *   6A 01 / 6A 00 / E8 <..>   the module's printf wrapper
 *   83 C4 1C
 *   C7 05 <frames> 00000000   frames := 0             <- the SAME operand again: a free proof
 *   8B 0D <alpha> / 89 4D C4  mov ecx,[substepAlpha]  stashed for the heading interpolation
 *
 * The two `frames` operands must be equal. That is what turns "twenty-one bytes happened to line
 * up" into "this is the debug statement of the function we want", without embedding one address.
 *
 * The pattern is registered as a DETOUR target: the first nine bytes are the ones our own branch
 * overwrites, so a second resolve, ours after a reinstall, or another mod's, must be able to
 * find the site by its tail instead. */
static const uint8_t SIG_CAMERA_UPDATE[] = {
    0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x88, 0x00, 0x00, 0x00,
    0xA1, 0x00, 0x00, 0x00, 0x00,
    0x50,
    0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00,
    0x51,
    0x8B, 0x15, 0x00, 0x00, 0x00, 0x00,
    0x52,
    0xA1, 0x00, 0x00, 0x00, 0x00,
    0x50,
    0x68, 0x00, 0x00, 0x00, 0x00,
    0x6A, 0x01,
    0x6A, 0x00,
    0xE8, 0x00, 0x00, 0x00, 0x00,
    0x83, 0xC4, 0x1C,
    0xC7, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00,
    0x89, 0x4D, 0xC4
};
static const uint8_t MSK_CAMERA_UPDATE[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF
};
#define OFFSET_UPDATE_OVERRIDE   0x11u
#define OFFSET_UPDATE_RESET      0x18u
#define OFFSET_UPDATE_FRAMES     0x1Eu
#define OFFSET_UPDATE_FRAMES_2   0x36u
#define OFFSET_UPDATE_ALPHA      0x40u

/* --- 0x004185E5  updateCam: the interpolated player heading ----------------------------------- *
 *
 *   8B 15 <headCur>  / 52     the current substep's heading
 *   A1    <headPrev> / 50     the previous substep's
 *   E8 <angle_diff>           wrap180(previous, current), negated inside
 *   83 C4 08
 *   D9 55 D4                  fst  [ebp-0x2C]
 *   D8 4D C4                  fmul [ebp-0x3C]        <- the alpha stashed by the block above
 *   D8 05 <headPrev>          fadd [headPrev]        <- the SAME operand again
 *   D9 5D D8                  fstp [ebp-0x28]        the interpolated heading
 *
 * i.e.  interp = previous + wrap180(current, previous) * alpha.
 *
 * This is the number the camera adds the yaw offset to, so it is the number the offset has to be
 * built against, and it has to be built against the values THIS frame's updateCam is about to use
 * rather than last frame's. That is the whole reason the write site is a detour on the prologue
 * above: at that instant the substeps have written both headings and the alpha, and updateCam has
 * not yet read any of them.
 *
 * The engine does NOT wrap the result, and neither do we, the difference is a multiple of a full
 * turn and the sum is wrapped by the engine two instructions before it reaches the camera. */
static const uint8_t SIG_CAMERA_INTERP[] = {
    0x8B, 0x15, 0x00, 0x00, 0x00, 0x00,
    0x52,
    0xA1, 0x00, 0x00, 0x00, 0x00,
    0x50,
    0xE8, 0x00, 0x00, 0x00, 0x00,
    0x83, 0xC4, 0x08,
    0xD9, 0x55, 0xD4,
    0xD8, 0x4D, 0xC4,
    0xD8, 0x05, 0x00, 0x00, 0x00, 0x00,
    0xD9, 0x5D, 0xD8
};
static const uint8_t MSK_CAMERA_INTERP[] = {
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF
};
#define OFFSET_INTERP_HEAD_CURRENT   0x02u
#define OFFSET_INTERP_HEAD_PREVIOUS  0x08u
#define OFFSET_INTERP_HEAD_PREV_2    0x1Du

/* --- 0x00418C81  updateCam: the recentre, and it yields all three cells at once ---------------- *
 *
 *   8B 0D <regionYaw> / 51    arg 3: the authored yaw of the region under the player
 *   8B 15 <offset>    / 52    arg 2: the camera yaw offset
 *   A1    <yawLag>    / 50    arg 1: THE RECENTRE RATE
 *   E8 <lerpAngle>
 *   83 C4 0C
 *   D9 1D <offset>            offset := lerpAngle(rate, offset, regionYaw)   <- SAME operand again
 *
 * lerpAngle(k,a,b) returns k*a + (1-k)*b, so at k = 0.96 a held offset is eaten with a half life
 * of about seventeen rendered frames. Writing 1.0 into the rate cell makes it return a unchanged.
 * The rate cell is rewritten from an immediate at the tail of every call, which is why the release
 * is "stop writing" and not "write the old value back".
 *
 * (There is a near-twin of this call two dozen instructions further on that handles the case where
 * the offset and the target are more than half a turn apart. It allocates its registers in a
 * different order and does not match this pattern, which is why this one is unique.)
 *
 * Nothing else patches this window. The frame-rate fix owns four sites in the same function, the
 * pitch lag immediate, the two follow-blend operands, the yaw deadband operand and the four lag
 * immediates at the tail, and all of them lie outside these thirty-four bytes. */
static const uint8_t SIG_CAMERA_RECENTRE[] = {
    0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00,
    0x51,
    0x8B, 0x15, 0x00, 0x00, 0x00, 0x00,
    0x52,
    0xA1, 0x00, 0x00, 0x00, 0x00,
    0x50,
    0xE8, 0x00, 0x00, 0x00, 0x00,
    0x83, 0xC4, 0x0C,
    0xD9, 0x1D, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t MSK_CAMERA_RECENTRE[] = {
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
};
#define OFFSET_RECENTRE_REGION_YAW  0x02u
#define OFFSET_RECENTRE_OFFSET      0x09u
#define OFFSET_RECENTRE_YAW_LAG     0x0Fu
#define OFFSET_RECENTRE_OFFSET_2    0x1Eu

/* --- 0x00418EDD  updateCam: the follow blend, which names the camera object -------------------- *
 *
 *   A1 <gView>                mov eax,[theCameraObject]
 *   D9 40 38                  fld  [eax+0x38]        the PREVIOUS camera yaw
 *   D9 5D E0 / D9 45 D8       stash it, load the interpolated heading
 *   D8 05 <offset>            fadd [offset]          <- the offset cell AGAIN, a third proof
 *
 * The first field of the camera object is its state: 0 follow, 1 fixed-look-at or fixed-heading,
 * 2 world-fixed. That single int is the "am I still the follow camera" test the exemption needs,
 * and it is written by updateCam itself on the arm that handles each region family. */
static const uint8_t SIG_CAMERA_VIEW[] = {
    0xA1, 0x00, 0x00, 0x00, 0x00,
    0xD9, 0x40, 0x38,
    0xD9, 0x5D, 0xE0,
    0xD9, 0x45, 0xD8,
    0xD8, 0x05, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t MSK_CAMERA_VIEW[] = {
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
};
#define OFFSET_VIEW_OBJECT   0x01u
#define OFFSET_VIEW_OFFSET   0x10u

/* --- 0x00418FC5  updateCam's tail: the pointer to the region the player is standing in --------- *
 *
 *   83 3D <region> 00 / 74 25   if (currentRegion != NULL) {
 *   8B 15 <region>              <- the SAME operand again
 *   8B 02                           eax = currentRegion->flags
 *   83 E0 10 / 85 C0 / 74 16        if (flags & 0x10) { the fast-lag arm }
 *
 * flags bit 0 = fixed look-at, bit 1 = fixed heading, bit 2 = CUT, bit 3 = world-fixed,
 * bit 4 = fast lag. This is belt and braces on top of the camera object's state: the state is set
 * from these same flags by the arms above, so on the shipped levels the two agree, but a region
 * that carried only the cut bit would take the follow arm and leave the state at 0. Reading the
 * flags directly means the exemption does not rest on a census of the shipped levels.
 *
 * The pattern ends exactly where the frame-rate fix's four lag immediates begin, so the two cannot
 * overlap whichever of the two DLLs loads first. */
static const uint8_t SIG_CAMERA_REGION[] = {
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x74, 0x25,
    0x8B, 0x15, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x02,
    0x83, 0xE0, 0x10,
    0x85, 0xC0,
    0x74, 0x16
};
static const uint8_t MSK_CAMERA_REGION[] = {
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF
};
#define OFFSET_REGION_POINTER    0x02u
#define OFFSET_REGION_POINTER_2  0x0Bu

/* --- 0x004186B6  updateCam: the cell that decides WHICH of the two yaw arms runs -------------- *
 *
 * The arm nobody knew was there. The camera yaw is not one formula, it is two, and which of them
 * runs is decided by a global this feature had never written:
 *
 *   0x00418EC6  D9 05 <camTurn>       fld   [camTurn]
 *               D8 1D <zero>          fcomp [0.0]
 *               DF E0 / F6 C4 40      fnstsw ax / test ah,0x40      <- C3, i.e. "equal"
 *   0x00418ED7  0F 85 <rel32>         jne   0x00418F86              <- taken when EQUAL
 *
 * so the branch is taken when camTurn IS zero, and 0x00418F86 is the simple arm:
 *
 *   arm B  0x00418F86   stored = wrap360(interpolatedHeading + yawOffset)
 *   arm A  0x00418F6D   stored = wrap360(w*previousCameraYaw + w*target), both weights the SAME
 *                       cell [0x4A8170] = 0.5 in retail, i.e. an arithmetic mean
 *
 * Everything this feature computes assumes arm B. Arm A eases the yaw toward the wanted angle over
 * a frame, and its 0/360 seam unwrap is gated on the SIGN of camTurn plus absolute tests against
 * 90 and 270, a proxy for "which way is the body turning", not "which way is the shortest path
 * from where the camera is to where it should be". Free look decouples the camera from the body,
 * which is precisely what makes that proxy wrong: a camera at 0.4 degrees wanting 357.7, 2.7
 * degrees away, is measured as +357.3 and a fifth of that is taken, a 78-degree jump in one
 * frame. And when the camera sits between 90 and 270 with a gap over half a turn, no seam branch
 * can fire at all.
 *
 * Arm A cannot be inverted honestly: it divides by a weight that reaches 14.9 at 300 fps, its
 * reachable arc per frame is only 24 degrees there, and one of its inputs is the value it is about
 * to produce.
 *
 * So the arm is forced instead, and this is the block that makes it possible:
 *
 *   0x004186B6  83 3D <reset> 00      cmp  [snapCountdown],0
 *               75 18                 jne  0x004186D7               (a snap: the change is 0)
 *   0x004186BF  8B 15 <lastInterp>    mov  edx,[lastInterp]         <- operand at +0x0B
 *               52 / 8B 45 D8 / 50    push it, push THIS frame's interpolated heading
 *               E8 <angle_diff>       wrap180 of the difference
 *               83 C4 08 / D9 5D D4   fstp [ebp-0x2C]
 *               EB 07
 *   0x004186D7  C7 45 D4 00000000     [ebp-0x2C] = 0.0              (the snap arm)
 *   0x004186DE  8B 4D D8
 *   0x004186E1  89 0D <lastInterp>    [lastInterp] = this frame's heading  <- operand at +0x2D
 *
 * and immediately afterwards:
 *
 *   0x004186E7  D9 45 D4 / D9 E0      fld [ebp-0x2C] / fchs
 *   0x004186EC  D9 15 <camTurn>       camTurn = -that difference
 *   0x00418718  D8 1D <deadband>      |camTurn| is then compared against 0.05 and zeroed below it
 *
 * camTurn is therefore NOT a camera turn rate. It is the signed per-frame change of the TARGET
 * heading, the body's. Writing this frame's interpolated heading into [lastInterp] BEFORE the
 * original runs makes the difference exactly zero, camTurn zero, and arm B the arm that runs.
 *
 * Why that is safe rather than clever:
 *   * the cell has exactly TWO references in the whole image, the read and the write above, and
 *     both are inside this one computation;
 *   * the write at +0x2B overwrites it with the engine's own value inside the same call, so
 *     nothing of ours is left behind and there is no restore path to get wrong. The release is
 *     "stop writing", the same discipline the recentre freeze already uses;
 *   * it does not need to be exact. The deadband three instructions later zeroes anything under
 *     0.05 degrees, so any float error in reproducing the engine's own interpolation is absorbed;
 *   * and nothing between the deadband and the arm select can undo it, which is the part that
 *     actually decides whether this works. Four further writers of camTurn sit in that stretch,
 *     0x0041896D, 0x00418AA5, 0x00418B1A and 0x00418B3F, the entries to fixed-lookat, world-fixed
 *     and fixed-heading and the return to follow. Every one of them is a literal `mov [camTurn], 0`.
 *     They can only reinforce arm B; there is no path on which one of them restores a non-zero
 *     value and quietly hands the frame back to the easing arm.
 *
 * The two operands must name the same cell, which is what separates this block from forty-nine
 * bytes that happened to line up. */
static const uint8_t SIG_CAMERA_LAST_INTERP[] = {
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x75, 0x18,
    0x8B, 0x15, 0x00, 0x00, 0x00, 0x00,
    0x52,
    0x8B, 0x45, 0xD8,
    0x50,
    0xE8, 0x00, 0x00, 0x00, 0x00,
    0x83, 0xC4, 0x08,
    0xD9, 0x5D, 0xD4,
    0xEB, 0x07,
    0xC7, 0x45, 0xD4, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x4D, 0xD8,
    0x89, 0x0D, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t MSK_CAMERA_LAST_INTERP[] = {
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
};
#define OFFSET_LAST_INTERP_READ  0x0Bu
#define OFFSET_LAST_INTERP_WRITE 0x2Du

/* --- 0x0044B804  Plr_AutoAim: the cone the auto-aim searches --------------------------------- *
 *
 *   55 8B EC 83 EC 44         push ebp / mov ebp,esp / sub esp,0x44   <- the detour prologue
 *   6A 00                     push 0
 *   68 9EC97F7F               push FLT_MAX                 the range limit
 *   68 00008041               push 16.0                    the cone half angle, in degrees
 *   A1 <pPlayer>
 *   8B 88 A0020000 / 51       push player->heading         <- what the cone is centred on
 *   6A 02
 *   8B 15 <pPlayer>           <- the SAME operand again
 *   8B 42 0C / 50             push player->hActor
 *   E8 <Thing_FindNearestInCone>
 *
 * Both pPlayer operands must be equal AND must be the same cell the player sites already resolved
 * out of Plr_Steer. That is what makes this the auto-aim of the player we are steering rather than
 * a similarly shaped function.
 *
 * The function takes exactly one argument and it is used: it is compared against 2 inside the
 * body, and its single caller pushes one dword and cleans four bytes afterwards. A thunk declared
 * to take none would let the original read its own caller's frame. */
static const uint8_t SIG_PLAYER_AUTO_AIM[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x44,
    0x6A, 0x00,
    0x68, 0x9E, 0xC9, 0x7F, 0x7F,
    0x68, 0x00, 0x00, 0x80, 0x41,
    0xA1, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x88, 0xA0, 0x02, 0x00, 0x00,
    0x51,
    0x6A, 0x02,
    0x8B, 0x15, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x42, 0x0C,
    0x50,
    0xE8
};
static const uint8_t MSK_PLAYER_AUTO_AIM[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF,
    0xFF
};
#define OFFSET_AUTO_AIM_PLAYER    0x13u
#define OFFSET_AUTO_AIM_PLAYER_2  0x22u

/* --- 0x0044BA3A  the fire action handler, and why the aim lives HERE and not in Plr_AutoAim -----
 *
 *   55 8B EC 83 EC 14        push ebp / mov ebp,esp / sub esp,0x14
 *   6A 01                    push 1
 *   A1 <pPlayer>             pPlayer                            <- operand at +0x09
 *   8B 48 0C / 51            push pPlayer->hActor
 *   E8 <bapobj_animSlot>     the animation slot                 <- masked, a rel32
 *   83 C4 08
 *   83 B8 44010000 00        cmp [slot+0x144], 0                the CLIP MARKER gate
 *
 * This is the handler the engine stores into pPlayer+0x64 and calls from the tail of phase 1 on a
 * LATER substep, when the firing animation reaches its marker. It is where the bolt is actually
 * spawned:
 *
 *   0044BB45  D9 80 A0020000   fld  [pPlayer+0x2A0]    the heading, read LIVE at spawn time
 *   0044BB4B  D8 81 78010000   fadd [pPlayer+0x178]    + the aim offset
 *                              -> wrapped, and the shot is fired along that
 *
 * That live read is the whole reason this site exists. Plr_AutoAim runs substeps earlier, and the
 * heading it saw is long gone by now, so a heading swapped across Plr_AutoAim aims the search
 * cone and nothing else. What decides where the bolt goes is the heading the BODY has at this
 * instant, plus this offset cell. Correct the cell here and the shot can be aimed anywhere the
 * player is looking while the body walks wherever the keys say.
 *
 * The player pointer is read out and cross-checked, exactly as the auto-aim site is: it is what
 * separates this from a similarly shaped handler belonging to something else. */
static const uint8_t SIG_PLAYER_FIRE_SHOT[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x14,
    0x6A, 0x01,
    0xA1, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x48, 0x0C,
    0x51,
    0xE8, 0x00, 0x00, 0x00, 0x00,
    0x83, 0xC4, 0x08,
    0x83, 0xB8, 0x44, 0x01, 0x00, 0x00, 0x00
};
static const uint8_t MSK_PLAYER_FIRE_SHOT[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
#define OFFSET_FIRE_SHOT_PLAYER 0x09u

_Static_assert(sizeof(SIG_CAMERA_UPDATE) == sizeof(MSK_CAMERA_UPDATE),
               "the updateCam pattern and its mask are different lengths");
_Static_assert(sizeof(SIG_CAMERA_INTERP) == sizeof(MSK_CAMERA_INTERP),
               "the interpolated-heading pattern and its mask are different lengths");
_Static_assert(sizeof(SIG_CAMERA_RECENTRE) == sizeof(MSK_CAMERA_RECENTRE),
               "the recentre pattern and its mask are different lengths");
_Static_assert(sizeof(SIG_CAMERA_VIEW) == sizeof(MSK_CAMERA_VIEW),
               "the camera-object pattern and its mask are different lengths");
_Static_assert(sizeof(SIG_CAMERA_REGION) == sizeof(MSK_CAMERA_REGION),
               "the camera-region pattern and its mask are different lengths");
_Static_assert(sizeof(SIG_PLAYER_AUTO_AIM) == sizeof(MSK_PLAYER_AUTO_AIM),
               "the auto-aim pattern and its mask are different lengths");
_Static_assert(sizeof(SIG_PLAYER_FIRE_SHOT) == sizeof(MSK_PLAYER_FIRE_SHOT),
               "the fire-handler pattern and its mask are different lengths");
_Static_assert(sizeof(SIG_CAMERA_LAST_INTERP) == sizeof(MSK_CAMERA_LAST_INTERP),
               "the last-interpolated-heading pattern and its mask are different lengths");

enum {
    SITE_CAMERA_UPDATE,
    SITE_CAMERA_INTERP,
    SITE_CAMERA_RECENTRE,
    SITE_CAMERA_VIEW,
    SITE_CAMERA_REGION,
    SITE_CAMERA_LAST_INTERP,
    SITE_PLAYER_AUTO_AIM,
    SITE_PLAYER_FIRE_SHOT,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY_DETOUR_MASKED("camera_update", SIG_CAMERA_UPDATE, MSK_CAMERA_UPDATE,
                                  CAMERA_UPDATE_PROLOGUE_SIZE),
    SIGNATURE_ENTRY_MASKED("camera_interp",   SIG_CAMERA_INTERP,   MSK_CAMERA_INTERP),
    SIGNATURE_ENTRY_MASKED("camera_recentre", SIG_CAMERA_RECENTRE, MSK_CAMERA_RECENTRE),
    SIGNATURE_ENTRY_MASKED("camera_view",     SIG_CAMERA_VIEW,     MSK_CAMERA_VIEW),
    SIGNATURE_ENTRY_MASKED("camera_region",   SIG_CAMERA_REGION,   MSK_CAMERA_REGION),
    SIGNATURE_ENTRY_MASKED("camera_last_interp", SIG_CAMERA_LAST_INTERP, MSK_CAMERA_LAST_INTERP),
    SIGNATURE_ENTRY_DETOUR_MASKED("player_auto_aim", SIG_PLAYER_AUTO_AIM, MSK_PLAYER_AUTO_AIM,
                                  PLAYER_AUTO_AIM_PROLOGUE_SIZE),
    SIGNATURE_ENTRY_DETOUR_MASKED("player_fire_shot", SIG_PLAYER_FIRE_SHOT, MSK_PLAYER_FIRE_SHOT,
                                  PLAYER_FIRE_SHOT_PROLOGUE_SIZE)
};

/* ============================================================================================
 * Every operand is read, range-checked and, where the same cell appears twice in one pattern,
 * cross-checked against its twin before it is believed.
 * ============================================================================================ */
static bool read_cell(uintptr_t site, uintptr_t operand_offset, const char *what,
                      uint32_t *out_address)
{
    uint32_t address = 0;

    if (site == 0) {
        return false;
    }
    if (!memory_read_u32(site + operand_offset, &address)) {
        log_warning("%s: the operand at %08X+%02X is unreadable, refused",
                    what, (unsigned)site, (unsigned)operand_offset);
        return false;
    }
    if (!memory_is_inside_image(address, sizeof(float))) {
        log_warning("%s would be at %08X, outside the image, refused",
                    what, (unsigned)address);
        return false;
    }

    *out_address = address;
    return true;
}

/* Two operands in the same pattern that must name the same cell. A mismatch means the twenty-odd
 * bytes lined up somewhere they should not have, and there is no safe way to guess which of the
 * two is the real one. */
static bool read_cell_twice(uintptr_t site, uintptr_t first, uintptr_t second, const char *what,
                            uint32_t *out_address)
{
    uint32_t a = 0;
    uint32_t b = 0;

    if (!read_cell(site, first, what, &a) || !read_cell(site, second, what, &b)) {
        return false;
    }
    if (a != b) {
        log_warning("%s: the two operands at %08X disagree (%08X vs %08X), this is not the site "
                    "we think it is, so it is refused",
                    what, (unsigned)site, (unsigned)a, (unsigned)b);
        return false;
    }

    *out_address = a;
    return true;
}

static bool resolve_update_site(camera_sites_t *out)
{
    uintptr_t site = sites[SITE_CAMERA_UPDATE].address;
    uint32_t  camera_override = 0;
    uint32_t  snap_countdown = 0;
    uint32_t  substep_alpha = 0;
    uint32_t  frames = 0;

    if (site == 0) {
        log_warning("camera_update did not resolve, free look stays OFF");
        return false;
    }
    if (!read_cell_twice(site, OFFSET_UPDATE_FRAMES, OFFSET_UPDATE_FRAMES_2,
                         "the camera's own frame counter", &frames) ||
        !read_cell(site, OFFSET_UPDATE_OVERRIDE, "the scripted-camera flag", &camera_override) ||
        !read_cell(site, OFFSET_UPDATE_RESET, "the camera snap countdown", &snap_countdown) ||
        !read_cell(site, OFFSET_UPDATE_ALPHA, "the substep interpolation factor", &substep_alpha)) {
        return false;
    }

    out->update_cam      = site;
    out->camera_override = (const int32_t *)(uintptr_t)camera_override;
    out->snap_countdown  = (const int32_t *)(uintptr_t)snap_countdown;
    out->substep_alpha   = (const float *)(uintptr_t)substep_alpha;
    return true;
}

static bool resolve_interp_site(camera_sites_t *out)
{
    uintptr_t site = sites[SITE_CAMERA_INTERP].address;
    uint32_t  previous = 0;
    uint32_t  current = 0;

    if (site == 0) {
        log_warning("camera_interp did not resolve, free look stays OFF");
        return false;
    }
    if (!read_cell_twice(site, OFFSET_INTERP_HEAD_PREVIOUS, OFFSET_INTERP_HEAD_PREV_2,
                         "the previous substep's heading", &previous) ||
        !read_cell(site, OFFSET_INTERP_HEAD_CURRENT, "the current substep's heading", &current)) {
        return false;
    }
    if (previous == current) {
        log_warning("the two heading-history cells are the same address (%08X), free look stays "
                    "OFF", (unsigned)previous);
        return false;
    }

    out->head_previous = (const float *)(uintptr_t)previous;
    out->head_current  = (const float *)(uintptr_t)current;
    return true;
}

static bool resolve_recentre_site(camera_sites_t *out, uint32_t *out_offset_cell)
{
    uintptr_t site = sites[SITE_CAMERA_RECENTRE].address;
    uint32_t  offset = 0;
    uint32_t  yaw_lag = 0;
    uint32_t  region_yaw = 0;

    if (site == 0) {
        log_warning("camera_recentre did not resolve, free look stays OFF");
        return false;
    }
    if (!read_cell_twice(site, OFFSET_RECENTRE_OFFSET, OFFSET_RECENTRE_OFFSET_2,
                         "the camera yaw offset", &offset) ||
        !read_cell(site, OFFSET_RECENTRE_YAW_LAG, "the camera recentre rate", &yaw_lag) ||
        !read_cell(site, OFFSET_RECENTRE_REGION_YAW, "the region's authored yaw", &region_yaw)) {
        return false;
    }
    if (offset == yaw_lag || offset == region_yaw || yaw_lag == region_yaw) {
        log_warning("two of the three recentre arguments are the same cell, free look stays OFF");
        return false;
    }

    out->camera_yaw_offset = (volatile float *)(uintptr_t)offset;
    out->yaw_lag           = (volatile float *)(uintptr_t)yaw_lag;
    out->region_yaw        = (const float *)(uintptr_t)region_yaw;
    *out_offset_cell       = offset;
    return true;
}

static bool resolve_view_site(camera_sites_t *out, uint32_t offset_cell)
{
    uintptr_t site = sites[SITE_CAMERA_VIEW].address;
    uint32_t  view = 0;
    uint32_t  offset = 0;

    if (site == 0) {
        log_warning("camera_view did not resolve, free look stays OFF");
        return false;
    }
    if (!read_cell(site, OFFSET_VIEW_OBJECT, "the camera object", &view) ||
        !read_cell(site, OFFSET_VIEW_OFFSET, "the camera yaw offset", &offset)) {
        return false;
    }
    /* A third, independent site has to name the same offset cell. The recentre writes it and this
     * blend reads it; if the two disagree, one of the two patterns matched the wrong place. */
    if (offset != offset_cell) {
        log_warning("the follow blend adds [%08X] but the recentre writes [%08X]; they must be "
                    "the same cell, so free look stays OFF",
                    (unsigned)offset, (unsigned)offset_cell);
        return false;
    }

    out->view = (void *const *)(uintptr_t)view;
    return true;
}

/* Optional. Its absence costs the cut-bit and fast-lag tests; the camera object's state still
 * catches every fixed-camera family, because updateCam sets that state from these same flags. */
static void resolve_region_site(camera_sites_t *out)
{
    uintptr_t site = sites[SITE_CAMERA_REGION].address;
    uint32_t  region = 0;

    if (site == 0 ||
        !read_cell_twice(site, OFFSET_REGION_POINTER, OFFSET_REGION_POINTER_2,
                         "the current camera region", &region)) {
        log_warning("the current camera region did not resolve. Free look still installs and still "
                    "releases on every fixed camera, because the camera object's own state is set "
                    "from the same region flags, what is lost is the direct test of the cut bit "
                    "on a region that is otherwise a follow region.");
        return;
    }

    out->current_region = (const uint8_t *const *)(uintptr_t)region;
}

/* Optional, and its absence is the one degraded mode in this file that changes how the camera
 * MOVES rather than what it is aimed at. Without this cell the engine keeps choosing between its
 * two yaw arms on its own, and on any frame the body is turning it chooses the eased one whose
 * seam handling is wrong for a decoupled camera. The caller says so in as many words. */
static void resolve_last_interp_site(camera_sites_t *out)
{
    uintptr_t site = sites[SITE_CAMERA_LAST_INTERP].address;
    uint32_t  cell = 0;

    if (site == 0 ||
        !read_cell_twice(site, OFFSET_LAST_INTERP_READ, OFFSET_LAST_INTERP_WRITE,
                         "the camera's last interpolated heading", &cell)) {
        return;
    }

    out->last_interp = (volatile float *)(uintptr_t)cell;
}

/* Optional. Its absence costs the auto-aim cone; the aim snap still drives the body to the camera
 * while an attack is live, so a held attack still aims where the player is looking. */
static void resolve_auto_aim_site(camera_sites_t *out, const void *expected_player_pointer)
{
    uintptr_t site = sites[SITE_PLAYER_AUTO_AIM].address;
    uint32_t  player = 0;

    if (site == 0 ||
        !read_cell_twice(site, OFFSET_AUTO_AIM_PLAYER, OFFSET_AUTO_AIM_PLAYER_2,
                         "the auto-aim's player pointer", &player)) {
        log_warning("Plr_AutoAim did not resolve, the auto-aim cone stays centred on the body. "
                    "The aim snap still turns the body to the camera while an attack is live.");
        return;
    }
    if ((uintptr_t)player != (uintptr_t)expected_player_pointer) {
        log_warning("Plr_AutoAim reads the player at %08X but the steering reads it at %08X; "
                    "that is not the same player, so the auto-aim cone is left alone",
                    (unsigned)player, (unsigned)(uintptr_t)expected_player_pointer);
        return;
    }

    out->auto_aim = site;
}

static void resolve_fire_shot_site(camera_sites_t *out, const void *expected_player_pointer)
{
    uintptr_t site = sites[SITE_PLAYER_FIRE_SHOT].address;
    uint32_t  player = 0;

    if (site == 0 || !read_cell(site, OFFSET_FIRE_SHOT_PLAYER, "the fire handler's player pointer",
                                &player)) {
        log_warning("the fire handler did not resolve, a shot keeps going where the BODY faces, "
                    "so aiming while moving still needs the body turned to the camera");
        return;
    }
    if ((uintptr_t)player != (uintptr_t)expected_player_pointer) {
        log_warning("the fire handler reads the player at %08X but the steering reads it at %08X; "
                    "not the same player, so it is left alone",
                    (unsigned)player, (unsigned)(uintptr_t)expected_player_pointer);
        return;
    }

    out->fire_shot = site;
}

/* ============================================================================================ */
bool camera_sites_resolve(camera_sites_t *out, const void *expected_player_pointer)
{
    uint32_t offset_cell = 0;

    out->update_cam        = 0;
    out->auto_aim          = 0;
    out->fire_shot         = 0;
    out->camera_yaw_offset = NULL;
    out->yaw_lag           = NULL;
    out->region_yaw        = NULL;
    out->head_previous     = NULL;
    out->head_current      = NULL;
    out->substep_alpha     = NULL;
    out->camera_override   = NULL;
    out->snap_countdown    = NULL;
    out->view              = NULL;
    out->current_region    = NULL;
    out->last_interp       = NULL;

    (void)signature_resolve_table(sites, SITE_COUNT);

    if (!resolve_update_site(out) ||
        !resolve_interp_site(out) ||
        !resolve_recentre_site(out, &offset_cell) ||
        !resolve_view_site(out, offset_cell)) {
        return false;
    }

    resolve_region_site(out);
    resolve_last_interp_site(out);
    resolve_auto_aim_site(out, expected_player_pointer);
    resolve_fire_shot_site(out, expected_player_pointer);

    log_info("camera cells: offset=%08X recentreRate=%08X regionYaw=%08X headPrev=%08X "
             "headCur=%08X alpha=%08X gOver=%08X reset=%08X view=%08X region=%08X "
             "lastInterp=%08X",
             (unsigned)(uintptr_t)out->camera_yaw_offset, (unsigned)(uintptr_t)out->yaw_lag,
             (unsigned)(uintptr_t)out->region_yaw, (unsigned)(uintptr_t)out->head_previous,
             (unsigned)(uintptr_t)out->head_current, (unsigned)(uintptr_t)out->substep_alpha,
             (unsigned)(uintptr_t)out->camera_override, (unsigned)(uintptr_t)out->snap_countdown,
             (unsigned)(uintptr_t)out->view, (unsigned)(uintptr_t)out->current_region,
             (unsigned)(uintptr_t)out->last_interp);
    return true;
}
