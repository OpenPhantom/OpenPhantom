/* cheats_jump_boost.c: jump boost, and the five consequences of falling that it suppresses.
 *
 * SIZE NOTE. Over 600 lines on purpose. This is one cheat plus the five punishments the engine
 * applies to whatever comes down again: fall damage, the airborne death timer, the fall distance
 * threshold, the camera type latch and the frozen camera target. Each needs its own site, its own
 * naked detour and the disassembly justifying it, and none means anything without the others.
 * Splitting them further would scatter the evidence for one behaviour, which is what the size rule
 * exists to prevent rather than to cause.
 *
 * Split out of cheats_openphantom.c; nothing changed in the move. */
#include "cheats_openphantom.h"
#include "floor_probe.h"
#include "cheats_internal.h"

#include "sim_pause.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <windows.h>

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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
/* PLAYER_POSITION_OFFSET moved to cheats_internal.h: free camera's exit teleport reads the same
 * field, and one number in one place cannot drift. The evidence line moved with it. */

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
 * THE PATTERN FOR THIS GATE IS NOT REPEATED HERE. sim_pause.c, in this same DLL, resolves this
 * exact site, reads the same operand and owns the cell. This feature asks it to hold the pause
 * rather than resolving the address a second time and writing the cell itself. Two resolvers of
 * one address is how the two of them came to fight; see sim_pause.h for the sequence that left
 * the game frozen with nothing holding it. */

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

/* The fall grace, and why it lives here rather than with the free camera that grants it.
 *
 * The five sites below are the only things in this project that suppress a consequence of falling,
 * and they were all written for jump boost. The free camera's teleport creates the same situation
 * from a different direction: the player arrives at whatever altitude the camera was flown to, and
 * a fall nobody chose to take should not be a death. Rather than a second set of hooks, the gate
 * becomes a question with two answers, and the sites go on asking one question.
 *
 * It is a deadline compared with the counter rather than a tick, so it needs nothing to run every
 * frame and cannot drift if something stops running.
 *
 * It ends on the first landing. on_fall_damage is only reached BY landing, so its own arm clears
 * the grace: the fall is over, and a player who then walks off a ledge should be treated normally.
 * The cap exists for the fall that never lands, over a spot with nothing modelled beneath it,
 * where suppressing the airborne timer forever would be worse than the death it prevents. */
#define FALL_GRACE_SECONDS 10.0f

static LONGLONG fall_grace_until;      /* performance-counter ticks; 0 = no grace */

void cheats_openphantom_grant_fall_grace(void)
{
    LARGE_INTEGER frequency;
    LARGE_INTEGER now;

    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0 ||
        !QueryPerformanceCounter(&now)) {
        fall_grace_until = 0;
        return;
    }
    fall_grace_until = now.QuadPart + (LONGLONG)((double)frequency.QuadPart *
                                                 (double)FALL_GRACE_SECONDS);
}

static bool fall_grace_active(void)
{
    LARGE_INTEGER now;

    if (fall_grace_until == 0) {
        return false;
    }
    if (!QueryPerformanceCounter(&now) || now.QuadPart >= fall_grace_until) {
        fall_grace_until = 0;
        return false;
    }
    return true;
}

/* No floor, no suppression: a fall with nothing beneath it gets the retail engine, whole.
 *
 * Every suppression in this file exists to stop a big jump being punished for coming down hard.
 * None of them was ever meant for a fall off the edge of the world, and applying them there is
 * what turned that fall from a death into an unrecoverable state:
 *
 *   Confirmed in the field. Fall off a ledge with jump boost on, or after a free camera teleport,
 *   and the player keeps going far below the world. The audio goes very loud, the death screen
 *   arrives long after it should have, and loading from that death screen does not work: the game
 *   comes back still falling and still wrong, and only loading from the main menu clears it. All
 *   of it is one cause, which is that the fall was never allowed to end.
 *
 * The test is whether there is anywhere to land. It is asked once, when the fall begins, and it
 * gates all five suppressions rather than only the death timer. A boosted jump has ground beneath
 * it and keeps its full immunity however long it takes to come down. A fall with nothing beneath
 * it is handed back to retail entirely: the damage, both deaths and both camera latches behave as
 * they do with no cheat installed, because that is the behaviour known to end properly.
 *
 * A time limit was tried first and was the wrong shape: it cannot tell a high jump from a void
 * fall, so any value that spares the jump also lets the void fall run long enough to break.
 *
 * bapmap_probeFloor is the engine's own answer to that question. It seeds dist to 3.4e38 and only
 * replaces it when a floor polygon is actually found under the probe's own x/y, and the one range
 * limit in its acceptance rule applies to floors above the feet, not below, so it reaches as far
 * down as the level goes. Read out of the decompiled body rather than assumed.
 *
 * The question is asked at the camera latch below, which retail fires at the earliest of the three
 * fall branches, the very moment the fall becomes significant and before the 2 second timer is
 * armed. That is the honest "this fall has begun" signal and it costs no hook of its own. */
static bool fall_has_landing = true;   /* until a fall says otherwise */

/* True when there is a floor somewhere under the player right now. Answers TRUE when the probe
 * cannot be resolved at all, so a build that cannot ask the question keeps the immunity it had
 * rather than quietly starting to kill people. */
/* Both pieces of fall state, cleared together.
 *
 * fall_has_landing is answered when a fall BEGINS and set back to true only by landing, which is
 * right inside one level and wrong across two. Skipping a level while falling over something with
 * no floor under it carried "there is nothing to land on" into the next level, where it turns off
 * all five suppressions, and the engine's own fall-distance death then fired on arrival. Reported
 * from a test build: jump boost on, level skip, dead on the next level.
 *
 * The fall belonged to a world that no longer exists, so the honest answer is not to carry it. This
 * does not touch whether jump boost is ON: a player who asked for it keeps it, and what they get on
 * the new level is the immunity it is supposed to give. */
void cheats_openphantom_reset_fall_state(void)
{
    fall_has_landing = true;
    fall_grace_until = 0;
}

/* Jump boost is switched OFF across a level change, and back on afterwards.
 *
 * Reported from a test build: jump boost on, level skip, dead on arrival in the next level. The
 * fall state carried across the transition was one cause and is reset above, and this is the other
 * half, asked for directly: whatever else a level change does to a boosted jump, it does it with
 * the boost switched off.
 *
 * It is a suspend rather than a plain switch off because the player asked for the cheat and should
 * not have to ask again after every level. The flag is what tells the two apart: resume only puts
 * back what suspend took, so somebody who switches it off themselves mid transition stays off. */
static bool jump_boost_suspended;

void cheats_openphantom_suspend_jump_boost(void)
{
    if (jump_boost_suspended || !own_state.cheats[CHEATS_OWN_JUMP_BOOST].on) {
        return;
    }
    own_state.cheats[CHEATS_OWN_JUMP_BOOST].on = false;
    jump_boost_suspended = true;
}

void cheats_openphantom_resume_jump_boost(void)
{
    if (!jump_boost_suspended) {
        return;
    }
    jump_boost_suspended = false;
    own_state.cheats[CHEATS_OWN_JUMP_BOOST].on = true;
}


static bool floor_exists_under_player(void)
{
    void *player_record = *(void **)(uintptr_t)PLAYER_RECORD_PTR_ADDR;

    if (player_record == NULL) {
        return true;
    }
    return floor_probe_below((const float *)((char *)player_record + PLAYER_POSITION_OFFSET),
                             NULL) != FLOOR_PROBE_NONE;
}

/* The one question the five sites ask, and the floor test lives HERE so that every one of them
 * inherits it. No site has to remember to add it, and a suppression added later gets it free. */
static bool fall_consequences_suppressed(void)
{
    if (!fall_has_landing) {
        return false;          /* nothing to land on: retail gets its own behaviour back, whole */
    }
    return own_state.cheats[CHEATS_OWN_JUMP_BOOST].on || fall_grace_active();
}

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
    /* Asked BEFORE the state below is cleared, because clearing it would change the answer. */
    const bool suppressed = fall_consequences_suppressed();

    /* Reached only BY landing, so whatever this fall was, it is over here: a granted grace has
     * done its job, and the next fall gets to ask the floor question fresh. Both are cleared even
     * when the fall was NOT suppressed, so a fall ruled to have no landing takes its damage and
     * still leaves the state clean behind it. Jump boost's own suppression is unaffected: it is a
     * cheat that stays on until it is switched off. */
    fall_grace_until = 0;
    fall_has_landing = true;

    if (!suppressed) {
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
        sub    esp, 112
        fnsave [esp]
        call on_fall_damage
        frstor [esp]
        add    esp, 112
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
    if (!fall_consequences_suppressed() && own_state.death_trigger != NULL) {
        own_state.death_trigger(2);
    }
}

static void *time_death_continue;

static void __declspec(naked) hook_time_death(void)
{
    __asm {
        pushad
        pushfd
        sub    esp, 112
        fnsave [esp]
        call on_time_death
        frstor [esp]
        add    esp, 112
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
    distance_death_skip = fall_consequences_suppressed();
    if (!distance_death_skip && own_state.death_trigger != NULL) {
        own_state.death_trigger(1);
    }
}

static void __declspec(naked) hook_distance_death(void)
{
    __asm {
        pushad
        pushfd
        sub    esp, 112
        fnsave [esp]
        call on_distance_death
        frstor [esp]
        add    esp, 112
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
    /* Retail fires this at the moment a fall first becomes significant, which makes it the place
     * to ask whether this fall has anywhere to end. Asked whether or not the camera latch itself
     * is suppressed: the question is about the FALL, not about what this cheat does to the
     * camera, and asked ONCE per fall rather than every frame because the answer cannot change while
     * falling straight down and a probe is not free. */
    fall_has_landing = floor_exists_under_player();

    if (!fall_consequences_suppressed() && own_state.camera_type_select != NULL) {
        own_state.camera_type_select(0xD);
    }
}

static void *camera_lock_continue;

static void __declspec(naked) hook_camera_lock(void)
{
    __asm {
        pushad
        pushfd
        sub    esp, 112
        fnsave [esp]
        call on_camera_lock
        frstor [esp]
        add    esp, 112
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
    if (!fall_consequences_suppressed() && own_state.camera_freeze_target != NULL) {
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
        sub    esp, 112
        fnsave [esp]
        call on_camera_freeze
        frstor [esp]
        add    esp, 112
        popfd
        popad
        jmp dword ptr [camera_freeze_continue]
    }
}

/* GetAsyncKeyState reads the physical key regardless of which window has it, so without this the
 * player would drift while the game sits alt-tabbed in the background and something else entirely
 * is holding E or Q. Compares the foreground window's owning process to this one instead of
 * tracking a HWND of our own, which needs nothing set up anywhere else in this DLL. */

/* Jump boost needs at least one of its two sites; either alone still helps whichever characters
 * route through that function (see SIG_JUMP_ENTRY's own comment), so this does not require both
 * the way install_freecam below does - a partial resolve here is still a real, working cheat for
 * part of the cast, not half of a feature that does nothing on its own. */
void install_jump_boost(void)
{
    bool jump_ok, jedi_jump_ok;

    /* Set unconditionally, before either site is even attempted: the getter must answer something
     * sane the instant the panel can ask for it, not only after a resolve that might still fail. */
    own_state.jump_boost_scale = JUMP_BOOST_SCALE_DEFAULT;

    jump_ok = cheats_install_one(SIG_JUMP_ENTRY, NULL, sizeof SIG_JUMP_ENTRY,
                          (const void *)&hook_jump_entry, &own_state.jump_entry_detour,
                          JUMP_ENTRY_PROLOGUE_SIZE, "the Jump mode entry");
    if (jump_ok) {
        own_state.jump_entry_original = (mode_enter_fn_t)own_state.jump_entry_detour.original;
    }

    jedi_jump_ok = cheats_install_one(SIG_JEDI_JUMP_ENTRY, NULL, sizeof SIG_JEDI_JUMP_ENTRY,
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
void install_fall_punishment_immunity(void)
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
