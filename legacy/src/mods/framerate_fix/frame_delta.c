/* frame_delta.c: measure the frame period ourselves, because the engine's measurement of it loses
 * resolution the longer the process has been open.
 *
 * ============================== What is actually broken =======================================
 *
 * sys_waitForFrame computes the frame delta as the difference of two timestamps, and the older of
 * the two has already been rounded into a float32 counting seconds since the process started. The
 * whole computation is five instructions, at 0x00475B97 onwards:
 *
 *     00475B97  call 0x00475B2D          t = platform seconds minus the process epoch, in st(0)
 *     00475B9C  fst  dword [0x00868718]  rounded into float32; st(0) itself is left exact
 *     00475BA2  fsub dword [0x006F83E0]  minus the previous sample, which is the rounded one
 *     00475BA8  fstp dword [0x00868714]  the frame delta, carrying that rounding
 *     ...
 *     00475BD3  mov  eax, [0x00868718]   at the end of the wait, the previous sample is restamped
 *     00475BD8  mov  [0x006F83E0], eax   from the rounded cell, not from the exact register
 *
 * so what the engine reports is round32(t_now_exact minus round32(t_previous)). The error does not
 * accumulate, because every frame measures afresh from the platform clock. What it does is jitter,
 * bounded by half an ulp of the absolute uptime, and that bound grows as the process stays open.
 *
 * [0x00868718] is seconds since the process started and nothing resets it. Scanning the whole .text
 * for the operand bytes of that address returns twenty three hits, all of them real memory
 * operands, of which exactly one is a write, the fst above. The epoch it counts from is written at
 * exactly one site inside the time module's install arm, and that arm is reached once per process.
 * Behind the call at 0x00475B97 the platform time is computed as an integer performance counter
 * scaled by a qword factor, so the value in st(0) carries at least double precision. It is only the
 * store into the global that throws that precision away.
 *
 * Feeding a perfectly regular 6.25 ms of real time into those two instructions and reading back
 * what they would report:
 *
 *     uptime      reported delta, low    high         spread
 *     600 s       6.2256 ms              6.2744 ms     48.8 us
 *     3600 s      6.1523 ms              6.3477 ms    195.3 us
 *     8192 s      5.8594 ms              6.6406 ms    781.3 us
 *
 * That last row is 12.5 per cent of the frame, on a machine holding its cap perfectly. Three things
 * downstream turn it into motion. The spin exit test at 0x00475BB7 compares this same delta against
 * the cap, so the pacing decision inherits the jitter of the measurement. The simulation
 * accumulator adds the delta every frame. And every per frame filter in the game, the camera anchor
 * blend and the menu tweens among them, integrates it. What a player would describe is judder that
 * gets worse across a long session and that a level reload does not clear, which is what separates
 * this defect from the substep phase precession the clock rebase deals with.
 *
 * ============================== What this does instead ========================================
 *
 * It measures the period with QueryPerformanceCounter, subtracts in double, and writes the result
 * into the delta cell after sys_waitForFrame has returned. Two counter readings subtracted in
 * double and then converted give a few milliseconds, and float32 represents a few milliseconds to
 * within a nanosecond. The rounding that grows with uptime is gone because nothing large is ever
 * rounded.
 *
 * The engine's timestamp at [0x00868718] is left exactly as it was. Twenty two instructions read
 * it, as birth stamps, as ages against a stamp and in two places as a phase multiplied by the
 * absolute clock, and none of them is this module's business. Widening it in place was considered
 * and rejected for the same reason: it is four bytes in the middle of a data layout and all
 * twenty two readers decode it as a dword.
 *
 * The delta also stays a measurement. Handing the accumulator the cap period instead was proposed
 * and refused. It removes nothing, because the rounding it was aimed at is the accumulator's own
 * and is set by how large the accumulator has grown; and once the machine cannot hold the cap, a
 * fixed period per frame lets the simulation fall behind real time without bound. The 0.1 s clamp
 * inside the substep loop does not save that case, because it limits how far the accumulator may
 * jump forward and never how far it may lag.
 *
 * ============================== What it does not fix ==========================================
 *
 * Not the substep phase precession. That is the rounding of the simulation accumulator's own
 * addition, whose size is set by the magnitude of the accumulator rather than by the quality of the
 * addend, so a better delta changes the phase of a deterministic pattern and not its amplitude. The
 * two features are independent, and neither is a substitute for the other.
 *
 * ============================== Status ========================================================
 *
 * On by default, PreciseFrameTime=1, played and accepted by the maintainer. What stands behind it
 * offline is a byte census and arithmetic, which is a separate claim from how it feels. Every
 * address quoted above was read out of the retail executable and is an annotation only; one of the
 * three builds that ship is a recompile which puts the same code somewhere else, which is why the
 * two sites below are found by pattern.
 */
#include "frame_delta.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* --- 0x00475B75  sys_waitForFrame, at the function start ------------------------------------- *
 * The same anchor framerate_fix reads its cap immediates out of, which is why the pattern is the
 * same one rather than a second one that would have to be kept in step.
 *
 * The prologue is eleven bytes and that is an instruction boundary:
 *
 *     00475B75  55                 push ebp
 *     00475B76  8B EC              mov  ebp, esp
 *     00475B78  51                 push ecx
 *     00475B79  83 3D 94228800 00  cmp  dword [0x00882294], 0     the "60fps" cheat cell
 *     00475B80  75 09              jne  0x00475B8B
 *
 * Eleven bytes stops short of the cap immediates at +0x10 and +0x19, so this detour and the render
 * cap patch can both live on this function without either standing on the other. */
static const uint8_t SIG_FRAME_DELTA_PROLOGUE[] = {
    0x55, 0x8B, 0xEC, 0x51, 0x83, 0x3D, 0x94, 0x22, 0x88, 0x00, 0x00, 0x75, 0x09,
    0xC7, 0x45, 0xFC, 0x89, 0x88, 0x08, 0x3D, 0xEB, 0x07
};

/* The cap immediate lies inside this pattern, and another patch in this same DLL rewrites it.
 *
 * The render cap replaces the four bytes at +0x10, the authored 1/30, with whatever TargetFps asks
 * for, and it runs before this module installs. An unmasked pattern therefore matched nothing and
 * this feature switched itself off with a message naming the wrong cause. It is the detour rule in
 * its other form: a pattern must not depend on bytes somebody else writes, whether that somebody is
 * a detour or a sibling patch in the same directory.
 *
 * The four immediate bytes are wildcarded. What is left still carries the absolute address of the
 * cheat cell, and that is what keeps the anchor unique. */
static const uint8_t MASK_FRAME_DELTA_PROLOGUE[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF
};
#define WAIT_FOR_FRAME_PROLOGUE 11u

/* --- 0x00475BA8  the store of the delta ------------------------------------------------------ *
 * The one cell this module writes. Its address is read out of the store's own operand rather than
 * written down here:
 *
 *     00475BA8  D9 1D 14878600     fstp dword [0x00868714]
 *
 * D9 /3 is fstp m32fp, so the four operand bytes begin at +0x02. */
static const uint8_t SIG_FRAME_DELTA_STORE[] = {
    0xD9, 0x1D, 0x14, 0x87, 0x86, 0x00
};
#define OFFSET_FRAME_DELTA_CELL 0x02u

/* A frame longer than this is not a frame, it is a stall or a level load, and a counter that went
 * backwards is a broken counter. Either way the engine's own value is left in place. */
#define MAX_PLAUSIBLE_SECONDS 10.0

/* One line when the first delta is actually written, and one every few minutes after that. The
 * install line alone cannot distinguish a module that is working from one that resolved its sites
 * and then never wrote anything, and that is the failure that looks most like success. 30000 frames
 * is about four minutes at 120 fps, so this is a handful of lines a session and not a flood. */
#define REPORT_INTERVAL_FRAMES 30000u

typedef void (__cdecl *wait_for_frame_fn_t)(void);

typedef struct frame_delta_state {
    bool            installed;
    bool            active;
    detour_t        wait;
    volatile float *cell;

    LARGE_INTEGER   frequency;
    LARGE_INTEGER   previous;
    bool            have_previous;

    uint32_t        frames;
    uint32_t        replaced;
} frame_delta_state_t;

static frame_delta_state_t delta_state;

/* The engine's clamp stays the authority on a long frame.
 *
 * sys_runSubsteps clamps whatever it is handed to 0.1 s before the accumulator sees it, so a level
 * load or a stall cannot inject a hundred substeps. This writes an honest measurement and lets that
 * clamp do its job. It does not clamp on its own, because a second ceiling with a different value
 * would be a second opinion nobody asked for.
 *
 * A scan for direct calls to this function returns five sites, two in the frame loop, two in the
 * menu loops and one in the debug loop, and none of them is reachable from the substep loop. So the
 * hook runs once per frame in the menus as well as in play, and never with the delta cell holding
 * the substep period instead of the frame period. */
static void __cdecl hook_wait_for_frame(void)
{
    wait_for_frame_fn_t original = (wait_for_frame_fn_t)delta_state.wait.original;
    LARGE_INTEGER       now;
    double              seconds;

    original();

    if (!delta_state.active || delta_state.cell == NULL) {
        return;
    }
    ++delta_state.frames;

    if (!QueryPerformanceCounter(&now)) {
        return;                                 /* leave the engine's own value in place */
    }
    if (!delta_state.have_previous) {
        delta_state.previous      = now;
        delta_state.have_previous = true;
        return;                                 /* the first frame has nothing to measure from */
    }

    /* The whole point of the module is this line: the subtraction happens in double, between two
     * counts that have not been rounded into a float carrying hours of uptime. What lands in the
     * float32 is a delta of a few milliseconds, which that type represents to within a nanosecond.
     */
    seconds = (double)(now.QuadPart - delta_state.previous.QuadPart)
            / (double)delta_state.frequency.QuadPart;
    delta_state.previous = now;

    if (!(seconds > 0.0) || !(seconds < MAX_PLAUSIBLE_SECONDS)) {
        return;                                 /* also catches NaN */
    }
    *delta_state.cell = (float)seconds;
    ++delta_state.replaced;

    if (delta_state.replaced == 1u) {
        log_info("the frame delta is now measured here, first sample %.4f ms", seconds * 1000.0);
    } else if ((delta_state.frames % REPORT_INTERVAL_FRAMES) == 0u) {
        /* The gap between the two counts is the number of frames left to the engine, which is the
         * first frame plus anything the plausibility test refused. A gap that keeps growing means
         * the counter is being read across a boundary this module does not understand. */
        log_info("frame delta: %u frames seen, %u measured here, latest %.4f ms",
                 (unsigned)delta_state.frames, (unsigned)delta_state.replaced, seconds * 1000.0);
    }
}

void frame_delta_install(bool enabled)
{
    uintptr_t store;
    uintptr_t site;
    uint32_t  address;

    if (delta_state.installed) {
        return;
    }
    delta_state.installed = true;

    if (!enabled) {
        log_info("PreciseFrameTime=0, the frame delta keeps the engine's own arithmetic and its "
                 "resolution keeps decaying with process uptime");
        return;
    }

    if (!QueryPerformanceFrequency(&delta_state.frequency) ||
        delta_state.frequency.QuadPart == 0) {
        log_warning("no performance counter, the frame delta is left as the engine computes it");
        return;
    }

    /* The cell first, because there is no point detouring anything if we cannot say where to write
     * the answer, and a detour cannot be taken back once it is placed. */
    store = signature_find_unique(SIG_FRAME_DELTA_STORE, NULL, sizeof(SIG_FRAME_DELTA_STORE));
    if (store == 0) {
        log_warning("the frame delta store did not resolve, the delta is left alone");
        return;
    }
    if (!memory_read_u32(store + OFFSET_FRAME_DELTA_CELL, &address) ||
        !memory_is_inside_image(address, sizeof(float))) {
        log_warning("the frame delta operand reads %08X, outside the image, refused",
                    (unsigned)address);
        return;
    }
    delta_state.cell = (volatile float *)(uintptr_t)address;

    site = signature_find_detour_target(SIG_FRAME_DELTA_PROLOGUE, MASK_FRAME_DELTA_PROLOGUE,
                                        sizeof(SIG_FRAME_DELTA_PROLOGUE),
                                        WAIT_FOR_FRAME_PROLOGUE);
    if (site == 0) {
        log_warning("sys_waitForFrame did not resolve, the frame delta is left alone");
        return;
    }
    if (!detour_install(&delta_state.wait, site, (const void *)hook_wait_for_frame,
                        WAIT_FOR_FRAME_PROLOGUE)) {
        log_warning("sys_waitForFrame could not be detoured, the frame delta is left alone");
        return;
    }

    delta_state.active = true;
    log_info("frame delta computed in double at %08X (cell %08X). The engine derives it by "
             "subtracting a float32 timestamp that counts seconds since the process started and is "
             "never reset, so its resolution decays with uptime. This replaces the result and "
             "leaves that timestamp alone.",
             (unsigned)site, (unsigned)address);
}
