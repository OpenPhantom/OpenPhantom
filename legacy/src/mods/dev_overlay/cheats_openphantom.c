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
#include "cheats_original_actions.h"

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

/* --- 0x0044EC80 and 0x0044EDF6: the two mode-entry functions that launch the player upward ------
 *
 * The player's mode field (+0x60) is a POINTER to a descriptor, not an enum (see player_record.h's
 * own comment on this, cross-project). The mode dispatch table at 0x004B54B0 holds fourteen such
 * descriptor pointers in enum order; reading it directly (table[6] and table[7]) gives the exact
 * addresses these two functions write into +0x60 below, cross-confirming which function enters
 * which mode rather than trusting the mode-name debug strings alone:
 *
 *   table[6] (Jump)      = 0x004B5338
 *   table[7] (Jedi Jump) = 0x004B5368
 *
 * Both functions open identically for the first twenty bytes - a guard that skips the whole jump if
 * some player-record float at +0x168 fails a threshold check against a shared constant at
 * 0x004A86A4 - which is exactly the shape this file's own SIG_USE_AMMO/SIG_DAMAGE comment already
 * warns about: a pattern that stopped at the shared prefix would match either function, or others
 * like it for the remaining mode-entry functions (Sabre Attack, Panaka Attack, Push Block) this
 * feature has no reason to touch. Both patterns below therefore reach all the way to the
 * `mov [ecx+0x60],<that mode's own descriptor address>` instruction - the exact table[6]/table[7]
 * values above, embedded as the pattern's own trailing bytes - which is unique to each function by
 * construction, not by luck.
 *
 * 0x0044EC80, disassembled directly (not decompiled) and hand-verified instruction by instruction
 * against x86 encoding rather than transcribed from Ghidra's listing text alone:
 *
 *   0044ec80  55                       push ebp
 *   0044ec81  8B EC                    mov ebp,esp
 *   0044ec83  A1 20524B00              mov eax,[0x004b5220]      ; pPlayer
 *   0044ec88  D9 80 68010000           fld dword ptr [eax+0x168] ; the guard float
 *   0044ec8e  D8 1D A4864A00           fcomp dword ptr [0x004a86a4]
 *   0044ec94  DF E0                    fnstsw ax
 *   0044ec96  F6 C4 41                 test ah,0x41
 *   0044ec99  75 05                    jnz +5
 *   0044ec9b  E9 E0000000              jmp 0x0044ed80             ; guard failed, skip the jump
 *   0044eca0  8B 0D 20524B00           mov ecx,[0x004b5220]
 *   0044eca6  C7 41 60 38534B00        mov dword ptr [ecx+0x60],0x004b5338   ; -> Jump mode
 *
 * Continuing a little further (not part of the pattern, kept here as the evidence for where +0xB4
 * gets its value): a nearby-ledge query at 0x0040c464 branches between a flat fallback constant
 * (0x4099999a = 4.8f, written at 0x0044ece8) and a per-character table read (DAT_004b5210[charIdx],
 * written at 0x0044ed19) - either way the result lands in the SAME field, [ecx+0xb4], right after
 * the already-confirmed +0xB0 PLAYER_CURRENT_SPEED. That shared destination, not either individual
 * source, is what this feature hooks around: reading it back right after calling the original
 * covers both paths without needing to know which one fired.
 *
 * 0x0044EDF6 (Jedi Jump) opens byte-identical for the same twenty bytes, proven by disassembling it
 * independently rather than assumed from the first function's shape - the only differences before
 * the divergence point are the JMP's own rel32 (this function is longer, so the guard-failure
 * target is farther away) and, critically, the mode descriptor address written: 0x004b5368, table
 * [7], not table[6]. It writes the SAME +0xb4 field a little further into its own body (0x0044ee7c
 * / 0x0044eeae, confirmed by disassembling past the pattern below), through the identical
 * fallback-constant/per-character-table shape. */
static const uint8_t SIG_JUMP_ENTRY[] = {
    0x55,                                              /* push ebp                              */
    0x8B, 0xEC,                                        /* mov ebp,esp                           */
    0xA1, 0x20, 0x52, 0x4B, 0x00,                      /* mov eax,[0x004b5220]                  */
    0xD9, 0x80, 0x68, 0x01, 0x00, 0x00,                /* fld dword ptr [eax+0x168]             */
    0xD8, 0x1D, 0xA4, 0x86, 0x4A, 0x00,                /* fcomp dword ptr [0x004a86a4]          */
    0xDF, 0xE0,                                        /* fnstsw ax                             */
    0xF6, 0xC4, 0x41,                                  /* test ah,0x41                          */
    0x75, 0x05,                                        /* jnz +5                                */
    0xE9, 0xE0, 0x00, 0x00, 0x00,                      /* jmp 0x0044ed80                        */
    0x8B, 0x0D, 0x20, 0x52, 0x4B, 0x00,                /* mov ecx,[0x004b5220]                  */
    0xC7, 0x41, 0x60, 0x38, 0x53, 0x4B, 0x00           /* mov [ecx+0x60],0x004b5338 -> Jump      */
};
#define JUMP_ENTRY_PROLOGUE_SIZE 8u   /* first boundary at/past five bytes: push ebp; mov ebp,esp;
                                        * mov eax,[0x004b5220] - identical in both functions, but the
                                        * SIGNATURE above reaches nineteen bytes past this to stay
                                        * unique; only these first eight are ever relocated */

static const uint8_t SIG_JEDI_JUMP_ENTRY[] = {
    0x55,                                              /* push ebp                              */
    0x8B, 0xEC,                                        /* mov ebp,esp                           */
    0xA1, 0x20, 0x52, 0x4B, 0x00,                      /* mov eax,[0x004b5220]                  */
    0xD9, 0x80, 0x68, 0x01, 0x00, 0x00,                /* fld dword ptr [eax+0x168]             */
    0xD8, 0x1D, 0xA4, 0x86, 0x4A, 0x00,                /* fcomp dword ptr [0x004a86a4]          */
    0xDF, 0xE0,                                        /* fnstsw ax                             */
    0xF6, 0xC4, 0x41,                                  /* test ah,0x41                          */
    0x75, 0x05,                                        /* jnz +5                                */
    0xE9, 0x10, 0x01, 0x00, 0x00,                      /* jmp 0x0044ef26                        */
    0x8B, 0x0D, 0x20, 0x52, 0x4B, 0x00,                /* mov ecx,[0x004b5220]                  */
    0xC7, 0x41, 0x60, 0x68, 0x53, 0x4B, 0x00           /* mov [ecx+0x60],0x004b5368 -> JediJump  */
};
#define JEDI_JUMP_ENTRY_PROLOGUE_SIZE 8u   /* same reasoning as JUMP_ENTRY_PROLOGUE_SIZE above */

#define PLAYER_VERTICAL_VELOCITY_OFFSET 0xB4u   /* float; right after the already-confirmed +0xB0
                                                  * PLAYER_CURRENT_SPEED, written by both functions
                                                  * above via either of their two paths */
/* Velocity, not height - jump height scales with velocity SQUARED under the engine's own linear
 * gravity decay, so 1.3 is roughly a 69% higher jump, not 30%. A first guess for "noticeably
 * higher, not silly" the same way TINY_PLAYER_SCALE above is; no retail precedent either direction.
 * Runtime-adjustable rather than fixed - the dev panel's own value row (see overlay_model.c) reads
 * and writes this through the getter/setter below, so this is only ever where a fresh install
 * starts, not the whole of what the cheat can be. Clamped on every write into a range wide enough
 * to be useful in both directions (a player weaker than retail is exactly as legitimate an ask as
 * one much stronger) but short of anything that turns a launch into a projectile the level's own
 * collision was never built to catch at the far end, or a no-op at the near one. */
#define JUMP_BOOST_SCALE_DEFAULT 1.3f
#define JUMP_BOOST_SCALE_MIN     0.5f
#define JUMP_BOOST_SCALE_MAX     5.0f

/* --- 0x0044F162 and the fixed offset into it: fall damage, suppressed while jump boost is on -----
 *
 * A higher jump is also a longer fall, so boosting jump height quietly raises how often ordinary
 * landings cross retail's own fall-damage threshold - a side effect of this cheat, not something a
 * player asking for a higher jump was asking to be punished for. Rather than a cheat of its own,
 * this ties directly to CHEATS_OWN_JUMP_BOOST's own on/off state: no extra row, no extra toggle,
 * fall damage simply stops mattering for exactly as long as the jump it is a consequence of is
 * boosted.
 *
 * FUN_0044F162 is the player's per-frame ground-contact resolver - footstep sound, snap-to-ground,
 * landing animation state, AND fall damage all live in this one function, reached once per frame
 * regardless of whether the player is actually on the ground. The fall-damage decision is one
 * self-contained block near its end, gated behind two threshold compares (impact speed against
 * +0xB4 - the same PLAYER_VERTICAL_VELOCITY this file already uses for jump boost itself - and an
 * accumulated fall-distance field at +0x360) and a mode check that exempts Stand and Swim:
 *
 *   0044f56e  MOV EAX,[0x004b5220]         ; pPlayer
 *   0044f573  FLD  float [EAX+0xb4]        ; PLAYER_VERTICAL_VELOCITY
 *   0044f579  FCOMP float [0x004a8758]     ; minimum impact speed for damage to apply at all
 *   0044f584  JZ   0x0044f60f              ; too soft - no damage, different landing state
 *   0044f58a  ...                          ; landing sound, always plays past this point
 *   0044f59c  MOV ECX,[0x004b5220]
 *   0044f5a2  FLD  float [ECX+0x360]       ; accumulated fall distance
 *   0044f5a8  FCOMP float [0x004a875c]     ; minimum fall distance for damage
 *   0044f5b3  JNZ  0x0044f5e5              ; not far enough - alt "hard land, no damage" state
 *   0044f5b5..f5ca                         ; mode == Stand or Swim - skip damage either way
 *   0044f5cc  PUSH 0xa                     ; the damage amount - FIXED, not scaled by fall speed
 *   0044f5ce  CALL 0x00459ece              ; the SAME generic damage-apply function SIG_DAMAGE
 *                                          ; above already resolves independently
 *   0044f5d3  ADD  ESP,0x4
 *   0044f5d6  PUSH 0x3f                    ; landing animation/state, runs whether or not damage did
 *
 * Confirmed by disassembling the whole function directly, not decompiled text. The call site itself
 * is not matched by its own signature - a hand-encoded pattern for ten bytes buried five hundred
 * bytes into an unrelated function is exactly the shape that already failed silently once in this
 * file (see THING_DRAW_TO_SCALE_CALL_OFFSET's own comment). Instead it is computed as a fixed
 * offset from this function's OWN entry, which the twenty-byte signature below finds uniquely (the
 * compare against player-record+0x36C == 0x59 is distinctive enough on its own), and then checked
 * three separate ways before being trusted: the opcode at the computed address must be PUSH imm8
 * with operand 0x0A, the next opcode must be CALL rel32, and that call's own resolved target must
 * equal the SAME address SIG_DAMAGE's own detour already found - not merely "some executable
 * address", a specific, independently-corroborated one. */
static const uint8_t SIG_PLAYER_GROUND_CONTACT[] = {
    0x55,                                       /* push ebp                              */
    0x8B, 0xEC,                                 /* mov ebp,esp                           */
    0x83, 0xEC, 0x28,                           /* sub esp,0x28                          */
    0xA1, 0x20, 0x52, 0x4B, 0x00,               /* mov eax,[0x004b5220]                  */
    0x83, 0xB8, 0x6C, 0x03, 0x00, 0x00, 0x59,   /* cmp dword ptr [eax+0x36c],0x59        */
    0x75, 0x42                                  /* jnz +0x42                             */
};
#define FALL_DAMAGE_CALL_OFFSET   0x46Au   /* 0x0044F5CC - 0x0044F162, measured directly off both
                                            * addresses in the disassembly above */

/* FALL DEATH IS A SEPARATE MECHANISM FROM FALL DAMAGE, NOT THE SAME ONE ESCALATING.
 *
 * Field reported: jumping very high with jump boost triggers a death screen and a level reload,
 * not just the 10-point hit above. Player health never comes into it anywhere in this path - the
 * player's own health global (0x0086D57C, the same one hook_damage above subtracts from) is
 * written only by a level-load/respawn reset and a weapon-swap function, confirmed by their own
 * xrefs; nothing in FUN_0044F162 ever reads it. Instead, the SAME ground-contact function calls a
 * dedicated "force the player into Death mode" function - 0x004500B0, decompiled directly and
 * confirmed unconditional: `pPlayer->mode = &DeathDescriptor; pPlayer->0x364 = cause;` with no
 * health check anywhere in it - from two OTHER sites, both upstream of the fall-damage block
 * above and keyed off the exact same two fields jump boost already interacts with:
 *
 *   TIME-BASED, mid-air, no landing needed - a 2.0 second airborne countdown timer at +0x35C,
 *   armed the moment accumulated fall distance first crosses _DAT_004a86f4 and ticked down every
 *   frame by frame-delta (+0x74). Once it reaches ~0:
 *     0044f2c1  FLD [+0x35c]; FSUB [+0x74]; FSTP [+0x35c]         ; timer -= dt
 *     0044f2db  FLD [+0x35c]; FCOMP [0x4a86a4]; ...; JZ 0044f31b  ; still running - skip
 *     0044f311  PUSH 0x2
 *     0044f313  CALL 0x004500b0                                  ; DEATH, cause 2
 *     0044f318  ADD ESP,0x4                                      ; falls through to 0044f31b either
 *                                                                 ; way - same target the JZ above
 *                                                                 ; already uses, so suppressing
 *                                                                 ; just this call needs no special
 *                                                                 ; resume target of its own.
 *
 *   DISTANCE-BASED, checked on landing, BEFORE the known 10-damage block - a fixed distance
 *   ceiling, larger than the 10-damage one:
 *     0044f480  MOV EAX,[0x004b5220]
 *     0044f485  FLD [EAX+0x360]              ; accumulated fall distance
 *     0044f48b  FCOMP [0x004a86dc]           ; a SEPARATE, LARGER threshold than fall damage's own
 *     0044f496  JNZ 0044f4ca                 ; below the ceiling - skip death, land normally
 *     0044f4bb  PUSH 0x1
 *     0044f4bd  CALL 0x004500b0              ; DEATH, cause 1
 *     0044f4c2  ADD ESP,0x4
 *     0044f4c5  JMP 0x0044f88d               ; UNCONDITIONAL early return - the function bails out
 *                                            ; the instant death fires, skipping the rest of its own
 *                                            ; landing bookkeeping entirely.
 *   That trailing JMP is why this site needs a different shape from the other two below: simply
 *   skipping the CALL and falling through to it anyway would still take the early return, leaving
 *   the player's own fall-state machine (+0x358/+0x360) never reset and mode never returned to
 *   Stand - stuck "falling" while visibly standing on the ground. Suppressing this one has to
 *   resume at 0044f4ca instead, the SAME place the ORIGINAL "below the ceiling" branch already
 *   goes, so a suppressed death reads as an ordinary landing in every way, not merely a death that
 *   didn't happen.
 *
 * Both call sites are found the same way as the fall-damage one above, from FUN_0044F162's own
 * entry plus a fixed, confirmed offset - and, since 0x004500B0 has no earlier independent
 * confirmation anywhere in this project the way 0x00459ece did, the two sites are cross-checked
 * against EACH OTHER instead: both must resolve to the exact same address before either is
 * trusted, which is exactly the kind of second, independent measurement THING_DRAW_TO_SCALE_
 * CALL_OFFSET's own comment argues for and didn't have available to it. */
#define TIME_DEATH_CALL_OFFSET           0x1AFu   /* 0x0044F311 - 0x0044F162: the PUSH, not the CALL
                                                    * two bytes later - the block starts where the
                                                    * ten bytes this file actually detours starts */
#define DISTANCE_DEATH_CALL_OFFSET       0x359u   /* 0x0044F4BB - 0x0044F162, same reasoning */
#define DISTANCE_DEATH_SKIP_TARGET_OFFSET 0x368u  /* 0x0044F4CA - 0x0044F162: where the ORIGINAL
                                                    * "below the ceiling" branch already lands */

/* Ten bytes each - push imm8 (2) + call rel32 (5) + add esp,4 (3) - shared by the fall-damage call
 * above and both death calls below. Unlike every OTHER prologue size in this file, these are
 * allowed to include a CALL: none of the three hooks below ever replay these bytes through
 * detour_install()'s own relocated trampoline (build_trampoline() copies raw bytes with no
 * relative-address fixup, so a relocated CALL would resolve to the wrong target if it were ever
 * jumped into - it just never is here). Each original effect is reproduced by calling the real
 * function directly in C instead - hook_damage() for fall damage, which is also what lets it
 * compose correctly with Unlimited health rather than bypassing it, and the resolved 0x004500B0
 * pointer for the two death triggers. */
#define GROUND_CONTACT_CALL_PROLOGUE_SIZE 10u

/* A THIRD SIDE EFFECT OF THE SAME "SIGNIFICANT FALL" TRANSITION: THE CAMERA DETACHES AND STICKS.
 *
 * Field reported: with damage and death both suppressed, a high boosted jump still pitches the
 * camera down to look at the player from above partway through the fall, and it never lets go -
 * stays stuck looking down even after landing and moving away. This is retail's own dramatic-fall
 * camera, confirmed by decompiling every function named below directly rather than inferring from
 * the call site alone, and it fires from the EARLIEST of the three fall-consequence branches in
 * this function - the very moment accumulated distance first crosses _DAT_004a86f4, before the
 * 2-second timer above is even armed:
 *
 *   0044f1bd  CMP [+0x358],0                 ; only the first time a fall becomes "significant"
 *   0044f1dd  FLD [+0x360]; FCOMP [0x4a86f4]; JNZ 0044f261   ; below threshold - nothing below runs
 *   0044f1f6  MOV [+0x358],2                 ; arms the state the 2-second timer above counts down
 *   0044f225  PUSH 0xd
 *   0044f227  CALL 0x0041840a                ; DAT_005bb4e8=1, DAT_005bb4f0=0xd - LATCHES the
 *                                            ; camera's own per-tick type selector to "type 0xd"
 *                                            ; every frame from here on, confirmed by decompiling
 *                                            ; 0x0041840a directly: `DAT_005bb4e8=1; DAT_005bb4f0=
 *                                            ; param_1;` with no condition on either write.
 *   0044f22c  ADD ESP,0x4
 *   0044f22f  (ECX = pPlayer+0x118, the player's live position)
 *   0044f23b  PUSH ECX
 *   0044f23c  CALL 0x0044F891                ; freezes pPlayer+0x1b8/1bc/1c0 to the CURRENT
 *                                            ; position and sets pPlayer+0x1c4=1 - confirmed by
 *                                            ; decompiling 0x0044F891 directly: three unconditional
 *                                            ; field copies from its own three-float argument plus
 *                                            ; `pPlayer->0x1c4 = 1;`. Something elsewhere in the
 *                                            ; per-frame camera-target code (not itself detoured
 *                                            ; here) checks +0x1c4 and, while it is set, feeds THIS
 *                                            ; frozen point to the camera instead of the player's
 *                                            ; own live position - which is the "detaches and looks
 *                                            ; down at a fixed spot" half of what gets reported.
 *   0044f241  ADD ESP,0x4
 *
 * WHY IT NEVER LETS GO. The only function anywhere in the binary that clears DAT_005bb4e8 back to
 * 0 is 0x00418421, and none of ITS callers (the pause menu, a dialogue camera, two other UI/
 * cutscene sites) have anything to do with landing or the player's own fall state - confirmed via
 * get_xrefs_to on 0x00418421 directly. Nothing in this function's own landing/cleanup paths
 * (state 3's handling, the normal return-to-Stand block, or the fall-death handling above) ever
 * calls it or clears pPlayer+0x1c4 either. In vanilla play a fall big enough to reach this branch
 * overwhelmingly ends in the death this file already suppresses, so a landing that survives past
 * this point was apparently never a case retail's own camera code needed to recover from - exactly
 * the case jump boost now creates on purpose.
 *
 * get_xrefs_to on 0x0044F891 finds exactly ONE caller anywhere in the binary - this site - so
 * suppressing it here can never affect anything else. 0x0041840A has several OTHER callers (a
 * dialogue camera, cutscene-adjacent code) that are left completely untouched: this hooks the
 * CALL INSTRUCTION inside FUN_0044F162, not the function itself, so every other caller's own
 * behaviour is unaffected either way. Both call targets are verified against the LITERAL addresses
 * confirmed above by direct decompilation, the same rigor as the cross-checks elsewhere in this
 * section - there being no earlier independent resolution of either address in this project to
 * cross-check against instead, decompiling each one directly and confirming its own body is the
 * strongest evidence actually available here. */
#define CAMERA_LOCK_CALL_OFFSET   0xC3u   /* 0x0044F225 - 0x0044F162: push 0xd; call 0x0041840a */
#define CAMERA_FREEZE_CALL_OFFSET 0xD9u   /* 0x0044F23B - 0x0044F162: push ecx; call 0x0044F891 -
                                           * ONE byte of push, not two: this pushes a REGISTER
                                           * (0x51), not an immediate, so its own prologue is nine
                                           * bytes, not ten - verified separately from the shared
                                           * push-imm8 shape every other site in this section uses */
#define CAMERA_FREEZE_PROLOGUE_SIZE 9u
#define PLAYER_POSITION_OFFSET 0x118u   /* what 0x0044F891's own argument (ECX = pPlayer+0x118)
                                         * points at - the player's live position, three floats */

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
typedef void (__cdecl *mode_enter_fn_t)(void);
typedef void (__cdecl *death_trigger_fn_t)(int32_t cause);
typedef void (__cdecl *camera_type_fn_t)(int32_t type);
typedef void (__cdecl *camera_freeze_fn_t)(const float *position);

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
    detour_t                jump_entry_detour;
    detour_t                jedi_jump_entry_detour;
    detour_t                fall_damage_detour;
    detour_t                time_death_detour;
    detour_t                distance_death_detour;
    death_trigger_fn_t      death_trigger;   /* resolved 0x004500B0; NULL until both fall-death
                                              * sites cross-validate against each other */
    detour_t                camera_lock_detour;
    detour_t                camera_freeze_detour;
    camera_type_fn_t        camera_type_select;    /* resolved 0x0041840A */
    camera_freeze_fn_t      camera_freeze_target;   /* resolved 0x0044F891 */
    use_ammo_fn_t           ammo_original;
    damage_fn_t             damage_original;
    thing_draw_fn_t         thing_draw_original;
    camera_update_fn_t      camera_update_original;
    mode_enter_fn_t         jump_entry_original;
    mode_enter_fn_t         jedi_jump_entry_original;
    scale_matrix_fn_t       scale_matrix_compose;      /* the retail matrix-scale composer, called
                                                          * directly rather than detoured; NULL if
                                                          * its own site did not resolve */
    uintptr_t               camera_view_address;       /* address OF the camera object pointer;
                                                          * 0 = unresolved, free camera unavailable */
    uintptr_t               sim_pause_flag_address;     /* address of the sim-freeze cell; 0 =
                                                          * unresolved, free camera unavailable */
    float                   jump_boost_scale;           /* see JUMP_BOOST_SCALE_DEFAULT above */
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

/* Jump boost. Calling the original FIRST and unconditionally is what makes this a boost and not a
 * reimplementation: the jump still happens exactly as retail built it, guard check and all, and
 * only once it has already decided to jump and written its own velocity does this cheat touch
 * anything, scaling whatever value is now sitting at +0xB4 - the fallback constant or the
 * per-character table value, whichever path the original just took. See SIG_JUMP_ENTRY's own
 * comment for why this needs two hooks rather than one. */
static void __cdecl hook_jump_entry(void)
{
    own_state.jump_entry_original();

    if (own_state.cheats[CHEATS_OWN_JUMP_BOOST].on) {
        void *player_record = *(void **)(uintptr_t)PLAYER_RECORD_PTR_ADDR;

        if (player_record != NULL) {
            float *vertical_velocity =
                (float *)((char *)player_record + PLAYER_VERTICAL_VELOCITY_OFFSET);
            *vertical_velocity *= own_state.jump_boost_scale;
        }
    }
}

static void __cdecl hook_jedi_jump_entry(void)
{
    own_state.jedi_jump_entry_original();

    if (own_state.cheats[CHEATS_OWN_JUMP_BOOST].on) {
        void *player_record = *(void **)(uintptr_t)PLAYER_RECORD_PTR_ADDR;

        if (player_record != NULL) {
            float *vertical_velocity =
                (float *)((char *)player_record + PLAYER_VERTICAL_VELOCITY_OFFSET);
            *vertical_velocity *= own_state.jump_boost_scale;
        }
    }
}

/* Fall damage, suppressed only while jump boost is on - see SIG_PLAYER_GROUND_CONTACT's own
 * comment for the mechanism and why the call this replaces is never replayed through a relocated
 * trampoline. Calls hook_damage() - the C function above, not the engine's own site - rather than
 * own_state.damage_original directly: that is what lets Unlimited health still win if both are on
 * at once, the same composition every other pair of cheats in this file that could interact gets,
 * instead of this one quietly bypassing it. own_state.damage_original being non-NULL is verified
 * once, at install time (see install_fall_punishment_immunity), specifically so this never has to
 * check it on every single landing. */
static void __cdecl on_fall_damage(void)
{
    if (!own_state.cheats[CHEATS_OWN_JUMP_BOOST].on) {
        hook_damage(10);
    }
}

/* A naked trampoline, the same shape as hook_npc_damage above and for the same reason: the ten
 * bytes this replaces sit in the middle of FUN_0044F162's own body, not at a call site this file
 * can simply redirect, so this must be entered by JMP with the surrounding registers intact.
 * Unlike hook_npc_damage, there is no "replay the original instructions unchanged" path here at
 * all - on_fall_damage() above already reproduces the ten bytes' whole effect (apply exactly 10
 * damage, through hook_damage() so Unlimited health still composes correctly), so every path
 * through this stub resumes at the SAME place, fall_damage_continue, one past the ten bytes this
 * detour owns. */
static void *fall_damage_continue;   /* target + GROUND_CONTACT_CALL_PROLOGUE_SIZE, always resumed
                                       * here - see FALL_DAMAGE_CALL_OFFSET's own comment */

static void __declspec(naked) hook_fall_damage(void)
{
    __asm {
        pushad
        pushfd
        call on_fall_damage
        popfd
        popad
        jmp dword ptr [fall_damage_continue]
    }
}

/* Fall death, time-based site. Same shape as hook_fall_damage above, and for the same reason it
 * needs no special resume target of its own: the original call's own fallthrough (add esp,4) lands
 * on exactly the same instruction the "timer still running" branch already jumps to, so replaying
 * the death call or skipping it both resume in the same place. own_state.death_trigger is only
 * ever non-NULL once install_fall_punishment_immunity() has cross-checked it against the distance
 * site below, so this never risks calling through a half-resolved pointer. */
static void __cdecl on_time_death(void)
{
    if (!own_state.cheats[CHEATS_OWN_JUMP_BOOST].on && own_state.death_trigger != NULL) {
        own_state.death_trigger(2);
    }
}

static void *time_death_continue;

static void __declspec(naked) hook_time_death(void)
{
    __asm {
        pushad
        pushfd
        call on_time_death
        popfd
        popad
        jmp dword ptr [time_death_continue]
    }
}

/* Fall death, distance-based site - the one with the trailing unconditional early return, so
 * unlike the other two this needs TWO resume targets, chosen by what on_distance_death() decided,
 * the same two-target shape hook_npc_damage above already uses for its own skip/continue choice.
 * Replaying the call resumes at the original site's own fallthrough (the early-return jump, so a
 * real death still bails out of ground-contact processing exactly as retail does); suppressing it
 * resumes at DISTANCE_DEATH_SKIP_TARGET_OFFSET instead - the SAME place the original "below the
 * ceiling" branch already goes, so a suppressed death reads as an ordinary landing, not a death
 * that silently didn't happen. */
static bool  distance_death_skip;
static void *distance_death_apply_continue;
static void *distance_death_skip_continue;

static void __cdecl on_distance_death(void)
{
    distance_death_skip = own_state.cheats[CHEATS_OWN_JUMP_BOOST].on;
    if (!distance_death_skip && own_state.death_trigger != NULL) {
        own_state.death_trigger(1);
    }
}

static void __declspec(naked) hook_distance_death(void)
{
    __asm {
        pushad
        pushfd
        call on_distance_death
        popfd
        popad
        cmp byte ptr distance_death_skip, 0
        jnz distance_death_resume_skip
        jmp dword ptr [distance_death_apply_continue]
    distance_death_resume_skip:
        jmp dword ptr [distance_death_skip_continue]
    }
}

/* Camera lock, site one of two: the type-0xd latch. Same single-continue shape as hook_fall_damage
 * and hook_time_death above - the call's own fallthrough is exactly where the site's own next
 * instruction already sits (0044f22f), so replaying the call or skipping it both resume in the
 * same place. own_state.camera_type_select is only ever non-NULL once install_fall_punishment_
 * immunity() has verified it against the literal 0x0041840A - see CAMERA_LOCK_CALL_OFFSET's own
 * comment for why a literal is what's actually available here. */
static void __cdecl on_camera_lock(void)
{
    if (!own_state.cheats[CHEATS_OWN_JUMP_BOOST].on && own_state.camera_type_select != NULL) {
        own_state.camera_type_select(0xD);
    }
}

static void *camera_lock_continue;

static void __declspec(naked) hook_camera_lock(void)
{
    __asm {
        pushad
        pushfd
        call on_camera_lock
        popfd
        popad
        jmp dword ptr [camera_lock_continue]
    }
}

/* Camera lock, site two of two: freezing the camera's own target point. Needed alongside
 * camera_lock above, not instead of it: 0x0044F891 is the ONLY place pPlayer+0x1c4 ever gets set
 * (confirmed - get_xrefs_to on 0x0044F891 finds exactly this one caller in the whole binary), and
 * the per-frame camera-target code that checks +0x1c4 (not itself detoured by this project) reads
 * it independently of whatever camera TYPE is currently selected - so suppressing only the type
 * latch above would still leave the camera's TARGET pinned to this frozen point even though its
 * TYPE stayed normal. While jump boost is on, this call is skipped entirely, the same shape as
 * every other hook in this section: +0x1c4 simply stays whatever it already was - 0, since this
 * is its only writer - so the per-frame camera code keeps reading the player's own live position
 * exactly as it would on an ordinary fall that never reached this branch at all. While jump boost
 * is off, the call still happens exactly as retail wrote it, argument included. */
static void __cdecl on_camera_freeze(void)
{
    if (!own_state.cheats[CHEATS_OWN_JUMP_BOOST].on && own_state.camera_freeze_target != NULL) {
        void *player_record = *(void **)(uintptr_t)PLAYER_RECORD_PTR_ADDR;

        if (player_record != NULL) {
            own_state.camera_freeze_target(
                (const float *)((char *)player_record + PLAYER_POSITION_OFFSET));
        }
    }
}

static void *camera_freeze_continue;

static void __declspec(naked) hook_camera_freeze(void)
{
    __asm {
        pushad
        pushfd
        call on_camera_freeze
        popfd
        popad
        jmp dword ptr [camera_freeze_continue]
    }
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

/* Jump boost needs at least one of its two sites; either alone still helps whichever characters
 * route through that function (see SIG_JUMP_ENTRY's own comment), so this does not require both
 * the way install_freecam below does - a partial resolve here is still a real, working cheat for
 * part of the cast, not half of a feature that does nothing on its own. */
static void install_jump_boost(void)
{
    bool jump_ok, jedi_jump_ok;

    /* Set unconditionally, before either site is even attempted: the getter must answer something
     * sane the instant the panel can ask for it, not only after a resolve that might still fail. */
    own_state.jump_boost_scale = JUMP_BOOST_SCALE_DEFAULT;

    jump_ok = install_one(SIG_JUMP_ENTRY, NULL, sizeof SIG_JUMP_ENTRY,
                          (const void *)&hook_jump_entry, &own_state.jump_entry_detour,
                          JUMP_ENTRY_PROLOGUE_SIZE, "the Jump mode entry");
    if (jump_ok) {
        own_state.jump_entry_original = (mode_enter_fn_t)own_state.jump_entry_detour.original;
    }

    jedi_jump_ok = install_one(SIG_JEDI_JUMP_ENTRY, NULL, sizeof SIG_JEDI_JUMP_ENTRY,
                               (const void *)&hook_jedi_jump_entry,
                               &own_state.jedi_jump_entry_detour, JEDI_JUMP_ENTRY_PROLOGUE_SIZE,
                               "the Jedi Jump mode entry");
    if (jedi_jump_ok) {
        own_state.jedi_jump_entry_original =
            (mode_enter_fn_t)own_state.jedi_jump_entry_detour.original;
    }

    if (!jump_ok && !jedi_jump_ok) {
        return;
    }
    own_state.cheats[CHEATS_OWN_JUMP_BOOST].available = true;
    log_info("jump boost: %s%s%s covered",
             jump_ok ? "Jump" : "", (jump_ok && jedi_jump_ok) ? " and " : "",
             jedi_jump_ok ? "Jedi Jump" : "");
}

/* Shared by all three sites below: reads the "push imm8; call rel32" pair at fn_entry+call_offset,
 * checks the pushed byte matches what that exact call site is expected to push (the "cause" or
 * damage amount, since more than one push/call pair exists in this function and a wrong offset
 * landing on some OTHER one must not be trusted just because it happens to decode as valid), and
 * resolves the call's own target. Never trusted alone - every caller cross-checks the resolved
 * target against something independent before using it, per this section's own header comment. */
static bool read_ground_contact_call(uintptr_t fn_entry, uint32_t call_offset,
                                     uint8_t expected_operand, uintptr_t *out_target)
{
    uintptr_t call_site = fn_entry + call_offset;
    uint8_t   opcode = 0;
    uint8_t   operand = 0;
    uint8_t   call_opcode = 0;
    uint32_t  rel32 = 0;

    if (!memory_read_u8(call_site, &opcode) || opcode != 0x6A ||
        !memory_read_u8(call_site + 1, &operand) || operand != expected_operand ||
        !memory_read_u8(call_site + 2, &call_opcode) || call_opcode != 0xE8 ||
        !memory_read_u32(call_site + 3, &rel32)) {
        return false;
    }
    *out_target = call_site + 7u + rel32;
    return true;
}

/* Not gated on jump boost itself having resolved - the mode-entry sites in install_jump_boost()
 * and this one are entirely independent parts of the engine, and a player whose jump boost never
 * armed still gets a bogus "on" flag if this checked own_state.cheats[CHEATS_OWN_JUMP_BOOST].
 * available instead of the one thing fall damage actually needs: hook_damage() reachable and safe
 * to call, which needs own_state.damage_original resolved (see install_one's own call for
 * SIG_DAMAGE, earlier in cheats_openphantom_install() and therefore already attempted by the time
 * this runs). No row of its own either way - see SIG_PLAYER_GROUND_CONTACT's own comment for why
 * all three sites here ride on CHEATS_OWN_JUMP_BOOST's existing toggle rather than being cheats of
 * their own.
 *
 * The three sites are independent of EACH OTHER too, fall damage most of all - a player might have
 * Unlimited health off (so hook_damage() can't be called safely, fall damage stays live) while
 * still getting fall-death immunity, which needs nothing from hook_damage() at all. Each of the
 * three is attempted and logged on its own merits; none is required for the others to proceed. */
static void install_fall_punishment_immunity(void)
{
    uintptr_t fn_entry;
    uintptr_t call_site;
    uintptr_t call_target;
    uintptr_t time_death_target = 0;
    uintptr_t distance_death_target = 0;

    fn_entry = signature_find_unique(SIG_PLAYER_GROUND_CONTACT, NULL,
                                     sizeof SIG_PLAYER_GROUND_CONTACT);
    if (fn_entry == 0) {
        log_warning("the ground-contact function did not resolve, jump boost will not suppress "
                    "fall damage or fall death");
        return;
    }

    /* Fall damage: needs hook_damage() reachable, and its own target cross-checked against
     * SIG_DAMAGE's own already-resolved address (see this section's own header comment). */
    if (own_state.damage_original == NULL) {
        log_warning("fall-damage immunity needs the damage-apply site resolved first, and it did "
                    "not - jump boost will not suppress fall damage until that resolves");
    } else if (!read_ground_contact_call(fn_entry, FALL_DAMAGE_CALL_OFFSET, 0x0A, &call_target) ||
              call_target != own_state.damage_detour.target) {
        log_warning("the fall-damage call site did not check out, jump boost will not suppress "
                    "fall damage");
    } else {
        call_site = fn_entry + FALL_DAMAGE_CALL_OFFSET;
        if (!detour_install(&own_state.fall_damage_detour, call_site,
                            (const void *)&hook_fall_damage, GROUND_CONTACT_CALL_PROLOGUE_SIZE)) {
            log_warning("the fall-damage call site at %08X could not be detoured, jump boost will "
                        "not suppress fall damage", (unsigned)call_site);
        } else {
            fall_damage_continue =
                (void *)(call_site + GROUND_CONTACT_CALL_PROLOGUE_SIZE);
            log_info("fall damage will be suppressed while jump boost is on, hooked at %08X",
                     (unsigned)call_site);
        }
    }

    /* Fall death: the two sites cross-checked against EACH OTHER, since unlike fall damage there
     * is no earlier independent resolution of 0x004500B0 to check against - see this section's own
     * header comment. Both must agree before either is trusted, and the address must be executable
     * before own_state.death_trigger is ever set to it. */
    if (!read_ground_contact_call(fn_entry, TIME_DEATH_CALL_OFFSET, 0x02, &time_death_target) ||
        !read_ground_contact_call(fn_entry, DISTANCE_DEATH_CALL_OFFSET, 0x01,
                                  &distance_death_target) ||
        time_death_target != distance_death_target ||
        !memory_is_executable_range(time_death_target, 1)) {
        log_warning("the fall-death call sites did not check out or disagree with each other, "
                    "jump boost will not suppress fall death");
        return;
    }
    own_state.death_trigger = (death_trigger_fn_t)time_death_target;

    call_site = fn_entry + TIME_DEATH_CALL_OFFSET;
    if (!detour_install(&own_state.time_death_detour, call_site, (const void *)&hook_time_death,
                        GROUND_CONTACT_CALL_PROLOGUE_SIZE)) {
        log_warning("the time-based fall-death call site at %08X could not be detoured",
                    (unsigned)call_site);
    } else {
        time_death_continue = (void *)(call_site + GROUND_CONTACT_CALL_PROLOGUE_SIZE);
        log_info("the airborne-too-long death timer will be suppressed while jump boost is on, "
                 "hooked at %08X", (unsigned)call_site);
    }

    call_site = fn_entry + DISTANCE_DEATH_CALL_OFFSET;
    if (!detour_install(&own_state.distance_death_detour, call_site,
                        (const void *)&hook_distance_death, GROUND_CONTACT_CALL_PROLOGUE_SIZE)) {
        log_warning("the fall-distance death call site at %08X could not be detoured",
                    (unsigned)call_site);
    } else {
        distance_death_apply_continue =
            (void *)(call_site + GROUND_CONTACT_CALL_PROLOGUE_SIZE);
        distance_death_skip_continue = (void *)(fn_entry + DISTANCE_DEATH_SKIP_TARGET_OFFSET);
        log_info("the fall-distance death threshold will be suppressed while jump boost is on, "
                 "hooked at %08X", (unsigned)call_site);
    }

    /* Camera lock: the two sites verified against the literal addresses confirmed by direct
     * decompilation above (see CAMERA_LOCK_CALL_OFFSET's own comment for why a literal, not
     * another cross-check, is what's actually available here), each independent of the fall-
     * damage and fall-death sites above - a player could have either of those fail to resolve and
     * still get a camera that behaves normally through a boosted fall. */
    if (!read_ground_contact_call(fn_entry, CAMERA_LOCK_CALL_OFFSET, 0x0D, &call_target) ||
        call_target != 0x0041840Au) {
        log_warning("the camera-lock call site did not check out, jump boost will not stop the "
                    "camera detaching on a big fall");
    } else {
        call_site = fn_entry + CAMERA_LOCK_CALL_OFFSET;
        if (!detour_install(&own_state.camera_lock_detour, call_site,
                            (const void *)&hook_camera_lock, GROUND_CONTACT_CALL_PROLOGUE_SIZE)) {
            log_warning("the camera-lock call site at %08X could not be detoured", (unsigned)call_site);
        } else {
            own_state.camera_type_select = (camera_type_fn_t)call_target;
            camera_lock_continue = (void *)(call_site + GROUND_CONTACT_CALL_PROLOGUE_SIZE);
            log_info("the fall camera's type latch will be suppressed while jump boost is on, "
                     "hooked at %08X", (unsigned)call_site);
        }
    }

    /* Nine bytes, not ten (see CAMERA_FREEZE_CALL_OFFSET's own comment) - this one call is not
     * verified via read_ground_contact_call(), whose shared shape assumes a two-byte push-imm8
     * header every other site in this file actually has. */
    {
        uintptr_t freeze_call_site = fn_entry + CAMERA_FREEZE_CALL_OFFSET;
        uint8_t   push_opcode = 0;
        uint8_t   call_opcode = 0;
        uint32_t  rel32 = 0;
        uintptr_t freeze_target = 0;

        if (!memory_read_u8(freeze_call_site, &push_opcode) || push_opcode != 0x51 ||
            !memory_read_u8(freeze_call_site + 1, &call_opcode) || call_opcode != 0xE8 ||
            !memory_read_u32(freeze_call_site + 2, &rel32)) {
            log_warning("the camera-freeze call site did not check out, jump boost will not stop "
                        "the camera locking onto a fixed point on a big fall");
        } else {
            freeze_target = freeze_call_site + 6u + rel32;
            if (freeze_target != 0x0044F891u) {
                log_warning("the camera-freeze call site did not check out, jump boost will not "
                            "stop the camera locking onto a fixed point on a big fall");
            } else if (!detour_install(&own_state.camera_freeze_detour, freeze_call_site,
                                       (const void *)&hook_camera_freeze,
                                       CAMERA_FREEZE_PROLOGUE_SIZE)) {
                log_warning("the camera-freeze call site at %08X could not be detoured",
                            (unsigned)freeze_call_site);
            } else {
                own_state.camera_freeze_target = (camera_freeze_fn_t)freeze_target;
                camera_freeze_continue =
                    (void *)(freeze_call_site + CAMERA_FREEZE_PROLOGUE_SIZE);
                log_info("the fall camera's frozen target will be suppressed while jump boost is "
                         "on, hooked at %08X", (unsigned)freeze_call_site);
            }
        }
    }
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
    own_state.cheats[CHEATS_OWN_JUMP_BOOST].name = "Jump boost";
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
    install_jump_boost();
    install_fall_punishment_immunity();

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
           own_state.cheats[CHEATS_OWN_JUMP_BOOST].available ||
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

float cheats_openphantom_jump_boost_scale(void)
{
    return own_state.jump_boost_scale;
}

void cheats_openphantom_jump_boost_set_scale(float scale)
{
    if (scale < JUMP_BOOST_SCALE_MIN) {
        scale = JUMP_BOOST_SCALE_MIN;
    }
    if (scale > JUMP_BOOST_SCALE_MAX) {
        scale = JUMP_BOOST_SCALE_MAX;
    }
    own_state.jump_boost_scale = scale;
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

/* --- "Skip to next level": the same signal a level's own exit trigger sends -------------------- *
 *
 * main_game_movie_sequencer_loop (0x0043EC42) is the campaign driver: it loads each level by name
 * off its own per-level table, keyed by an index at DAT_0088136c, then busy-polls
 * `while (DAT_00881368 == 2) sys_frame();` for as long as that level is actually running. When that
 * loop exits it reads DAT_00881368 again and, if it is 3, treats it as "level complete": broadcasts
 * task 6, increments its own level index, and loads the next entry off its own table - exactly the
 * housekeeping a real level exit needs, done by the driver itself.
 *
 * DAT_00881368 is written to 3 from exactly one place in the whole binary: FUN_00429880, case
 * (param_2 == 1). FUN_00429880 has exactly one caller anywhere: the level script interpreter
 * (FUN_00433d0b, dialogue_anim_fix.c's own "opcode 0x202" function), script opcode 0x606,
 * sub-command 1 - `FUN_00429880(param_1, *local_c, local_c[1], local_c[2])` when `*local_c == 1`.
 * That is almost certainly the literal command a level's own exit trigger/volume issues.
 *
 * This writes DAT_00881368 = 3 directly - the same value that one script command produces - rather
 * than calling FUN_00429880 (which needs a live script-context pointer this panel does not have) or
 * campaign_loadLevel (which needs the NEXT level's own path string, something
 * main_game_movie_sequencer_loop already works out for itself from its own table). The cell itself
 * is cheats_original_actions.c's own OP_CREDITS_VAR, exposed read-only from there rather than
 * re-resolved here: it is genuinely the same single global cell "gurshick" already found, not a
 * second one that happens to share a value.
 *
 * FIELD-UNTESTED. "gurshick" (the SAME cell, value 1) is confirmed to misbehave when triggered from
 * this panel rather than the retail console's own blocking loop - see
 * cheats_original_actions.c's own resolve_credits() comment. Value 3 takes a structurally different
 * path with no credits-sequence startup involved, so that specific failure mode should not apply,
 * but that is reasoning, not a field result. */
bool cheats_openphantom_end_level_is_available(void)
{
    return cheats_original_actions_level_status_cell() != NULL;
}

bool cheats_openphantom_end_level_invoke(void)
{
    volatile int32_t *cell = cheats_original_actions_level_status_cell();

    if (cell == NULL) {
        return false;
    }
    *cell = 3;
    return true;
}
