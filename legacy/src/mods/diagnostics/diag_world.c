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

enum {
    SITE_MOVER_OPEN,
    SITE_MOVER_CLOSE,
    SITE_MOVER_TICK,
    SITE_AI_SET_MODE,
    SITE_AI_RETURN_MODE,
    SITE_AI_OPCODE,
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
    SIGNATURE_ENTRY_DETOUR("ai_opcode",      SIG_AI_OPCODE,      AI_OPCODE_PROLOGUE)
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

typedef struct diag_world_state {
    bool     sites_resolved;
    detour_t mover_open;
    detour_t mover_close;
    detour_t mover_tick;
    detour_t ai_set_mode;
    detour_t ai_return_mode;
    detour_t ai_opcode;
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

    mover_census.per_frame = frame_hook_add(mover_census_report);
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
