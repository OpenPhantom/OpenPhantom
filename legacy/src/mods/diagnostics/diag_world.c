/* diag_world.c: the world's own traffic, movers and the AI state machine.
 *
 * ==================================== SIZE NOTE ===============================================
 *
 * This file is over 600 lines. It crossed the limit when the mover call-site census was added, and
 * the census is the reason: about a third of the file is now one measurement, its side state and
 * the reporting that makes it readable, while the six observers around it are a few stores each.
 * Most of the rest is the reasoning behind two things a reader cannot see in the code, why the
 * opcode hook may detour into the middle of a function, and why the integrator has two early
 * returns that have to be told apart.
 *
 * The next seam is the census, and it is a clean one. mover_census_* touches no other state in this
 * file, shares only the tick detour that feeds it, and would move to a file of its own with one
 * function exported and one call added where hook_mover_tick already calls it. That is the cut to
 * make when this file next grows.
 *
 * It is not the mover-versus-AI split, which looks obvious and was measured and rejected: both
 * halves share resolve_sites_once, the signature table and diag_install_observer, so cutting there
 * puts one table behind a translation unit boundary from half its users and buys nothing.
 */
#include "diag_world.h"

#include "diag_install.h"
#include "diag_log.h"
#include "diag_names.h"

#include "common/detour.h"
#include "common/frame_hook.h"
#include "common/host_image.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <intrin.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* --- bapmap_openMover 0x00408B50 -------------------------------------------------------------- *
 * The open command. It fires EVERY FRAME while a body stands on a pressure plate
 * (bapmap_firePlate has no rising edge), which is why the hook snapshots the mover BEFORE and
 * AFTER the call and stays quiet when nothing changed.
 *   world+0x620 = numMovers, world+0x624 = ppMover[] (an INLINE array, not a pointer to one)
 *   mover+0x00 = active, +0x04 = type, +0x08 = id, +0x2C = pose, +0x34 = dir */
static const uint8_t SIG_MOVER_OPEN[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x20, 0x83, 0x7D, 0x08, 0x00, 0x74, 0x14,
    0x83, 0x7D, 0x0C, 0x00, 0x7C, 0x0E, 0x8B, 0x45
};
#define MOVER_OPEN_PROLOGUE 6u

/* --- bapmap_closeMover 0x00408DF5 ------------------------------------------------------------- *
 * "Close" is the SCRIPT's word for it; what happens is `dir = 2`, and what that means depends on
 * the type (type 1 freezes, types 2/6 begin the open dwell, types 3/4/5 latch for good). */
static const uint8_t SIG_MOVER_CLOSE[] = {
    0x55, 0x8B, 0xEC, 0x51, 0x83, 0x7D, 0x08, 0x00, 0x74, 0x14, 0x83, 0x7D,
    0x0C, 0x00, 0x7C, 0x0E, 0x8B, 0x45, 0x08, 0x8B, 0x4D, 0x0C, 0x3B, 0x88,
    0x20, 0x06, 0x00, 0x00
};
#define MOVER_CLOSE_PROLOGUE 8u

/* --- bapmap_tickMover 0x00409170 -------------------------------------------------------------- *
 * The integrator. It is the only place a mover advances by itself (a door closing after its dwell,
 * a one-shot latching, a push button entering its wait phase). Level 2, debounced on `dir`.
 *
 * It is NOT called once per frame from one place. It has nine call sites, one of which is reached
 * from inside the world draw, so which one reaches a given mover first decides where that mover
 * actually integrates. Measured, it is the per-frame one that does essentially all of it. That is
 * the question level 3 exists to answer, and it is why the pattern's gate cell matters: the opening
 * run of the function, read as bytes on retail WMAIN.EXE, contains two independent early returns.
 *
 *   00409170  55 8B EC 83 EC 2C       prologue
 *   00409176  83 3D CC5F5B00 00       cmp  dword [0x005B5FCC], 0
 *   0040917D  0F 85 C7040000          jne  0x0040964A          return
 *   00409183  8B 45 08                mov  eax,[ebp+8]         the mover
 *   00409186  D9 45 0C                fld  dword [ebp+0xC]     `now`
 *   00409189  D8 58 30                fcomp dword [eax+0x30]   mover->timeBase
 *   0040918C  DF E0                   fnstsw ax
 *   0040918E  F6 C4 40                test ah, 0x40
 *   00409191  0F 85 B3040000          jne  0x0040964A          return
 *   ...
 *   0040964C  5D C3                   pop ebp / ret
 *
 * The second branch really is "equal, return", and it reads like the opposite if the fnstsw and the
 * test are skipped over. fcomp puts its answer in the x87 status word, fnstsw moves it into ah, and
 * bit 6 of ah is C3, which is set when the operands were equal. `test ah,0x40` therefore clears ZF
 * exactly when they were equal, so the jne is taken on equality. This is the branch that produces a
 * freeze: the world clock is written only inside the substep loop, so on a frame with no substep
 * every mover sees the time it has already integrated to and returns.
 *
 * The first branch is a second, independent early return and it is easy to miss entirely.
 * [0x005B5FCC] has exactly one reference in the whole image, this read, and no instruction anywhere
 * writes it, so on this build the gate is never taken. That is a census over one binary rather than
 * a proof about every path, because a bulk write reaching the cell as part of a larger structure
 * would not show up in an operand scan. The census below counts the case instead of assuming it
 * away, which costs one comparison per call.
 *
 * The gate cell's operand sits at +0x08 and is read out of the matched pattern rather than being
 * written down, so the census works on any build the pattern resolves on. */
static const uint8_t SIG_MOVER_TICK[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x2C, 0x83, 0x3D, 0xCC, 0x5F, 0x5B, 0x00,
    0x00, 0x0F, 0x85, 0xC7, 0x04, 0x00, 0x00, 0x8B
};
#define MOVER_TICK_PROLOGUE 6u
#define OFFSET_MOVER_GATE_CELL 0x08u

/* --- ai_setMode 0x004335A5 / ai_returnMode 0x00433634 ----------------------------------------- *
 * Opcode 0x400 "Set AI Mode" = PUSH, opcode 0x401 "Return Mode" = POP of the FOUR-DEEP stack,
 * the misreading "jump to state 0" cost 348 sites across 11 levels. Both are EDGES, not
 * per-frame traffic.
 *   character: +0x04 name[12], +0x18 enmyIndex, +0x20 state, +0x7C aiMode
 *              +0x80/+0x84/+0x88 the three history slots, +0x8C modeTicks */
static const uint8_t SIG_AI_SET_MODE[] = {
    0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0x8B, 0x48, 0x7C, 0x3B, 0x4D, 0x0C,
    0x74, 0x72, 0x8B, 0x55, 0x08, 0x8B, 0x45, 0x08
};
#define AI_SET_MODE_PROLOGUE 6u
static const uint8_t SIG_AI_RETURN_MODE[] = {
    0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0x8B, 0x4D, 0x08, 0x8B, 0x91, 0x80,
    0x00, 0x00, 0x00, 0x89, 0x50, 0x7C, 0x8B, 0x45
};
#define AI_RETURN_MODE_PROLOGUE 6u

/* --- ai_dispatch 0x00433E51 ------------------------------------------------------------------- *
 * The only site in the whole DLL that does not sit on a function entry. The opcode dispatcher was
 * inlined by MSVC INTO ai_run (0x433D0B); it has no symbol and no frame of its own. There is
 * therefore no way to observe "which opcode is running" with an ordinary detour. What follows is a
 * detour INTO THE MIDDLE of a function, and that is deliberately tied to three conditions:
 *
 *   (1) the pattern is the proof. The two stolen instructions
 *         0F BF 4D E4          movsx ecx, word ptr [ebp-0x1C]   ; the resolved opcode
 *         89 8D 70 FF FF FF    mov  [ebp-0x90], ecx
 *       encode the ebp offsets the hook reads. If the pattern matches, the offsets are proven; if
 *       it does not, the hook is off. Nothing is assumed.
 *   (2) 10 bytes, an exact instruction boundary, and a linear disassembly of the WHOLE .text
 *       (three phase offsets) finds NO branch targeting into those 10 bytes.
 *   (3) The hook is `naked` and saves pushad/pushfd AND the complete x87 state (fnsave/frstor,
 *       108 bytes), at this point the engine is in the middle of a frame, and the logger uses
 *       the CRT.
 *
 * Off by default. At about 36 actors and about 10 opcodes per tick it costs roughly 11,000 calls
 * per second; the formatting is the expensive part, not the detour. */
static const uint8_t SIG_AI_OPCODE[] = {
    0x0F, 0xBF, 0x4D, 0xE4, 0x89, 0x8D, 0x70, 0xFF, 0xFF, 0xFF, 0x81, 0xBD,
    0x70, 0xFF, 0xFF, 0xFF, 0x80, 0x01, 0x00, 0x00
};
#define AI_OPCODE_PROLOGUE 10u

/* --- bapmap_polyToWorld 0x00419490 -------------------------------------------------------------
 * One of the two known render-path callers of bapmap_tickMover (see the census above): decompiled
 * as `FUN_00419490(int world, ushort *object, float *outPos, undefined4 *outExtra)`, plain cdecl,
 * four arguments, no surprises. Counted here at its own entry so a per-frame explosion in how many
 * times the WHOLE FUNCTION runs is told apart from an explosion in what one call does once inside
 * it; the mover census alone cannot tell those two apart, since it only sees tickMover. */
static const uint8_t SIG_POLY_TO_WORLD[] = {
    0x8B, 0x4C, 0x24, 0x04, 0x83, 0xEC, 0x3C, 0x53, 0x55, 0x56, 0x33, 0xF6, 0x85, 0xC9
};
#define POLY_TO_WORLD_PROLOGUE 7u

/* --- bapvrt_transformWorld 0x004199b0 ------------------------------------------------------------
 * The other known render-path caller of bapmap_tickMover, called from render_prepareFrame
 * (0x0043f5ac). Decompiled as `FUN_004199b0(int world)`: it walks a FIXED grid of `world+0xbc + 1`
 * buckets (a global array, DAT_008bc880), each a linked list of objects, and inside that walk is
 * where the nine-call-site census's own two render-path entries into tickMover live. The bucket
 * count is a level-static number, it has no reason to change frame to frame, so if THIS
 * function's own per-frame entry count spikes, the whole world transform is being redone
 * repeatedly in one frame (portal or mirror recursion is the standing suspect for a tight,
 * multi-surfaced shaft); if it stays flat while tickMover still spikes, the explosion is inside
 * one call's own bucket walk instead, which is a different question with a different answer. */
static const uint8_t SIG_TRANSFORM_WORLD[] = {
    0x81, 0xEC, 0xE8, 0x00, 0x00, 0x00, 0x8B, 0x0D, 0xE4, 0x83, 0x6F, 0x00
};
#define TRANSFORM_WORLD_PROLOGUE 6u

/* --- FUN_0040be00 0x0040be00 --------------------------------------------------------------------
 * The general line trace: clears a 0x22-dword result structure, then walks the SAME broadphase
 * candidate iterator (FUN_0040d7bf/FUN_0040d7dd) bapmap_polyToWorld's own callers were found sitting
 * behind, testing each candidate through FUN_0040e06b, the distance-along-a-ray-to-a-plane helper
 * that call site 0x0040e081 in the poly-to-world census names as the dominant one during the stall.
 * The result structure carries a hit mover pointer and subnode index (result+0x20, result+0x24) as
 * well as the hit distance, so this is mover-aware: it is what a sweep against the world, including
 * a moving lift, has to be. Decompiled as `void FUN_0040be00(undefined4 context, float *result)`,
 * plain cdecl, two arguments. */
static const uint8_t SIG_TRACE_GENERAL[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x7C, 0x57, 0xC7, 0x45, 0xC8, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x45, 0x0C, 0x8B, 0x48, 0x18
};
#define TRACE_GENERAL_PROLOGUE 7u

/* --- FUN_0040c2be 0x0040c2be --------------------------------------------------------------------
 * The floor trace: the same broadphase walk as the general trace above, but filtered to candidates
 * whose own type nibble at +0x3e is exactly 0xE before it is even tested, and it stops at the first
 * one rather than keeping the closest. A single-purpose "what floor polygon is under this point"
 * query built out of the same shared iterator and the same FUN_0040e06b distance helper. Decompiled
 * as `float10 FUN_0040c2be(undefined4 context)`, plain cdecl, one argument, returns through ST(0). */
static const uint8_t SIG_TRACE_FLOOR[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x44, 0x6A, 0x00, 0x8B, 0x45, 0x08, 0x50,
    0x8B, 0x0D, 0x60, 0x00, 0x8A, 0x00
};
#define TRACE_FLOOR_PROLOGUE 8u

enum {
    SITE_MOVER_OPEN,
    SITE_MOVER_CLOSE,
    SITE_MOVER_TICK,
    SITE_AI_SET_MODE,
    SITE_AI_RETURN_MODE,
    SITE_AI_OPCODE,
    SITE_POLY_TO_WORLD,
    SITE_TRANSFORM_WORLD,
    SITE_TRACE_GENERAL,
    SITE_TRACE_FLOOR,
    SITE_COUNT
};

/* Every one of these is a detour target, so every one uses the detour form of the macro.
 *
 * All six patterns begin with the bytes the detour overwrites. Registered with the plain macro,
 * whichever DLL resolves second finds a `jmp` where it expected the prologue, matches zero times,
 * and switches itself off with a message naming the wrong cause. The chaining in common/detour.c
 * handles the sharing correctly and never gets the chance to run.
 *
 * That is not hypothetical here: the mover integrator is wanted by a second feature, and the silent
 * loser would be whichever of the two loaded later, including the census this file provides. The
 * detour form falls back to the pattern's tail and proves the head is either the authored prologue
 * or a branch, so both owners resolve. */
static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY_DETOUR("mover_open",     SIG_MOVER_OPEN,     MOVER_OPEN_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR("mover_close",    SIG_MOVER_CLOSE,    MOVER_CLOSE_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR("mover_tick",     SIG_MOVER_TICK,     MOVER_TICK_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR("ai_set_mode",    SIG_AI_SET_MODE,    AI_SET_MODE_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR("ai_return_mode", SIG_AI_RETURN_MODE, AI_RETURN_MODE_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR("ai_opcode",      SIG_AI_OPCODE,      AI_OPCODE_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR("poly_to_world",    SIG_POLY_TO_WORLD,    POLY_TO_WORLD_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR("transform_world",  SIG_TRANSFORM_WORLD,  TRANSFORM_WORLD_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR("trace_general",    SIG_TRACE_GENERAL,    TRACE_GENERAL_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR("trace_floor",      SIG_TRACE_FLOOR,      TRACE_FLOOR_PROLOGUE)
};

#define WORLD_MOVER_COUNT 0x620
#define WORLD_MOVER_ARRAY 0x624
#define MOVER_ACTIVE      0x00
#define MOVER_TYPE        0x04
#define MOVER_ID          0x08
#define MOVER_POSE        0x2C
#define MOVER_TIME_BASE   0x30
#define MOVER_DIRECTION   0x34

#define CHARACTER_NAME       0x04   /* char[12] */
#define CHARACTER_ENMY_INDEX 0x18
#define CHARACTER_STATE      0x20
#define CHARACTER_AI_MODE    0x7C

typedef void (__cdecl *mover_command_fn_t)(void *world, int32_t index);
typedef void (__cdecl *mover_tick_fn_t)(void *mover, float now);
typedef void (__cdecl *ai_set_mode_fn_t)(void *actor, int32_t new_mode);
typedef void (__cdecl *ai_return_mode_fn_t)(void *actor);
typedef void (__cdecl *poly_to_world_fn_t)(void *world, void *object, float *out_pos,
                                            void *out_extra);
typedef void (__cdecl *transform_world_fn_t)(void *world);
typedef void (__cdecl *trace_general_fn_t)(void *context, float *result);
typedef double (__cdecl *trace_floor_fn_t)(void *context);

typedef struct diag_world_state {
    bool     sites_resolved;
    detour_t mover_open;
    detour_t mover_close;
    detour_t mover_tick;
    detour_t ai_set_mode;
    detour_t ai_return_mode;
    detour_t ai_opcode;
    detour_t poly_to_world;
    detour_t transform_world;
    detour_t trace_general;
    detour_t trace_floor;
} diag_world_state_t;

static diag_world_state_t world_state;

/* common/frame_hook.c caps a single DLL at 4 callbacks (MAX_FRAME_CALLBACKS), shared with
 * diag_frame.c's own per-second summary and diag_present.c's hook. Five censuses each registering
 * their own callback blew straight through that and silently starved the other two, defined below,
 * after the report function it ticks is declared, and registered from every census's own install
 * function; frame_hook_add is idempotent per callback, so the five install call sites collapse to
 * exactly one slot no matter which trigger sub-levels are actually armed. */
static void diag_world_census_tick(void);

/* The opcode hook keeps its trampoline in a file-scope pointer because its only caller is inline
 * assembly, which cannot reach into a structure member by name. */
static void *opcode_trampoline;

static void resolve_sites_once(void)
{
    if (world_state.sites_resolved) {
        return;
    }
    world_state.sites_resolved = true;
    signature_resolve_table(sites, SITE_COUNT);
}

/* ============================================================================================ */
static uint8_t *mover_at(void *world, int32_t index)
{
    uint8_t *record = (uint8_t *)world;

    if (record == NULL || index < 0 || index >= *(const int32_t *)(record + WORLD_MOVER_COUNT)) {
        return NULL;
    }
    return *(uint8_t **)(record + WORLD_MOVER_ARRAY + index * 4);
}

static void snapshot_mover(const uint8_t *mover, int32_t *direction, int32_t *active, float *pose)
{
    *direction = (mover != NULL) ? *(const int32_t *)(mover + MOVER_DIRECTION) : -1;
    *active    = (mover != NULL) ? *(const int32_t *)(mover + MOVER_ACTIVE) : -1;
    *pose      = (mover != NULL) ? *(const float *)(mover + MOVER_POSE) : 0.0f;
}

static void __cdecl hook_mover_open(void *world, int32_t index)
{
    mover_command_fn_t original = (mover_command_fn_t)world_state.mover_open.original;
    uint8_t *mover = mover_at(world, index);
    int32_t  direction_before;
    int32_t  active_before;
    int32_t  direction_after;
    int32_t  active_after;
    float    pose_before;
    float    pose_after;

    snapshot_mover(mover, &direction_before, &active_before, &pose_before);
    original(world, index);
    snapshot_mover(mover, &direction_after, &active_after, &pose_after);

    /* Pressure plates call this EVERY FRAME. Only the change is an event. */
    if (mover != NULL && (direction_before != direction_after || active_before != active_after)) {
        diag_log_write("trg  mover %d (%s) OPEN: dir %s -> %s, active %d -> %d, pose %.2f",
                       (int)*(const int32_t *)(mover + MOVER_ID),
                       diag_numbered_name(diag_mover_types,
                                          *(const int32_t *)(mover + MOVER_TYPE)),
                       diag_numbered_name(diag_mover_directions, direction_before),
                       diag_numbered_name(diag_mover_directions, direction_after),
                       (int)active_before, (int)active_after, (double)pose_after);
    } else if (mover == NULL) {
        diag_log_write("trg  mover %d OPEN: index outside the mover table, engine ignores it",
                       (int)index);
    }
}

static void __cdecl hook_mover_close(void *world, int32_t index)
{
    mover_command_fn_t original = (mover_command_fn_t)world_state.mover_close.original;
    uint8_t *mover = mover_at(world, index);
    int32_t  direction_before;
    int32_t  active_before;
    int32_t  direction_after;
    int32_t  active_after;
    float    pose_before;
    float    pose_after;

    snapshot_mover(mover, &direction_before, &active_before, &pose_before);
    original(world, index);
    snapshot_mover(mover, &direction_after, &active_after, &pose_after);

    if (mover != NULL && direction_before != direction_after) {
        diag_log_write("trg  mover %d (%s) CLOSE: dir %s -> %s, pose %.2f",
                       (int)*(const int32_t *)(mover + MOVER_ID),
                       diag_numbered_name(diag_mover_types,
                                          *(const int32_t *)(mover + MOVER_TYPE)),
                       diag_numbered_name(diag_mover_directions, direction_before),
                       diag_numbered_name(diag_mover_directions, direction_after),
                       (double)pose_after);
    }
}

/* ============================================================================================
 * The mover call-site census. Level 3, and it patches nothing.
 *
 * The design for interpolating movers assumed the integrator runs once per frame from the per-frame
 * message, so that a bracket around the world draw would contain a mover's pose but not its
 * advance. A reading of the call graph says otherwise: of the nine callers, two are reached from
 * the draw, and if either of those reaches a mover first then the mover integrates inside the
 * proposed bracket and the whole design is unbuildable. Both ways it can fail present as "the fix
 * did nothing", which is the worst possible symptom to debug.
 *
 * So this measures it instead of arguing about it. Per call site: how often it is reached, and how
 * often the mover it was handed had a clock older than the time being passed in, which is exactly
 * the condition under which the function does anything at all.
 *
 * The call sites are discovered, not written down. The tick's own address comes from the pattern,
 * and every `call rel32` in the host's code section that targets it is a call site. The census then
 * reports the count it found, which is itself a finding on any build other than the one this was
 * derived from, and no address in this file has to be right for it to work.
 *
 * What that scan found on retail WMAIN.EXE, kept here as evidence rather than as data the code
 * reads. Nine `call rel32` sites target 0x00409170 and no absolute reference to that address exists
 * anywhere, so there is no call through a pointer to miss. The return addresses are
 *
 *   00408B44  00409799  0040A33D  0040A849  0040AF2F  0040AFE8  0040C3F7
 *   004194EC  00419B31
 *
 * and the last two are the ones the whole question turns on. 0x004194E7 sits inside
 * bapmap_polyToWorld 0x00419490 and 0x00419B2C inside bapvrt_transformWorld 0x004199B0, which
 * render_prepareFrame calls at 0x0043F5AC. If either of those reaches a mover before the per-frame
 * message does, that mover integrates inside the draw. Writing those nine addresses into the code
 * would have bought nothing and would have tied the census to one build.
 * ============================================================================================ */
#define MOVER_CALL_SITES_MAX 16u
#define MOVER_CENSUS_FRAMES  200u

typedef struct mover_census {
    bool            armed;
    bool            per_frame;
    const uint32_t *gate_cell;

    size_t          site_count;
    uintptr_t       site_return[MOVER_CALL_SITES_MAX];
    uint32_t        site_calls[MOVER_CALL_SITES_MAX];
    uint32_t        site_stale_clock[MOVER_CALL_SITES_MAX];

    uint32_t        calls_from_nowhere;
    uint32_t        gate_closed;
    uint32_t        frames;
    uint32_t        calls;
} mover_census_t;

static mover_census_t mover_census;

static void mover_census_find_call_sites(uintptr_t tick_address)
{
    uintptr_t text = host_image_text();
    size_t    size = host_image_text_size();
    size_t    index;

    if (text == 0 || size < 5 || !memory_is_readable_range(text, size)) {
        return;
    }

    for (index = 0; index + 5u <= size; ++index) {
        const uint8_t *at = (const uint8_t *)(text + index);
        int32_t        displacement;

        if (*at != 0xE8) {
            continue;
        }
        memcpy(&displacement, at + 1, sizeof(displacement));
        if ((uintptr_t)((intptr_t)(text + index) + 5 + displacement) != tick_address) {
            continue;
        }
        if (mover_census.site_count < MOVER_CALL_SITES_MAX) {
            mover_census.site_return[mover_census.site_count] = text + index + 5u;
            ++mover_census.site_count;
        }
    }
}

static void mover_census_record(const void *return_address, const uint8_t *mover, float now)
{
    size_t index;

    if (!mover_census.armed) {
        return;
    }
    ++mover_census.calls;

    if (mover_census.gate_cell != NULL && *mover_census.gate_cell != 0) {
        ++mover_census.gate_closed;
    }

    for (index = 0; index < mover_census.site_count; ++index) {
        if (mover_census.site_return[index] != (uintptr_t)return_address) {
            continue;
        }
        ++mover_census.site_calls[index];
        /* The condition the function's second early return is decided on, read before the call
         * because the call is what changes it. Equal means the mover has already integrated to
         * this time and the call will do nothing. */
        if (mover != NULL && *(const float *)(mover + MOVER_TIME_BASE) != now) {
            ++mover_census.site_stale_clock[index];
        }
        return;
    }
    ++mover_census.calls_from_nowhere;
}

static void mover_census_report(void)
{
    uintptr_t base = host_image_base();
    size_t    index;

    if (!mover_census.armed) {
        return;
    }
    ++mover_census.frames;
    if (mover_census.frames < MOVER_CENSUS_FRAMES) {
        return;
    }

    diag_log_write("mvr  census over %u frames: %u calls, %.2f per frame, %u through a closed "
                   "gate, %u from an unrecognised return address",
                   (unsigned)mover_census.frames, (unsigned)mover_census.calls,
                   (double)mover_census.calls / (double)mover_census.frames,
                   (unsigned)mover_census.gate_closed,
                   (unsigned)mover_census.calls_from_nowhere);

    for (index = 0; index < mover_census.site_count; ++index) {
        if (mover_census.site_calls[index] == 0) {
            continue;
        }
        diag_log_write("mvr    site %u return +%06X: %u calls, %u with a clock older than the "
                       "time passed in (%.2f calls per frame)",
                       (unsigned)index,
                       (unsigned)(mover_census.site_return[index] - base),
                       (unsigned)mover_census.site_calls[index],
                       (unsigned)mover_census.site_stale_clock[index],
                       (double)mover_census.site_calls[index] / (double)mover_census.frames);
    }

    mover_census.frames             = 0;
    mover_census.calls              = 0;
    mover_census.gate_closed        = 0;
    mover_census.calls_from_nowhere = 0;
    for (index = 0; index < mover_census.site_count; ++index) {
        mover_census.site_calls[index]       = 0;
        mover_census.site_stale_clock[index] = 0;
    }
}

static void mover_census_install(uintptr_t tick_address)
{
    uint32_t gate;

    if (tick_address == 0) {
        return;
    }

    mover_census_find_call_sites(tick_address);
    if (mover_census.site_count == 0) {
        log_warning("the mover census found no call site for the integrator at %08X, so there is "
                    "nothing to bucket against and it stays off",
                    (unsigned)tick_address);
        return;
    }

    if (memory_read_u32(tick_address + OFFSET_MOVER_GATE_CELL, &gate) &&
        memory_is_inside_image(gate, sizeof(uint32_t))) {
        mover_census.gate_cell = (const uint32_t *)(uintptr_t)gate;
    }

    mover_census.per_frame = frame_hook_add(diag_world_census_tick);
    mover_census.armed     = true;

    log_info("mover census armed: %u call sites for the integrator at %08X, gate cell %08X, "
             "reporting every %u frames%s",
             (unsigned)mover_census.site_count, (unsigned)tick_address,
             (unsigned)(uintptr_t)mover_census.gate_cell, (unsigned)MOVER_CENSUS_FRAMES,
             mover_census.per_frame
                 ? ""
                 : ". The per-frame hook is NOT available, so nothing will ever be reported");
}

static void __cdecl hook_mover_tick(void *mover, float now)
{
    mover_tick_fn_t original = (mover_tick_fn_t)world_state.mover_tick.original;
    uint8_t *record = (uint8_t *)mover;
    int32_t  direction_before = (record != NULL)
                              ? *(const int32_t *)(record + MOVER_DIRECTION) : -1;

    /* Taken first, and before the original runs, because both of the function's early returns are
     * decided on state the call itself may change. The detour replaced the prologue with a branch,
     * so the engine's own `call` pushed this address and it names the call site. */
    mover_census_record(_ReturnAddress(), record, now);

    original(mover, now);

    if (record != NULL && *(const int32_t *)(record + MOVER_DIRECTION) != direction_before) {
        diag_log_write("trg  mover %d (%s) advances: dir %s -> %s, pose %.2f",
                       (int)*(const int32_t *)(record + MOVER_ID),
                       diag_numbered_name(diag_mover_types,
                                          *(const int32_t *)(record + MOVER_TYPE)),
                       diag_numbered_name(diag_mover_directions, direction_before),
                       diag_numbered_name(diag_mover_directions,
                                          *(const int32_t *)(record + MOVER_DIRECTION)),
                       (double)*(const float *)(record + MOVER_POSE));
    }
}

/* ============================================================================================
 * The render-path census: how often the two known callers of the mover census above are entered,
 * not what they do once inside. Level 4, rides on nothing else, arms independently of the mover
 * census so it can answer on its own whether an explosion is "this function ran too many times
 * this frame" or "one run of it walked more than it should have"; the mover census cannot tell
 * those apart because it only sees calls to the integrator, not to its own two callers.
 * ============================================================================================ */
#define RENDER_CENSUS_FRAMES 200u

typedef struct render_census {
    bool     armed;
    bool     per_frame;
    uint32_t poly_to_world_calls;
    uint32_t transform_world_calls;
    uint32_t frames;
} render_census_t;

static render_census_t render_census;

static void render_census_report(void)
{
    if (!render_census.armed) {
        return;
    }
    ++render_census.frames;
    if (render_census.frames < RENDER_CENSUS_FRAMES) {
        return;
    }

    diag_log_write("rdr  census over %u frames: bapmap_polyToWorld %u calls (%.2f per frame), "
                   "bapvrt_transformWorld %u calls (%.2f per frame)",
                   (unsigned)render_census.frames,
                   (unsigned)render_census.poly_to_world_calls,
                   (double)render_census.poly_to_world_calls / (double)render_census.frames,
                   (unsigned)render_census.transform_world_calls,
                   (double)render_census.transform_world_calls / (double)render_census.frames);

    render_census.frames                 = 0;
    render_census.poly_to_world_calls    = 0;
    render_census.transform_world_calls  = 0;
}

/* ============================================================================================
 * Who calls bapmap_polyToWorld. Level 5, and the render census above is what earns it: with
 * bapvrt_transformWorld pinned at exactly one call a frame through the worst of a measured stall
 * while bapmap_polyToWorld climbed from a few hundred calls a frame to over five thousand, the
 * question stopped being "is the world transform re-entered" (measured, no) and became "what is
 * driving one function that is meant to run about once per visible object to run that many times
 * in one frame instead". This answers it the same way the mover census answers the same kind of
 * question about bapmap_tickMover: by finding every `call rel32` in the host's own .text that
 * targets bapmap_polyToWorld and bucketing live traffic against the site it actually came from.
 *
 * A read of the call graph (not written into the code, kept here as evidence) finds fifteen static
 * callers on retail WMAIN.EXE, clustered in two groups: eleven sit close together between 0x40c464
 * and 0x40e672, which by their addresses alone look like one family of per-object-type draw
 * routines (the engine dispatches drawing by object kind, and this is the shape that dispatch
 * takes in the binary); the other four are further out, at 0x42aafe, 0x43a5ff, 0x456e8b and
 * 0x457032. Which of those a live session actually goes through, and in what proportion, is
 * exactly what a call count without a call site cannot say, which is why the mover census took
 * the same approach rather than trusting a hand-written list. */
#define POLY_CALL_SITES_MAX 24u
#define POLY_CENSUS_FRAMES  200u

typedef struct poly_census {
    bool            armed;
    bool            per_frame;

    size_t          site_count;
    uintptr_t       site_return[POLY_CALL_SITES_MAX];
    uint32_t        site_calls[POLY_CALL_SITES_MAX];

    uint32_t        calls_from_nowhere;
    uint32_t        frames;
    uint32_t        calls;
} poly_census_t;

static poly_census_t poly_census;

static void poly_census_find_call_sites(uintptr_t poly_address)
{
    uintptr_t text = host_image_text();
    size_t    size = host_image_text_size();
    size_t    index;

    if (text == 0 || size < 5 || !memory_is_readable_range(text, size)) {
        return;
    }

    for (index = 0; index + 5u <= size; ++index) {
        const uint8_t *at = (const uint8_t *)(text + index);
        int32_t        displacement;

        if (*at != 0xE8) {
            continue;
        }
        memcpy(&displacement, at + 1, sizeof(displacement));
        if ((uintptr_t)((intptr_t)(text + index) + 5 + displacement) != poly_address) {
            continue;
        }
        if (poly_census.site_count < POLY_CALL_SITES_MAX) {
            poly_census.site_return[poly_census.site_count] = text + index + 5u;
            ++poly_census.site_count;
        }
    }
}

static void poly_census_record(const void *return_address)
{
    size_t index;

    if (!poly_census.armed) {
        return;
    }
    ++poly_census.calls;

    for (index = 0; index < poly_census.site_count; ++index) {
        if (poly_census.site_return[index] != (uintptr_t)return_address) {
            continue;
        }
        ++poly_census.site_calls[index];
        return;
    }
    ++poly_census.calls_from_nowhere;
}

static void poly_census_report(void)
{
    uintptr_t base = host_image_base();
    size_t    index;

    if (!poly_census.armed) {
        return;
    }
    ++poly_census.frames;
    if (poly_census.frames < POLY_CENSUS_FRAMES) {
        return;
    }

    diag_log_write("ply  census over %u frames: %u calls, %.2f per frame, %u from an "
                   "unrecognised return address",
                   (unsigned)poly_census.frames, (unsigned)poly_census.calls,
                   (double)poly_census.calls / (double)poly_census.frames,
                   (unsigned)poly_census.calls_from_nowhere);

    for (index = 0; index < poly_census.site_count; ++index) {
        if (poly_census.site_calls[index] == 0) {
            continue;
        }
        diag_log_write("ply    site %u return +%06X: %u calls (%.2f per frame)",
                       (unsigned)index, (unsigned)(poly_census.site_return[index] - base),
                       (unsigned)poly_census.site_calls[index],
                       (double)poly_census.site_calls[index] / (double)poly_census.frames);
    }

    poly_census.frames             = 0;
    poly_census.calls              = 0;
    poly_census.calls_from_nowhere = 0;
    for (index = 0; index < poly_census.site_count; ++index) {
        poly_census.site_calls[index] = 0;
    }
}

static void poly_census_install(uintptr_t poly_address)
{
    if (poly_address == 0) {
        return;
    }

    poly_census_find_call_sites(poly_address);
    if (poly_census.site_count == 0) {
        log_warning("the poly-to-world census found no call site for %08X, so there is nothing "
                    "to bucket against and it stays off",
                    (unsigned)poly_address);
        return;
    }

    poly_census.per_frame = frame_hook_add(diag_world_census_tick);
    poly_census.armed     = true;

    log_info("poly-to-world census armed: %u call sites for %08X, reporting every %u frames%s",
             (unsigned)poly_census.site_count, (unsigned)poly_address,
             (unsigned)POLY_CENSUS_FRAMES,
             poly_census.per_frame
                 ? ""
                 : ". The per-frame hook is NOT available, so nothing will ever be reported");
}

static void __cdecl hook_poly_to_world(void *world, void *object, float *out_pos, void *out_extra)
{
    poly_to_world_fn_t original = (poly_to_world_fn_t)world_state.poly_to_world.original;

    if (render_census.armed) {
        ++render_census.poly_to_world_calls;
    }
    /* Taken before the original runs, so the address named is always the caller's own, never
     * anything the call itself might change. Nothing else in this DLL detours this function's
     * own entry, so unlike the mover census there is no second hook to be mistaken for the
     * caller here. */
    poly_census_record(_ReturnAddress());
    original(world, object, out_pos, out_extra);
}

static void __cdecl hook_transform_world(void *world)
{
    transform_world_fn_t original = (transform_world_fn_t)world_state.transform_world.original;

    if (render_census.armed) {
        ++render_census.transform_world_calls;
    }
    original(world);
}

static void render_census_install(void)
{
    render_census.per_frame = frame_hook_add(diag_world_census_tick);
    render_census.armed     = true;

    log_info("render census armed: counting entries to bapmap_polyToWorld and "
             "bapvrt_transformWorld, reporting every %u frames%s",
             (unsigned)RENDER_CENSUS_FRAMES,
             render_census.per_frame
                 ? ""
                 : ". The per-frame hook is NOT available, so nothing will ever be reported");
}

/* ============================================================================================
 * Who calls the two traces. Level 6. The poly-to-world census named FUN_0040e06b's own call site
 * as the dominant one during the stall, and FUN_0040e06b has exactly one job: it is the per-candidate
 * distance test both FUN_0040be00 (the general, mover-aware line trace) and FUN_0040c2be (the floor
 * trace) run inside the same shared broadphase walk. Neither of those two is a callee bapmap_polyToWorld
 * chooses for itself; they are the reason it runs at all in this path, so the next question is
 * which of THEIR OWN callers, spread across player movement, AI and physics, is the one actually
 * asking for a trace thousands of times in one frame. One shared shape, two independent instances,
 * the same reason the poly-to-world census above did not just reuse the mover census's own state. */
#define CALL_CENSUS_SITES_MAX 24u
#define CALL_CENSUS_FRAMES    200u

typedef struct call_census {
    const char     *tag;      /* the log line prefix, e.g. "tgc" */
    const char     *what;     /* named in the arm/report lines */
    bool            armed;
    bool            per_frame;
    size_t          site_count;
    uintptr_t       site_return[CALL_CENSUS_SITES_MAX];
    uint32_t        site_calls[CALL_CENSUS_SITES_MAX];
    uint32_t        calls_from_nowhere;
    uint32_t        frames;
    uint32_t        calls;
} call_census_t;

static void call_census_find_call_sites(call_census_t *census, uintptr_t target_address)
{
    uintptr_t text = host_image_text();
    size_t    size = host_image_text_size();
    size_t    index;

    if (text == 0 || size < 5 || !memory_is_readable_range(text, size)) {
        return;
    }

    for (index = 0; index + 5u <= size; ++index) {
        const uint8_t *at = (const uint8_t *)(text + index);
        int32_t        displacement;

        if (*at != 0xE8) {
            continue;
        }
        memcpy(&displacement, at + 1, sizeof(displacement));
        if ((uintptr_t)((intptr_t)(text + index) + 5 + displacement) != target_address) {
            continue;
        }
        if (census->site_count < CALL_CENSUS_SITES_MAX) {
            census->site_return[census->site_count] = text + index + 5u;
            ++census->site_count;
        }
    }
}

static void call_census_record(call_census_t *census, const void *return_address)
{
    size_t index;

    if (!census->armed) {
        return;
    }
    ++census->calls;

    for (index = 0; index < census->site_count; ++index) {
        if (census->site_return[index] != (uintptr_t)return_address) {
            continue;
        }
        ++census->site_calls[index];
        return;
    }
    ++census->calls_from_nowhere;
}

static void call_census_report(call_census_t *census)
{
    uintptr_t base = host_image_base();
    size_t    index;

    if (!census->armed) {
        return;
    }
    ++census->frames;
    if (census->frames < CALL_CENSUS_FRAMES) {
        return;
    }

    diag_log_write("%s  census over %u frames: %s: %u calls, %.2f per frame, %u from an "
                   "unrecognised return address",
                   census->tag, (unsigned)census->frames, census->what, (unsigned)census->calls,
                   (double)census->calls / (double)census->frames,
                   (unsigned)census->calls_from_nowhere);

    for (index = 0; index < census->site_count; ++index) {
        if (census->site_calls[index] == 0) {
            continue;
        }
        diag_log_write("%s    site %u return +%06X: %u calls (%.2f per frame)",
                       census->tag, (unsigned)index, (unsigned)(census->site_return[index] - base),
                       (unsigned)census->site_calls[index],
                       (double)census->site_calls[index] / (double)census->frames);
    }

    census->frames             = 0;
    census->calls              = 0;
    census->calls_from_nowhere = 0;
    for (index = 0; index < census->site_count; ++index) {
        census->site_calls[index] = 0;
    }
}

static void call_census_install(call_census_t *census, uintptr_t target_address)
{
    if (target_address == 0) {
        return;
    }

    call_census_find_call_sites(census, target_address);
    if (census->site_count == 0) {
        log_warning("the %s census found no call site for %08X, so there is nothing to bucket "
                    "against and it stays off",
                    census->what, (unsigned)target_address);
        return;
    }

    census->armed = true;

    log_info("%s census armed: %u call sites for %08X, reporting every %u frames%s",
             census->what, (unsigned)census->site_count, (unsigned)target_address,
             (unsigned)CALL_CENSUS_FRAMES,
             census->per_frame
                 ? ""
                 : ". The per-frame hook is NOT available, so nothing will ever be reported");
}

static call_census_t trace_general_census = { "tgc", "general trace (FUN_0040be00)" };
static call_census_t trace_floor_census   = { "tfc", "floor trace (FUN_0040c2be)" };

static void trace_general_census_report(void) { call_census_report(&trace_general_census); }
static void trace_floor_census_report(void)   { call_census_report(&trace_floor_census); }

static void __cdecl hook_trace_general(void *context, float *result)
{
    trace_general_fn_t original = (trace_general_fn_t)world_state.trace_general.original;

    call_census_record(&trace_general_census, _ReturnAddress());
    original(context, result);
}

static double __cdecl hook_trace_floor(void *context)
{
    trace_floor_fn_t original = (trace_floor_fn_t)world_state.trace_floor.original;

    call_census_record(&trace_floor_census, _ReturnAddress());
    return original(context);
}

static void trace_census_install(uintptr_t trace_general_address, uintptr_t trace_floor_address)
{
    trace_general_census.per_frame = frame_hook_add(diag_world_census_tick);
    call_census_install(&trace_general_census, trace_general_address);

    trace_floor_census.per_frame = frame_hook_add(diag_world_census_tick);
    call_census_install(&trace_floor_census, trace_floor_address);
}

static void diag_world_census_tick(void)
{
    mover_census_report();
    render_census_report();
    poly_census_report();
    trace_general_census_report();
    trace_floor_census_report();
}

/* ============================================================================================ */
static const char *actor_tag(const void *actor)
{
    static char buffers[2][64];
    static int  turn;
    const uint8_t *record = (const uint8_t *)actor;
    char          *buffer;

    turn = (turn + 1) & 1;
    buffer = buffers[turn];

    if (record == NULL) {
        _snprintf(buffer, sizeof(buffers[0]), "%s", "(null)");
        buffer[sizeof(buffers[0]) - 1] = '\0';
        return buffer;
    }
    _snprintf(buffer, sizeof(buffers[0]), "#%d \"%s\" [%s]",
              (int)*(const int32_t *)(record + CHARACTER_ENMY_INDEX),
              diag_safe_string((const char *)(record + CHARACTER_NAME), 12),
              diag_numbered_name(diag_enemy_states,
                                 *(const int32_t *)(record + CHARACTER_STATE)));
    buffer[sizeof(buffers[0]) - 1] = '\0';
    return buffer;
}

static void __cdecl hook_ai_set_mode(void *actor, int32_t new_mode)
{
    ai_set_mode_fn_t original = (ai_set_mode_fn_t)world_state.ai_set_mode.original;
    int32_t old_mode = (actor != NULL)
                     ? *(const int32_t *)((const uint8_t *)actor + CHARACTER_AI_MODE) : -1;

    original(actor, new_mode);
    if (actor == NULL) {
        return;
    }

    if (old_mode == new_mode) {
        diag_log_write("fsm  %s SetMode %d == the current mode -> only the mode timer is zeroed",
                       actor_tag(actor), (int)new_mode);
    } else {
        diag_log_write("fsm  %s SetMode %d -> %d  (PUSH onto the 4-deep stack)",
                       actor_tag(actor), (int)old_mode, (int)new_mode);
    }
}

static void __cdecl hook_ai_return_mode(void *actor)
{
    ai_return_mode_fn_t original = (ai_return_mode_fn_t)world_state.ai_return_mode.original;
    int32_t old_mode = (actor != NULL)
                     ? *(const int32_t *)((const uint8_t *)actor + CHARACTER_AI_MODE) : -1;

    original(actor);
    if (actor != NULL) {
        diag_log_write("fsm  %s ReturnMode %d -> %d  (POP; the floor refills from the start mode)",
                       actor_tag(actor), (int)old_mode,
                       (int)*(const int32_t *)((const uint8_t *)actor + CHARACTER_AI_MODE));
    }
}

/* External linkage so the optimiser cannot remove it, its only caller is inline assembly. */
void __stdcall diag_on_ai_opcode(int opcode, void *actor)
{
    const char *name = diag_name_of(diag_fsm_opcodes, (int32_t)opcode);

    diag_log_write("fsm  %s op %03X %s", actor_tag(actor), (unsigned)opcode,
                   (name != NULL) ? name : "(no handler)");
}

/* NAKED. Saves the GP registers, the flags and the complete x87 state, the detour sits in the
 * middle of ai_run, ebp points at ITS frame, and the logger uses the CRT. fnsave reinitialises
 * the FPU as a side effect; frstor restores stack AND control word. */
static __declspec(naked) void hook_ai_opcode(void)
{
    __asm {
        pushad
        pushfd
        sub     esp, 112
        fnsave  [esp]
        movsx   eax, word ptr [ebp - 01Ch]     /* the resolved opcode, offset proven by the pattern */
        mov     ecx, [ebp + 8]                 /* ai_run(character *actor)                            */
        push    ecx
        push    eax
        call    diag_on_ai_opcode              /* __stdcall: cleans up after itself                   */
        frstor  [esp]
        add     esp, 112
        popfd
        popad
        jmp     dword ptr [opcode_trampoline]
    }
}

/* ============================================================================================ */
int diag_trigger_install(int trigger_level)
{
    int installed = 0;

    if (trigger_level <= 0) {
        return 0;
    }
    resolve_sites_once();

    installed += diag_install_observer(sites, SITE_MOVER_OPEN, &world_state.mover_open,
                                       (const void *)hook_mover_open, MOVER_OPEN_PROLOGUE,
                                       "mover OPEN - debounced, because pressure plates call it "
                                       "every frame") ? 1 : 0;
    installed += diag_install_observer(sites, SITE_MOVER_CLOSE, &world_state.mover_close,
                                       (const void *)hook_mover_close, MOVER_CLOSE_PROLOGUE,
                                       "mover CLOSE (the script command)") ? 1 : 0;
    if (trigger_level >= 2) {
        installed += diag_install_observer(sites, SITE_MOVER_TICK, &world_state.mover_tick,
                                           (const void *)hook_mover_tick, MOVER_TICK_PROLOGUE,
                                           "phase changes of the mover integrator (a door closing "
                                           "by itself and so on)") ? 1 : 0;
    }
    /* Level 3 rides on level 2's detour rather than installing a second one, so the census cannot
     * be armed without the hook that feeds it. */
    if (trigger_level >= 3 && world_state.mover_tick.original != NULL) {
        mover_census_install(sites[SITE_MOVER_TICK].address);
    }
    if (trigger_level >= 4) {
        bool poly_ok = diag_install_observer(sites, SITE_POLY_TO_WORLD, &world_state.poly_to_world,
                                             (const void *)hook_poly_to_world,
                                             POLY_TO_WORLD_PROLOGUE,
                                             "entries to bapmap_polyToWorld");
        bool xform_ok = diag_install_observer(sites, SITE_TRANSFORM_WORLD,
                                              &world_state.transform_world,
                                              (const void *)hook_transform_world,
                                              TRANSFORM_WORLD_PROLOGUE,
                                              "entries to bapvrt_transformWorld");
        installed += poly_ok ? 1 : 0;
        installed += xform_ok ? 1 : 0;
        if (poly_ok || xform_ok) {
            render_census_install();
        }
        /* Level 5 rides on the same detour as level 4's poly_to_world observer, for the same
         * reason level 3's mover census rides on level 2's tick detour. */
        if (trigger_level >= 5 && poly_ok) {
            poly_census_install(sites[SITE_POLY_TO_WORLD].address);
        }
        if (trigger_level >= 6) {
            bool trace_general_ok = diag_install_observer(sites, SITE_TRACE_GENERAL,
                                                           &world_state.trace_general,
                                                           (const void *)hook_trace_general,
                                                           TRACE_GENERAL_PROLOGUE,
                                                           "entries to the general line trace "
                                                           "(FUN_0040be00)");
            bool trace_floor_ok = diag_install_observer(sites, SITE_TRACE_FLOOR,
                                                         &world_state.trace_floor,
                                                         (const void *)hook_trace_floor,
                                                         TRACE_FLOOR_PROLOGUE,
                                                         "entries to the floor trace "
                                                         "(FUN_0040c2be)");
            installed += trace_general_ok ? 1 : 0;
            installed += trace_floor_ok ? 1 : 0;
            trace_census_install(trace_general_ok ? sites[SITE_TRACE_GENERAL].address : 0,
                                 trace_floor_ok ? sites[SITE_TRACE_FLOOR].address : 0);
        }
    }
    return installed;
}

int diag_fsm_install(int fsm_level)
{
    int installed = 0;

    if (fsm_level <= 0) {
        return 0;
    }
    resolve_sites_once();

    installed += diag_install_observer(sites, SITE_AI_SET_MODE, &world_state.ai_set_mode,
                                       (const void *)hook_ai_set_mode, AI_SET_MODE_PROLOGUE,
                                       "opcode 0x400 Set AI Mode (PUSH)") ? 1 : 0;
    installed += diag_install_observer(sites, SITE_AI_RETURN_MODE, &world_state.ai_return_mode,
                                       (const void *)hook_ai_return_mode,
                                       AI_RETURN_MODE_PROLOGUE,
                                       "opcode 0x401 Return Mode (POP)") ? 1 : 0;

    if (fsm_level >= 2) {
        if (diag_install_observer(sites, SITE_AI_OPCODE, &world_state.ai_opcode,
                                  (const void *)hook_ai_opcode, AI_OPCODE_PROLOGUE,
                                  "EVERY executed opcode, a detour INTO ai_run, expensive, the "
                                  "x87 state is saved")) {
            opcode_trampoline = world_state.ai_opcode.original;
            ++installed;
        }
    }
    return installed;
}
