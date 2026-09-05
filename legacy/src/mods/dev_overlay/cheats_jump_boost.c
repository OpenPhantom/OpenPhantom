/* cheats_jump_boost.c: the jump boost cheat itself, and the two mode-entry sites it scales.
 *
 * Owns one thing: the multiplier applied to the vertical velocity the engine's own jump-entry
 * code writes, at both of the sites that write it, plus the suspend and resume a level change
 * needs. Everything the engine then does to whatever comes down again lives in
 * cheats_fall_consequences.c.
 *
 * SEAM. This file was over the hard limit. The cut taken is the launch against the landing: the
 * two mode-entry detours that make a jump higher stay here, and the five fall-consequence sites,
 * the fall grace and the floor test that gates them moved out whole. The size note this file used
 * to carry argued against splitting those five from EACH OTHER, and that argument is kept: they
 * moved together, into one file, with their evidence. What the note did not cover is the boundary
 * between the cheat and its consequences, which is a real change of subject. Nothing here reads
 * the fall state and nothing there reads the scale; the only thing crossing is the cheat's own on
 * flag, which lives in the state record every cheat file already writes, so no static had to be
 * duplicated or wrapped in an accessor to make the cut.
 *
 * Split out of cheats_openphantom.c; nothing changed in the move. */
#include "cheats_openphantom.h"
#include "cheats_internal.h"

#include "common/detour.h"
#include "common/logging.h"

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
 * Both functions open identically for the first twenty bytes: a guard that skips the whole jump if
 * some player-record float at +0x168 fails a threshold check against a shared constant at
 * 0x004A86A4, which is exactly the shape this file's own SIG_USE_AMMO/SIG_DAMAGE comment already
 * warns about: a pattern that stopped at the shared prefix would match either function, or others
 * like it for the remaining mode-entry functions (Sabre Attack, Panaka Attack, Push Block) this
 * feature has no reason to touch. Both patterns below therefore reach all the way to the
 * `mov [ecx+0x60],<that mode's own descriptor address>` instruction, the exact table[6]/table[7]
 * values above, embedded as the pattern's own trailing bytes, which is unique to each function by
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
 * written at 0x0044ed19). Either way the result lands in the SAME field, [ecx+0xb4], right after
 * the already-confirmed +0xB0 PLAYER_CURRENT_SPEED. That shared destination, not either individual
 * source, is what this feature hooks around: reading it back right after calling the original
 * covers both paths without needing to know which one fired.
 *
 * 0x0044EDF6 (Jedi Jump) opens byte-identical for the same twenty bytes, proven by disassembling it
 * independently rather than assumed from the first function's shape; the only differences before
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
                                        * mov eax,[0x004b5220], identical in both functions, but the
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
/* Velocity, not height. Jump height scales with velocity SQUARED under the engine's own linear
 * gravity decay, so 1.3 is roughly a 69% higher jump, not 30%. A first guess for "noticeably
 * higher, not silly" the same way TINY_PLAYER_SCALE above is; no retail precedent either direction.
 * Runtime-adjustable rather than fixed; the dev panel's own value row (see overlay_model.c) reads
 * and writes this through the getter/setter below, so this is only ever where a fresh install
 * starts, not the whole of what the cheat can be. Clamped on every write into a range wide enough
 * to be useful in both directions (a player weaker than retail is exactly as legitimate an ask as
 * one much stronger) but short of anything that turns a launch into a projectile the level's own
 * collision was never built to catch at the far end, or a no-op at the near one. */

/* Free camera, site one of two: the simulation pause flag.
 *
 * Every fly-mode attempt above fought the player's own state machine one function at a time
 * because the player kept simulating. FUN_0043e9f2, the per-frame pump, gates the entire
 * fixed-timestep substep driver on two cells, confirmed byte-for-byte against the running image:
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
 * entirely, so nothing registered on it runs at all, not just the player, the whole world. That
 * is confirmed as the SAME cell the retail ESC pause menu sets
 * (gameplay_open_pause_menu at 0x0043FAB5 writes it before its own blocking menu loop and clears it
 * after), so this is not a guessed side door, it is the mechanism the game already uses to pause
 * itself. This feature only ever writes the flag directly; it does not call the two
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
 * updateCam's follow blend: `mov eax,[gView]; fld [eax+0x38]`, the previous camera yaw, read
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
 * 0x004187ce even begins, and camera_sites.c only ever traces the follow-state arm, so this is new.
 * The source, traced back further, is SetCamTarget (FUN_004184cc): the player's own per-tick
 * dispatch (FUN_00447d38 case 3) is one of its five call sites, feeding it the player's own
 * position. That is the actual coupling this feature breaks, not the state field, which changing
 * alone does nothing, since every state still re-derives a TARGET offset from the same anchor
 * every call. Freezing the simulation (the site above) stops SetCamTarget from ever being called
 * again, which stops the anchor's own feed cold; nothing then contends with a write made AFTER
 * calling the original updateCam, since this file's write runs strictly after the whole original
 * function, including its own second, later write to yaw alone at 0x00418fa1, part of the
 * follow-blend arm, has already finished. Roll (+0x3c) is left alone; a free camera has no use
 * for it and neither does anything reading euler.x/euler.y elsewhere. */

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

/* Jump boost. Calling the original FIRST and unconditionally is what makes this a boost and not a
 * reimplementation: the jump still happens exactly as retail built it, guard check and all, and
 * only once it has already decided to jump and written its own velocity does this cheat touch
 * anything, scaling whatever value is now sitting at +0xB4, either the fallback constant or the
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

/* Jump boost needs at least one of its two sites; either alone still helps whichever characters
 * route through that function (see SIG_JUMP_ENTRY's own comment), so this does not require both
 * the way install_freecam in cheats_free_camera.c does; a partial resolve here is still a real,
 * working cheat for part of the cast, not half of a feature that does nothing on its own. */
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
