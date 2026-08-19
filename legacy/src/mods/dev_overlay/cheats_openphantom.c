/* cheats_openphantom.c: unlimited ammunition, unlimited health, no fog, invincible NPCs, one-shot
 * NPCs, giant player, tiny player, and free camera. The third cheat this project adds, no fog, is
 * a different enough shape - see cheats_no_fog.h - that it lives in its own file and this one only
 * dispatches CHEATS_OWN_NO_FOG's queries to it. Free camera is documented next to its own two
 * signatures below rather than up here, the two sites it needs each carrying their own byte
 * evidence at the point they are used - invincible NPCs and one-shot NPCs are the same, documented
 * next to SIG_NPC_DAMAGE_APPLY, and giant/tiny player next to SIG_THING_DRAW.
 *
 * ==============================================================================================
 * THE TWO SITES, READ OUT OF THE RETAIL EXECUTABLE
 *
 * Ammunition is spent in one place. 0x00459FD4, and the whole function is this:
 *
 *   00459FD4  55 8B EC                 push ebp; mov ebp,esp
 *   00459FD7  83 3D 7C D5 86 00 00     cmp  [0086D57C],0        ; the player status record
 *   00459FDE  75 02 EB 35              return when it is null
 *   00459FE2  8B 45 08                 mov  eax,[ebp+8]         ; weaponId
 *   00459FE5  8B 0D 7C D5 86 00        mov  ecx,[0086D57C]
 *   00459FEB  8B 54 81 10              mov  edx,[ecx+eax*4+0x10] ; ammo[weaponId], base +0x10
 *   00459FEF  2B 55 0C                 sub  edx,[ebp+0x0C]       ; minus the shot's cost
 *   00459FF2  8B 45 08                 mov  eax,[ebp+8]
 *   00459FF5  8B 0D 7C D5 86 00        mov  ecx,[0086D57C]
 *   00459FFB  89 54 81 10              mov  [ecx+eax*4+0x10],edx
 *   00459FFF  8B 15 7C D5 86 00        mov  edx,[0086D57C]
 *   0045A005  8B 45 08                 mov  eax,[ebp+8]
 *   0045A008  3B 42 08                 cmp  eax,[edx+8]          ; the weapon in hand
 *   0045A00B  75 0A                    jnz  past the flash
 *   0045A00D  C7 05 24 FB 6C 00 ...    the weapon bar's flash target
 *
 * Damage is applied in one place. 0x00459ECE:
 *
 *   00459ECE  55 8B EC                 push ebp; mov ebp,esp
 *   00459ED1  83 3D 7C D5 86 00 00     cmp  [0086D57C],0
 *   00459ED8  75 02 EB 3C              return when it is null
 *   00459EDC  A1 7C D5 86 00           mov  eax,[0086D57C]
 *   00459EE1  8B 08                    mov  ecx,[eax]            ; health, at +0x00
 *   00459EE3  2B 4D 08                 sub  ecx,[ebp+8]          ; minus the damage
 *   00459EE6  8B 15 7C D5 86 00        mov  edx,[0086D57C]
 *   00459EEC  89 0A                    mov  [edx],ecx
 *   00459EEE  A1 7C D5 86 00           mov  eax,[0086D57C]
 *   00459EF1  83 38 00                 cmp  dword ptr [eax],0
 *   00459EF4  7D 0C                    jge  past the clamp
 *   00459EF6  8B 0D 7C D5 86 00        mov  ecx,[0086D57C]
 *   00459EFC  C7 01 00 00 00 00        mov  [ecx],0              ; never below zero
 *   00459F02  C7 05 00 FB 6C 00 ...    the health bar's flash target
 *
 * Two things follow from these listings and both shape the design.
 *
 * FIRST, the ammunition base offset is +0x10 and the weapon in hand is at +0x08. The front end
 * reads its ammunition readout from the same +0x10 base, independently, which is the cross check
 * that this is the array and not a neighbour.
 *
 * SECOND, both functions begin with the identical ten bytes, because both start by testing the same
 * record pointer, and so do several of their neighbours in the same compiland. A pattern that
 * stopped at the prologue would match a handful of functions and this file would patch whichever
 * came first. Each pattern here therefore reaches into the body, to the instruction that names what
 * the function actually does: the scaled index load for ammunition, the health load and subtract
 * for damage. The record pointer inside them is masked and never depended on.
 *
 * ==============================================================================================
 * WHY A DETOUR THAT DECLINES, RATHER THAN A TOPPED UP COUNTER
 *
 * The obvious implementation of unlimited ammunition writes the magazine full once a frame. That
 * fights the pickup code, makes the weapon bar flash on frames nothing happened, and writes a value
 * into the save. Declining to subtract does none of that: the counter simply never moves, every
 * other consumer sees exactly what it saw before, and switching the cheat off leaves a state the
 * game could have reached on its own.
 *
 * The same argument holds for damage, with one addition. Returning early also skips the health
 * bar's flash, which is correct rather than a side effect: nothing hurt the player, so nothing
 * should flash. Death, the hurt animation and the knockback all hang off health having fallen, so
 * none of them can fire either.
 *
 * What this deliberately does NOT cover, because it is a different mechanism and would need its own
 * evidence: anything that sets health directly rather than subtracting from it. A scripted death
 * and the console's own "kill me now" both go elsewhere and are not blocked here.
 * ============================================================================================ */
#include "cheats_openphantom.h"

#include "cheats_no_fog.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <windows.h>

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 0x00459FD4, thirty bytes: prologue, the null test, and the scaled load that is this
 * function's own. The record pointer appears twice and is masked in both places. */
static const uint8_t SIG_USE_AMMO[] = {
    0x55, 0x8B, 0xEC,                                /* push ebp; mov ebp,esp        */
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00,        /* cmp [record],0               */
    0x75, 0x02,                                      /* jnz +2                       */
    0xEB, 0x00,                                      /* jmp out                      */
    0x8B, 0x45, 0x08,                                /* mov eax,[ebp+8]   weaponId   */
    0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00,              /* mov ecx,[record]             */
    0x8B, 0x54, 0x81, 0x10,                          /* mov edx,[ecx+eax*4+0x10]     */
    0x2B, 0x55, 0x0C                                 /* sub edx,[ebp+0x0C]           */
};
/* THE LAST THREE BYTES ARE THE WHOLE POINT. The function that GIVES ammunition sits twenty nine
 * bytes earlier and is identical up to here:
 *
 *   00459FA6  8B 54 81 10 03 55 0C     mov edx,[ecx+eax*4+0x10]; ADD edx,[ebp+0x0C]
 *   00459FEB  8B 54 81 10 2B 55 0C     mov edx,[ecx+eax*4+0x10]; SUB edx,[ebp+0x0C]
 *
 * Stopping at the load matched both, and the first of the two is the one that hands out pickups.
 * Detouring that instead would have made every pickup a no-op while the cheat was on, which is the
 * opposite of unlimited ammunition and would have looked like a pickup bug. */
static const uint8_t MSK_USE_AMMO[] = {
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF,
    0xFF, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof(SIG_USE_AMMO) == sizeof(MSK_USE_AMMO),
               "the use ammo pattern and its mask are different lengths");

/* 0x00459ECE, twenty four bytes: prologue, the null test, and the health load and subtract. */
static const uint8_t SIG_DAMAGE[] = {
    0x55, 0x8B, 0xEC,                                /* push ebp; mov ebp,esp        */
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00,        /* cmp [record],0               */
    0x75, 0x02,                                      /* jnz +2                       */
    0xEB, 0x00,                                      /* jmp out                      */
    0xA1, 0x00, 0x00, 0x00, 0x00,                    /* mov eax,[record]             */
    0x8B, 0x08,                                      /* mov ecx,[eax]     health     */
    0x2B, 0x4D, 0x08                                 /* sub ecx,[ebp+8]   the damage */
};
static const uint8_t MSK_DAMAGE[] = {
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF,
    0xFF, 0x00,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof(SIG_DAMAGE) == sizeof(MSK_DAMAGE),
               "the damage pattern and its mask are different lengths");

/* TEN, AND NOT FIVE, AND THE DIFFERENCE IS A CRASH.
 *
 * The trampoline copies these bytes verbatim and appends a jump past them; there is no length
 * disassembler anywhere in the shared code. So the size has to land on a real instruction boundary.
 * Both functions open:
 *
 *   55                      push ebp                 boundary at 0
 *   8B EC                   mov ebp,esp              boundary at 1
 *   83 3D xx xx xx xx 00    cmp [record],0           boundary at 3, and the next is at 10
 *
 * Five is inside that `cmp`. A trampoline built from it decodes as a compare against a wild address
 * assembled out of the jump this patch had just written, and the original is called on every path
 * where the cheat is OFF, so the game would have died on the first shot fired and the first hit
 * taken. Ten is the first boundary at or past the five bytes a jump needs. */
#define STATUS_PROLOGUE_SIZE 10u

/* --- 0x004338EC  enemy_receiveDamage: the NPC health write ----------------------------------- *
 * Found hunting for the enemy-side counterpart to unlimited health, starting from dismemberment.c's
 * own DEATH GATE (0x0043707D), which is reached only when an NPC's health, at character record
 * +0x38, has fallen to zero or below - established there by the retail compare at 0x437070. That
 * pinned the FIELD; this pins the WRITE. Decompiling enemy_receiveDamage (0x00433803, named for the
 * six call sites Ghidra's own xrefs find into it, all message-dispatch arms) shows exactly one line
 * that ever changes it: `*(int*)(param_1+0x38) = *(int*)(param_1+0x38) - local_18;` - local_18 is
 * the damage this call computed (a flat 3, doubled to 6 for a severable-limb hit - the same doubling
 * dismemberment.c's own header already documents at 0x4338B3, a few instructions earlier in this
 * same function). Disassembled, that line is five back-to-back three-byte instructions, an exact
 * 15-byte block with no rel32 and nothing environment-dependent in it:
 *
 *   004338EC  8B 55 08     mov edx,[ebp+8]      victim (character*), this function's own param_1
 *   004338EF  8B 42 38     mov eax,[edx+0x38]   health
 *   004338F2  2B 45 EC     sub eax,[ebp-0x14]   minus local_18, the computed damage
 *   004338F5  8B 4D 08     mov ecx,[ebp+8]
 *   004338F8  89 41 38     mov [ecx+0x38],eax   health -= damage, written back
 *
 * Traced forward to the end of the function (0x00433985), NOTHING after this block ever reads EAX,
 * ECX or EDX left over from it - every later use of the victim record reloads it fresh from
 * [ebp+8], and the first flag-testing instruction after it (FCOMP/FNSTSW at 0x004338FE) sets its
 * own flags rather than reading the SUB's. That is what makes this block safe to detour as a whole
 * and either skip entirely or replace outright, rather than needing to preserve anything about how
 * it executed - unlike updateCam's chained detour above, which must run the original underneath
 * every time, this site's own hook is free to decide whether the trampoline runs at all. */
static const uint8_t SIG_NPC_DAMAGE_APPLY[] = {
    0x8B, 0x55, 0x08,             /* mov edx,[ebp+8]      victim (character*)            */
    0x8B, 0x42, 0x38,             /* mov eax,[edx+0x38]   health                         */
    0x2B, 0x45, 0xEC,             /* sub eax,[ebp-0x14]   minus the computed damage      */
    0x8B, 0x4D, 0x08,             /* mov ecx,[ebp+8]                                     */
    0x89, 0x41, 0x38,             /* mov [ecx+0x38],eax   health -= damage, written back */
    0xD9, 0x45, 0xF8               /* fld [ebp-8]  the next instruction, kept only for the extra
                                     * uniqueness/chaining margin every other detour site in this
                                     * file also carries past its own prologue - never read for a
                                     * value */
};
/* Fifteen, not eighteen: the trampoline may only copy what the detour target's own site comment
 * proves is safe to relocate, and the three FLD bytes above are trailing context for
 * signature_find_detour_target's own chaining check, not part of what gets overwritten. */
#define NPC_DAMAGE_PROLOGUE_SIZE 15u

/* --- 0x0040FE70  rdThing_Draw: every drawn object's own render call, including the player ------
 * SUB ESP,0x48 / MOV ECX,0xC / PUSH EBP / MOV EBP,[ESP+0x50] - no push-ebp;mov-ebp,esp frame at
 * all: this is one of the frame-pointer-omitted /O2 translation units this game's own toolchain
 * analysis (see engine/engine-identification.md) already found this build mixes with /Od per
 * source file. Still plain __cdecl(thing*, matrix[12]) at the ABI boundary regardless of how the
 * callee itself addresses its own params internally - the caller pushes two dwords and cleans its
 * own stack afterward (ADD ESP,8), the same shape hook_use_ammo/hook_damage above already detour,
 * so a normally-typed hook works here too, no naked-asm trick needed.
 *
 * Confirmed via xrefs to have exactly two callers: FUN_00417930's own switch(kind==1) arm - the
 * path every ordinary object takes, the player included - and emitter_drawParticles, for particle
 * sprites. Detouring the function's own entry rather than either call site catches both without
 * needing two patches; the particle path is untouched regardless, since this hook only ever acts
 * when the incoming thing is the player's own (see hook_thing_draw below).
 *
 * THE SCALE TRICK IS ALREADY IN THE RETAIL GAME. A few instructions past this prologue, gated
 * behind a specific cheat-flag slot and a hardcoded four-character model-name match - neither of
 * which this feature depends on, touches, or needs to fully identify - retail applies a flat 3.0x
 * scale to this exact incoming matrix, via a small, self-contained "compose a diagonal scale into
 * this transform" utility that this file calls directly rather than reimplementing: not something
 * built for this feature, something the shipped game already trusts to do this correctly for its
 * own reasons. See THING_DRAW_TO_SCALE_CALL_OFFSET below for how its address is found - not its
 * own independent signature, deliberately. */
static const uint8_t SIG_THING_DRAW[] = {
    0x83, 0xEC, 0x48,                    /* sub esp,0x48                */
    0xB9, 0x0C, 0x00, 0x00, 0x00,        /* mov ecx,0xC                 */
    0x55,                                /* push ebp                    */
    0x8B, 0x6C, 0x24, 0x50               /* mov ebp,[esp+0x50]          */
};
#define THING_DRAW_PROLOGUE_SIZE 8u

/* The matrix-scale composer at 0x0047E185, found as a CALL rel32 operand rather than matched by
 * its own signature. A hand-transcribed byte pattern for a thirteen-instruction function, derived
 * from disassembly text rather than raw bytes, is exactly the kind of thing that fails silently -
 * signature_find_unique just answers zero, indistinguishable from "this build does not have it" -
 * and a first attempt at exactly that here did fail silently. This is lower-risk: the CALL sits at
 * a fixed, confirmed offset from rdThing_Draw's own entry (0x67 bytes - measured directly off two
 * addresses already trusted for the detour above: entry 0x0040FE70, call instruction 0x0040FED7),
 * well past the eight bytes this file's own detour ever overwrites, so it reads correctly whether
 * this file's detour installed first or chained onto an existing one. The opcode byte is checked
 * before the operand is trusted, rather than assuming the offset is still right on some future
 * build - a wrong offset landing on a non-CALL byte would read a plausible but meaningless address,
 * and calling through that blind would be worse than refusing outright. */
#define THING_DRAW_TO_SCALE_CALL_OFFSET 0x67u

/* The player record pointer's own global cell - cross-confirmed by input_freeze.c/
 * framerate_stats.c as pPlayer, and independently here by disassembling Plr_AutoAim, which reads
 * [0x4B5220] then [that+0xC] and passes the result straight into bapobj_setNodeYaw's own bapObj
 * argument with no further resolution - so despite the "hActor" name at that offset, it is already
 * a raw bapObj pointer, not a handle needing translation through some pool lookup first. */
#define PLAYER_RECORD_PTR_ADDR 0x004B5220u
#define PLAYER_ACTOR_OFFSET    0x0Cu   /* bapObj*, confirmed via Plr_AutoAim as above            */
#define THING_OBJECT_OFFSET    0x9Cu   /* bapObj -> rdThing*, same OBJECT_THING dismemberment.c
                                        * already confirmed; redefined locally rather than shared,
                                        * this file does not otherwise depend on that one */

#define GIANT_PLAYER_SCALE 3.0f    /* the exact factor retail's own hardcoded special case uses */
#define TINY_PLAYER_SCALE  0.35f   /* a first guess for "small but still visible and playable" -
                                    * no retail precedent for this direction, unlike giant */

/* Free camera, site one of two: the simulation pause flag.
 *
 * Every fly-mode attempt above fought the player's own state machine one function at a time
 * because the player kept simulating. FUN_0043e9f2, the per-frame pump, gates the entire
 * fixed-timestep substep driver on two cells - confirmed byte-for-byte against the running image:
 *
 *   0043ea13  83 3D 44134838 00     cmp [00881344],0        ; a movie/quit-adjacent state, untouched
 *   0043ea1a  75 13                 jnz past the substep call
 *   0043ea1c  83 3D 00000000 00     cmp [SIM PAUSE FLAG],0  ; THE CELL THIS FEATURE USES
 *   0043ea23  75 0A                 jnz past the substep call
 *   0043ea25  6A 00                 push 0
 *   0043ea27  E8 00000000           call FUN_004756fc       ; the whole substep loop
 *   0043ea2c  83 C4 04              add esp,4
 *
 * FUN_004756fc is the fixed-timestep driver: it is where every registered per-tick callback runs,
 * including FUN_0047582a's table, which is where the player task installs FUN_00447d38 (Plr_
 * RunPhases, the whole player simulation). A non-zero pause flag skips the call to FUN_004756fc
 * entirely, so nothing registered on it runs at all - not just the player, the whole world. That
 * is confirmed as the SAME cell the retail ESC pause menu sets
 * (gameplay_open_pause_menu at 0x0043FAB5 writes it before its own blocking menu loop and clears it
 * after), so this is not a guessed side door, it is the mechanism the game already uses to pause
 * itself. This feature only ever writes the flag directly - it does not call the two
 * task_broadcast_cmd notifications retail's own pause menu also makes (audio ducking and similar),
 * a deliberate, smaller freeze: enough to stop the world moving under a free camera, not a
 * reproduction of the retail pause experience.
 *
 * The second cmp's operand, not the first, is what this pattern exists to read - the first is kept
 * as fixed, literal bytes purely for the uniqueness it adds, the same way other patterns in this
 * file keep a neighbouring instruction literal without needing its value. Not a detour target:
 * nothing here is ever overwritten, only read once for the address of the flag. */
static const uint8_t SIG_SIM_PAUSE_GATE[] = {
    0x83, 0x3D, 0x44, 0x13, 0x88, 0x00, 0x00,        /* cmp [00881344],0                    */
    0x75, 0x00,                                      /* jnz +N                               */
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00,        /* cmp [sim pause flag],0               */
    0x75, 0x00,                                      /* jnz +N                               */
    0x6A, 0x00,                                      /* push 0                               */
    0xE8, 0x00, 0x00, 0x00, 0x00,                    /* call the substep driver              */
    0x83, 0xC4, 0x04                                 /* add esp,4                            */
};
static const uint8_t MSK_SIM_PAUSE_GATE[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0x00,
    0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof(SIG_SIM_PAUSE_GATE) == sizeof(MSK_SIM_PAUSE_GATE),
               "the sim-pause-gate pattern and its mask are different lengths");
#define OFFSET_SIM_PAUSE_FLAG  0x0Bu   /* operand of the second cmp - the flag's own address */

/* Free camera, site two of two: the camera object pointer, and its per-frame update.
 *
 * SIG_CAMERA_VIEW is copied verbatim from enhanced_input's camera_sites.c (0x00418EDD, inside
 * updateCam's follow blend: `mov eax,[gView]; fld [eax+0x38]` - the previous camera yaw, read
 * immediately before the offset that feature adds to it). Same retail image, same bytes either
 * way, and that mod's own extensive cross-checking already established what they are; this file
 * resolves its own copy rather than reaching into another mod's DLL, the same independence every
 * other site in this file already keeps.
 *
 * updateCam itself, 0x00418544, is THE proven multi-tenant detour target of this whole project -
 * signature.h's own docs name it as chained by two DLLs already, and the chaining exists
 * specifically so a second detour on the same prologue does not have to scan for bytes the first
 * detour already overwrote. SIG_CAMERA_UPDATE and CAMERA_UPDATE_PROLOGUE_SIZE are copied from
 * camera_sites.c unchanged for exactly that reason: matching bytes, not just a matching address.
 *
 * WHERE THE CAMERA'S OWN POSITION LIVES, DISASSEMBLED DIRECTLY RATHER THAN TAKEN ON TRUST:
 *
 *   0041872f  MOV EAX,[gView]
 *   00418734  ADD EAX,0x14
 *   00418737  MOV ECX,[EBP-0x38] / MOV [EAX],ECX        ; anchor X  = view+0x14
 *   0041873c  MOV EDX,[EBP-0x34] / MOV [EAX+4],EDX       ; anchor Y  = view+0x18
 *   00418742  MOV ECX,[EBP-0x30] / MOV [EAX+8],ECX       ; anchor Z  = view+0x1c
 *   00418748  MOV EDX,[gView] / ADD EDX,0x34
 *   00418751  MOV EAX,[EBP-0x10] / MOV [EDX],EAX         ; euler.x   = view+0x34  (pitch, degrees)
 *   00418756  MOV ECX,[EBP-0xc] / MOV [EDX+4],ECX        ; euler.y   = view+0x38  (yaw, degrees)
 *   0041875c  MOV EAX,[EBP-8] / MOV [EDX+8],EAX          ; euler.z   = view+0x3c  (roll, untouched)
 *
 * written unconditionally, before the state (follow/fixed/world-fixed) dispatch that starts at
 * 0x004187ce even begins - camera_sites.c only ever traces the follow-state arm, so this is new.
 * The source, traced back further, is SetCamTarget (FUN_004184cc): the player's own per-tick
 * dispatch (FUN_00447d38 case 3) is one of its five call sites, feeding it the player's own
 * position. That is the actual coupling this feature breaks - not the state field, which changing
 * alone does nothing, since every state still re-derives a TARGET offset from the same anchor
 * every call. Freezing the simulation (the site above) stops SetCamTarget from ever being called
 * again, which stops the anchor's own feed cold; nothing then contends with a write made AFTER
 * calling the original updateCam, since this file's write runs strictly after the whole original
 * function - including its own second, later write to yaw alone at 0x00418fa1, part of the
 * follow-blend arm - has already finished. Roll (+0x3c) is left alone; a free camera has no use
 * for it and neither does anything reading euler.x/euler.y elsewhere. */
static const uint8_t SIG_CAMERA_VIEW[] = {
    0xA1, 0x00, 0x00, 0x00, 0x00,                     /* mov eax,[gView]                    */
    0xD9, 0x40, 0x38,                                 /* fld [eax+0x38]  previous camera yaw */
    0xD9, 0x5D, 0xE0,
    0xD9, 0x45, 0xD8,
    0xD8, 0x05, 0x00, 0x00, 0x00, 0x00                /* fadd [camera yaw offset]            */
};
static const uint8_t MSK_CAMERA_VIEW[] = {
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
};
_Static_assert(sizeof(SIG_CAMERA_VIEW) == sizeof(MSK_CAMERA_VIEW),
               "the camera-view pattern and its mask are different lengths");
#define OFFSET_CAMERA_VIEW_OBJECT   0x01u   /* operand of mov eax,[gView]                       */

/* 0x00418544, nine bytes: push ebp / mov ebp,esp / sub esp,0x88 - identical to camera_sites.c's
 * own CAMERA_UPDATE_PROLOGUE_SIZE, deliberately, since a mismatched prologue size on the SAME
 * chained site would misplace the trampoline for whichever DLL installs second. */
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
_Static_assert(sizeof(SIG_CAMERA_UPDATE) == sizeof(MSK_CAMERA_UPDATE),
               "the camera-update pattern and its mask are different lengths");
#define CAMERA_UPDATE_PROLOGUE_SIZE  9u

#define CAMERA_ANCHOR_X_OFFSET   0x14u
#define CAMERA_ANCHOR_Y_OFFSET   0x18u
#define CAMERA_ANCHOR_Z_OFFSET   0x1Cu
#define CAMERA_EULER_PITCH_OFFSET 0x34u
#define CAMERA_EULER_YAW_OFFSET   0x38u

/* eyeX/Y/Z, view+0x24/0x28/0x2c: eyeX = anchorX + (the local rig offset, view+0x04/0x08, rotated
 * by the current euler); eyeZ = anchorZ + offsetZ(view+0x0c), unrotated. Confirmed directly by
 * decompiling FUN_004181b9, the function that builds these every rendered frame from the anchor
 * and the euler updateCam just wrote - not taken on the strength of an earlier report alone.
 * Read only once, at free camera's own rising edge, to compute a look-at rather than to drive
 * anything ongoing - see that site's own comment for why. */
#define CAMERA_EYE_X_OFFSET      0x24u
#define CAMERA_EYE_Y_OFFSET      0x28u
#define CAMERA_EYE_Z_OFFSET      0x2Cu

#define DEG_TO_RAD               0.017453293f
#define RAD_TO_DEG               57.295780f
#define FREECAM_MOVE_SPEED       12.0f    /* world units/second, only the STARTING value now that
                                            * the wheel adjusts it at runtime (see freecam_speed) -
                                            * still a first guess, pending a field round, for what
                                            * the wheel then tunes away from */
#define FREECAM_MOUSE_DEGREES_PER_COUNT 0.15f   /* degrees of turn per pixel of cursor delta */
#define FREECAM_PITCH_LIMIT      89.0f    /* degrees; kept short of vertical to avoid a gimbal flip */
#define FREECAM_MIN_SPEED        0.5f     /* world units/second */
#define FREECAM_MAX_SPEED        200.0f   /* world units/second */
#define FREECAM_SPEED_PER_NOTCH  1.1f     /* multiplicative, Blender's own fly-mode feel: constant
                                            * ratio per notch reads as even control at both the slow
                                            * and the fast end, where a constant per-notch ADD would
                                            * feel enormous down low and glacial up high */

typedef void (__cdecl *use_ammo_fn_t)(int32_t weapon_id, int32_t amount);
typedef void (__cdecl *damage_fn_t)(int32_t amount);
typedef void (__cdecl *camera_update_fn_t)(void);
typedef int32_t (__cdecl *thing_draw_fn_t)(void *thing, float *matrix);
typedef void (__cdecl *scale_matrix_fn_t)(float *matrix, const float *scale_vec3);

typedef struct own_cheat {
    const char *name;
    bool        available;
    bool        on;
} own_cheat_t;

typedef struct cheats_own_state {
    bool                    installed;
    own_cheat_t             cheats[CHEATS_OWN_COUNT];
    detour_t                ammo_detour;
    detour_t                damage_detour;
    detour_t                npc_damage_detour;
    detour_t                thing_draw_detour;
    detour_t                camera_update_detour;
    use_ammo_fn_t           ammo_original;
    damage_fn_t             damage_original;
    thing_draw_fn_t         thing_draw_original;
    camera_update_fn_t      camera_update_original;
    scale_matrix_fn_t       scale_matrix_compose;      /* the retail matrix-scale composer, called
                                                          * directly rather than detoured; NULL if
                                                          * its own site did not resolve */
    uintptr_t               camera_view_address;       /* address OF the camera object pointer;
                                                          * 0 = unresolved, free camera unavailable */
    uintptr_t               sim_pause_flag_address;     /* address of the sim-freeze cell; 0 =
                                                          * unresolved, free camera unavailable */
} cheats_own_state_t;

static cheats_own_state_t own_state;

/* ============================================================================================ */
/* The hooks. Each is the whole feature: ask, and either decline or hand on unchanged. */

static void __cdecl hook_use_ammo(int32_t weapon_id, int32_t amount)
{
    if (own_state.cheats[CHEATS_OWN_UNLIMITED_AMMO].on) {
        return;
    }
    own_state.ammo_original(weapon_id, amount);
}

static void __cdecl hook_damage(int32_t amount)
{
    if (own_state.cheats[CHEATS_OWN_UNLIMITED_HEALTH].on) {
        return;
    }
    own_state.damage_original(amount);
}

/* Invincible NPCs and one-shot NPCs, sharing SIG_NPC_DAMAGE_APPLY's own site rather than needing
 * one signature each: both are decisions about the same fifteen bytes, "does the write happen, and
 * with what value", so one detour answers both. Where hook_camera_update above must always run the
 * original underneath it (that site is chained, other DLLs' features depend on it happening), this
 * one is not chained by anything and its own site comment already proves nothing downstream reads
 * EAX/ECX/EDX or the SUB's flags - so this hook is free to skip the original three-instruction
 * write outright rather than only being able to run alongside it.
 *
 * A naked trampoline rather than a call-site redirect like hook_use_ammo/hook_damage above: those
 * two detour a CALL instruction, so the hook can simply BE the function and choose whether to call
 * `original`. This site is a straight-line block in the middle of enemy_receiveDamage's own body,
 * not a call, so what gets overwritten is regular code and the hook must be entered by JMP with the
 * surrounding registers intact - the same shape dismemberment.c's own death-gate hook already uses,
 * extended here with a choice of WHERE to resume: normal play calls the trampoline (the original
 * three instructions, unmodified); either cheat sets npc_damage_skip and resumes at
 * npc_damage_continue instead, immediately past the write, having already decided the outcome
 * itself. on_npc_damage() communicates that choice through the static flag rather than a return
 * value in EAX, because popad below would overwrite EAX with whatever it held before the call and
 * erase any answer left in a register. */
static void  *npc_damage_trampoline;   /* the original 15 bytes, replayed when neither cheat is on */
static void  *npc_damage_continue;     /* site + NPC_DAMAGE_PROLOGUE_SIZE, resumed when one skips it */
static bool   npc_damage_skip;

static void __cdecl on_npc_damage(char *frame_pointer)
{
    void *victim = *(void **)(frame_pointer + 0x08);   /* [ebp+8], enemy_receiveDamage's param_1 */

    npc_damage_skip = false;
    if (victim == NULL) {
        return;
    }
    /* Invincible wins if both happen to be on at once: refusing the hit outright is the more
     * obviously correct answer than a hit that is simultaneously "took no damage" and "died". */
    if (own_state.cheats[CHEATS_OWN_INVINCIBLE_NPCS].on) {
        npc_damage_skip = true;      /* the write below never runs at all; health is untouched */
        return;
    }
    if (own_state.cheats[CHEATS_OWN_ONE_SHOT_NPCS].on) {
        /* <=0 is exactly what the death gate this function feeds (0x00437070, see dismemberment.c's
         * own DEATH GATE comment) tests for, so this is indistinguishable to the rest of the engine
         * from a hit that happened to deal lethal damage the ordinary way. */
        *(int32_t *)((char *)victim + 0x38) = 0;
        npc_damage_skip = true;
    }
}

static void __declspec(naked) hook_npc_damage(void)
{
    __asm {
        pushad
        pushfd
        push ebp
        call on_npc_damage
        add  esp, 4
        popfd
        popad
        cmp  byte ptr npc_damage_skip, 0
        jnz  npc_damage_resume_after
        jmp  dword ptr [npc_damage_trampoline]
    npc_damage_resume_after:
        jmp  dword ptr [npc_damage_continue]
    }
}

/* Giant player and tiny player, sharing one detour on rdThing_Draw rather than needing one each -
 * both are the same decision, "does the player's own render matrix get scaled, and by how much",
 * answered before the real draw runs. A plain typed hook, not a naked one: rdThing_Draw is a
 * regular __cdecl(thing*, matrix[12]) at the ABI boundary regardless of its own frame-pointer-
 * omitted body (see SIG_THING_DRAW's own comment), so there is nothing here that needs pushad/
 * pushfd the way the mid-function NPC-damage site above does.
 *
 * The player's own thing is chased fresh off the player-record global on every single call rather
 * than cached anywhere - this needs no rising-edge bookkeeping the way free camera's own seeding
 * does, because there is nothing here that persists between frames to begin with: the incoming
 * matrix is already a full rebuild of the player's real position and orientation for this frame,
 * every frame, so scaling it is inherently transient and switching either cheat off needs no
 * un-write, the next call simply stops scaling.
 *
 * FIELD-TESTED: scaling `matrix` in place - the caller's own working buffer for this object, not
 * something owned by this call - also scales the force-push ability's own reach and power, because
 * bapobj_drawAll reads that same buffer again right after this call returns for something that has
 * nothing to do with rendering. A local-copy version that left the caller's own numbers untouched
 * was tried and worked, but was reverted: combat is not meaningfully usable at either scale anyway,
 * so the extra copy bought correctness nothing was actually asking for. Swap back to a local copy
 * (see git history) if that ever stops being true. */
static int32_t __cdecl hook_thing_draw(void *thing, float *matrix)
{
    if (own_state.scale_matrix_compose != NULL && thing != NULL) {
        void *player_record = *(void **)(uintptr_t)PLAYER_RECORD_PTR_ADDR;

        if (player_record != NULL) {
            void *player_actor = *(void **)((char *)player_record + PLAYER_ACTOR_OFFSET);

            if (player_actor != NULL &&
                *(void **)((char *)player_actor + THING_OBJECT_OFFSET) == thing) {
                float scale_vec[3];

                if (own_state.cheats[CHEATS_OWN_GIANT_PLAYER].on) {
                    scale_vec[0] = scale_vec[1] = scale_vec[2] = GIANT_PLAYER_SCALE;
                    own_state.scale_matrix_compose(matrix, scale_vec);
                } else if (own_state.cheats[CHEATS_OWN_TINY_PLAYER].on) {
                    scale_vec[0] = scale_vec[1] = scale_vec[2] = TINY_PLAYER_SCALE;
                    own_state.scale_matrix_compose(matrix, scale_vec);
                }
            }
        }
    }
    return own_state.thing_draw_original(thing, matrix);
}

/* GetAsyncKeyState reads the physical key regardless of which window has it, so without this the
 * player would drift while the game sits alt-tabbed in the background and something else entirely
 * is holding E or Q. Compares the foreground window's owning process to this one instead of
 * tracking a HWND of our own, which needs nothing set up anywhere else in this DLL. */
static bool is_game_foreground(void)
{
    HWND  foreground = GetForegroundWindow();
    DWORD owner_pid = 0;

    if (foreground == NULL) {
        return false;
    }
    GetWindowThreadProcessId(foreground, &owner_pid);
    return owner_pid == GetCurrentProcessId();
}

/* NULL until dev_overlay.c wires it in - see this pointer's own type, declared in
 * cheats_openphantom.h, for why it is a pointer rather than a direct call. */
static cheats_openphantom_wheel_source_fn_t wheel_source;

void cheats_openphantom_set_wheel_source(cheats_openphantom_wheel_source_fn_t fn)
{
    wheel_source = fn;
}

/* Free camera. Owned entirely by this file, seeded from wherever the camera already is the moment
 * the cheat switches on: nothing else may be trusted to hold still. */
static float freecam_x, freecam_y, freecam_z;
static float freecam_yaw, freecam_pitch;   /* degrees */
static bool  freecam_valid = false;
static bool  freecam_cursor_hidden = false;   /* true while THIS cheat owns the OS cursor */

/* The scroll wheel adjusts this, Blender-fly-mode style, rather than it being the fixed
 * FREECAM_MOVE_SPEED constant every other tuned number in this file starts as. Deliberately a
 * file-level static rather than reseeded at the rising edge alongside position/orientation: a
 * speed dialled in once is worth keeping across a toggle off and back on, the same way Blender
 * itself remembers it walk speed between uses rather than resetting it every time. */
static float freecam_speed = FREECAM_MOVE_SPEED;

static LARGE_INTEGER freecam_perf_frequency;
static LONGLONG      freecam_last_tick;
static POINT         freecam_cursor_anchor;

/* The exit hotkey. Free camera's own mouse look claims the cursor, which leaves both the dev panel
 * and the game's own pause menu unreachable - no cursor, no click, and Escape does not work either
 * while the simulation this cheat paused is what would normally answer it. Without a keyboard-only
 * way out, turning this cheat on is a one-way door. 0 = unbound; the panel's hotkey row (see
 * overlay_model.c) is how a key gets into this. */
static int32_t freecam_hotkey;
static bool    freecam_hotkey_was_down;

int32_t cheats_openphantom_freecam_hotkey(void)
{
    return freecam_hotkey;
}

void cheats_openphantom_freecam_set_hotkey(int32_t virtual_key)
{
    freecam_hotkey = virtual_key;
    freecam_hotkey_was_down = false;   /* do not treat the binding key's own release as an edge */
}

/* Chained: this file's own override runs strictly AFTER the original updateCam, on every DLL's
 * behalf whichever order they loaded in (see common/detour.h). Calling the original unconditionally,
 * before even looking at whether the cheat is on, is what keeps this a well-behaved link in that
 * chain rather than a break in it - enhanced_input's own free-look feature is chained on this exact
 * same site and must keep running underneath this one regardless of what this cheat is doing. */
static void __cdecl hook_camera_update(void)
{
    void **view_slot;
    void  *view;

    own_state.camera_update_original();

    if (!own_state.cheats[CHEATS_OWN_FREECAM].on) {
        if (freecam_valid) {
            /* Falling edge: hand the world back. The camera itself needs no un-write - the very
             * next updateCam call recomputes it from the player the ordinary way, since nothing
             * here touches state (+0x00) or anything else the follow logic reads. */
            if (own_state.sim_pause_flag_address != 0) {
                *(int32_t *)own_state.sim_pause_flag_address = 0;
            }
            if (freecam_cursor_hidden) {
                ShowCursor(TRUE);
                freecam_cursor_hidden = false;
            }
            freecam_valid = false;
        }
        return;
    }

    if (own_state.camera_view_address == 0) {
        return;
    }
    view_slot = (void **)own_state.camera_view_address;
    view = *view_slot;
    if (view == NULL) {
        return;
    }

    if (!freecam_valid) {
        /* Rising edge: seed POSITION from wherever the camera already is, so switching on never
         * snaps the view, and pause the simulation - see SIG_SIM_PAUSE_GATE's own comment for why
         * this one flag is enough to stop the player and the whole world simulating out from
         * under a camera that is no longer looking through the player's own eyes. */
        freecam_x = *(float *)((uint8_t *)view + CAMERA_ANCHOR_X_OFFSET);
        freecam_y = *(float *)((uint8_t *)view + CAMERA_ANCHOR_Y_OFFSET);
        freecam_z = *(float *)((uint8_t *)view + CAMERA_ANCHOR_Z_OFFSET);

        /* ORIENTATION is computed fresh - a look-at from the retail camera's own eye position
         * toward its anchor (which tracks the player every frame; see updateCam's own site
         * comment above) - rather than copied from the raw euler fields the way position is.
         * Field-reported: switching free camera on could start it pointing "at the sky". The raw
         * pitch is periodic (FUN_004181b9 wraps it every frame, confirmed by decompiling it) so
         * that alone should not have caused it - sin/cos already handle any wrap correctly - which
         * points instead at the retail camera's own momentary rotation (lag catching up, a look at
         * something tall) simply not being a sensible starting orientation for a DIFFERENT
         * camera's use. Anchor and eye are both positions, immune to whatever the retail camera's
         * rotation happened to be doing, so a look-at from one to the other is deterministic
         * regardless of the cause. */
        {
            float eye_x = *(float *)((uint8_t *)view + CAMERA_EYE_X_OFFSET);
            float eye_y = *(float *)((uint8_t *)view + CAMERA_EYE_Y_OFFSET);
            float eye_z = *(float *)((uint8_t *)view + CAMERA_EYE_Z_OFFSET);
            float dx = freecam_x - eye_x;
            float dy = freecam_y - eye_y;
            float dz = freecam_z - eye_z;
            float horiz = sqrtf(dx * dx + dy * dy);

            if (horiz > 0.0001f || fabsf(dz) > 0.0001f) {
                freecam_yaw   = atan2f(-dx, dy) * RAD_TO_DEG;
                freecam_pitch = atan2f(dz, horiz) * RAD_TO_DEG;
            } else {
                /* Degenerate: eye and anchor coincide (a fixed or world-locked camera, most
                 * likely). Nothing to look at from nowhere away, so this falls back to whatever
                 * the raw fields hold, at least clamped into this file's own range. */
                freecam_yaw   = *(float *)((uint8_t *)view + CAMERA_EULER_YAW_OFFSET);
                freecam_pitch = *(float *)((uint8_t *)view + CAMERA_EULER_PITCH_OFFSET);
            }
            if (freecam_pitch > FREECAM_PITCH_LIMIT) {
                freecam_pitch = FREECAM_PITCH_LIMIT;
            }
            if (freecam_pitch < -FREECAM_PITCH_LIMIT) {
                freecam_pitch = -FREECAM_PITCH_LIMIT;
            }
        }
        if (own_state.sim_pause_flag_address != 0) {
            *(int32_t *)own_state.sim_pause_flag_address = 1;
        }
        QueryPerformanceFrequency(&freecam_perf_frequency);
        {
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            freecam_last_tick = now.QuadPart;
        }
        GetCursorPos(&freecam_cursor_anchor);
        ShowCursor(FALSE);
        freecam_cursor_hidden = true;
        /* Discard whatever the wheel accumulated while this cheat was off - overlay_input.c
         * observes it unconditionally, panel open or closed, cheat on or off, so without this a
         * scroll from minutes ago would show up as a sudden speed jump on the very first frame. */
        if (wheel_source != NULL) {
            (void)wheel_source();
        }
        freecam_valid = true;
    }

    /* Panel-aware mouse handling was tried here (skip look/movement and leave the cursor alone
     * while the dev panel is open) and REVERTED after a field-reported regression: closing the
     * panel left freecam_yaw climbing on its own every frame with no mouse input at all, and
     * looking unresponsive at the same time - both symptoms of the OS cursor being unable to reach
     * this cheat's anchor point, most likely enhanced_resolution's own ClipCursor confinement
     * (focus_guard.c) interacting with the re-anchor on panel close. Reverted to the simple,
     * previously-working shape rather than chase that live.
     *
     * That attempt was solving the wrong problem anyway. The actual complaint was not "I want the
     * panel usable while flying", it was "I have no way OUT of free camera once the mouse is
     * claimed" - the panel is unreachable without a working cursor and Escape does nothing while
     * this cheat has the simulation paused, so a player who did not already know a hotkey existed
     * was simply stuck. The exit hotkey below is the actual fix: keyboard only, needs no cursor at
     * all, and does not touch how the mouse behaves while flying. Whether the panel itself is ever
     * usable mid-flight is a separate question, unresolved, and no longer the one that mattered. */
    if (freecam_hotkey != 0 && is_game_foreground()) {
        bool hotkey_down = (GetAsyncKeyState(freecam_hotkey) & 0x8000) != 0;

        if (hotkey_down && !freecam_hotkey_was_down) {
            freecam_hotkey_was_down = hotkey_down;
            own_state.cheats[CHEATS_OWN_FREECAM].on = false;
            return;   /* the falling-edge cleanup above runs on the NEXT call to this hook */
        }
        freecam_hotkey_was_down = hotkey_down;
    }

    if (is_game_foreground()) {
        float dt;
        POINT cursor_now;

        {
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            dt = (freecam_perf_frequency.QuadPart > 0)
                     ? (float)((double)(now.QuadPart - freecam_last_tick) /
                               (double)freecam_perf_frequency.QuadPart)
                     : 0.0f;
            freecam_last_tick = now.QuadPart;
        }
        if (dt < 0.0f || dt > 0.25f) {
            dt = 0.0f;   /* a stall, or the very first tick since the rising edge above */
        }

        /* Mouse look: cursor-delta polling rather than enhanced_input's own raw-input thread.
         * Reusing that thread would mean either reaching into another mod's DLL (this project's
         * mods do not depend on each other) or duplicating its hard-won shim workaround for
         * WMAIN.EXE's own application-compatibility fix (see raw_mouse.c's own header comment) -
         * both worse than the jitter this simpler path accepts for what is a debug camera, not
         * competitive aim. */
        if (GetCursorPos(&cursor_now)) {
            long dx = cursor_now.x - freecam_cursor_anchor.x;
            long dy = cursor_now.y - freecam_cursor_anchor.y;

            if (dx != 0 || dy != 0) {
                /* Yaw (X) field-tested inverted from the first build and flipped, and stayed
                 * flipped - confirmed correct. Pitch (Y) was flipped in that same round on the
                 * assumption both axes were backward together; field testing showed that guess
                 * wrong - X's flip was right, Y's undid a pitch sign that was already correct - so
                 * this puts pitch back to its first-build sign while keeping yaw's fix. */
                freecam_yaw   -= (float)dx * FREECAM_MOUSE_DEGREES_PER_COUNT;
                freecam_pitch -= (float)dy * FREECAM_MOUSE_DEGREES_PER_COUNT;
                if (freecam_pitch > FREECAM_PITCH_LIMIT) {
                    freecam_pitch = FREECAM_PITCH_LIMIT;
                }
                if (freecam_pitch < -FREECAM_PITCH_LIMIT) {
                    freecam_pitch = -FREECAM_PITCH_LIMIT;
                }
                SetCursorPos(freecam_cursor_anchor.x, freecam_cursor_anchor.y);
            }
        }

        /* Scroll wheel adjusts fly speed, the same feel Blender's own fly/walk navigation uses.
         * overlay_input.c observes WM_MOUSEWHEEL unconditionally (panel open or closed) precisely
         * because this needs it while FLYING, which is exactly when the panel is closed - see that
         * file's own comment for how it confirmed the message actually reaches its hook (the
         * engine's top-level window procedure special-cases only four message types and falls
         * through to the same registered-handler chain for everything else, wheel included).
         * Multiplicative per notch rather than additive, so the same scroll feels proportionate
         * whether the current speed is barely-crawling or already fast. wheel_source is NULL if
         * dev_overlay.c never wired it in (its own site did not resolve) - then this simply never
         * fires, the same as every other optional site in this file failing quietly. */
        if (wheel_source != NULL) {
            int32_t wheel = wheel_source();

            if (wheel != 0) {
                float notches = (float)wheel / (float)WHEEL_DELTA;

                freecam_speed *= powf(FREECAM_SPEED_PER_NOTCH, notches);
                if (freecam_speed < FREECAM_MIN_SPEED) {
                    freecam_speed = FREECAM_MIN_SPEED;
                }
                if (freecam_speed > FREECAM_MAX_SPEED) {
                    freecam_speed = FREECAM_MAX_SPEED;
                }
            }
        }

        /* WASD along the full 3D view direction (so looking up while holding W climbs, exactly
         * the free-fly feel the pitch-redirect attempt on the PLAYER tried and failed at - the
         * difference is that nothing here is fighting a third-person camera's own resting angle,
         * because nothing here is a third-person camera anymore, it is this cheat's own camera).
         * E/Q add a world-vertical on top, independent of pitch, for straight up/down without
         * needing to look at the sky or the floor first.
         *
         * FIELD-TESTED, WRONG, THEN FIXED FROM THE ENGINE'S OWN CODE RATHER THAN A SECOND GUESS.
         * The first build of this used fwd_x=+sin(yaw)*cos(pitch) and right_y=-sin(yaw), a self-
         * consistent guess with no independent evidence behind it. Field test: "W doesn't always
         * go forward" - correct near yaw=0, where sin(0)=0 hides the error, and increasingly wrong
         * as yaw grew. Rather than guess a second time, this is now read out of the engine itself:
         * FUN_004181b9 (the function that builds the render eye position every frame) calls
         * FUN_0047cfbc, a generic euler-degrees-to-rotation-matrix utility used at roughly fifty
         * sites across the binary, whose matrix decodes to
         *
         *     right   = ( cos(yaw),             sin(yaw),            0          )
         *     forward = (-sin(yaw)*cos(pitch),   cos(yaw)*cos(pitch), sin(pitch))
         *
         * at zero roll (this camera's roll, +0x3c, is never written by anything this file found).
         * Independently cross-checked against the game's OWN built-in debug free-cam (arrow keys,
         * FUN_004190c1 -> FUN_00418349): its translation is worldX += dx*cos(yaw)-dy*sin(yaw),
         * worldY += dx*sin(yaw)+dy*cos(yaw), which for pure forward (dx=0,dy=1) gives exactly
         * (-sin(yaw), cos(yaw)), matching the formula above at pitch=0. Two independent sites
         * agree, which is what "confirmed" means in this file rather than "guessed once more". */
        {
            float forward = 0.0f;
            float strafe  = 0.0f;
            float vertical = 0.0f;

            if ((GetAsyncKeyState('W') & 0x8000) != 0) { forward  += 1.0f; }
            if ((GetAsyncKeyState('S') & 0x8000) != 0) { forward  -= 1.0f; }
            if ((GetAsyncKeyState('D') & 0x8000) != 0) { strafe   += 1.0f; }
            if ((GetAsyncKeyState('A') & 0x8000) != 0) { strafe   -= 1.0f; }
            if ((GetAsyncKeyState('E') & 0x8000) != 0) { vertical += 1.0f; }
            if ((GetAsyncKeyState('Q') & 0x8000) != 0) { vertical -= 1.0f; }

            if (forward != 0.0f || strafe != 0.0f || vertical != 0.0f) {
                float yaw_rad   = freecam_yaw * DEG_TO_RAD;
                float pitch_rad = freecam_pitch * DEG_TO_RAD;
                float cos_yaw   = cosf(yaw_rad);
                float sin_yaw   = sinf(yaw_rad);
                float cos_pitch = cosf(pitch_rad);
                float sin_pitch = sinf(pitch_rad);
                float fwd_x     = -sin_yaw * cos_pitch;
                float fwd_y     = cos_yaw * cos_pitch;
                float fwd_z     = sin_pitch;
                float right_x   = cos_yaw;
                float right_y   = sin_yaw;
                float planar    = sqrtf(forward * forward + strafe * strafe);
                float scale     = (planar > 1.0f) ? (1.0f / planar) : 1.0f;   /* no diagonal boost */
                float speed     = freecam_speed * dt;

                freecam_x += (fwd_x * forward + right_x * strafe) * scale * speed;
                freecam_y += (fwd_y * forward + right_y * strafe) * scale * speed;
                freecam_z += fwd_z * forward * scale * speed + vertical * speed;
            }
        }
    }

    *(float *)((uint8_t *)view + CAMERA_ANCHOR_X_OFFSET)    = freecam_x;
    *(float *)((uint8_t *)view + CAMERA_ANCHOR_Y_OFFSET)    = freecam_y;
    *(float *)((uint8_t *)view + CAMERA_ANCHOR_Z_OFFSET)    = freecam_z;
    *(float *)((uint8_t *)view + CAMERA_EULER_PITCH_OFFSET) = freecam_pitch;
    *(float *)((uint8_t *)view + CAMERA_EULER_YAW_OFFSET)   = freecam_yaw;
}

/* ============================================================================================ */

static bool install_one(const uint8_t *bytes, const uint8_t *mask, size_t size,
                        const void *hook, detour_t *detour, size_t prologue_size,
                        const char *what)
{
    uintptr_t site = signature_find_detour_target(bytes, mask, size, prologue_size);

    if (site == 0) {
        log_warning("%s did not resolve, so that cheat is offered as unavailable rather than as a "
                    "row that ticks and does nothing", what);
        return false;
    }
    if (!detour_install(detour, site, hook, prologue_size)) {
        log_warning("%s at %08X could not be detoured, that cheat stays unavailable",
                    what, (unsigned)site);
        return false;
    }
    log_info("%s hooked at %08X", what, (unsigned)site);
    return true;
}

/* Both NPC cheats live behind this one detour - see on_npc_damage's own comment for why one site
 * answers both. */
static void install_npc_damage(void)
{
    if (!install_one(SIG_NPC_DAMAGE_APPLY, NULL, sizeof SIG_NPC_DAMAGE_APPLY,
                     (const void *)&hook_npc_damage, &own_state.npc_damage_detour,
                     NPC_DAMAGE_PROLOGUE_SIZE, "the NPC health write")) {
        return;
    }
    npc_damage_trampoline = own_state.npc_damage_detour.original;
    npc_damage_continue   = (void *)(own_state.npc_damage_detour.target + NPC_DAMAGE_PROLOGUE_SIZE);
    own_state.cheats[CHEATS_OWN_INVINCIBLE_NPCS].available = true;
    own_state.cheats[CHEATS_OWN_ONE_SHOT_NPCS].available   = true;
}

/* Both player-scale cheats live behind this one detour - see hook_thing_draw's own comment for why
 * one site answers both. Needs its own matrix-scale composer resolved first: a cheat that could
 * intercept the draw call but had nothing to scale with would not be half a feature, it would be a
 * detour doing nothing, so this refuses the whole thing rather than offering that. */
static void install_player_scale(void)
{
    uintptr_t call_site;
    uint8_t   opcode;
    uint32_t  rel32;
    uintptr_t scale_target;

    if (!install_one(SIG_THING_DRAW, NULL, sizeof SIG_THING_DRAW, (const void *)&hook_thing_draw,
                     &own_state.thing_draw_detour, THING_DRAW_PROLOGUE_SIZE,
                     "the object render call")) {
        return;
    }

    call_site = own_state.thing_draw_detour.target + THING_DRAW_TO_SCALE_CALL_OFFSET;
    if (!memory_read_u8(call_site, &opcode) || opcode != 0xE8 ||
        !memory_read_u32(call_site + 1, &rel32)) {
        log_warning("the matrix-scale composer's own call site at %08X did not check out, "
                    "giant/tiny player stay unavailable", (unsigned)call_site);
        return;
    }
    scale_target = call_site + 5 + rel32;
    if (!memory_is_executable_range(scale_target, 1)) {
        log_warning("the matrix-scale composer resolved to %08X, which is not executable, "
                    "refused", (unsigned)scale_target);
        return;
    }

    own_state.scale_matrix_compose = (scale_matrix_fn_t)scale_target;
    own_state.thing_draw_original  = (thing_draw_fn_t)own_state.thing_draw_detour.original;
    own_state.cheats[CHEATS_OWN_GIANT_PLAYER].available = true;
    own_state.cheats[CHEATS_OWN_TINY_PLAYER].available  = true;
    log_info("giant/tiny player: object render call hooked at %08X, matrix-scale composer at "
             "%08X", (unsigned)own_state.thing_draw_detour.target, (unsigned)scale_target);
}

/* Free camera needs both sites - the pause flag and the camera object pointer - to mean anything:
 * a camera that could roam but never stopped the world moving underneath it is not this feature,
 * and neither is a pause with nothing to look through. A partial resolve here is not offered as
 * half a feature; either both resolve or the cheat says why and stays unavailable. */
static bool install_freecam(void)
{
    uintptr_t pause_site;
    uintptr_t view_site;
    uintptr_t update_site;
    uint32_t  pause_flag_address = 0;
    uint32_t  view_address = 0;

    pause_site = signature_find_unique(SIG_SIM_PAUSE_GATE, MSK_SIM_PAUSE_GATE,
                                       sizeof SIG_SIM_PAUSE_GATE);
    if (pause_site == 0 ||
        !memory_read_u32(pause_site + OFFSET_SIM_PAUSE_FLAG, &pause_flag_address) ||
        !memory_is_inside_image(pause_flag_address, sizeof(int32_t))) {
        log_warning("free camera: the simulation pause gate did not resolve, that cheat stays "
                    "unavailable");
        return false;
    }

    view_site = signature_find_unique(SIG_CAMERA_VIEW, MSK_CAMERA_VIEW, sizeof SIG_CAMERA_VIEW);
    if (view_site == 0 ||
        !memory_read_u32(view_site + OFFSET_CAMERA_VIEW_OBJECT, &view_address) ||
        !memory_is_inside_image(view_address, sizeof(void *))) {
        log_warning("free camera: the camera object site did not resolve, that cheat stays "
                    "unavailable");
        return false;
    }

    update_site = signature_find_detour_target(SIG_CAMERA_UPDATE, MSK_CAMERA_UPDATE,
                                               sizeof SIG_CAMERA_UPDATE,
                                               CAMERA_UPDATE_PROLOGUE_SIZE);
    if (update_site == 0) {
        log_warning("free camera: the camera update site did not resolve, that cheat stays "
                    "unavailable");
        return false;
    }
    if (!detour_install(&own_state.camera_update_detour, update_site,
                        (const void *)&hook_camera_update, CAMERA_UPDATE_PROLOGUE_SIZE)) {
        log_warning("free camera: the camera update site at %08X could not be detoured, that "
                    "cheat stays unavailable", (unsigned)update_site);
        return false;
    }

    own_state.camera_update_original = (camera_update_fn_t)own_state.camera_update_detour.original;
    own_state.sim_pause_flag_address = (uintptr_t)pause_flag_address;
    own_state.camera_view_address    = (uintptr_t)view_address;
    log_info("free camera: pause flag at %08X, camera object pointer at %08X, update chained at "
             "%08X - WASD moves along the view, mouse looks, E/Q move vertically",
             (unsigned)pause_flag_address, (unsigned)view_address, (unsigned)update_site);
    return true;
}

bool cheats_openphantom_install(void)
{
    if (own_state.installed) {
        return true;
    }

    own_state.cheats[CHEATS_OWN_UNLIMITED_AMMO].name = "Unlimited ammunition";
    own_state.cheats[CHEATS_OWN_UNLIMITED_HEALTH].name = "Unlimited health";
    own_state.cheats[CHEATS_OWN_NO_FOG].name = "No fog";
    own_state.cheats[CHEATS_OWN_INVINCIBLE_NPCS].name = "Invincible NPCs";
    own_state.cheats[CHEATS_OWN_ONE_SHOT_NPCS].name = "One-shot NPCs";
    own_state.cheats[CHEATS_OWN_GIANT_PLAYER].name = "Giant player";
    own_state.cheats[CHEATS_OWN_TINY_PLAYER].name = "Tiny player";
    own_state.cheats[CHEATS_OWN_FREECAM].name = "Free camera";

    if (install_one(SIG_USE_AMMO, MSK_USE_AMMO, sizeof SIG_USE_AMMO,
                    (const void *)&hook_use_ammo, &own_state.ammo_detour, STATUS_PROLOGUE_SIZE,
                    "the ammunition spend")) {
        own_state.ammo_original = (use_ammo_fn_t)own_state.ammo_detour.original;
        own_state.cheats[CHEATS_OWN_UNLIMITED_AMMO].available = true;
    }

    if (install_one(SIG_DAMAGE, MSK_DAMAGE, sizeof SIG_DAMAGE,
                    (const void *)&hook_damage, &own_state.damage_detour, STATUS_PROLOGUE_SIZE,
                    "the damage application")) {
        own_state.damage_original = (damage_fn_t)own_state.damage_detour.original;
        own_state.cheats[CHEATS_OWN_UNLIMITED_HEALTH].available = true;
    }

    install_npc_damage();
    install_player_scale();

    if (install_freecam()) {
        own_state.cheats[CHEATS_OWN_FREECAM].available = true;
    }

    /* A different shape - see cheats_no_fog.h - so it owns its own state and this only asks it. */
    (void)cheats_no_fog_install();

    own_state.installed = true;

    /* All six stand for the life of the process whether used or not: each detour costs one
     * comparison per call while off, and the fog tick costs one comparison per frame while off.
     * That is the price of being able to switch any of them from the panel at any moment. If none
     * resolved there is nothing to switch, and the caller says so once. */
    return own_state.cheats[CHEATS_OWN_UNLIMITED_AMMO].available ||
           own_state.cheats[CHEATS_OWN_UNLIMITED_HEALTH].available ||
           own_state.cheats[CHEATS_OWN_INVINCIBLE_NPCS].available ||
           own_state.cheats[CHEATS_OWN_ONE_SHOT_NPCS].available ||
           own_state.cheats[CHEATS_OWN_GIANT_PLAYER].available ||
           own_state.cheats[CHEATS_OWN_TINY_PLAYER].available ||
           own_state.cheats[CHEATS_OWN_FREECAM].available ||
           cheats_no_fog_is_available();
}

const char *cheats_openphantom_name(cheats_own_id_t id)
{
    if ((unsigned)id >= (unsigned)CHEATS_OWN_COUNT) {
        return NULL;
    }
    return own_state.cheats[id].name;
}

bool cheats_openphantom_is_available(cheats_own_id_t id)
{
    if (id == CHEATS_OWN_NO_FOG) {
        return cheats_no_fog_is_available();
    }
    if ((unsigned)id >= (unsigned)CHEATS_OWN_COUNT) {
        return false;
    }
    return own_state.cheats[id].available;
}

bool cheats_openphantom_is_on(cheats_own_id_t id)
{
    if (id == CHEATS_OWN_NO_FOG) {
        return cheats_no_fog_is_on();
    }
    if ((unsigned)id >= (unsigned)CHEATS_OWN_COUNT) {
        return false;
    }
    return own_state.cheats[id].on;
}

bool cheats_openphantom_toggle(cheats_own_id_t id)
{
    if (id == CHEATS_OWN_NO_FOG) {
        return cheats_no_fog_toggle();
    }
    if ((unsigned)id >= (unsigned)CHEATS_OWN_COUNT ||
        !own_state.cheats[id].available) {
        return false;
    }
    /* Turning free camera ON without an exit hotkey bound is a one-way door: the mouse it claims
     * is also what the dev panel and the game's own pause menu need, and there is no way to close
     * this cheat again without one. The authoritative gate lives here rather than only in the
     * panel's own row.available, so nothing that reaches this function directly can bypass it. */
    if (id == CHEATS_OWN_FREECAM && !own_state.cheats[id].on && freecam_hotkey == 0) {
        return false;
    }
    own_state.cheats[id].on = !own_state.cheats[id].on;
    /* Giant and tiny player are mutually exclusive: turning one on turns the other off, rather
     * than leaving a row reading ON that has no visible effect because hook_thing_draw's own
     * precedence (giant checked first) is the one actually deciding what gets applied. */
    if (own_state.cheats[id].on) {
        if (id == CHEATS_OWN_GIANT_PLAYER) {
            own_state.cheats[CHEATS_OWN_TINY_PLAYER].on = false;
        } else if (id == CHEATS_OWN_TINY_PLAYER) {
            own_state.cheats[CHEATS_OWN_GIANT_PLAYER].on = false;
        }
    }
    return own_state.cheats[id].on;
}
