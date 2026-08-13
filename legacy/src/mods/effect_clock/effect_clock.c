/* effect_clock.c: three effects that re-roll themselves once per rendered frame, put back on the
 * engine's own substep clock.
 *
 * SIZE NOTE. This file runs well over six hundred lines and the great majority of it is comment.
 * The executable part is one bracket around a call, two one line replacements for a random draw,
 * and the installation. What is long is the evidence: a census of every draw from the engine's
 * random generator, the eighteen of those that a drawn frame reaches, the message each of them
 * hangs off, and the disassembly of the three that turned out to be genuinely per frame.
 *
 * The seam that was taken is substep_noise.c, which holds the arithmetic the two replacements
 * answer with and the seed the arc bracket pins. That part is a pure function of a substep number
 * and an object's place in the draw order, it has no engine in it, and it can therefore be checked
 * without the game, which is what earned it a file of its own. What is left here is everything that
 * touches the process: the patterns, the resolved cells, the hooks and the per frame reset.
 *
 * The seam that was measured and rejected is a split by site, one file for the arcs and one for the
 * object draw. All three share the generator, the substep counter and the per frame reset, so that
 * split would have duplicated the state and put the census somewhere other than the code it
 * selects. Moving the evidence out instead is the thing this tree's rules forbid, so the file
 * stays whole.
 *
 * ================================ What is actually broken =====================================
 *
 * Three things on the drawing path draw a fresh random number every time they are drawn, and the
 * drawing path runs at whatever rate the display does.
 *
 *   THE LIGHTNING ARCS have no geometry state at all. Every time the pool is drawn, every bolt is
 *   rebuilt from scratch by recursive midpoint displacement, and every displacement is a fresh
 *   draw. At the 30 frames a second this was authored for that is the intended crackle; at 240 it
 *   is eight times too fast and the eye reads it as static rather than as lightning.
 *
 *   THE FLICKER FLAG. An object carrying flag bit 0x40 draws a number each time it is drawn, and
 *   when the number lands under 0.2 the whole object is skipped: no draw, no animation advance, no
 *   reflection, no shadow. Authored, that is dark six times a second. At 240 frames a second it is
 *   dark forty eight times a second.
 *
 *   THE HALO BRIGHTNESS takes a random addition of up to 0.4 and is then clamped into 0 to 1, once
 *   per object per drawn frame. At the authored rate that is a twinkle. Four or eight times faster
 *   the additions average out and it settles into a steady blur.
 *
 * None of this is an interpolation problem. There is nothing to interpolate between: an arc is not
 * a thing that moves from A to B, it is a fresh random shape each time. The repair is not to blend
 * two states, it is to stop asking for a new one so often.
 *
 * ============================== How the three sites were found ================================
 *
 * By census rather than by looking around, because an effect that re-rolls itself has to draw from
 * the generator and therefore has to appear in its caller list. A sweep of `E8 rel32` over the
 * whole code section finds 136 call sites of the generator at 0x0049A580. Intersecting those with
 * the functions a drawn frame reaches leaves 18, spread over eleven functions:
 *
 *     0x00411028  bapobj_drawAll      1 site    the FLICKER skip
 *     0x004146BE                      1
 *     0x00416232                      2
 *     0x00417711                      1
 *     0x0041FB9E  bgl_randomUnitXYZ   2         the random supplier itself, see below
 *     0x00439AB6  fxfade              1         the halo pool
 *     0x0043A229  fxprint             1
 *     0x0043ABF0  fxshield            2
 *     0x0043B701  fxshield            1
 *     0x0043D095  fxzappo             3         the arc pool
 *     0x0043D784  fxzappo             3         the recursive midpoint displacement
 *
 * A first pass attributed all eighteen to only two functions, which was wrong. It grouped each
 * site under the nearest preceding entry point, and that heuristic walks straight across function
 * boundaries whenever a function does not begin with the shape it looks for. A backwards scan for
 * `55 8B EC` preceded by a `ret`, an `int3` or a padding `nop` gives the eleven above, and the
 * table rests on that scan rather than on the first one.
 *
 * ==================== Being on the drawing path is not being clocked per frame =================
 *
 * Each of the eleven was traced up its callers to the module proc it belongs to, and the message
 * was read out of that proc's jump table in the image. The dispatch shape is
 * `msg -= N; if (msg > M) default; jmp [msg*4 + TABLE]`, so the arm a function sits on is a fact
 * in the bytes and not something to infer from what the function appears to do.
 *
 *     fxzappo    0x0043D095   root 0x00438CD0   message 0x0D   PER FRAME
 *     fxshield   0x0043B701   root 0x00438CD0   message 0x0E   per substep, already correct
 *     bapobj     0x00411028   root 0x00410870   message 0x0D   PER FRAME
 *     fxfade     0x00439AB6   reached from bapobj_drawAll      PER FRAME
 *     0x004146BE  under shot_updateAll 0x004524B9, a task      per substep, already correct
 *     0x00416232  messages 0x06 and 0x07                       setup
 *     0x00417711  messages 0x05, 0x08 and 0x09                 setup
 *     0x0043ABF0  reached from the enemy code through aiext    simulation
 *     0x0043A229  reached from the SW_TEXT widget              user interface
 *
 * THE BODY SPHERE is the entry worth recording. It lives in the same module as the arcs, three of
 * the eighteen sites are in it across its two functions, and from the outside it looks exactly
 * like the arcs: a random shape rebuilt around a body. It sits on message 0x0E, the substep
 * broadcast, so it is already clocked correctly and pacing it would have made it worse. Reading
 * the jump table rather than assuming from the resemblance is what caught that.
 *
 * 0x0041FB9E IS NOT A TARGET EITHER, and for a reason that changes what it is rather than merely
 * ruling it out. It is bgl_randomUnitXYZ, with its wrapper bgl_randomUnit at 0x0041FC2C, and its
 * body is the textbook uniform point on a sphere: `z = 2r/32767 - 1` from one draw,
 * `theta = 360*r/32767` from the other, then `1 - z*z`. That is where its two of the eighteen
 * sites come from. The angle constant is 360, so the module works in degrees rather than radians.
 * It is not an effect that re-rolls itself, it is the supplier that effects draw from, and how
 * often it is asked is entirely its caller's business. A per frame rate here would be the caller's
 * defect, not this module's.
 *
 * ================================ How the repair works ========================================
 *
 * The whole trick is that an arc is a pure function of the numbers it draws. The generator is a
 * linear congruential one with a SINGLE WORD of state, so pinning that word on the way into the
 * pool call makes every draw inside it reproducible and the same bolt is rebuilt bit for bit.
 * Deriving the pinned value from the engine's own substep counter makes the bolt change exactly
 * when the simulation steps, which is 32 times a second. The original word is put back on the way
 * out, so the simulation's own stream is not disturbed by a single draw: it sees the sequence it
 * would have seen, because the effect's draws are undone rather than inserted.
 *
 * The flicker and the halo cannot use that bracket. Both sit inside the object draw, and pinning
 * the generator across the whole of bapobj_drawAll would freeze the halo pool at 0x00439AB6 and
 * everything else that is drawn inside that call. So for those two the single `call` is redirected
 * instead, to a replacement that answers in the same 0 to 32767 range the generator does, which
 * leaves the engine's own comparison and scaling untouched.
 *
 * ONE DELIBERATE DEVIATION, written down rather than buried. Those two replacements do not call
 * the engine's generator at all, so the drawing path stops advancing it. The simulation's random
 * sequence therefore stops depending on how many frames were drawn. The original behaviour was
 * already frame rate dependent at exactly that point, so there is no single authentic behaviour
 * being given up, but it is a change and this is where it is stated.
 */
#include "effect_clock.h"

#include "substep_noise.h"

#include "common/frame_hook.h"
#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define EFFECT_CLOCK_SECTION "effect_clock"

/* --- 0x0049A580  the generator, and its one word of state ------------------------------------ *
 *   0049A580  A1 1C A6 4B 00        mov eax,[0x004BA61C]
 *   0049A585  8D 0C 40              lea ecx,[eax + eax*2]
 *   0049A588  8D 14 88              lea edx,[eax + ecx*4]
 *   0049A58B  C1 E2 04              shl edx,4
 *   0049A58E  03 D0                 add edx,eax
 *   0049A590  C1 E2 08              shl edx,8
 *   0049A593  2B D0                 sub edx,eax
 *   0049A595  8D 84 90 C3 9E 26 00  lea eax,[eax + edx*4 + 0x269EC3]
 *   0049A59C  A3 1C A6 4B 00        mov [0x004BA61C],eax
 *   0049A5A1  C1 F8 10              sar eax,16
 *   0049A5A4  25 FF 7F 00 00        and eax,0x7FFF
 *
 * The shift and add chain folds out to a multiply by 214013 and the displacement is 2531011: the
 * compiler runtime's own `rand` constants, returning bits 16 to 30. ONE WORD of state at
 * 0x004BA61C is the whole reason the arc repair is three lines of logic. On the Edit Tool's
 * recompile that cell is at 0x004BA5CC, which is why the address is read out of the operand here
 * and not written down. Both absolute operands name the same cell, both are wildcarded, and the
 * value is taken from the first. */
static const uint8_t SIG_RAND_SEED[] = {
    0xA1, 0x00, 0x00, 0x00, 0x00,
    0x8D, 0x0C, 0x40,
    0x8D, 0x14, 0x88,
    0xC1, 0xE2, 0x04,
    0x03, 0xD0,
    0xC1, 0xE2, 0x08,
    0x2B, 0xD0,
    0x8D, 0x84, 0x90, 0xC3, 0x9E, 0x26, 0x00,
    0xA3
};
static const uint8_t MSK_RAND_SEED[] = {
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF
};
#define OFFSET_SEED_OPERAND      1u

/* --- the substep counter --------------------------------------------------------------------- *
 * Taken from the one place that advances it, inside the simulation driver, so the cell this DLL
 * reads is the cell the simulation writes and not a cosmetic copy of it:
 *
 *   A1 ?? ?? ?? ??   mov eax,[substepCounter]
 *   83 C0 01         add eax,1
 *   A3 ?? ?? ?? ??   mov [substepCounter],eax
 *   D9 05            fld  (the frame clock arithmetic that follows the increment)
 *
 * This cell moves on the Edit Tool's recompile as well, to 0x004B8810, while the code addresses
 * around the patched call sites do not move at all. That combination, two data cells relocated and
 * the code where it was, is the argument for reading both out of a matched operand rather than
 * carrying a table of addresses per build. */
static const uint8_t SIG_SUBSTEP_COUNTER[] = {
    0xA1, 0x00, 0x00, 0x00, 0x00,
    0x83, 0xC0, 0x01,
    0xA3, 0x00, 0x00, 0x00, 0x00,
    0xD9, 0x05
};
static const uint8_t MSK_SUBSTEP_COUNTER[] = {
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF
};
#define OFFSET_COUNTER_OPERAND   1u

/* --- 0x00438D94  the one call to the arc pool ------------------------------------------------ *
 * The pool at 0x0043D095 takes NO arguments and walks the whole list itself, from 0x006CB188. That
 * fact refuted this DLL's first design, which pinned a seed derived from the substep counter AND
 * an instance index on the assumption that the draw function was told which bolt it was drawing.
 * It is not. Every instance draws from the same stream in turn, so the instances decorrelate by
 * construction and one seed for the whole call is correct rather than merely convenient. The
 * sibling at 0x0043D784 is the recursive midpoint displacement, calling itself at 0x0043D8A6 and
 * 0x0043D8BE, and it is reached from inside the pool loop, so bracketing the one call covers its
 * three draws too.
 *
 * WHY THE CALL SITE AND NOT THE PROLOGUE. 0x0043D095 opens `55 8B EC 83 EC 70 56`, so its prologue
 * is six bytes, and a scan of every relative branch between it and the next function finds none
 * that targets those six bytes. A prologue detour would therefore have been legal here, unlike the
 * display flip elsewhere in this engine where a retry jumps back into its own second instruction.
 * It was still not used, because the pool has exactly ONE relative caller, 0x00438D94. Rewriting
 * four displacement bytes touches less than a detour does, cannot collide with the function's own
 * control flow, and needs no trampoline.
 *
 * The anchor is the surrounding shape, two consecutive calls between two identical
 * `xor eax,eax / jmp` arm tails. BOTH call displacements are wildcards. The second is the four
 * bytes this patch writes, and a pattern that spelled them out would stop matching the moment it
 * had been applied once.
 *
 *   retail WMAIN.EXE                        site 0x00438D8B, call 0x00438D94 -> 0x0043D095
 *   wmain.exe                               identical
 *   obi.exe, the Edit Tool's recompile      identical addresses here, moved data cells */
static const uint8_t SIG_ARC_CALL_SITE[] = {
    0x33, 0xC0,                          /* xor eax,eax         the previous arm's tail  */
    0xEB, 0x00,                          /* jmp the epilogue                             */
    0xE8, 0x00, 0x00, 0x00, 0x00,        /* call the arc pool's sibling                  */
    0xE8, 0x00, 0x00, 0x00, 0x00,        /* call the ARC POOL, the one redirected here   */
    0x33, 0xC0,                          /* xor eax,eax                                  */
    0xEB                                 /* jmp the epilogue                             */
};
static const uint8_t MSK_ARC_CALL_SITE[] = {
    0xFF, 0xFF,
    0xFF, 0x00,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF,
    0xFF
};
#define OFFSET_ARC_CALL          9u

/* --- 0x00411438  the flicker test inside the object draw ------------------------------------- *
 *   00411425  C7 45 FC 01 00 00 00  mov  [ebp-4],1           bVisible = 1
 *   0041142C  8B 45 F8              mov  eax,[ebp-8]         the object
 *   0041142F  8B 08                 mov  ecx,[eax]           obj->flags
 *   00411431  83 E1 40              and  ecx,0x40            the FLICKER bit
 *   00411434  85 C9                 test ecx,ecx
 *   00411436  74 29                 je   not flickering, carry on
 *   00411438  E8 43 91 08 00        call 0x0049A580          the draw redirected here
 *   0041143D  89 85 30 FB FF FF     mov  [ebp-0x4D0],eax
 *   00411443  DB 85 30 FB FF FF     fild [ebp-0x4D0]
 *   00411449  D8 0D 2C 81 4A 00     fmul [0x004A812C]        1/32767
 *   0041144F  D8 1D 30 81 4A 00     fcomp [0x004A8130]       0.2
 *   00411455  DF E0                 fnstsw ax
 *   00411457  F6 C4 01              test ah,1
 *   0041145A  74 05                 je   carry on
 *   0041145C  E9 B5 FC FF FF        jmp  0x00411116          SKIP THE WHOLE OBJECT
 *
 * The jump at the end goes back to the loop head, so a flickering object loses its draw, its four
 * animation track updates, its reflections and its shadow for that frame. The threshold 0.2 at
 * 0x004A8130 and the scale 1/32767 at 0x004A812C are both read out of the image, not assumed.
 *
 * The replacement answers in the same 0 to 32767 range, so the `fmul` and the `fcomp` above are
 * left exactly as the engine wrote them.
 *
 *   retail WMAIN.EXE   call at 0x00411438 -> 0x0049A580
 *   obi.exe            call at 0x00411438 -> 0x0049A520
 *
 * The anchor is unique in all three builds, and that pair of rows is why the previous target is
 * read out of the displacement instead of being compared against a known address. */
static const uint8_t SIG_FLICKER_TEST[] = {
    0x83, 0xE1, 0x40,
    0x85, 0xC9,
    0x74, 0x00,
    0xE8, 0x00, 0x00, 0x00, 0x00,
    0x89, 0x85, 0x00, 0x00, 0x00, 0x00,
    0xDB, 0x85, 0x00, 0x00, 0x00, 0x00,
    0xD8, 0x0D, 0x00, 0x00, 0x00, 0x00,
    0xD8, 0x1D
};
static const uint8_t MSK_FLICKER_TEST[] = {
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0x00,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF
};
#define OFFSET_FLICKER_CALL      7u

/* --- 0x00439FBD  the halo brightness jitter -------------------------------------------------- *
 *   00439FB7  D8 4D F4              fmul [ebp-0x0C]          intensity *= the falloff
 *   00439FBA  D9 5D F4              fstp [ebp-0x0C]
 *   00439FBD  E8 BE 05 06 00        call 0x0049A580          the draw redirected here
 *   00439FC2  89 85 5C FE FF FF     mov  [ebp-0x1A4],eax
 *   00439FC8  DB 85 5C FE FF FF     fild [ebp-0x1A4]
 *   00439FCE  D8 0D 28 85 4A 00     fmul [0x004A8528]        1/32767
 *   00439FD4  D8 0D 2C 85 4A 00     fmul [0x004A852C]        0.4
 *   00439FDA  D8 45 F4              fadd [ebp-0x0C]          intensity += the jitter
 *   00439FE0  D8 1D 30 85 4A 00     fcomp [0x004A8530]       0.0, the low clamp
 *   00439FF9  D8 1D 34 85 4A 00     fcomp [0x004A8534]       1.0, the high clamp
 *
 * So the addition is at most 0.4 and the result is clamped into 0 to 1. All four constants are
 * read out of the image. The site is reached per object per drawn frame, through 0x00438E78 from
 * the object draw, which is what makes it twinkle at the authored rate and average into a steady
 * blur above it.
 *
 *   retail WMAIN.EXE   call at 0x00439FBD -> 0x0049A580
 *   obi.exe            call at 0x00439FBD -> 0x0049A520
 *
 * Unique in all three builds, same reasoning as the flicker for reading the old target rather than
 * assuming it. */
static const uint8_t SIG_HALO_JITTER[] = {
    0xD8, 0x4D, 0x00,
    0xD9, 0x5D, 0x00,
    0xE8, 0x00, 0x00, 0x00, 0x00,
    0x89, 0x85, 0x00, 0x00, 0x00, 0x00,
    0xDB, 0x85, 0x00, 0x00, 0x00, 0x00,
    0xD8, 0x0D, 0x00, 0x00, 0x00, 0x00,
    0xD8, 0x0D
};
static const uint8_t MSK_HALO_JITTER[] = {
    0xFF, 0xFF, 0x00,
    0xFF, 0xFF, 0x00,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF
};
#define OFFSET_HALO_CALL         6u

#define SUBSTEPS_PER_SECOND      32u   /* the simulation rate the substep counter advances at */
#define SUBSTEPS_PER_ROLL_MAX    32    /* one roll a second at the far end, and no slower */

typedef void (__cdecl *arc_pool_fn_t)(void);

typedef struct effect_clock_state {
    bool                     entered;        /* the entry point can be called more than once */
    bool                     anything_paced;
    volatile uint32_t       *seed;           /* validated once at install, then a plain store */
    const volatile uint32_t *counter;
    arc_pool_fn_t            arc_original;
    uint32_t                 substeps_per_roll;

    /* The flicker replacement. `sequence` counts the calls WITHIN one drawn frame and is reset at
     * the end of each frame, so the Nth flickering object of a frame asks the same question in
     * every frame until the simulation steps. That is what makes the answer stable inside a
     * substep without the replacement needing to know which object is asking. */
    bool                     flicker_paced;
    uint32_t                 sequence;

    /* The halo keeps its own ordinal. Sharing one with the flicker would in fact work, because
     * both are called from the same object loop in the same order, but two independent counters
     * mean neither effect's stability depends on how often the other one happened to ask. */
    bool                     halo_paced;
    uint32_t                 halo_sequence;
} effect_clock_state_t;

static effect_clock_state_t clock_state;

/* Which simulation step the effects are currently allowed to see. */
static uint32_t current_tick(void)
{
    return substep_noise_tick(*clock_state.counter, clock_state.substeps_per_roll);
}

/* Pin, call, restore. The restore is what keeps the simulation's own stream untouched: whatever
 * the effect drew is discarded, so the next simulation draw continues exactly where it would
 * have, and no part of the game outside this call can tell that the arcs were drawn at all. */
static void __cdecl hook_arc_pool(void)
{
    uint32_t saved = *clock_state.seed;

    *clock_state.seed = substep_noise_arc_seed(current_tick());
    clock_state.arc_original();
    *clock_state.seed = saved;
}

/* The replacement for the flicker test's draw. The answer depends only on the substep and on which
 * flickering object is asking, so every frame of one substep gets the same answers in the same
 * order and each object holds its state; the next substep re-rolls.
 *
 * The ordinal is stable in practice because the object list is rebuilt and sorted by asset name on
 * each draw, and no object spawns or dies between two frames of one simulation step. If that ever
 * stopped holding, the worst case is the behaviour this patch replaces, an object changing its
 * mind within a step. */
static uint32_t __cdecl hook_flicker_rand(void)
{
    uint32_t tick    = current_tick();
    uint32_t ordinal = clock_state.sequence++;

    return substep_noise_flicker(tick, ordinal);
}

/* The same idea for the halo, on its own ordinal. */
static uint32_t __cdecl hook_halo_rand(void)
{
    uint32_t tick    = current_tick();
    uint32_t ordinal = clock_state.halo_sequence++;

    return substep_noise_halo(tick, ordinal);
}

/* Called once per drawn frame from the shared frame hook, so both ordinals restart with the frame
 * the object list is walked in. Without this the counters would run on across frames and the
 * answers would drift within a substep, which is the very thing being repaired. */
static void on_frame_end(void)
{
    clock_state.sequence      = 0;
    clock_state.halo_sequence = 0;
}

/* The flicker and the halo differ only in which pattern locates them and which replacement they
 * get, so the installation is one helper carrying the site's own name into the log. */
static bool redirect_random_call(const uint8_t *sig, const uint8_t *mask, size_t size,
                                 size_t call_offset, const void *replacement, const char *what)
{
    uintptr_t site;
    uintptr_t target = 0;

    site = signature_find_unique(sig, mask, size);
    if (site == 0) {
        log_warning("the %s did not resolve, it keeps deciding once per rendered frame", what);
        return false;
    }
    if (!patch_read_call_target(site + call_offset, &target)) {
        log_warning("no usable call at %08X for the %s, nothing written",
                    (unsigned)(site + call_offset), what);
        return false;
    }
    if (patch_redirect_call(site + call_offset, replacement) != PATCH_RESULT_OK) {
        log_warning("the %s call at %08X was not redirected", what, (unsigned)(site + call_offset));
        return false;
    }
    log_info("  %-16s call at %08X redirected, was %08X", what,
             (unsigned)(site + call_offset), (unsigned)target);
    return true;
}

/* Resolve an absolute operand out of a matched pattern and validate the cell once, here, at
 * install. Everything after this is a plain load and store, because the hooks run on the drawing
 * path and a range check there would cost a system call per frame. */
static uint32_t *resolve_cell(const uint8_t *sig, const uint8_t *mask, size_t size,
                              size_t operand_offset, const char *what)
{
    uintptr_t site;
    uint32_t  cell = 0;

    site = signature_find_unique(sig, mask, size);
    if (site == 0) {
        log_warning("%s did not resolve", what);
        return NULL;
    }
    if (!memory_read_u32(site + operand_offset, &cell) ||
        !memory_is_inside_image((uintptr_t)cell, sizeof(uint32_t)) ||
        !memory_is_readable_range((uintptr_t)cell, sizeof(uint32_t))) {
        log_warning("%s at %08X names %08X, which is not a usable cell",
                    what, (unsigned)site, (unsigned)cell);
        return NULL;
    }
    log_info("  %-18s %08X from the operand at %08X", what, (unsigned)cell, (unsigned)site);
    return (uint32_t *)(uintptr_t)cell;
}

static void install_arc_bracket(void)
{
    uintptr_t site;
    uintptr_t call_site;
    uintptr_t original = 0;

    site = signature_find_unique(SIG_ARC_CALL_SITE, MSK_ARC_CALL_SITE, sizeof SIG_ARC_CALL_SITE);
    if (site == 0) {
        log_warning("the arc pool call site did not resolve, the arcs are left alone and keep "
                    "re-rolling once per rendered frame");
        return;
    }

    call_site = site + OFFSET_ARC_CALL;
    if (!patch_read_call_target(call_site, &original)) {
        log_warning("no usable call at %08X, the arcs are left alone", (unsigned)call_site);
        return;
    }

    clock_state.arc_original = (arc_pool_fn_t)original;
    if (patch_redirect_call(call_site, (const void *)&hook_arc_pool) != PATCH_RESULT_OK) {
        log_warning("the call at %08X was not redirected, the arcs are left alone",
                    (unsigned)call_site);
        return;
    }

    clock_state.anything_paced = true;
    log_info("lightning arcs paced: the pool at %08X is called from %08X on the per-frame message, "
             "and its shape is a pure function of the numbers it draws. The generator's state is "
             "pinned to the substep counter for the duration of that call and restored afterwards, "
             "so a bolt is rebuilt identically within one simulation step and re-rolled every %u "
             "of them. The simulation's own sequence is unchanged, because the effect's draws are "
             "undone rather than inserted.",
             (unsigned)original, (unsigned)call_site, (unsigned)clock_state.substeps_per_roll);
}

/* Both object-draw sites need the per-frame reset, so the frame hook is installed once and only
 * when at least one of them is wanted. If it cannot be installed, neither is patched: an ordinal
 * that never restarts would answer differently in successive frames of one substep, which is the
 * defect rather than the repair. */
static void install_object_draw_sites(bool want_flicker, bool want_halo)
{
    uint32_t rolls_per_second = SUBSTEPS_PER_SECOND / clock_state.substeps_per_roll;

    if (!frame_hook_add(on_frame_end)) {
        log_warning("the per-frame hook could not be installed, and without it an ordinal cannot "
                    "restart with the frame, so the flicker and the halo are left alone");
        return;
    }

    if (want_flicker &&
        redirect_random_call(SIG_FLICKER_TEST, MSK_FLICKER_TEST, sizeof SIG_FLICKER_TEST,
                             OFFSET_FLICKER_CALL, (const void *)&hook_flicker_rand,
                             "flicker test")) {
        clock_state.flicker_paced  = true;
        clock_state.anything_paced = true;
        log_info("flicker paced: an object carrying the flicker flag is skipped entirely, "
                 "animation advance included, when a draw lands under 0.2, and that draw happened "
                 "once per RENDERED frame. It now answers from the substep counter and the "
                 "object's place in the frame's draw order, so the object holds its state for a "
                 "whole simulation step and changes %u times a second instead of with the display.",
                 (unsigned)rolls_per_second);
    }

    if (want_halo &&
        redirect_random_call(SIG_HALO_JITTER, MSK_HALO_JITTER, sizeof SIG_HALO_JITTER,
                             OFFSET_HALO_CALL, (const void *)&hook_halo_rand, "halo jitter")) {
        clock_state.halo_paced     = true;
        clock_state.anything_paced = true;
        log_info("halo paced: the halo's brightness takes a random addition of up to 0.4 and is "
                 "then clamped into 0 to 1, once per object per RENDERED frame, so it twinkles at "
                 "the authored rate and averages into a steady blur above it. It now changes %u "
                 "times a second.", (unsigned)rolls_per_second);
    }
}

void effect_clock_install(void)
{
    int32_t   configured;
    bool      want_flicker;
    bool      want_halo;
    uint32_t *seed;
    uint32_t *counter;

    if (clock_state.entered) {
        return;
    }
    clock_state.entered = true;

    /* BEFORE anything that logs. Without it every line this DLL writes is dropped, including the
     * warnings that would name the reason it did nothing. */
    log_init("effect_clock", false);

    /* And this one too. common/ is a static library, so this DLL carries its own copy of the host
     * image state; another DLL having resolved it earlier does nothing for us. Without it the
     * scanner searches an empty range and every pattern comes back with zero matches, which reads
     * in the log exactly like an unsupported executable. */
    if (!host_image_resolve()) {
        log_error("no 32-bit host image, the effects keep re-rolling once per rendered frame");
        return;
    }

    if (!ini_read_bool(EFFECT_CLOCK_SECTION, "Enabled", true)) {
        log_info("disabled");
        return;
    }

    configured = ini_read_int(EFFECT_CLOCK_SECTION, "SubstepsPerRoll", 1);
    if (configured < 1 || configured > SUBSTEPS_PER_ROLL_MAX) {
        log_warning("SubstepsPerRoll %d is outside 1 to %d, using 1", (int)configured,
                    SUBSTEPS_PER_ROLL_MAX);
        configured = 1;
    }
    clock_state.substeps_per_roll = (uint32_t)configured;

    seed = resolve_cell(SIG_RAND_SEED, MSK_RAND_SEED, sizeof SIG_RAND_SEED,
                        OFFSET_SEED_OPERAND, "random seed");
    counter = resolve_cell(SIG_SUBSTEP_COUNTER, MSK_SUBSTEP_COUNTER, sizeof SIG_SUBSTEP_COUNTER,
                           OFFSET_COUNTER_OPERAND, "substep counter");
    /* The arcs need the seed cell and all three sites need the counter. If either did not resolve,
     * nothing is patched: this is a build the patterns do not describe, and a feature that is half
     * installed on an unknown build is worse than one that declined. */
    if (seed == NULL || counter == NULL) {
        log_warning("one of the two cells did not resolve, so no patch is applied and every effect "
                    "keeps the behaviour it had");
        return;
    }

    clock_state.seed    = (volatile uint32_t *)seed;
    clock_state.counter = (const volatile uint32_t *)counter;

    if (ini_read_bool(EFFECT_CLOCK_SECTION, "PaceLightning", true)) {
        install_arc_bracket();
    } else {
        log_info("PaceLightning=0, the arcs keep re-rolling once per rendered frame");
    }

    want_flicker = ini_read_bool(EFFECT_CLOCK_SECTION, "PaceFlicker", true);
    want_halo    = ini_read_bool(EFFECT_CLOCK_SECTION, "PaceHalo", true);
    if (want_flicker || want_halo) {
        install_object_draw_sites(want_flicker, want_halo);
    } else {
        log_info("PaceFlicker=0 and PaceHalo=0, the object draw is left alone");
    }

    if (clock_state.flicker_paced || clock_state.halo_paced) {
        log_info("side effect worth knowing: the drawing path no longer advances the engine's "
                 "global random generator, so the simulation's own sequence stops depending on how "
                 "many frames were drawn. The original was already frame-rate dependent there.");
    }

    if (!clock_state.anything_paced) {
        log_warning("nothing was paced");
    }
}
