/* diag_world.c: the world's own traffic, movers and the AI state machine.
 *
 * ==================================== SIZE NOTE ===============================================
 *
 * This file is over 600 lines. What is left once the censuses moved out is the byte evidence and
 * the observers that rest on it: about a third of the file is the ten signature patterns and the
 * disassembly that proves them, and most of the rest is the reasoning behind two things a reader
 * cannot see in the code, why the opcode hook may detour into the middle of a function, and why
 * the integrator has two early returns that have to be told apart.
 *
 * The seam this note used to name has been taken, in two cuts. diag_world_mover_census.c is the
 * one it named: mover_census_* touched no other state here and shared only the tick detour that
 * feeds it. diag_world_path_census.c is the same cut applied to the four draw path and trace
 * censuses, which as one file with the mover census would have come back near the limit. Each
 * census file owns its counters and its reports; every hook stays here with the detour and the
 * pattern it belongs to, and calls across.
 *
 * The next seam is NOT the mover-versus-AI split, which looks obvious and was measured and
 * rejected: both halves share resolve_sites_once, the signature table and diag_install_observer,
 * so cutting there puts one table behind a translation unit boundary from half its users and buys
 * nothing.
 */
#include "diag_world.h"

#include "diag_install.h"
#include "diag_log.h"
#include "diag_names.h"
#include "diag_world_mover_census.h"
#include "diag_world_path_census.h"

#include "common/detour.h"
#include "common/signature.h"

#include <intrin.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

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
 * would not show up in an operand scan. The mover census counts the case instead of assuming it
 * away, which costs one comparison per call.
 *
 * The gate cell's operand sits at +0x08 and is read out of the matched pattern rather than being
 * written down, so the census works on any build the pattern resolves on. */
static const uint8_t SIG_MOVER_TICK[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x2C, 0x83, 0x3D, 0xCC, 0x5F, 0x5B, 0x00,
    0x00, 0x0F, 0x85, 0xC7, 0x04, 0x00, 0x00, 0x8B
};
#define MOVER_TICK_PROLOGUE 6u

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
 * One of the two known render-path callers of bapmap_tickMover (see the mover census): decompiled
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
 * loser would be whichever of the two loaded later, including the census that detour feeds. The
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

static void __cdecl hook_poly_to_world(void *world, void *object, float *out_pos, void *out_extra)
{
    poly_to_world_fn_t original = (poly_to_world_fn_t)world_state.poly_to_world.original;

    render_census_count_poly_to_world();
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

    render_census_count_transform_world();
    original(world);
}

static void __cdecl hook_trace_general(void *context, float *result)
{
    trace_general_fn_t original = (trace_general_fn_t)world_state.trace_general.original;

    trace_general_census_record(_ReturnAddress());
    original(context, result);
}

static double __cdecl hook_trace_floor(void *context)
{
    trace_floor_fn_t original = (trace_floor_fn_t)world_state.trace_floor.original;

    trace_floor_census_record(_ReturnAddress());
    return original(context);
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
                                       "mover OPEN, debounced because pressure plates call it "
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
