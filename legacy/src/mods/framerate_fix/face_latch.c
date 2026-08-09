/* face_latch.c: the scripted facing command stops answering "still turning" above 32 fps.
 *
 * ==============================================================================================
 * The defect
 *
 * A scripted actor turns to face something with opcode 0x202, and asks whether the turn has
 * finished by polling rdPuppetTrack.bComplete at track+0x140. That flag is a LEVEL, not an edge,
 * it is raised inside the draw once per rendered frame, and it is read once per simulation step.
 *
 * Inside the track advance (the body of rdPuppet_advanceTrack 0x4839D0):
 *
 *     if ((f32)pKey->numFrames <= newTime) { pTrack->bComplete = 1; ... }
 *     else if (mode & RDMODE_HOLDEND)      { pTrack->time = numFrames; }
 *     ...
 *     pTrack->timePrev = pTrack->time;
 *
 * Once a clip that clamps at its last frame has got there, timePrev equals numFrames and every
 * later advance takes the same branch and raises the flag again. The two neighbouring latches are
 * not affected: the marker latch at +0x144 and the parameter event latch at +0x148 are both
 * guarded by timePrev < marker && marker <= newTime, so they fire once each. bComplete is the only
 * level in the record.
 *
 * ==============================================================================================
 * Why 32 fps is the exact threshold
 *
 * The main loop runs every simulation step and only then renders:
 *
 *     0043EA27  call 0x4756FC              ; sys_runSubsteps, the whole fixed-step accumulator
 *     0043EA48  push 0x0D / call 0x46F4A9  ; module broadcast, once per RENDERED FRAME
 *
 * The step is 1/32 s, and the animation advance runs from the draw. call 0x483D20 appears at three
 * sites in the whole image: 0x410CB5 and 0x410D99, both inside 0x410C57 and both passing seconds=0
 * so they seek rather than advance, and 0x4113EB inside bapobj_drawAll, which is the Bapobj
 * handler for message 0x0D. There is no per-step animation advance anywhere.
 *
 * So below 32 fps the frame delta exceeds 1/32 and some frames run two steps back to back. The
 * second of that pair has had no render in between, the flag is still the zero the previous poll
 * wrote, and the command answers "still turning". That happens 32 minus fps times a second, which
 * at the shipped 30 fps is twice a second, and it is the only reason the shipped game ever took
 * the in-progress branch at all.
 *
 * At or above 32 fps the frame delta is at most 1/32, a frame never runs two steps, every step is
 * preceded by at least one advance, and the answer is "finished" every single time.
 *
 * ==============================================================================================
 * What that costs in the game
 *
 * A scripted state whose only real work sits on the in-progress branch never does that work. In
 * the swamp the opening scene runs its script on the player's own object, so the player is
 * suspended for the duration and the release runs from a state the script never reaches. The
 * player can never move again, and the cutscene input lock reads zero throughout, so it looks
 * healthy from every direction except this one.
 *
 * ==============================================================================================
 * The repair
 *
 * The completion test 0x42E3AD has exactly one caller, 0x434109 inside the 0x202 handler, so a
 * detour there reaches the facing command and nothing else:
 *
 *     FIRST CALL   latches rec+0x1BC = rec+0x1C0, starts the clip, and when the opcode's third
 *                  operand is zero ORs RDMODE_HOLDEND into the track's mode flags at 0x42E456 and
 *                  0x42E47A. Returns 0, meaning still running.
 *     LATER CALLS  0x42E483 resolves the track, tests bComplete at track+0x140, returns 0 while it
 *                  is clear, and otherwise CLEARS it at 0x42E4AE and returns 1.
 *
 * This asks the engine first and keeps its answer whenever it says "still running", so the hook
 * can only ever turn a finished answer into an unfinished one. It never invents a completion and
 * it cannot start a clip twice. On one step in N it withholds the answer instead, and only for a
 * clip carrying RDMODE_HOLDEND, because such a clip has already stopped moving and delaying its
 * completion by one step is not visible. A clip that loops or free runs has not stopped, and
 * holding it up would delay whatever the script does next, so it is passed straight through.
 *
 * Withholding loses nothing, because the engine's own poll already cleared the flag before this
 * returns and the next rendered frame raises it again. That is the defect itself.
 *
 * Two repairs that look cleaner and are worse. Setting RDTRACK_HELD on the track from the poll
 * suppresses completion permanently, and across the shipped levels only a few dozen of the 2288
 * facing nodes have the shape that stalls while the rest need the completion to arrive. Making
 * bComplete edge triggered where it is written is a change on the shared path for every track of
 * every puppet, and the latch has at least three independent consumers: this one, the enemy tick
 * at 0x432EB6 with its consume at 0x432EDD, and opcode 0x20F at 0x4341A0 with its consume at
 * 0x4341BA. A single edge can be taken by whichever of them polls first.
 */
#include "face_latch.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 0x42E3AD, the completion test. The prologue is six bytes on an exact instruction boundary and
 * carries no relative operand, which is what lets the trampoline copy it:
 *
 *     0042E3AD  55              push ebp
 *     0042E3AE  8B EC           mov  ebp, esp
 *     0042E3B0  8B 45 08        mov  eax, [ebp+8]
 *
 * The pattern deliberately reaches past the prologue. Its 18 byte tail alone matches twice, at
 * 0x42E3B3 and 0x4333FB, and the bytes before the second are 00 74 60 8B 45 08, which is neither
 * the authored prologue nor a branch. The two stage detour rule therefore leaves exactly one
 * candidate. */
static const uint8_t SIG_FACE_COMPLETE[] = {
    0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0x8B, 0x48, 0x34, 0x8B, 0x51, 0x14,
    0x8B, 0x45, 0x08, 0x8B, 0x88, 0xC0, 0x01, 0x00, 0x00, 0x3B, 0x8A, 0xC8
};
#define FACE_COMPLETE_PROLOGUE 6u

/* 0x4757DB, the tail of the substep loop, where the simulation step counter is incremented:
 *
 *     004757DB  A1 60884B00     mov  eax, [g_tickCounter]
 *     004757E0  83 C0 01        add  eax, 1
 *     004757E3  A3 60884B00     mov  [g_tickCounter], eax
 *     004757E8  D9 05 28878600  fld  [g_simTime]
 *     004757EE  D8 05 14878600  fadd [dt]
 *     004757F4  D9 1D 28878600  fstp [g_simTime]
 *
 * Every absolute operand is wildcarded so the pattern carries no address at all, and the counter
 * is read out of the operand it matched. This is the SIMULATION STEP counter, which is a different
 * cell from the per-frame animation counter the rest of this DLL uses. */
static const uint8_t SIG_SUBSTEP_COUNTER[] = {
    0xA1, 0x00, 0x00, 0x00, 0x00,
    0x83, 0xC0, 0x01,
    0xA3, 0x00, 0x00, 0x00, 0x00,
    0xD9, 0x05, 0x00, 0x00, 0x00, 0x00,
    0xD8, 0x05, 0x00, 0x00, 0x00, 0x00,
    0xD9, 0x1D, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t MSK_SUBSTEP_COUNTER[] = {
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
};
#define OFFSET_SUBSTEP_COUNTER 0x01u

enum {
    SITE_FACE_COMPLETE,
    SITE_SUBSTEP_COUNTER,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY_DETOUR("face_complete",   SIG_FACE_COMPLETE, FACE_COMPLETE_PROLOGUE),
    SIGNATURE_ENTRY_MASKED("substep_counter", SIG_SUBSTEP_COUNTER, MSK_SUBSTEP_COUNTER)
};

/* The walk from the script record to the animation track. Every offset is taken from inside the
 * detoured function itself, at 0x42E42E to 0x42E47A:
 *
 *     object = [record + 0x34]
 *     thing  = [object + 0x9C]
 *     puppet = [thing  + 0x18]
 *     slot   = [object + 0xEC]
 *     track  = puppet + 8 + slot * 0x14C
 *
 * The stride and base agree with the track indexing inside the advance at 0x483D3A to 0x483D43,
 * which computes 83 * slot * 4 + 8, that is 0x14C * slot + 8. */
#define RECORD_OBJECT     0x034u
#define OBJECT_THING      0x09Cu
#define OBJECT_TRACK_SLOT 0x0ECu
#define THING_PUPPET      0x018u
#define TRACK_FIRST       0x008u
#define TRACK_STRIDE      0x14Cu
#define TRACK_MODE_FLAGS  0x13Cu
#define MODE_HOLD_END     0x001u
#define TRACK_SLOT_MAX    3

/* The caller at 0x434104 pushes the opcode's third operand and then the record, calls, does
 * add esp,8 and stores EAX. Two dword arguments, cdecl, an int32 result. */
typedef int32_t (__cdecl *face_complete_fn_t)(void *record, int32_t third_operand);

typedef struct face_latch_state {
    bool                     installed;
    detour_t                 detour;
    face_complete_fn_t       original;
    const volatile uint32_t *substep_counter;
    uint32_t                 yield_period;
    bool                     fallback_reported;
    uint32_t                 yields;
} face_latch_state_t;

static face_latch_state_t latch_state;

/* ============================================================================================ */
static bool read_pointer(uintptr_t address, uintptr_t *out)
{
    uint32_t value;

    if (!memory_is_readable_range(address, sizeof(uint32_t)) ||
        !memory_read_u32(address, &value) || value == 0) {
        return false;
    }
    *out = (uintptr_t)value;
    return true;
}

/* True when the clip this facing command is waiting on clamps at its last frame.
 *
 * That distinction is the whole safety argument, so it is read from the track's own mode flags
 * rather than assumed. The opcode's third operand is only the reason the flag was set in the first
 * place, and it is used as a fallback when the record cannot be walked. */
static bool clip_holds_at_end(void *record, int32_t third_operand, bool *known)
{
    uintptr_t object;
    uintptr_t thing;
    uintptr_t puppet;
    uintptr_t track;
    uint32_t  slot;
    uint32_t  flags;

    *known = false;
    if (!read_pointer((uintptr_t)record + RECORD_OBJECT, &object) ||
        !read_pointer(object + OBJECT_THING, &thing) ||
        !read_pointer(thing + THING_PUPPET, &puppet) ||
        !memory_is_readable_range(object + OBJECT_TRACK_SLOT, sizeof(uint32_t)) ||
        !memory_read_u32(object + OBJECT_TRACK_SLOT, &slot)) {
        return (third_operand == 0);
    }
    if (slot > (uint32_t)TRACK_SLOT_MAX) {
        return (third_operand == 0);
    }

    track = puppet + TRACK_FIRST + slot * TRACK_STRIDE;
    if (!memory_is_readable_range(track + TRACK_MODE_FLAGS, sizeof(uint32_t)) ||
        !memory_read_u32(track + TRACK_MODE_FLAGS, &flags)) {
        return (third_operand == 0);
    }

    *known = true;
    return (flags & MODE_HOLD_END) != 0;
}

bool face_latch_should_yield(int32_t engine_result, uint32_t substep_counter,
                             uint32_t yield_period, bool clip_holds_at_end)
{
    if (engine_result == 0) {
        return false;                 /* the engine says still running, there is nothing to hold */
    }
    if (yield_period == 0) {
        return false;                 /* switched off */
    }
    if ((substep_counter % yield_period) != 0) {
        return false;                 /* not this simulation step */
    }
    return clip_holds_at_end;
}

static int32_t __cdecl hook_face_complete(void *record, int32_t third_operand)
{
    int32_t result;
    bool    holds;
    bool    known;

    /* detour_install writes the branch before it stores the pointer back to the engine, so in
     * principle this can be entered with no way through. It cannot happen here, because the mods
     * are installed from the host's entry point on one thread before a single frame is drawn. The
     * guard stays because that argument is a property of the loader, not of this file. */
    if (latch_state.original == NULL) {
        return 0;
    }
    result = latch_state.original(record, third_operand);

    if (result == 0 || latch_state.substep_counter == NULL) {
        return result;
    }

    holds = clip_holds_at_end(record, third_operand, &known);
    if (!known && !latch_state.fallback_reported) {
        latch_state.fallback_reported = true;
        log_warning("the animation track could not be reached from the script record, so whether "
                    "the clip clamps at its last frame is being taken from the opcode's own "
                    "operand instead. Reported once.");
    }
    if (!face_latch_should_yield(result, *latch_state.substep_counter,
                                 latch_state.yield_period, holds)) {
        return result;
    }

    ++latch_state.yields;
    return 0;
}

/* ============================================================================================ */
static bool resolve_substep_counter(void)
{
    uintptr_t site = sites[SITE_SUBSTEP_COUNTER].address;
    uint32_t  address;

    if (site == 0) {
        log_warning("the substep counter pattern did not resolve, so there is no way to tell one "
                    "simulation step from the next and the facing command is left alone");
        return false;
    }
    if (!memory_read_u32(site + OFFSET_SUBSTEP_COUNTER, &address) ||
        !memory_is_inside_image(address, sizeof(uint32_t))) {
        log_warning("the substep counter operand at %08X is not an address inside the image, the "
                    "facing command is left alone", (unsigned)(site + OFFSET_SUBSTEP_COUNTER));
        return false;
    }
    latch_state.substep_counter = (const volatile uint32_t *)(uintptr_t)address;
    return true;
}

bool face_latch_install(int yield_period)
{
    if (latch_state.installed) {
        return true;
    }
    if (yield_period == 0) {
        log_warning("FaceLatchYield=0, a scripted actor whose state only makes progress on the "
                    "facing command's in-progress branch will stall above 32 fps");
        return false;
    }
    if (yield_period < 2)  { yield_period = 2; }
    if (yield_period > 64) { yield_period = 64; }

    signature_resolve_table(sites, SITE_COUNT);

    if (sites[SITE_FACE_COMPLETE].address == 0) {
        log_warning("face_complete did not resolve, the facing command keeps reporting finished "
                    "on every simulation step above 32 fps");
        return false;
    }
    if (!resolve_substep_counter()) {
        return false;
    }

    latch_state.yield_period = (uint32_t)yield_period;

    if (!detour_install(&latch_state.detour, sites[SITE_FACE_COMPLETE].address,
                        (const void *)hook_face_complete, FACE_COMPLETE_PROLOGUE)) {
        log_error("the facing completion test at %08X could not be detoured",
                  (unsigned)sites[SITE_FACE_COMPLETE].address);
        return false;
    }
    latch_state.original  = (face_complete_fn_t)latch_state.detour.original;
    latch_state.installed = true;

    log_info("facing command latch active at %08X: on one simulation step in %d the command "
             "answers 'still turning' instead of 'finished', but only for a clip that clamps at "
             "its last frame. Below 32 fps the engine produced that answer by itself; above it, "
             "it never does. substepCounter=%08X",
             (unsigned)sites[SITE_FACE_COMPLETE].address, yield_period,
             (unsigned)(uintptr_t)latch_state.substep_counter);
    return true;
}
