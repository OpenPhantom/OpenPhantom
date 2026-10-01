/* mp_signatures_enemy.c: the byte patterns for what the engine does to an actor's body. */
#include "mp_signatures_enemy.h"

#include "common/signature.h"

#include <stdint.h>

/* --- 0x004369C1  ai_startEmitter ---------------------------------------------------------------
 *
 * `void ai_startEmitter(character *a, i32 fxIndex, i32 unused, i32 node)`, __cdecl. Attaches a
 * particle emitter to an actor and keeps the handle at actor+0x1E0.
 *
 *     004369C1  55                 push ebp
 *     004369C2  8B EC              mov  ebp,esp
 *     004369C4  83 EC 14           sub  esp,14h        first boundary past five is 6
 *     004369C7  83 7D 0C 00        cmp  [ebp+0Ch],0    fxIndex < 0 is refused by the function
 *     004369CB  0F 8C ..           jl   epilogue
 *
 * The window stops on the `0F 8C` opcode and takes none of its displacement, so nothing in it
 * moves with a recompile and no mask is needed. Twelve bytes, and they match exactly once. */
const uint8_t SIG_MP_AI_START_EMITTER[12] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x14, 0x83, 0x7D, 0x0C, 0x00, 0x0F, 0x8C
};

/* --- 0x0042E900  enemy_detachPiece -------------------------------------------------------------
 *
 * `int enemy_detachPiece(character *actor, u32 part)`, __cdecl. Hulled to read what the engine was
 * asked to take off; never called by this feature, because the flying piece is the expensive half
 * and the body without the limb is the half that is seen.
 *
 *     0042E900  55                 push ebp
 *     0042E901  8B EC              mov  ebp,esp
 *     0042E903  83 EC 20           sub  esp,20h          first boundary past five is 6
 *     0042E906  8D 45 F0           lea  eax,[ebp-10h]
 *     0042E909  50                 push eax
 *     0042E90A  8D 4D 0C           lea  ecx,[ebp+0Ch]    the ADDRESS of the part: it is in/out
 *     0042E90D  51                 push ecx
 *     0042E90E  8B 55 08           mov  edx,[ebp+8]
 *     0042E911  8B 42 34           mov  eax,[edx+34h]    actor->pBody
 *     0042E914  50                 push eax
 *
 * Twenty one bytes and the window stops before the call's displacement, so nothing in it moves
 * with a recompile and it needs no mask.
 *
 * The second parameter of THIS function is a u32 VALUE, and the `lea ecx,[ebp+0Ch]` above is the
 * reason that is easy to get wrong: enemy_detachPiece takes the address of its OWN argument slot
 * and hands that down to bapobj_detachNode, which writes a body part mask back into it. At
 * 0x0042E9A3 this function reads that slot again: `and ecx,8` and, if set, `mov [edx+38h],0`:
 * a head node zeroes the actor's health. The write-back is therefore one level DOWN and says
 * nothing about how this function is called. A hull that reads the parameter as a pointer
 * dereferences the node number as an address, and that cost one crash. */
const uint8_t SIG_MP_ENEMY_DETACH_PIECE[21] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x20, 0x8D, 0x45, 0xF0, 0x50, 0x8D, 0x4D,
    0x0C, 0x51, 0x8B, 0x55, 0x08, 0x8B, 0x42, 0x34, 0x50
};

/* --- 0x00428EB3  within_range ------------------------------------------------------------------
 *
 * `int within_range(const vec3 *a, const vec3 *b, f32 r)`, __cdecl. Squared distance in three
 * axes against a squared radius, strictly less than. The engine has no second range test: the
 * activation scan and the removal test in the entity loop are its only two callers, which is why
 * both radii behave identically and why widening it once widens both gates.
 *
 *     00428EB3  55                 push ebp
 *     00428EB4  8B EC              mov  ebp,esp
 *     00428EB6  83 EC 14           sub  esp,14h        first boundary past five is 6
 *     00428EB9  8B 45 08           mov  eax,[ebp+8]    a
 *     00428EBC  8B 4D 0C           mov  ecx,[ebp+0Ch]  b
 *     00428EBF  D9 00              fld  dword [eax]
 *     00428EC1  D8 21              fsub dword [ecx]
 *     00428EC3  D9 5D F0           fstp dword [ebp-10h]
 *     00428EC6  8B                 (the next mov, cut here to end on a boundary)
 *
 * Twenty bytes, no absolute operand and no relative displacement among them, so nothing is
 * masked and nothing in the window moves with a recompile. They match exactly once in the image.
 *
 * It sits here rather than in the table's own file because that file is at its hard limit and
 * this neighbour had the room. The subject fits too: the range is what decides whether an
 * actor's body exists at all. */
const uint8_t SIG_MP_WITHIN_RANGE[20] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x14, 0x8B, 0x45, 0x08, 0x8B, 0x4D, 0x0C, 0xD9, 0x00,
    0xD8, 0x21, 0xD9, 0x5D, 0xF0, 0x8B
};

/* --- 0x0042E2F9 light_set, 0x0042E346 emitter_set, 0x0042E37B sound_set --------------------------
 *
 * `void <arm>(character *actor, int mode, i16 slot)`, __cdecl, all three the same shape. They are
 * the sinks of the script opcodes that switch something the LEVEL owns: 0x207, 0x208 and 0x210 for
 * a light, 0x212 for an emitter placement, 0x213 for a sound placement.
 *
 *     0042E2F9  55                 push ebp
 *     0042E2FA  8B EC              mov  ebp,esp
 *     0042E2FC  51                 push ecx           first boundary past five is 7
 *     0042E2FD  8B 45 08           mov  eax,[ebp+8]   actor
 *     0042E300  8B 48 10           mov  ecx,[eax+10h] actor->pPlacement
 *     0042E303  0F BF 55 10        movsx edx,word [ebp+10h]   the slot, read as a WORD
 *     0042E307  33 C0              xor  eax,eax
 *     0042E309  66 8B 84 51 9C..   mov  ax,[ecx+edx*2+9Ch]    <- the only byte that differs
 *
 * Twenty bytes are not enough. emitter_set and sound_set are identical over the first twenty and
 * part on the twenty first, the displacement of that last mov: 0x9C for the light slots, 0x64 for
 * the emitters, 0x5C for the sounds. A shorter pattern matches twice and takes the wrong one, and
 * the wrong one here means counting the steam as the hiss. Each is unique at twenty one.
 *
 * No absolute operand and no branch displacement in the window, so nothing is masked. */
const uint8_t SIG_MP_LIGHT_SET[21] = {
    0x55, 0x8B, 0xEC, 0x51, 0x8B, 0x45, 0x08, 0x8B, 0x48, 0x10, 0x0F, 0xBF, 0x55, 0x10,
    0x33, 0xC0, 0x66, 0x8B, 0x84, 0x51, 0x9C
};
const uint8_t SIG_MP_EMITTER_SET[21] = {
    0x55, 0x8B, 0xEC, 0x51, 0x8B, 0x45, 0x08, 0x8B, 0x48, 0x10, 0x0F, 0xBF, 0x55, 0x10,
    0x33, 0xC0, 0x66, 0x8B, 0x44, 0x51, 0x64
};
const uint8_t SIG_MP_SOUND_SET[21] = {
    0x55, 0x8B, 0xEC, 0x51, 0x8B, 0x45, 0x08, 0x8B, 0x48, 0x10, 0x0F, 0xBF, 0x55, 0x10,
    0x33, 0xC0, 0x66, 0x8B, 0x44, 0x51, 0x5C
};

/* --- 0x00456901  shot_shatterThing -------------------------------------------------------------
 *
 * `void shot_shatterThing(bapObj *obj, f32 chunkSpeed, i32 unused)`, __cdecl. Throws a piece of
 * the body for every visible node that has a mesh and hides the body; the third argument is read
 * by nothing. Hulled to see an enemy burst, and called on a client's replica before its removal.
 *
 *     00456901  55                 push ebp
 *     00456902  8B EC              mov  ebp,esp
 *     00456904  83 EC 7C           sub  esp,7Ch       first boundary past five is 6
 *     00456907  56 57              push esi; push edi
 *     00456909  C7 45 AC 00000000  mov  [ebp-54h],0  the resource handle, cleared
 *     00456910  E8 ..              call player_getActorIfAlive
 *
 * The window stops on the `E8` opcode and takes none of its displacement, so nothing in it moves
 * with a recompile and no mask is needed. Sixteen bytes, and they match exactly once. */
const uint8_t SIG_MP_SHOT_SHATTER_THING[16] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x7C, 0x56, 0x57, 0xC7, 0x45, 0xAC, 0x00, 0x00, 0x00, 0x00, 0xE8
};

/* --- 0x00432C30  the live player test inside enemy_tickAll -------------------------------------
 *
 * Not a function head but a call: the entity loop asks `player_getActorIfAlive` once, keeps the
 * answer in a local and measures the removal for distance against it. The call's operand is what
 * the world anchor rewrites, so the pattern starts on the `E8` and carries the three calls behind
 * it, which run the scan, as its anchor:
 *
 *     00432C30  E8 rel32           call player_getActorIfAlive     <- the redirected call
 *     00432C35  89 45 F8           mov  [ebp-8],eax                the body the removal reads
 *     00432C38  E8 rel32           call enemy_reserved1
 *     00432C3D  E8 rel32           call aiflag_expireTimed
 *     00432C42  E8 rel32           call enemy_activationScan
 *     00432C47  A1                 mov  eax,[abs32]               (cut on the opcode)
 *
 * All four displacements are masked, so a recompile that moves any callee and a module that
 * redirects one of these calls both leave the pattern whole. Twenty four bytes, and they match
 * exactly once in every image this tree is verified against. */
const uint8_t SIG_MP_ENEMY_TICK_ANCHOR_CALL[24] = {
    0xE8, 0x00, 0x00, 0x00, 0x00, 0x89, 0x45, 0xF8, 0xE8, 0x00, 0x00, 0x00, 0x00, 0xE8, 0x00, 0x00,
    0x00, 0x00, 0xE8, 0x00, 0x00, 0x00, 0x00, 0xA1
};
const uint8_t MSK_MP_ENEMY_TICK_ANCHOR_CALL[24] = {
    0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00,
    0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF
};

/* The two calls the world anchor repoints, each at the offset of its E8 in the pattern it is found
 * by, so that the declaration is the one line a pattern reading their operands is held against: it
 * has to mask them. The scan's pattern is in the first site table, which has no line to spare, so
 * its declaration stands here beside the entity loop's. */
SIGNATURE_REDIRECTED_CALL(SIG_MP_ENEMY_TICK_ANCHOR_CALL, ENEMY_TICK_ANCHOR_CALL);
SIGNATURE_REDIRECTED_CALL(SIG_ENEMY_ACTIVATION_SCAN, ENEMY_SCAN_ANCHOR_CALL);

/* --- 0x0043565C  inside enemy_isFacingTarget: the call to resolve_target ----------------------
 *
 * The conversation menu's facing test takes the position through the resolver with kind 0 and the
 * heading from this machine's own player. Matched for the address its call returns to, which the
 * target resolver's hull leaves to the engine. Never a detour target.
 *
 * The window starts six bytes into the function, behind its `push ebp; mov ebp,esp; sub esp,2Ch`,
 * because the head is somebody else's to overwrite: dialogue_menu_fix hulls this function on a six
 * byte prologue. Nothing here depends on those six bytes.
 *
 * The call displacement is masked. The jump behind it is the function's own way to its epilogue
 * and is matched whole, which is what makes the stretch unique among the resolver's ten callers. */
const uint8_t SIG_MP_FACING_TEST_RESOLVE[38] = {
    0xC7, 0x45, 0xEC, 0x00, 0x00, 0x00, 0x00, 0x8B, 0x45, 0xEC, 0x50, 0x8D, 0x4D, 0xF4, 0x51,
    0x8B, 0x55, 0x08, 0x52, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x83, 0xC4, 0x0C, 0x85, 0xC0, 0x75,
    0x07, 0x33, 0xC0, 0xE9, 0x2A, 0x02, 0x00, 0x00
};
const uint8_t MSK_MP_FACING_TEST_RESOLVE[38] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

/* --- 0x0043510A  inside ai_run, opcode 0x607: the call to player_respawnAt ---------------------
 *
 * The script's warp: the hero named by the operand is respawned at a placement's own position and
 * heading. Matched for the address the call returns to, which is how the one respawn a script asks
 * for is told apart from the cheats' hero swap and from this feature's own re-entry. Never a
 * detour target.
 *
 * The call displacement is masked; the frame offsets around it are ai_run's own and are matched. */
const uint8_t SIG_MP_WARP_RESPAWN_CALL[31] = {
    0x8B, 0x45, 0x90, 0x8B, 0x48, 0x24, 0x51, 0x8B, 0x55, 0x90, 0x81, 0xC2, 0xAC, 0x00, 0x00,
    0x00, 0x52, 0x8B, 0x45, 0xF8, 0x8B, 0x08, 0x51, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x83, 0xC4,
    0x0C
};
const uint8_t MSK_MP_WARP_RESPAWN_CALL[31] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
    0xFF
};
