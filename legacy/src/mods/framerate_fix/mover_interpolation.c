/* mover_interpolation.c: draw doors, lifts and platforms between the simulation steps.
 *
 * SIZE NOTE. This file runs over six hundred lines and the large majority of it is comment. The
 * executable part is a side table, two short hooks and one install function. What is long is the
 * evidence: the census that found the four consumers of a mover pose, the disassembly of each of
 * them, the latch the substep alpha has to be read through, the subnode layout, and the three
 * designs that were tried and rejected: detouring the shared callee, trusting the engine's own
 * previous pose, and deciding a track wrap on geometry. The seam taken is mover_blend.c, which
 * holds all of the arithmetic, none of the engine, and is tested on its own. A second seam between
 * the tick side and the draw side was looked at and rejected: both are the same side table, so
 * splitting them would move the table and its hash into a header and would separate the snapshot
 * from the only code that reads it.
 *
 * ================================ What is actually broken =====================================
 *
 * bapmap_tickMover integrates a mover from the world clock, and only the substep loop advances
 * that clock. So a mover's speed is correct at any frame rate and its pacing is correct, and the
 * picture in between is missing: at 160 frames a second four frames in five find the clock exactly
 * where they left it, return immediately, and draw the door where the previous frame drew it, and
 * the fifth frame moves it a whole 1/32 s of travel at once.
 *
 * ============================ Why nothing here has to be restored =============================
 *
 * The drawn matrix is an argument, not a fetch. At the site that feeds the world draw:
 *
 *     00419B4F  A1 A8B55B00        mov  eax,[0x005BB5A8]      the mover record
 *     00419B54  8D 8490 84000000   lea  eax,[eax+edx*4+0x84]  into the subnode array at +0x84
 *     00419B5B  A3 E4F95B00        mov  [0x005BF9E4],eax      the subnode pointer is parked here
 *     00419B60  83 C0 14           add  eax,0x14              subNode->world
 *     00419B63  50                 push eax                   argument three
 *     00419B64  51                 push ecx                   argument two
 *     00419B65  68 C8B55B00        push 0x005BB5C8            argument one, the destination
 *     00419B6A  E8 <rel32>         call bapmap_matMul3 0x0047DAD6
 *               83 C4 0C           add  esp,0xC               the caller cleans up: cdecl, three
 *
 * So an interpolated copy in a local reaches the draw without any record being written. That is
 * the same convention bapobj_drawAll has used since 1999, and it is why this feature needs no
 * bracket around the draw and no restore afterwards. The simulation never sees a blended pose.
 *
 * ============================== The four consumers, by census =================================
 *
 * bapmap_matMul3 0x0047DAD6 has twelve call sites in the image. Exactly three of them push the
 * shared destination 0x005BB5C8 as argument one and a mover subnode's world as argument three:
 *
 *     pattern at 0x00419B54   call 0x00419B6A   in bapvrt_transformWorld 0x004199B0
 *     pattern at 0x0041B5D2   call 0x0041B5E8   in 0x0041B070
 *     pattern at 0x0041BFEB   call 0x0041C002   in rdMaterial_pageStage
 *
 * A fourth consumer takes the same matrix into mat34_invertRigid 0x0047CEB9, immediately after the
 * first of those calls and out of the pointer parked at 0x005BF9E4:
 *
 *     00419B6F  8B 15 E4F95B00     mov  edx,[0x005BF9E4]
 *     00419B75  83 C4 0C           add  esp,0xC
 *     00419B78  83 C2 14           add  edx,0x14
 *     00419B7B  52                 push edx                   argument two
 *     00419B7C  68 48B55B00        push 0x005BB548            argument one
 *     00419B81  E8 <rel32>         call mat34_invertRigid
 *
 * Its product reaches the facing test at 0x00419D11 through the eye expressed in subnode local
 * space, so it belongs in the set. Interpolating what is drawn while deciding facing against a
 * different matrix is how a moving surface flickers.
 *
 * Redirecting one of the four and leaving the others would interpolate part of a door and leave
 * the rest of it stepped, which is a worse picture than the stepped one this replaces. It is all
 * four or none, and the rollback below is what enforces that.
 *
 * The rejected alternative was to detour bapmap_matMul3 itself and work out from the arguments
 * which caller this is. That is one patch instead of four, and it puts this DLL in front of all
 * twelve call sites on every frame in order to act on three of them.
 */
#include "mover_interpolation.h"

#include "mover_blend.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* --- the three matrix products ---------------------------------------------------------------- *
 *
 * Each pattern ends AT the call opcode and stops before the displacement that follows it, so it
 * still matches after the redirect has been written and resolving a second time cannot go wrong.
 * The call address is therefore the resolved site plus the pattern length minus one, which lands
 * on 0x00419B6A, 0x0041B5E8 and 0x0041C002. All three were confirmed to be `E8 rel32` before the
 * design rested on it, and patch_read_call_target checks the opcode again at install time.
 *
 * B differs from A only in registers: the base and index of the `lea`, and the register pushed as
 * argument two. C starts at the same store into the parked subnode pointer and differs in argument
 * two, which it forms out of the global at 0x006F83E4 plus 8 rather than out of a register. All
 * three end with the same destination push, which is what identifies them as the mover set. */
static const uint8_t SIG_MOVER_MATMUL_A[] = {
    0x8D, 0x84, 0x90, 0x84, 0x00, 0x00, 0x00,
    0xA3, 0xE4, 0xF9, 0x5B, 0x00,
    0x83, 0xC0, 0x14, 0x50, 0x51, 0x68, 0xC8, 0xB5, 0x5B, 0x00, 0xE8
};
static const uint8_t SIG_MOVER_MATMUL_B[] = {
    0x8D, 0x84, 0x8F, 0x84, 0x00, 0x00, 0x00,
    0xA3, 0xE4, 0xF9, 0x5B, 0x00,
    0x83, 0xC0, 0x14, 0x50, 0x52, 0x68, 0xC8, 0xB5, 0x5B, 0x00, 0xE8
};
static const uint8_t SIG_MOVER_MATMUL_C[] = {
    0xA3, 0xE4, 0xF9, 0x5B, 0x00,
    0x83, 0xC0, 0x14, 0x50, 0xA1, 0xE4, 0x83, 0x6F, 0x00,
    0x83, 0xC0, 0x08, 0x50, 0x68, 0xC8, 0xB5, 0x5B, 0x00, 0xE8
};

/* --- the invert, the fourth consumer ---------------------------------------------------------- *
 * The disassembly is in the file header. Same rule: the pattern ends at the E8, so the call is at
 * 0x00419B81. patch_redirect_call keeps that E8 and rewrites only its displacement, which is why
 * the callee itself is never touched and neither are the other callers: nine of the twelve calls
 * to bapmap_matMul3, and two of the three calls to mat34_invertRigid. */
static const uint8_t SIG_MOVER_INVERT[] = {
    0x8B, 0x15, 0xE4, 0xF9, 0x5B, 0x00,
    0x83, 0xC4, 0x0C, 0x83, 0xC2, 0x14, 0x52, 0x68, 0x48, 0xB5, 0x5B, 0x00, 0xE8
};

/* --- bapmap_tickMover 0x00409170, where the previous pose is captured ------------------------- *
 *
 * The same function the diagnostics DLL observes, and both may hold it. The detours chain and both
 * sites are declared in the detour form, so neither owner is the silent loser. This one is here
 * because the pose it leaves behind is what the draw interpolates from. */
static const uint8_t SIG_MOVER_TICK[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x2C, 0x83, 0x3D, 0xCC, 0x5F, 0x5B, 0x00,
    0x00, 0x0F, 0x85, 0xC7, 0x04, 0x00, 0x00, 0x8B
};
#define MOVER_TICK_PROLOGUE 6u

/* --- the alpha, which has to come through the engine's own latch ------------------------------ *
 *
 * In bapobj_drawAll:
 *
 *     00411063  A1 1C878600        mov  eax,[0x0086871C]        the global alpha
 *     00411068  89 85 E0FBFFFF     mov  [ebp-0x420],eax
 *     0041106E  83 3D E8AC5B00 00  cmp  dword [0x005BACE8],0    the gate
 *     00411075  74 0C              je   0x00411083
 *     00411077  8B 0D E4AC5B00     mov  ecx,[0x005BACE4]        the latched alpha
 *     0041107D  89 8D E0FBFFFF     mov  [ebp-0x420],ecx
 *     00411083  8B 95 E0FBFFFF     mov  edx,[ebp-0x420]
 *     00411089  89 15 E4AC5B00     mov  [0x005BACE4],edx
 *
 * Read the tail carefully. When the gate is clear the latch cell is refreshed from the global, and
 * when the gate is set the latch cell is read and written straight back unchanged. So the latch
 * freezes while the gate is armed instead of tracking the global, which is the engine's own guard
 * against a simulation that is not advancing. Reading the global directly would let movers drift
 * on through a menu or a cutscene while everything else stands still.
 *
 * The mover draw runs at step 6 of the frame and the object draw at step 7, so the latch cell
 * holds the previous frame's value when this code reads it. That does not matter: while the gate
 * is armed the cell is not changing, which is the whole point of it, and while the gate is clear
 * the expression takes the global instead.
 *
 * The three absolute operands are wildcarded and the addresses are read out of the matched
 * instruction. They were written out literally once, and that was a defect rather than a matter of
 * taste: these cells sit at different addresses in obi.exe, so a pattern that carries them can
 * only ever match the two original builds, and it fails in the way that is hardest to read, as
 * zero matches, which is indistinguishable from an unsupported executable. Reading the operand is
 * also what keeps the anchor working under forced ASLR and after another patch has edited a
 * neighbouring immediate. */
static const uint8_t SIG_SUBSTEP_ALPHA_LATCH[] = {
    0xA1, 0x00, 0x00, 0x00, 0x00,
    0x89, 0x85, 0xE0, 0xFB, 0xFF, 0xFF,
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x74, 0x0C,
    0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t MSK_SUBSTEP_ALPHA_LATCH[] = {
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
};
/* Each of these is the offset of the OPERAND, not of the instruction that carries it, and those
 * two are what an off by N here looks like: 0x14 reaches the `8B 0D` of the load rather than the
 * address behind it and yields 0xACE40D8B, which is not in the image. The opcode bytes in front of
 * each operand are verified below so that the mistake reports itself instead of reading as an
 * unsupported build. */
#define OFFSET_ALPHA_GLOBAL 0x01u       /* behind A1        mov eax,[abs32]   */
#define OFFSET_ALPHA_GATE   0x0Du       /* behind 83 3D     cmp dword [abs32] */
#define OFFSET_ALPHA_LATCH  0x16u       /* behind 8B 0D     mov ecx,[abs32]   */

enum {
    SITE_MATMUL_A,
    SITE_MATMUL_B,
    SITE_MATMUL_C,
    SITE_INVERT,
    SITE_TICK,
    SITE_ALPHA,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY("mover_matmul_a", SIG_MOVER_MATMUL_A),
    SIGNATURE_ENTRY("mover_matmul_b", SIG_MOVER_MATMUL_B),
    SIGNATURE_ENTRY("mover_matmul_c", SIG_MOVER_MATMUL_C),
    SIGNATURE_ENTRY("mover_invert",   SIG_MOVER_INVERT),
    SIGNATURE_ENTRY_DETOUR("mover_tick", SIG_MOVER_TICK, MOVER_TICK_PROLOGUE),
    SIGNATURE_ENTRY_MASKED("substep_alpha_latch", SIG_SUBSTEP_ALPHA_LATCH,
                           MSK_SUBSTEP_ALPHA_LATCH)
};

/* Where these six resolve, checked offline against all three builds. On the retail WMAIN.EXE and
 * on wmain.exe every one of them resolves uniquely. Five of the six carry an absolute operand of
 * their own, so they cannot match obi.exe, which is a recompile; there the redirects are never
 * written and the feature declines as a whole. The sixth is the alpha latch and it is address
 * free, so it may well match on the recompile, but one resolved site on its own installs nothing.
 * The German retail executable is byte identical to the English one and is the same build. */

/* The three redirected matrix products plus the invert. The call opcode sits at the last byte of
 * each pattern, so the site plus the pattern length minus one is the E8. */
#define REDIRECT_COUNT 4
static const size_t REDIRECT_SITE[REDIRECT_COUNT] = {
    SITE_MATMUL_A, SITE_MATMUL_B, SITE_MATMUL_C, SITE_INVERT
};
static const size_t REDIRECT_PATTERN_SIZE[REDIRECT_COUNT] = {
    sizeof(SIG_MOVER_MATMUL_A), sizeof(SIG_MOVER_MATMUL_B),
    sizeof(SIG_MOVER_MATMUL_C), sizeof(SIG_MOVER_INVERT)
};

/* Mover and subnode layout, each one taken from a site that uses it.
 *
 * The subnode array is at mover+0x84 and its stride is 0x9C, both visible in the `lea` in the file
 * header and in the copy loop the snapshot below mirrors. Inside a subnode, `world` at +0x14 is a
 * 3x3 at 0x14..0x37 followed by a translation vec3 at 0x38..0x43. That split is proven by the
 * engine's own previous pose copy at 0x00409AC0, which reads [+0x38] into prevWorldT at [+0x7C],
 * and 0x38 is 0x14 plus nine floats.
 *
 * The engine's own previous pose pair is deliberately NOT used, and the reason is the type 7 path:
 * it builds subnode zero's world out of an euler and a translation held on the mover record,
 * broadcasts that to the other subnodes, and leaves subnode zero's own prevWorldT, prevWorldEuler
 * and worldEuler unwritten, so the broadcast propagates stale values. A snapshot taken here is
 * true on every path a mover can take. */
#define MOVER_SUBNODE_COUNT   0x24
#define MOVER_SUBNODE_ARRAY   0x84
#define MOVER_POSE            0x2C
#define MOVER_TIME_BASE       0x30
#define SUBNODE_STRIDE        0x9C
#define SUBNODE_WORLD         0x14
#define MOVER_TRACK_LENGTH    0x28

/* One entry per subnode the game has ticked, keyed by its address. Open addressing on the pointer,
 * so a lookup on the draw path is a couple of loads. The table is deliberately much larger than
 * the 24 live movers a level was measured with, because the bound belongs to the data rather than
 * to that one observation, and a full table degrades to "not interpolated" rather than to a wrong
 * answer. */
#define SLOT_COUNT 512u

typedef struct mover_slot {
    const void *subnode;
    bool        usable;
    float       previous[MOVER_WORLD_FLOATS];
} mover_slot_t;

typedef struct mover_interpolation_state {
    bool      installed;
    bool      enabled;
    float     translation_limit;

    detour_t  tick;
    uintptr_t original_matmul;
    uintptr_t original_invert;
    uintptr_t redirect_call[REDIRECT_COUNT];
    uintptr_t redirect_original_target[REDIRECT_COUNT];

    const uint32_t *alpha_gate;
    const float    *alpha_global;
    const float    *alpha_latch;

    mover_slot_t slots[SLOT_COUNT];

    uint32_t frames;
    uint32_t blended;
    uint32_t rejected;
    uint32_t unknown;
    uint32_t wrapped;
    bool     reported_silence;
} mover_interpolation_state_t;

static mover_interpolation_state_t mover_state;

/* All three are cdecl, and each one was read rather than assumed. The matrix product is cleaned by
 * `add esp,0xC` after the call in the file header, three arguments. The invert is cleaned by
 * `add esp,8` at 0x00419B97, two arguments. bapmap_tickMover ends `5D C3` at 0x0040964C, a plain
 * `pop ebp` and `ret` with no immediate, so its caller cleans up as well; its two arguments sit at
 * [ebp+8] and [ebp+0xC], and the second is read with `fld dword`, which is where the float comes
 * from. Get one of these wrong and the stack is corrupted at a point nowhere near the symptom. */
typedef void (__cdecl *matmul_fn_t)(void *destination, const void *a, const float *world);
typedef void (__cdecl *invert_fn_t)(void *destination, const float *world);
typedef void (__cdecl *tick_mover_fn_t)(void *mover, float now);

/* ============================================================================================ */
static size_t slot_index_for(const void *subnode)
{
    /* The pointers are 0x9C apart, so the low bits alone would collide on every neighbour. */
    uintptr_t value = (uintptr_t)subnode;

    return (size_t)(((value >> 2) ^ (value >> 11)) & (SLOT_COUNT - 1u));
}

static mover_slot_t *slot_find(const void *subnode)
{
    size_t index = slot_index_for(subnode);
    size_t probe;

    for (probe = 0; probe < SLOT_COUNT; ++probe) {
        mover_slot_t *slot = &mover_state.slots[(index + probe) & (SLOT_COUNT - 1u)];

        if (slot->subnode == subnode) {
            return slot;
        }
        if (slot->subnode == NULL) {
            return NULL;
        }
    }
    return NULL;
}

static mover_slot_t *slot_reserve(const void *subnode)
{
    size_t index = slot_index_for(subnode);
    size_t probe;

    for (probe = 0; probe < SLOT_COUNT; ++probe) {
        mover_slot_t *slot = &mover_state.slots[(index + probe) & (SLOT_COUNT - 1u)];

        if (slot->subnode == subnode || slot->subnode == NULL) {
            slot->subnode = subnode;
            return slot;
        }
    }
    return NULL;                                    /* full: this subnode is simply not smoothed */
}

/* ============================================================================================ */
static bool current_alpha(float *out)
{
    if (mover_state.alpha_gate == NULL || mover_state.alpha_global == NULL ||
        mover_state.alpha_latch == NULL) {
        return false;
    }
    *out = (*mover_state.alpha_gate != 0) ? *mover_state.alpha_latch : *mover_state.alpha_global;
    return true;
}

/* Fills `out` with what should be drawn for this subnode and answers whether that differs from
 * what the engine would have drawn. The hooks are handed `subnode->world`, so the record itself is
 * that pointer less the 0x14 the call site added. */
static bool interpolated_world(float *out, const float *world)
{
    const mover_slot_t *slot;
    float               alpha;

    if (!mover_state.enabled) {
        return false;
    }
    slot = slot_find((const uint8_t *)world - SUBNODE_WORLD);
    if (slot == NULL) {
        ++mover_state.unknown;
        return false;
    }
    if (!slot->usable) {
        ++mover_state.rejected;
        return false;
    }
    if (!current_alpha(&alpha)) {
        return false;
    }
    if (!mover_blend_world(out, slot->previous, world, alpha, mover_state.translation_limit)) {
        ++mover_state.rejected;
        return false;
    }
    ++mover_state.blended;
    return true;
}

static void __cdecl hook_mover_matmul(void *destination, const void *a, const float *world)
{
    matmul_fn_t original = (matmul_fn_t)mover_state.original_matmul;
    float       scratch[MOVER_WORLD_FLOATS];

    if (world != NULL && interpolated_world(scratch, world)) {
        original(destination, a, scratch);
        return;
    }
    original(destination, a, world);
}

static void __cdecl hook_mover_invert(void *destination, const float *world)
{
    invert_fn_t original = (invert_fn_t)mover_state.original_invert;
    float       scratch[MOVER_WORLD_FLOATS];

    if (world != NULL && interpolated_world(scratch, world)) {
        original(destination, scratch);
        return;
    }
    original(destination, world);
}

/* ============================================================================================
 * The tick, which is where the previous pose comes from.
 *
 * The snapshot is taken before the original runs and only when the original is going to do
 * something, which the function itself decides by comparing the time it is handed against the
 * mover's own clock at +0x30. That is the same condition its second early return is made on.
 *
 * The track wrap is detected here rather than inferred from the geometry later. A free running
 * track runs its pose up to the track length and then back to the start, and that step is a jump
 * however small the resulting angle happens to look. The largest wrap in the first level moves 101
 * degrees and 298 world units about twice a second and a rotation threshold would catch it, but
 * three other movers wrap by so little that the same threshold passes them and the whole track
 * would be drawn running backwards across a frame. Comparing the pose before and after the
 * original settles it with one subtraction and nothing to tune. */
static void snapshot_subnodes(const uint8_t *mover, bool wrapped)
{
    int32_t count;
    int32_t index;

    /* Both guards take the structured-exception form for the same reason the one in
     * hook_tick_mover above does: this runs underneath a detour the stall census measured at
     * thousands of calls a frame, and a system call is not affordable there. */
    if (!memory_try_readable((uintptr_t)mover, MOVER_SUBNODE_ARRAY + SUBNODE_STRIDE)) {
        return;
    }
    count = *(const int32_t *)(mover + MOVER_SUBNODE_COUNT);
    if (count <= 0 || count > 256) {
        return;
    }
    if (!memory_try_readable((uintptr_t)(mover + MOVER_SUBNODE_ARRAY),
                             (size_t)count * SUBNODE_STRIDE)) {
        return;
    }

    for (index = 0; index < count; ++index) {
        const uint8_t *subnode = mover + MOVER_SUBNODE_ARRAY + (size_t)index * SUBNODE_STRIDE;
        mover_slot_t  *slot    = slot_reserve(subnode);

        if (slot == NULL) {
            continue;
        }
        if (wrapped) {
            slot->usable = false;
            continue;
        }
        memcpy(slot->previous, subnode + SUBNODE_WORLD, sizeof(slot->previous));
        slot->usable = true;
    }
}

static void __cdecl hook_tick_mover(void *mover, float now)
{
    tick_mover_fn_t original = (tick_mover_fn_t)mover_state.tick.original;
    const uint8_t  *record   = (const uint8_t *)mover;
    bool            integrating;
    float           pose_before = 0.0f;

    /* memory_try_readable, NOT memory_is_readable_range, and the difference is the whole reason
     * this hook was field-reported as a frame-rate stall. The asking form walks the region list
     * through VirtualQuery, which is a system call. This hook sits on bapmap_tickMover, and a
     * census taken during the reported stall measured that function at 3,400 calls per FRAME while
     * settled debris was being created near a lift: every one of them was arriving through this
     * detour, so the guard alone was driving thousands of kernel transitions a frame and cost
     * roughly ninety per cent of the frame rate (8.5 fps against 60 with this feature switched
     * off, same encounter, same debris count). The span checked is unchanged and still covers
     * every field read below, MOVER_TRACK_LENGTH at 0x28, MOVER_POSE at 0x2C and MOVER_TIME_BASE
     * at 0x30; only the way it is checked changed, to the structured-exception form common/memory.c
     * documents for exactly this case, which is a few instructions instead of a syscall. */
    integrating = (record != NULL) &&
                  memory_try_readable((uintptr_t)record, MOVER_TIME_BASE + sizeof(float)) &&
                  (*(const float *)(record + MOVER_TIME_BASE) != now);

    if (integrating) {
        pose_before = *(const float *)(record + MOVER_POSE);
        snapshot_subnodes(record, false);
    }

    original(mover, now);

    /* A drop is not automatically a wrap. Direction arm 3 integrates a reversing mover as
     * `pose = pose - rate * dt`, so a door on its return leg decreases on every single tick.
     * Treating that as a wrap would leave a whole class of mover smoothed on the way out and
     * stepped on the way back, which is exactly the seam the all or nothing rule elsewhere in this
     * file exists to prevent.
     *
     * The two are separable exactly rather than by tolerance: a wrap subtracts a whole track
     * length and a reversal at most one substep of travel, so half a track length lies strictly
     * between them. */
    if (integrating) {
        float pose_after = *(const float *)(record + MOVER_POSE);
        float track      = *(const float *)(record + MOVER_TRACK_LENGTH);

        if (pose_after < pose_before && track > 0.0f &&
            (pose_before - pose_after) > (track * 0.5f)) {
            ++mover_state.wrapped;
            snapshot_subnodes(record, true);
        }
    }
}

/* ============================================================================================ */
/* Reads one absolute operand, having first checked that the instruction in front of it is the one
 * that was expected. An operand is only worth believing when its opcode is where it should be, and
 * the check is what turns an offset counted to the wrong place into a message that names it. */
static bool read_operand(uintptr_t site, uintptr_t operand_offset, const char *what,
                         const uint8_t *opcode, size_t opcode_size, uint32_t *out)
{
    if (!patch_validate_bytes(site + operand_offset - opcode_size, opcode, opcode_size)) {
        log_warning("the %s operand at +%02X is not preceded by its opcode, so the offset is "
                    "wrong or this is a different build; movers are NOT interpolated",
                    what, (unsigned)operand_offset);
        return false;
    }
    if (!memory_read_u32(site + operand_offset, out) ||
        !memory_is_inside_image(*out, sizeof(uint32_t))) {
        log_warning("the %s operand reads %08X, which is outside the image; movers are NOT "
                    "interpolated", what, (unsigned)*out);
        return false;
    }
    return true;
}

static bool resolve_alpha_cells(void)
{
    static const uint8_t OPCODE_MOV_EAX_ABS[] = { 0xA1 };
    static const uint8_t OPCODE_CMP_ABS[]     = { 0x83, 0x3D };
    static const uint8_t OPCODE_MOV_ECX_ABS[] = { 0x8B, 0x0D };
    uintptr_t site = sites[SITE_ALPHA].address;
    uint32_t  address;

    if (site == 0) {
        return false;
    }
    if (!read_operand(site, OFFSET_ALPHA_GLOBAL, "global alpha",
                      OPCODE_MOV_EAX_ABS, sizeof(OPCODE_MOV_EAX_ABS), &address)) {
        return false;
    }
    mover_state.alpha_global = (const float *)(uintptr_t)address;

    if (!read_operand(site, OFFSET_ALPHA_GATE, "alpha gate",
                      OPCODE_CMP_ABS, sizeof(OPCODE_CMP_ABS), &address)) {
        return false;
    }
    mover_state.alpha_gate = (const uint32_t *)(uintptr_t)address;

    if (!read_operand(site, OFFSET_ALPHA_LATCH, "latched alpha",
                      OPCODE_MOV_ECX_ABS, sizeof(OPCODE_MOV_ECX_ABS), &address)) {
        return false;
    }
    mover_state.alpha_latch = (const float *)(uintptr_t)address;

    log_info("alpha cells: global %08X, gate %08X, latched %08X",
             (unsigned)(uintptr_t)mover_state.alpha_global,
             (unsigned)(uintptr_t)mover_state.alpha_gate,
             (unsigned)(uintptr_t)mover_state.alpha_latch);
    return true;
}

/* All four or none. One redirected consumer and three untouched ones would draw a door in two
 * places at once, which is a worse picture than the stepped one this feature exists to replace.
 * The patch layer has no rollback of its own, so it is composed here out of the original target
 * read back before each write. */
static void undo_redirects(size_t written)
{
    size_t index;

    for (index = 0; index < written; ++index) {
        if (patch_redirect_call(mover_state.redirect_call[index],
                                (const void *)mover_state.redirect_original_target[index])
            != PATCH_RESULT_OK) {
            log_error("the redirect at %08X could not be put back. One consumer of the mover pose "
                      "now disagrees with the others; set InterpolateMovers=0 and restart.",
                      (unsigned)mover_state.redirect_call[index]);
        }
    }
}

static bool install_redirects(void)
{
    /* The three matrix products share one replacement because they share one callee; the invert
     * has its own because its argument list is shorter by one. */
    const void *replacement[REDIRECT_COUNT] = {
        (const void *)hook_mover_matmul,
        (const void *)hook_mover_matmul,
        (const void *)hook_mover_matmul,
        (const void *)hook_mover_invert
    };
    size_t index;

    for (index = 0; index < REDIRECT_COUNT; ++index) {
        uintptr_t site = sites[REDIRECT_SITE[index]].address;
        uintptr_t call;
        uintptr_t target;

        if (site == 0) {
            log_warning("%s did not resolve, so none of the four mover consumers is redirected",
                        sites[REDIRECT_SITE[index]].name);
            undo_redirects(index);
            return false;
        }
        call = site + REDIRECT_PATTERN_SIZE[index] - 1u;    /* the pattern ends at the E8 */

        if (!patch_read_call_target(call, &target)) {
            log_warning("no call at %08X, so none of the four is redirected", (unsigned)call);
            undo_redirects(index);
            return false;
        }
        mover_state.redirect_call[index]            = call;
        mover_state.redirect_original_target[index] = target;

        if (index < 3) {
            /* The three matrix products share one hook, so they have to share one callee. On the
             * builds checked they do: a caller census of bapmap_matMul3 finds twelve call sites
             * and these three are among them. Without this comparison a build where they diverged
             * would leave all three hooks calling whichever target happened to be read last, which
             * is a wrong call rather than a missing feature, so it refuses instead. */
            if (index > 0 && mover_state.original_matmul != target) {
                log_warning("the matrix product at %08X calls %08X while the earlier ones call "
                            "%08X, so this is not a build this feature understands; rolling back "
                            "and leaving movers stepped",
                            (unsigned)call, (unsigned)target,
                            (unsigned)mover_state.original_matmul);
                undo_redirects(index);
                return false;
            }
            mover_state.original_matmul = target;
        } else {
            mover_state.original_invert = target;
        }

        if (patch_redirect_call(call, replacement[index]) != PATCH_RESULT_OK) {
            log_warning("the redirect at %08X could not be written, rolling back the %u already "
                        "in place", (unsigned)call, (unsigned)index);
            undo_redirects(index);
            return false;
        }
    }
    return true;
}

void mover_interpolation_install(bool enabled, float translation_limit)
{
    if (mover_state.installed) {
        return;
    }
    mover_state.installed         = true;
    mover_state.translation_limit = (translation_limit > 0.0f) ? translation_limit : 0.0f;

    if (!enabled) {
        log_info("InterpolateMovers=0, doors and lifts keep stepping at the simulation rate");
        return;
    }

    signature_resolve_table(sites, SITE_COUNT);

    if (!resolve_alpha_cells()) {
        log_warning("the substep alpha latch did not resolve, so movers are NOT interpolated. "
                    "Reading the plain global instead would let them drift on through a menu.");
        return;
    }
    if (sites[SITE_TICK].address == 0) {
        log_warning("bapmap_tickMover did not resolve, so there is nowhere to capture a previous "
                    "pose and movers are NOT interpolated");
        return;
    }
    if (!install_redirects()) {
        return;
    }
    if (!detour_install(&mover_state.tick, sites[SITE_TICK].address,
                        (const void *)hook_tick_mover, MOVER_TICK_PROLOGUE)) {
        log_warning("the mover tick could not be detoured, rolling back all four redirects");
        undo_redirects(REDIRECT_COUNT);
        return;
    }

    mover_state.enabled = true;
    log_info("movers are interpolated: 4 consumers redirected, alpha via the latch at %08X, "
             "translation limit %.1f units per step",
             (unsigned)(uintptr_t)mover_state.alpha_gate, (double)mover_state.translation_limit);
}

/* ============================================================================================ */
#define REPORT_INTERVAL_FRAMES 600u

void mover_interpolation_sample(void)
{
    if (!mover_state.enabled) {
        return;
    }
    ++mover_state.frames;
    if (mover_state.frames < REPORT_INTERVAL_FRAMES) {
        return;
    }

    /* A feature that installed and then never ran has shipped twice in this tree, so silence
     * reports itself once rather than reading like success. */
    if (mover_state.blended == 0 && !mover_state.reported_silence) {
        mover_state.reported_silence = true;
        log_warning("mover interpolation is installed and has smoothed NOTHING in %u frames "
                    "(%u poses were unknown to it, %u refused). Either this level has no movers "
                    "or the previous pose is not reaching the draw.",
                    (unsigned)mover_state.frames, (unsigned)mover_state.unknown,
                    (unsigned)mover_state.rejected);
    } else if (mover_state.blended != 0) {
        log_info("movers: %u poses blended, %u refused, %u unknown, %u track wraps over %u frames",
                 (unsigned)mover_state.blended, (unsigned)mover_state.rejected,
                 (unsigned)mover_state.unknown, (unsigned)mover_state.wrapped,
                 (unsigned)mover_state.frames);
    }

    mover_state.frames   = 0;
    mover_state.blended  = 0;
    mover_state.rejected = 0;
    mover_state.unknown  = 0;
    mover_state.wrapped  = 0;
}
