/* mp_signatures_scene.c: the byte patterns a scene is found by. See the header for who uses which
 * and why the first eight moved here from mp_cutscene.c, unchanged.
 */
#include "mp_signatures_scene.h"

#include <stdint.h>

/* ==============================================================================================
 * The sites.
 *
 * Every address operand is masked. An address written into a pattern is the thing the pattern
 * exists to avoid, and all three of these read globals whose place is not a fact about a build.
 * Each pattern matches exactly once in all six executables that are checked, the Edit Tool's own
 * recompile included, which is unusual enough to be worth saying: these three are untouched by
 * that rebuild.
 *
 * All three are called from one place, and it is not a function: the script opcode 0x604 that
 * locks the player is an inline arm of the AI runner, so the opcode itself cannot be detoured.
 * What the arm does, read off its body: it calls the letterbox with its second argument
 * unconditionally, before any branch, and only when its first argument is not negative does it
 * call the camera override with that argument and then the input lock with level 5. That is why
 * three doors are held and not one: holding only the lock leaves a player who may move while the
 * bars are drawn and the camera is somewhere else.
 * ============================================================================================ */

/* Dialog_EnterInputLock 0x00430ED9, __cdecl (int32_t level).
 *
 * `push ebp; mov ebp,esp; sub esp,4; mov [ebp-4],0; cmp [g_cinematicLock],0; jnz +0x11`. The
 * comparison against its own cell is what separates it from its release, which is byte for byte
 * the same head with the opposite branch. The cell is 0x006C4D8C on retail, and the two bodies
 * are: the enter sets input mode 4 only when the cell reads 0 and then stores its level; the
 * release at 0x00430F18 unwinds only while the cell is above 0 and restores input mode 0 only
 * when the unwind reaches 0. So a release for a lock this module never took does nothing, which
 * is what makes refusing the enter safe, and a cell clamped to zero would make the enter set
 * mode 4 every time and the release never reach the restore. */
const uint8_t SIG_SCENE_LOCK_ENTER[20] = {
    0x55, 0x8B, 0xEC, 0x51, 0xC7, 0x45, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x83,
    0x3D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x75, 0x11
};
const uint8_t MSK_SCENE_LOCK_ENTER[20] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF
};

/* fxfade_setLetterbox 0x004396CF, __cdecl (int32_t on).
 *
 * `cmp [ebp+8],0; jz; cmp [a],0; jnz; cmp [b],0; jnz`. Two guards against two different globals
 * in a row is what makes twenty seven bytes enough. */
const uint8_t SIG_SCENE_LETTERBOX[27] = {
    0x55, 0x8B, 0xEC, 0x83, 0x7D, 0x08, 0x00, 0x74, 0x1C, 0x83, 0x3D, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x75, 0x13, 0x83, 0x3D, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x75, 0x0A
};
const uint8_t MSK_SCENE_LETTERBOX[27] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
    0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF
};

/* bapview_overrideOn 0x0041840A, __cdecl (int32_t group).
 *
 * Hulled, and refused only for the callers that are a script. A census of the branch targets in
 * the image gives it seven callers, and three of them are ordinary play a deathmatch reaches
 * constantly:
 *
 *   Plr_UpdateFall 0x0044F162     the death camera when a fall turns fatal, region 13
 *   Plr_EnterTripodGun 0x00450457 mounting a gun, region 13
 *   death_continue 0x0043FB40     the death screen, region 13
 *
 * Refusing those would take the camera away from a player who falls, mounts a gun or dies, which
 * is a worse thing than a scene moving the view for a moment. This file refused none of them for
 * that reason, on the reading that the hook cannot tell them apart from inside. That reading was
 * half right: the argument does not say who asked, the return address does. The three sites a
 * script asks from are resolved by their own patterns below, and a take is refused only when it
 * will return to one of them.
 *
 * Its address is read out of the calls those three sites make, not searched for by this pattern.
 * Another module may detour this function before this one does, and its branch then stands where
 * the pattern's head was. What is left, a load, a store and a return, is common enough in the
 * image to name no function at all, so the search found nothing and the hull was never placed.
 * The callers are nobody's to change, and each names the function in its call. The pattern is
 * still the proof: it has to hold at the address the calls name, its head as authored or already
 * a branch, and where it still matches whole it has to be at that same address.
 *
 * The release, bapview_overrideOff 0x00418421, is left open on a client and for an arena, and
 * that is a finding rather than an omission. It writes a constant nought into the same cell
 * instead of putting a saved value back, so a release for a take that never happened restores
 * the resting value and costs nobody anything. That is the test this file applies to every pair
 * it half closes. On a host it is hulled for another reason, further down: there a far player's
 * script would take down the camera of the host's own scene.
 *
 * Its twin sits directly behind it storing that zero, which is why the immediate
 * `01 00 00 00` is unmasked. The function is twenty three bytes end to end:
 *
 *   0041840A  55 8B EC                          push ebp; mov ebp, esp
 *   0041840D  C7 05 E8 B4 5B 00 01 00 00 00     mov dword [0x005BB4E8], 1   the override flag
 *   00418417  8B 45 08                          mov eax, [ebp+8]
 *   0041841A  A3 F0 B4 5B 00                    mov [0x005BB4F0], eax       the camera group
 *   0041841F  5D C3                             pop ebp; ret */
const uint8_t SIG_SCENE_VIEW_OVERRIDE[23] = {
    0x55, 0x8B, 0xEC, 0xC7, 0x05, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x8B, 0x45, 0x08, 0xA3, 0x00, 0x00, 0x00, 0x00, 0x5D, 0xC3
};
const uint8_t MSK_SCENE_VIEW_OVERRIDE[23] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF
};

/* player_suspend 0x00450F25, __cdecl (int32_t actor), and the third thing a scene takes.
 *
 * A script that wants the hero as an actor asks for this, and the engine hands the player's own
 * body over to the script's placement: the player module is stopped, the actor drives the body,
 * and only `enemy_delete` gives it back. Twenty two placements in the shipped levels carry the
 * flag that asks for it.
 *
 * On a client that is one script on the wrong machine away from a player who cannot move and whose
 * body is then written by the host's own record for that placement. The answer for a refusal is
 * nought, which is what the engine's own body answers when the player is in a mode it will not
 * take (anything but standing, a sabre attack or Panaka), and the caller already handles that: it
 * waits in state 0x10 and asks again next tick.
 *
 *   00450F25  55 8B EC              push ebp; mov ebp, esp
 *   00450F28  A1 20 52 4B 00        mov  eax, [0x4B5220]      ; the player record, masked here
 *   00450F2D  83 78 0C 00 / 74 0C   no object: answer nought
 *   00450F33  8B 0D .. / 83 79 04 00 / 75 07   and no module either
 *   00450F3F  33 C0 / E9 ..         the nought
 *   00450F46  8B 15 .. / 81 7A 60 .. the mode it will take the player out of
 *
 * The head alone (`push ebp; mov ebp, esp; mov eax, [imm32]`) matches 110 times in the image,
 * which is the trap the pattern table exists for: the two guards and the mode compare behind it
 * are what make it this function. Every absolute operand is masked, because those are the very
 * addresses that move between builds.
 *
 * Eight bytes to the first instruction boundary past five, and that is the prologue. */
const uint8_t SIG_SCENE_SUSPEND[46] = {
    0x55, 0x8B, 0xEC, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x83, 0x78, 0x0C, 0x00,
    0x74, 0x0C, 0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x83, 0x79, 0x04, 0x00,
    0x75, 0x07, 0x33, 0xC0, 0xE9, 0x00, 0x00, 0x00, 0x00, 0x8B, 0x15, 0x00,
    0x00, 0x00, 0x00, 0x81, 0x7A, 0x60, 0x00, 0x00, 0x00, 0x00
};
const uint8_t MSK_SCENE_SUSPEND[46] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00,
    0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
};

/* player_resume 0x00450FF1, __cdecl (void), the other half of that pair and the site this
 * module was missing.
 *
 * Whoever refuses a raise has to refuse the matching lower in the same file, unless the lower is
 * itself conditional. This one is not. Its only question is whether the player has a body:
 *
 *   00450FF1  55 8B EC              push ebp; mov ebp, esp
 *   00450FF4  A1 20 52 4B 00        mov  eax, [0x4B5220]      ; the player record, masked here
 *   00450FF9  83 78 0C 00 / 75 07   no object: answer nought
 *   00450FFF  33 C0 / E9 ..         the nought
 *   00451006  8B 0D ..              and then the work, with no second question asked
 *
 * The work is `moduleState = savedModuleState`, and on a machine where nothing was ever parked
 * that store reads nought. A player module at nought is a player who cannot move, cannot turn
 * and cannot be spawned again, because every re-entry in this feature and the engine's own
 * respawn read that cell as a gate. Its one caller is the removal path, which asks for every
 * placement carrying the handover flag, whether a grab ever happened or not.
 *
 * Two more things it does to a player who was never parked, both read off the body: it copies
 * the body's position and rotation back into the record, which is a resync rather than a jump
 * because that body is the player's own, and for the two sabre heroes it drives the blade from
 * the record's own wish, which can switch a drawn blade off or an undrawn one on.
 *
 * Eight bytes to the first instruction boundary past five, the same shape as the grab, and no
 * relative branch inside them. Both absolute operands and the jump distance are masked, because
 * those are the three fields that move between builds. The bare head matches 110 times in the
 * image; the two guards behind it are what make this pattern this function. */
const uint8_t SIG_SCENE_RESUME[27] = {
    0x55, 0x8B, 0xEC, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x83, 0x78, 0x0C, 0x00,
    0x75, 0x07, 0x33, 0xC0, 0xE9, 0x00, 0x00, 0x00, 0x00, 0x8B, 0x0D, 0x00,
    0x00, 0x00, 0x00
};
const uint8_t MSK_SCENE_RESUME[27] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00,
    0x00, 0x00, 0x00
};

/* The three places a script takes the camera, matched for their own sake rather than as detour
 * targets. What is wanted from each is the address the engine returns to after its call, which
 * is what tells the camera hull who asked.
 *
 * Every one of them ends past its own call, the call is five bytes, and the number beside each
 * pattern is the offset from its first byte to the instruction after that call. The call
 * displacement is masked, so nothing here depends on where the callee sits.
 *
 * The camera dolly opcode, whose first operand decides by its sign whether a sequence begins or
 * ends:
 *
 *   00434F38  8B 45 F8 / 83 38 00 / 7C 10   the sign test that picks the end from the beginning
 *   00434F40  8B 4D F8 / 8B 11 / 52         the group
 *   00434F46  E8 ..                         bapview_overrideOn
 *   00434F4B  83 C4 04 / EB 19              and the return address this yields */
const uint8_t SIG_SCENE_DOLLY_TAKE[24] = {
    0x8B, 0x45, 0xF8, 0x83, 0x38, 0x00, 0x7C, 0x10, 0x8B, 0x4D, 0xF8, 0x8B,
    0x11, 0x52, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x83, 0xC4, 0x04, 0xEB, 0x19
};
const uint8_t MSK_SCENE_DOLLY_TAKE[24] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

/* The lock player opcode, the same sign test one opcode further on, reached after the bars have
 * already been drawn. The `6A 05` behind its call is the lock level a scene takes, and it is what
 * separates this site from the one above, whose bytes are otherwise the same shape. */
const uint8_t SIG_SCENE_LOCK_TAKE[24] = {
    0x8B, 0x55, 0xF8, 0x83, 0x3A, 0x00, 0x7C, 0x1A, 0x8B, 0x45, 0xF8, 0x8B,
    0x08, 0x51, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x83, 0xC4, 0x04, 0x6A, 0x05
};
const uint8_t MSK_SCENE_LOCK_TAKE[24] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

/* A spoken line, which takes the camera for its speaker whenever the group it is given is not
 * negative. The absolute load and the float read behind the call are the dialogue's own clock,
 * and they are what make twenty six bytes enough. */
const uint8_t SIG_SCENE_SPEAK_TAKE[26] = {
    0x83, 0x7D, 0x0C, 0x00, 0x7C, 0x0C, 0x8B, 0x55, 0x0C, 0x52, 0xE8, 0x00,
    0x00, 0x00, 0x00, 0x83, 0xC4, 0x04, 0xA1, 0x00, 0x00, 0x00, 0x00, 0xD9,
    0x40, 0x54
};
const uint8_t MSK_SCENE_SPEAK_TAKE[26] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
    0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF
};

/* ==============================================================================================
 * The two releases, and the sites the host's scene reads.
 * ============================================================================================ */

/* Dialog_LeaveInputLock 0x00430F18, __cdecl (int32_t level).
 *
 * The same head as the lock's entry with the opposite branch, `jle` where the entry has `jnz`, and
 * behind it the compare of the caller's level against the lock's own. Both cell operands are
 * masked; the one at +13 is also what camera_handback_fix reads, and the eleven byte prologue a
 * hull takes stops short of it:
 *
 *   00430F18  55                       push ebp
 *   00430F19  8B EC                    mov  ebp, esp
 *   00430F1B  51                       push ecx
 *   00430F1C  C7 45 FC 00 00 00 00     mov  dword [ebp-4], 0        eleven bytes to here
 *   00430F23  83 3D 8C 4D 6C 00 00     cmp  dword [0x006C4D8C], 0
 *   00430F2A  7E 25                    jle  the end
 *   00430F2C  A1 8C 4D 6C 00           mov  eax, [0x006C4D8C]
 *   00430F31  3B 45 08                 cmp  eax, [ebp+8]
 *   00430F34  7F 1B                    jg   the end
 *
 * So a release lets go of a lock that stands above nought and at or below the level it is handed,
 * sets the input mode to play, and answers 1; for any other lock it does nothing and answers 0.
 * Five calls reach it in the retail image, 00430352, 0043037E and 00430E97 from the dialogue and
 * 00434F57 and 00434FA6 from the two script ends. */
const uint8_t SIG_SCENE_LOCK_LEAVE[30] = {
    0x55, 0x8B, 0xEC, 0x51, 0xC7, 0x45, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x83, 0x3D, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x7E, 0x25, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x3B, 0x45, 0x08, 0x7F, 0x1B
};
const uint8_t MSK_SCENE_LOCK_LEAVE[30] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00,
    0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

/* bapview_overrideOff 0x00418421, __cdecl (void), the twin behind bapview_overrideOn.
 *
 *   00418421  55                               push ebp
 *   00418422  8B EC                            mov  ebp, esp
 *   00418424  C7 05 E8 B4 5B 00 00 00 00 00    mov  dword [0x005BB4E8], 0   thirteen bytes to here
 *   0041842E  5D                               pop  ebp
 *   0041842F  C3                               ret
 *
 * Fifteen bytes, and a hull takes thirteen of them: what is left, a pop and a return, names no
 * function, so this pattern is never searched for. With the operand masked it is also the shape of
 * every function that stores a nought into one cell, and five of them have it in each of the six
 * executables this tree is checked against. It is held at the address the two script ends call,
 * the tail exactly and the head as authored or already another module's branch. The store's
 * operand is absolute and masked; nothing in the thirteen bytes is relative. Six calls reach it
 * in the retail image, 00434F50 and 00434F9F from the two script ends and 00417A93, 00430EAC,
 * 0044013A and 00450933 from the engine's own. */
const uint8_t SIG_SCENE_VIEW_RELEASE[15] = {
    0x55, 0x8B, 0xEC, 0xC7, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5D, 0xC3
};
const uint8_t MSK_SCENE_VIEW_RELEASE[15] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

/* 0x00447DA1, inside player_tickTask: the respawn's two arms.
 *
 * State 4 starts the fade out `fxfade_startTintOpaque(2, 1.0f, 1, 0, 0, 0)` and sets state 3;
 * state 3 feeds the camera and, once the cell the fade's draw sets reads 1, respawns. The call's
 * displacement, the player record, the camera call and the cell are masked, and so are the two
 * short jumps, whose distances a recompile may move. Unique in all six images this tree checks,
 * the Edit Tool's recompile included. */
const uint8_t SIG_SCENE_TINT_RESPAWN[96] = {
    0x6A, 0x00, 0x6A, 0x00, 0x6A, 0x00, 0x6A, 0x01, 0x68, 0x00, 0x00, 0x80, 0x3F, 0x6A, 0x02,
    0xE8, 0x00, 0x00, 0x00, 0x00, 0x83, 0xC4, 0x18, 0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00, 0xC7,
    0x41, 0x04, 0x03, 0x00, 0x00, 0x00, 0xEB, 0x00, 0x6A, 0x00, 0x8B, 0x15, 0x00, 0x00, 0x00,
    0x00, 0x8B, 0x82, 0xA0, 0x02, 0x00, 0x00, 0x50, 0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x81,
    0xC1, 0x18, 0x01, 0x00, 0x00, 0x51, 0x8B, 0x15, 0x00, 0x00, 0x00, 0x00, 0x81, 0xC2, 0xCC,
    0x02, 0x00, 0x00, 0x52, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x83, 0xC4, 0x10, 0x83, 0x3D, 0x00,
    0x00, 0x00, 0x00, 0x01, 0x75, 0x00
};
const uint8_t MSK_SCENE_TINT_RESPAWN[96] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00,
    0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
    0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00
};

/* 0x00450F46, inside player_suspend: the three modes it will park a player from.
 *
 * `cmp [pr+60h], sabre; je; cmp [pr+60h], panaka; jne; call; call; ...; cmp [pr+60h], stand; je`.
 * The three descriptors are masked immediates, the player record and the three calls masked too.
 * It starts well past the grab's own eight byte prologue, which mp_cutscene hulls, and reads none
 * of the bytes that hull writes. Unique in all six images. */
const uint8_t SIG_SCENE_SUSPEND_MODES[92] = {
    0x8B, 0x15, 0x00, 0x00, 0x00, 0x00, 0x81, 0x7A, 0x60, 0x00, 0x00, 0x00, 0x00, 0x74, 0x0E,
    0xA1, 0x00, 0x00, 0x00, 0x00, 0x81, 0x78, 0x60, 0x00, 0x00, 0x00, 0x00, 0x75, 0x2D, 0xE8,
    0x00, 0x00, 0x00, 0x00, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x6A, 0x02, 0x8B, 0x0D, 0x00, 0x00,
    0x00, 0x00, 0x8B, 0x51, 0x68, 0x8B, 0x42, 0x0C, 0x50, 0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x51, 0x0C, 0x52, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x83, 0xC4, 0x0C, 0xEB, 0x12, 0xA1,
    0x00, 0x00, 0x00, 0x00, 0x81, 0x78, 0x60, 0x00, 0x00, 0x00, 0x00, 0x74, 0x04, 0x33, 0xC0,
    0xEB, 0x00
};
const uint8_t MSK_SCENE_SUSPEND_MODES[92] = {
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF,
    0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00,
    0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00
};

/* 0x004479F8, inside player_save: the mode pointer turned into its index.
 *
 * `mov eax, [pr]; mov ecx, [eax+60h]; push ecx; push table; call; add esp, 8; mov edx, [pr];
 * mov [edx+39Ch], eax`. The record, the table and the call are masked; the store into the saved
 * index at +0x39C tells this push from the aux table's push just before it. Unique in five
 * images. */
const uint8_t SIG_SCENE_MODE_TABLE_SAVE[34] = {
    0xA1, 0x00, 0x00, 0x00, 0x00, 0x8B, 0x48, 0x60, 0x51, 0x68, 0x00, 0x00, 0x00, 0x00, 0xE8,
    0x00, 0x00, 0x00, 0x00, 0x83, 0xC4, 0x08, 0x8B, 0x15, 0x00, 0x00, 0x00, 0x00, 0x89, 0x82,
    0x9C, 0x03, 0x00, 0x00
};
const uint8_t MSK_SCENE_MODE_TABLE_SAVE[34] = {
    0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF
};

/* 0x00447BB0, inside player_restore: the saved index turned back into the mode pointer.
 *
 * `mov ecx, [pr]; mov edx, [ecx+39Ch]; mov eax, [pr]; mov ecx, [edx*4+table]; mov [eax+60h],
 * ecx`. The record and the table are masked. Unique in five images. */
const uint8_t SIG_SCENE_MODE_TABLE_RESTORE[27] = {
    0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x8B, 0x91, 0x9C, 0x03, 0x00, 0x00, 0xA1, 0x00, 0x00,
    0x00, 0x00, 0x8B, 0x0C, 0x95, 0x00, 0x00, 0x00, 0x00, 0x89, 0x48, 0x60
};
const uint8_t MSK_SCENE_MODE_TABLE_RESTORE[27] = {
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00,
    0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF
};
