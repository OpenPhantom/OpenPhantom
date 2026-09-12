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
#include "common/memory.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

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
 * 0x004A86A4, the shape this file's own SIG_USE_AMMO/SIG_DAMAGE comment already warns about: a
 * pattern that stopped at the shared prefix would match either function, or others
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
    0xA1, 0x00, 0x00, 0x00, 0x00,                      /* mov eax,[the player record]           */
    0xD9, 0x80, 0x68, 0x01, 0x00, 0x00,                /* fld dword ptr [eax+0x168]             */
    0xD8, 0x1D, 0xA4, 0x86, 0x4A, 0x00,                /* fcomp dword ptr [0x004a86a4]          */
    0xDF, 0xE0,                                        /* fnstsw ax                             */
    0xF6, 0xC4, 0x41,                                  /* test ah,0x41                          */
    0x75, 0x05,                                        /* jnz +5                                */
    0xE9, 0xE0, 0x00, 0x00, 0x00,                      /* jmp 0x0044ed80                        */
    0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00,                /* mov ecx,[the player record]           */
    0xC7, 0x41, 0x60, 0x38, 0x53, 0x4B, 0x00           /* mov [ecx+0x60],0x004b5338 -> Jump      */
};
#define JUMP_ENTRY_PROLOGUE_SIZE 8u   /* first boundary at/past five bytes: push ebp; mov ebp,esp;
                                        * mov eax,[the player record], identical in both
                                        * functions, but the SIGNATURE above reaches nineteen bytes
                                        * past this to stay unique; only these first eight are ever
                                        * relocated */

static const uint8_t SIG_JEDI_JUMP_ENTRY[] = {
    0x55,                                              /* push ebp                              */
    0x8B, 0xEC,                                        /* mov ebp,esp                           */
    0xA1, 0x00, 0x00, 0x00, 0x00,                      /* mov eax,[the player record]           */
    0xD9, 0x80, 0x68, 0x01, 0x00, 0x00,                /* fld dword ptr [eax+0x168]             */
    0xD8, 0x1D, 0xA4, 0x86, 0x4A, 0x00,                /* fcomp dword ptr [0x004a86a4]          */
    0xDF, 0xE0,                                        /* fnstsw ax                             */
    0xF6, 0xC4, 0x41,                                  /* test ah,0x41                          */
    0x75, 0x05,                                        /* jnz +5                                */
    0xE9, 0x10, 0x01, 0x00, 0x00,                      /* jmp 0x0044ef26                        */
    0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00,                /* mov ecx,[the player record]           */
    0xC7, 0x41, 0x60, 0x68, 0x53, 0x4B, 0x00           /* mov [ecx+0x60],0x004b5368 -> JediJump  */
};
#define JEDI_JUMP_ENTRY_PROLOGUE_SIZE 8u   /* same reasoning as JUMP_ENTRY_PROLOGUE_SIZE above */

/* The two loads of the player record are masked, one mask for both patterns, and each site is
 * then required to load the player from the cell player_slot found. The pattern still names the
 * function by its guard, its jump and the descriptor it stores; the mask is what turns the two
 * operands from a copy of an address into a check against one. */
static const uint8_t MSK_JUMP_ENTRY[] = {
    0xFF,
    0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof MSK_JUMP_ENTRY == sizeof SIG_JUMP_ENTRY &&
               sizeof MSK_JUMP_ENTRY == sizeof SIG_JEDI_JUMP_ENTRY,
               "the jump entry patterns and their mask are different lengths");
#define JUMP_ENTRY_PLAYER_OPERAND_FIRST  4u
#define JUMP_ENTRY_PLAYER_OPERAND_SECOND 34u

/* The descriptor each pattern ends on, read out of the pattern so the two stay one fact. */
#define ENTRY_DESCRIPTOR_OFFSET 41u        /* the imm32 of `mov [ecx+0x60],<descriptor>` */
#define PLAYER_MODE_OFFSET      0x60u
static uint32_t pattern_descriptor(const uint8_t *pattern)
{
    uint32_t value;

    memcpy(&value, pattern + ENTRY_DESCRIPTOR_OFFSET, sizeof value);
    return value;
}

/* The original entered its mode, so it wrote the take-off velocity this cheat scales. On the
 * guard exit at +0x1B it writes nothing and leaves the mode alone, and scaling then multiplied
 * whatever +0xB4 last held. The descriptor the pattern ends on is the value the success path
 * stores at +0x60, so the mode pointer reading it is the test. */
static void scale_take_off(uint32_t descriptor)
{
    void *player_record = player_slot_current();

    if (player_record != NULL &&
        *(const uint32_t *)((const char *)player_record + PLAYER_MODE_OFFSET) == descriptor) {
        float *vertical_velocity =
            (float *)((char *)player_record + PLAYER_VERTICAL_VELOCITY_OFFSET);
        *vertical_velocity *= own_state.jump_boost_scale;
    }
}

/* JUMP_BOOST_SCALE_DEFAULT, 1.3 in cheats_internal.h, is what install_jump_boost() below seeds
 * own_state.jump_boost_scale with. Velocity, not height. Jump height scales with velocity SQUARED
 * under the engine's own linear gravity decay, so 1.3 is roughly a 69% higher jump, not 30%. A
 * first guess for "noticeably higher, not silly" the same way cheats_openphantom.c's
 * TINY_PLAYER_SCALE is; no retail precedent either direction. Runtime-adjustable rather than
 * fixed; the dev panel's own value row (see overlay_model.c) reads and writes this through the
 * getter and setter in cheats_openphantom.c, so this is only ever where a fresh install starts,
 * not the limit of what the cheat can be. Clamped on every write into a range wide enough
 * to be useful in both directions (a player weaker than retail is exactly as legitimate an ask as
 * one much stronger) but short of anything that turns a launch into a projectile the level's own
 * collision was never built to catch at the far end, or a no-op at the near one. */

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

/* Jump boost. Calling the original FIRST and unconditionally keeps this a boost rather than a
 * reimplementation: the jump still happens exactly as retail built it, guard check and all, and
 * only once it has entered its mode and written its own velocity does this cheat touch anything,
 * scaling whatever value is now sitting at +0xB4, either the fallback constant or the
 * per-character table value, whichever path the original just took. See SIG_JUMP_ENTRY's own
 * comment for why this needs two hooks rather than one. */
static void __cdecl hook_jump_entry(void)
{
    own_state.jump_entry_original();

    if (own_state.cheats[CHEATS_OWN_JUMP_BOOST].on) {
        scale_take_off(pattern_descriptor(SIG_JUMP_ENTRY));
    }
}

static void __cdecl hook_jedi_jump_entry(void)
{
    own_state.jedi_jump_entry_original();

    if (own_state.cheats[CHEATS_OWN_JUMP_BOOST].on) {
        scale_take_off(pattern_descriptor(SIG_JEDI_JUMP_ENTRY));
    }
}

/* One mode entry: found, checked to load the player from the cell every other reader uses, and
 * only then detoured. The check comes before the detour because a detour cannot be taken out. */
static bool install_jump_entry(const uint8_t *pattern, size_t size, size_t prologue,
                               const void *hook, detour_t *detour, const char *what)
{
    uintptr_t site = signature_find_detour_target(pattern, MSK_JUMP_ENTRY, size, prologue);
    uint32_t  first = 0;
    uint32_t  second = 0;
    uint8_t   head = 0;

    if (site == 0) {
        log_warning("%s did not resolve, so that half of jump boost is not offered", what);
        return false;
    }
    /* The second load sits past the prologue and is always checked. The first sits inside the
     * eight bytes another DLL's detour replaces, so it is checked only while the head is still
     * the authored push ebp; behind a jump it holds that jump's displacement. */
    if (!memory_read_u32(site + JUMP_ENTRY_PLAYER_OPERAND_SECOND, &second) ||
        second != player_slot_address() ||
        (memory_read_u8(site, &head) && head == 0x55u &&
         (!memory_read_u32(site + JUMP_ENTRY_PLAYER_OPERAND_FIRST, &first) ||
          first != second))) {
        log_warning("%s at %08X loads the player from %08X and %08X where every other reader "
                    "uses %08X, so that half of jump boost is not offered", what,
                    (unsigned)site, (unsigned)first, (unsigned)second,
                    (unsigned)player_slot_address());
        return false;
    }
    if (!detour_install(detour, site, hook, prologue)) {
        log_warning("%s at %08X could not be detoured, that half of jump boost is not offered",
                    what, (unsigned)site);
        return false;
    }
    log_info("%s hooked at %08X", what, (unsigned)site);
    return true;
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

    jump_ok = install_jump_entry(SIG_JUMP_ENTRY, sizeof SIG_JUMP_ENTRY, JUMP_ENTRY_PROLOGUE_SIZE,
                                 (const void *)&hook_jump_entry, &own_state.jump_entry_detour,
                                 "the Jump mode entry");
    if (jump_ok) {
        own_state.jump_entry_original = (mode_enter_fn_t)own_state.jump_entry_detour.original;
    }

    jedi_jump_ok = install_jump_entry(SIG_JEDI_JUMP_ENTRY, sizeof SIG_JEDI_JUMP_ENTRY,
                                      JEDI_JUMP_ENTRY_PROLOGUE_SIZE,
                                      (const void *)&hook_jedi_jump_entry,
                                      &own_state.jedi_jump_entry_detour,
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
