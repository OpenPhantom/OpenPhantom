/* framerate_fix.c: lift the render cap, and pay for everything that lifting it costs.
 *
 * SIZE NOTE: a little over six hundred lines. This file is the DLL's entry point and it also owns
 * the two engine sites that are frame COUNTERS used as clocks, the animation tick and the emitter
 * dormancy limit, because both are corrected in the same way and from the same argument. The bulk
 * is not code: it is the byte census that decides which readers of the animation counter are
 * affected, and a correction to that census that stood wrong in this file for a while. The seam,
 * measured rather than guessed, is those two counter patches together with the per frame tick that
 * drives one of them, which is roughly a third of the file and shares only the config struct with
 * the rest. It was not taken here because it means adding a source file, which belongs in a change
 * of its own. Everything else has already been moved out: the camera, the drawn euler and pose,
 * the facing latch, the frame delta, the particle clock, the simulation clock rebase, the movers
 * and the statistics each own a file.
 *
 * ==============================================================================================
 * The simulation is already free
 *
 * sys_runSubsteps 0x4756FC is a fixed-step accumulator at 1/32 s with the clamp on the INCOMING
 * dt (0.1 s). AI, the player tick, projectiles and body collision all hang off it. Raising the
 * render rate costs the simulation nothing, and nothing in this DLL touches that loop, except
 * to nail it down, see the sim-rate pin below.
 *
 * THE RENDER CAP is one dword:
 *     0x475B82  mov [ebp-4], 0x3D088889     ; 1/30 s
 *     0x475B8B  mov [ebp-4], 0x3C888889     ; 1/60 s   ("60fps" cheat arm)
 *     0x475BAE  cmp [0x4B7D78], 0           ; g_frameLimiterOn, ships as 1
 *     0x475BC9  push 0 / call [Sleep]       ; a BUSY WAIT
 *
 * What actually breaks at a higher rate, and what this DLL therefore compensates:
 *   (1) the five per-frame camera dampers  -> camera_compensation.c
 *   (2) the animation clock g_clockTicks   -> here
 *   (3) the pose throttle and the drawn euler -> draw_interpolation.c
 *   (4) the emitter dormancy counter       -> here
 *
 * What is not a compensation but a choice, each behind its own switch and each off unless the ini
 * says otherwise: measuring the frame period more precisely (frame_delta.c), drawing particles at
 * the instant the frame shows (particle_clock.c), rebasing the world clock so the drawn instant
 * and the simulation agree (sim_clock.c), and interpolating movers (mover_interpolation.c).
 *
 * What is proven not to need compensation: the LOD cross-fade (a real seconds delta), the input
 * latch and the pause gate. Movers were on that list and are not any more: they derive their dt
 * from the world clock, which only the substep loop advances, so they are correct rather than
 * smooth, and smoothing them is the separate opt-in above.
 */
#include "framerate_fix.h"

#include "camera_compensation.h"
#include "draw_interpolation.h"
#include "face_latch.h"
#include "frame_delta.h"
#include "framerate_stats.h"
#include "mover_interpolation.h"
#include "particle_clock.h"
#include "sim_clock.h"

#include "common/frame_hook.h"
#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"
#include "common/signature.h"

#include <windows.h>
#include <mmsystem.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#pragma comment(lib, "winmm.lib")

#define FRAMERATE_SECTION "framerate_fix"

/* --- 0x00475B75  sys_waitForFrame (function start) ------------------------------------------- *
 *   +0x10 : the 1/30 immediate (0x3D088889)
 *   +0x19 : the 1/60 immediate (the "60fps" cheat arm)
 *   +0x3B : the absolute address of g_frameLimiterOn   (cmp [0x4B7D78], 0 at +0x39)
 *   +0x54 : the `push 0` fed to Sleep                                                           */
static const uint8_t SIG_WAIT_FOR_FRAME[] = {
    0x55, 0x8B, 0xEC, 0x51, 0x83, 0x3D, 0x94, 0x22, 0x88, 0x00, 0x00, 0x75, 0x09,
    0xC7, 0x45, 0xFC, 0x89, 0x88, 0x08, 0x3D, 0xEB, 0x07
};
#define OFFSET_CAP_30_IMMEDIATE  0x10u
#define OFFSET_CAP_60_IMMEDIATE  0x19u
#define OFFSET_LIMITER_CMP       0x39u
#define OFFSET_LIMITER_ADDRESS   0x3Bu
#define OFFSET_SLEEP_PUSH        0x54u

/* --- 0x00475737  sys_runSubsteps: the simulation rate selector ------------------------------- *
 *   83 3D 94228800 00                cmp [g_cheat60fps], 0
 *   75 0C                            jne +0x0C
 *   C7 05 14878600 0000003D          g_frameDelta = 1/32     <- imm at +0x0F
 *   EB 0A
 *   C7 05 14878600 0000803C          g_frameDelta = 1/64     <- imm at +0x1B
 *
 * This is the one place the simulation rate is decided, and [0x882294] is the "60fps" CHEAT,
 * one flag, read in exactly two places in the whole image, that moves the render cap 30->60 AND
 * the substep 32->64 together. Doubling the substep doubles every PURE PER-SUBSTEP constant:
 *
 *     NPC gravity          velocity.z -= 0.9 per substep         (aiext.c:3123)
 *     NPC turn ramp        3, 6, 9, 12 deg PER SUBSTEP           (aiext.c:2846)
 *     pathfinding throttle exactly ONE actor re-plans per substep (aiext.c:2714)
 *     pitch convergence    new*0.25 + old*0.75 per substep       (aiext.c:3239)
 *     shimmy               0.01 u per substep                    (player.c:1734)
 *
 * Everything else in NPC movement IS dt-scaled and therefore immune, because inside the substep
 * loop g_frameDelta IS the substep. So: leave the substep alone and NPC speed, turning and
 * pathfinding cannot change, whatever the render rate is. Move it and all five break at once. */
static const uint8_t SIG_SUBSTEP_SELECT[] = {
    0x83, 0x3D, 0x94, 0x22, 0x88, 0x00, 0x00, 0x75, 0x0C,
    0xC7, 0x05, 0x14, 0x87, 0x86, 0x00, 0x00, 0x00, 0x00, 0x3D,
    0xEB, 0x0A,
    0xC7, 0x05, 0x14, 0x87, 0x86, 0x00, 0x00, 0x00, 0x80, 0x3C
};
#define OFFSET_SUBSTEP_32_IMMEDIATE 0x0Fu
#define OFFSET_SUBSTEP_64_IMMEDIATE 0x1Bu
#define SUBSTEP_32_BITS  0x3D000000u
#define SUBSTEP_64_BITS  0x3C800000u

/* --- 0x0046C1B5  g_clockTicks++ inside render_frameEnd --------------------------------------- *
 *   8B 15 10 87 86 00   mov edx, [g_clockTicks]     -> its address at +0x02
 *
 * g_clockTicks [0x868710] is incremented ONCE PER FRAME by exactly one instruction in the whole
 * image. bapmap_waterWave 0x428615 reads it as if it were a clock, so at 144 fps the water runs
 * 4.8x too fast.
 *
 * CORRECTED 2026-08-07 (control pass, byte census of [0x868710]). The surface UV animation does
 * NOT read this counter; that sentence stood here and was wrong. The whole-image census finds
 * NINE instructions touching 0x868710: one writer (0x0046C1BE) and eight readers, and none of them
 * is in bapvrt. bapvrt_stepMaterialAnim 0x0041AE50 reads `[world+0x50]` (0x0041AE9F) and divides
 * by the material's period; world+0x50 is written ONLY by 0x0041F0C9 (`__ftol(seconds * 1000.0)`,
 * const 0x4A8214 = 1000.0f), which has exactly TWO callers, both inside sys_runSubsteps
 * (0x00475794, 0x004757B4), both passing simulation seconds. So world+0x50 is the SIMULATION clock
 * in milliseconds, advanced per substep, the surface UV animation is already frame-rate
 * independent and AnimationClockMode does not govern it.
 *
 * The other seven readers, and why the water is still the only one that matters:
 *   0x0040D2B8 + 0x0040D3C0 (fn 0x0040D269), a once-per-frame cache stamp: `if (cache != ticks)
 *                                              invalidate; ... cache = ticks`. Rate-independent.
 *   0x0042F183 + 0x0042F27D, candy_pushTrail / candy_allocTrailNode: a trail node is stamped with
 *                              the frame counter and expires `birth + len + 3` frames later. This
 *                              IS rate-dependent (a trail dies 3x sooner at 90 fps), and it is NOT
 *                              covered by AnimationClockMode, which rewrites the counter to
 *                              elapsed*30; that happens to fix it too, for the same reason it
 *                              fixes the water. Left as an observation, not a claim of intent.
 *   0x00475C08 + 0x00475C3A (fn 0x00475BE1), the fps readout; its output 0x006F83D0 has exactly
 *                              one reader, the debug overlay at 0x0046C254. Cosmetic. */
static const uint8_t SIG_CLOCK_TICKS_INCREMENT[] = {
    0x8B, 0x15, 0x10, 0x87, 0x86, 0x00, 0x83, 0xC2, 0x01, 0x89, 0x15, 0x10, 0x87, 0x86, 0x00
};
#define OFFSET_CLOCK_TICKS_ADDRESS 0x02u

/* --- 0x0042238D  emitter_renderAll: the thirty-culled-frames dormancy counter ---------------- *
 *   83 B8 14010000 1E   cmp [eax+0x114], 0x1E      <- imm8 at +0x06
 *   7C 46               jl  ...
 * An emitter that has failed both culls thirty times goes dormant. That is a FRAME COUNT used as a
 * clock: at 144 fps an off-screen emitter sleeps after 0.21 s instead of 1.0 s. The compare is
 * `83 /7 ib`, a SIGN-EXTENDED imm8, so the largest value we can write is 127.
 *
 * The count is CUMULATIVE and not consecutive, which this comment used to get wrong. Across the
 * whole particle band the counter field at +0x114 has exactly two writers: the increment at
 * 0x00422384 and a zero inside emitter_rebaseClocks 0x00422427, which is the wake-up path. Neither
 * is on the draw path, so a frame that does draw the emitter does not reset it. That does not
 * change the patch, which corrects the same quantity either way; it changes what the number means,
 * because an emitter that is culled intermittently still reaches the limit eventually. */
static const uint8_t SIG_EMITTER_DORMANCY[] = {
    0x83, 0xB8, 0x14, 0x01, 0x00, 0x00, 0x1E, 0x7C, 0x46, 0x8B, 0x4D
};
#define OFFSET_DORMANCY_IMMEDIATE 0x06u
#define DORMANCY_RETAIL_FRAMES      30u
#define DORMANCY_MAX_FRAMES        127u

enum {
    SITE_WAIT_FOR_FRAME,
    SITE_SUBSTEP_SELECT,
    SITE_CLOCK_TICKS_INCREMENT,
    SITE_EMITTER_DORMANCY,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    /* A detour target since frame_delta.c took it, so it has to use the detour form: this pattern
     * begins with the very bytes that detour overwrites, and without the fallback this entry would
     * resolve on a run where frame_delta is off and silently stop resolving on one where it is on,
     * taking the render cap with it. */
    SIGNATURE_ENTRY_DETOUR("wait_for_frame",  SIG_WAIT_FOR_FRAME, 11u),
    SIGNATURE_ENTRY("substep_select",        SIG_SUBSTEP_SELECT),
    SIGNATURE_ENTRY("clock_ticks_increment", SIG_CLOCK_TICKS_INCREMENT),
    SIGNATURE_ENTRY("emitter_dormancy",      SIG_EMITTER_DORMANCY)
};

typedef struct framerate_config {
    bool  enabled;
    int   target_fps;            /* 0 = uncapped (clears g_frameLimiterOn) */
    bool  compensate_camera;
    bool  compensate_camera_anchor;  /* the one camera patch that REWRITES code, not an operand */
    bool  compensate_animation;
    bool  spin_sleep;
    bool  pin_simulation_rate;
    int   animation_clock_mode;  /* 0 = per frame (original), 1 = 30 Hz (authored rate) */
    bool  interpolate_pitch_roll;
    bool  pose_per_frame;
    int   face_latch_yield;
    bool  precise_frame_time;
    bool  interpolate_particles;
    bool  rebase_sim_clock;
    bool  interpolate_movers;
    float mover_travel_limit;    /* world units a mover may cross in one simulation step */
    int   process_priority;      /* 0 = leave it alone, 1 = above normal, 2 = high */
    int   stats_frame_interval;
    int   stats_player_frames;
} framerate_config_t;

typedef struct framerate_state {
    bool               installed;
    framerate_config_t config;

    volatile float    *frame_delta;
    volatile uint32_t *clock_ticks;

    float              smoothed_delta;
    double             clock_accumulator;
} framerate_state_t;

static framerate_state_t framerate_state;

/* ============================================================================================ */
static void load_config(void)
{
    framerate_config_t *config = &framerate_state.config;

    config->enabled                = ini_read_bool(FRAMERATE_SECTION, "Enabled", true);
    config->target_fps             = ini_read_int (FRAMERATE_SECTION, "TargetFps", 0);
    config->compensate_camera      = ini_read_bool(FRAMERATE_SECTION, "CompensateCamera", true);
    config->compensate_camera_anchor =
        ini_read_bool(FRAMERATE_SECTION, "CompensateCameraAnchor", true);
    config->compensate_animation   = ini_read_bool(FRAMERATE_SECTION, "CompensateAnimation", true);
    config->spin_sleep             = ini_read_bool(FRAMERATE_SECTION, "SpinSleep", false);
    config->pin_simulation_rate    = ini_read_bool(FRAMERATE_SECTION, "PinSimulationRate", true);
    config->animation_clock_mode   = ini_read_int (FRAMERATE_SECTION, "AnimationClockMode", 1);
    config->interpolate_pitch_roll = ini_read_bool(FRAMERATE_SECTION, "InterpolatePitchRoll", true);
    config->pose_per_frame         = ini_read_bool(FRAMERATE_SECTION, "PosePerFrame", true);
    config->face_latch_yield       = ini_read_int (FRAMERATE_SECTION, "FaceLatchYield", 16);
    config->precise_frame_time     = ini_read_bool(FRAMERATE_SECTION, "PreciseFrameTime", true);
    config->process_priority       = ini_read_int (FRAMERATE_SECTION, "ProcessPriority", 0);
    config->interpolate_particles  =
        ini_read_bool(FRAMERATE_SECTION, "InterpolateParticles", true);
    config->rebase_sim_clock       = ini_read_bool(FRAMERATE_SECTION, "RebaseSimClock", true);
    config->interpolate_movers     = ini_read_bool(FRAMERATE_SECTION, "InterpolateMovers", true);
    config->mover_travel_limit     =
        ini_read_float(FRAMERATE_SECTION, "MoverTravelLimitPerStep", 64.0f);
    config->stats_frame_interval   = ini_read_int (FRAMERATE_SECTION, "StatsFrameInterval", 0);
    config->stats_player_frames    = ini_read_int (FRAMERATE_SECTION, "StatsPlayerFrames", 0);

    if (config->target_fps < 0)    { config->target_fps = 0; }
    if (config->target_fps > 1000) { config->target_fps = 1000; }
    if (config->animation_clock_mode < 0 || config->animation_clock_mode > 1) {
        config->animation_clock_mode = 1;
    }
    if (config->face_latch_yield < 0)  { config->face_latch_yield = 0; }
    if (config->face_latch_yield > 64) { config->face_latch_yield = 64; }
    if (!(config->mover_travel_limit > 0.0f)) { config->mover_travel_limit = 0.0f; }
    if (config->stats_frame_interval < 0) { config->stats_frame_interval = 0; }
    if (config->stats_player_frames  < 0) { config->stats_player_frames  = 0; }
}

/* ============================================================================================ */
/* The frame delta cell holds two different quantities, and which one this DLL reads is decided by
 * WHERE it reads it.
 *
 * A whole-image census of the dword [0x868714] finds five writers and 65 readers. The writers are
 * the clamp on the incoming dt at 0x00475713, the substep at 0x00475740, the substep-with-cheat at
 * 0x0047574C, a restore at 0x00475820 and the measured frame period at 0x00475BA8. sys_runSubsteps
 * saves the frame period into a local at 0x00475734, pins the cell to the substep for the duration
 * of its loop, and puts the frame period back at 0x00475820 on its way out.
 * So a reader inside the loop gets the substep and a reader outside it gets the frame
 * period. The per-frame hook below runs at render_frameEnd, which is outside, so what it reads is
 * the frame period. Reading only the first half of that sentence leads to the opposite conclusion,
 * which was drawn once here and cost a round. */
static void resolve_globals(void)
{
    uintptr_t frame_end = frame_hook_site();
    uintptr_t clock_site = sites[SITE_CLOCK_TICKS_INCREMENT].address;
    uint32_t  address;

    if (frame_end != 0 &&
        memory_read_u32(frame_end + FRAME_HOOK_FRAME_DELTA_OPERAND_OFFSET, &address) &&
        memory_is_inside_image(address, sizeof(float))) {
        framerate_state.frame_delta = (volatile float *)(uintptr_t)address;
        log_info("g_frameDelta = %08X", (unsigned)address);
    }

    if (clock_site != 0 &&
        memory_read_u32(clock_site + OFFSET_CLOCK_TICKS_ADDRESS, &address) &&
        memory_is_inside_image(address, sizeof(uint32_t))) {
        framerate_state.clock_ticks = (volatile uint32_t *)(uintptr_t)address;
        log_info("g_clockTicks = %08X", (unsigned)address);
    }

    if (framerate_state.config.compensate_animation && framerate_state.clock_ticks == NULL) {
        log_warning("g_clockTicks did not resolve, water waves WILL run at the frame rate");
    }
}

/* ============================================================================================ */
static void patch_render_cap(void)
{
    uintptr_t site = sites[SITE_WAIT_FOR_FRAME].address;
    uint8_t   cmp_opcode[2];
    uint32_t  limiter_address;

    if (site == 0) {
        log_warning("wait_for_frame did not resolve, the 30 Hz cap STAYS");
        return;
    }

    if (framerate_state.config.target_fps > 0) {
        float cap = 1.0f / (float)framerate_state.config.target_fps;

        patch_write_f32(site + OFFSET_CAP_30_IMMEDIATE, cap);
        patch_write_f32(site + OFFSET_CAP_60_IMMEDIATE, cap);   /* the cheat arm must not undo us */
        log_info("render cap -> %d fps (%.8f s) at %08X and %08X",
                 framerate_state.config.target_fps, (double)cap,
                 (unsigned)(site + OFFSET_CAP_30_IMMEDIATE),
                 (unsigned)(site + OFFSET_CAP_60_IMMEDIATE));
    } else {
        /* Uncapped: clear g_frameLimiterOn. Its address is the operand of `cmp [imm32], 0`, and
         * the opcode pair is checked before the operand is believed. */
        if (!memory_read(site + OFFSET_LIMITER_CMP, cmp_opcode, sizeof(cmp_opcode)) ||
            cmp_opcode[0] != 0x83 || cmp_opcode[1] != 0x3D) {
            log_warning("the limiter `cmp [imm32],0` shape is not at %08X, so the cap is left "
                        "alone", (unsigned)(site + OFFSET_LIMITER_CMP));
            return;
        }
        if (!memory_read_u32(site + OFFSET_LIMITER_ADDRESS, &limiter_address) ||
            !memory_is_inside_image(limiter_address, sizeof(uint32_t))) {
            log_warning("the limiter address %08X is out of image, the cap is left alone",
                        (unsigned)limiter_address);
            return;
        }
        patch_write_u32(limiter_address, 0);
        log_info("UNCAPPED, g_frameLimiterOn [%08X] cleared", (unsigned)limiter_address);
    }

    if (framerate_state.config.spin_sleep) {
        /* The wait is `push 0 / call [Sleep]`. Turning the 0 into a 1 gives the scheduler a real
         * yield and drops the busy core from 100 % to a few percent, at the price of up to about
         * 1 ms of overshoot per frame. Off by default because that overshoot is visible jitter at
         * high frame rates; the original really does spin. */
        static const uint8_t expected_push[2] = { 0x6A, 0x00 };
        uintptr_t push_site = site + OFFSET_SLEEP_PUSH;

        if (patch_validate_bytes(push_site, expected_push, sizeof(expected_push))) {
            timeBeginPeriod(1);
            patch_write_u8(push_site + 1, 1);
            log_info("SpinSleep on, Sleep(0) becomes Sleep(1) at %08X, timer resolution 1 ms",
                     (unsigned)push_site);
        } else {
            log_warning("SpinSleep, no `push 0` at %08X, left alone", (unsigned)push_site);
        }
    }
}

static void pin_simulation_rate(void)
{
    uintptr_t site = sites[SITE_SUBSTEP_SELECT].address;
    uint32_t  immediate_32;
    uint32_t  immediate_64;

    if (!framerate_state.config.pin_simulation_rate) {
        log_warning("PinSimulationRate=0, the '60fps' cheat can still move the SIMULATION to "
                    "64 Hz, which doubles NPC gravity, the turn ramp and the pathfinding re-plan "
                    "rate");
        return;
    }
    if (site == 0) {
        log_warning("substep_select did not resolve, the simulation rate is NOT pinned");
        return;
    }

    if (!memory_read_u32(site + OFFSET_SUBSTEP_32_IMMEDIATE, &immediate_32) ||
        !memory_read_u32(site + OFFSET_SUBSTEP_64_IMMEDIATE, &immediate_64)) {
        return;
    }
    if (immediate_32 != SUBSTEP_32_BITS || immediate_64 != SUBSTEP_64_BITS) {
        log_warning("the substep immediates are %08X / %08X, expected %08X / %08X, refused",
                    (unsigned)immediate_32, (unsigned)immediate_64,
                    (unsigned)SUBSTEP_32_BITS, (unsigned)SUBSTEP_64_BITS);
        return;
    }

    patch_write_u32(site + OFFSET_SUBSTEP_64_IMMEDIATE, SUBSTEP_32_BITS);
    log_info("simulation rate PINNED at 1/32 s, both arms of %08X now write %08X, so the "
             "'60fps' cheat can no longer touch the substep",
             (unsigned)site, (unsigned)SUBSTEP_32_BITS);
}

static void patch_emitter_dormancy(void)
{
    uintptr_t site = sites[SITE_EMITTER_DORMANCY].address;
    uintptr_t immediate;
    uint8_t   current;
    unsigned  frames;

    if (!framerate_state.config.compensate_animation) {
        return;
    }
    if (site == 0) {
        log_warning("emitter_dormancy did not resolve, off-screen emitters sleep early");
        return;
    }

    immediate = site + OFFSET_DORMANCY_IMMEDIATE;
    if (!memory_read_u8(immediate, &current)) {
        return;
    }
    if (current != DORMANCY_RETAIL_FRAMES) {
        log_warning("the emitter dormancy immediate is %u, expected %u, refused",
                    current, DORMANCY_RETAIL_FRAMES);
        return;
    }

    /* Exact up to 127 fps and saturating above it (0.88 s at 144, 0.53 s at 240). */
    frames = (framerate_state.config.target_fps > 0)
           ? (unsigned)framerate_state.config.target_fps
           : DORMANCY_MAX_FRAMES;
    if (frames < DORMANCY_RETAIL_FRAMES) { frames = DORMANCY_RETAIL_FRAMES; }
    if (frames > DORMANCY_MAX_FRAMES)    { frames = DORMANCY_MAX_FRAMES; }

    patch_write_u8(immediate, (uint8_t)frames);

    /* The seconds are reported against the rate the count was DERIVED from, and when there is no
     * such rate the line says so instead of printing a number. It used to divide by the
     * configured rate unconditionally, so with the limiter removed it printed "0.00 s", which
     * reads as a measurement of zero rather than as the absence of one. */
    if (framerate_state.config.target_fps > 0) {
        log_info("emitter dormancy %u -> %u frames at %08X, which is %.2f s at the configured "
                 "%d fps", DORMANCY_RETAIL_FRAMES, frames, (unsigned)immediate,
                 (double)frames / framerate_state.config.target_fps,
                 framerate_state.config.target_fps);
        return;
    }

    /* Uncapped, so the count cannot be turned into a time in advance, and the ceiling is not
     * ours to choose: the engine compares against a sign extended byte immediate, 83 B8 14 01 00
     * 00 1E, so 127 is the largest value those bytes can hold at all. The dormancy therefore
     * shortens as the frame rate rises, and above about 127 frames a second it is shorter than
     * the second the original intended. That is a real regression toward the defect this patch
     * exists to remove, and removing it properly means counting something other than rendered
     * frames here, which needs the counter's own increment site rather than this comparison.
     * Until then it is named rather than hidden. */
    log_info("emitter dormancy %u -> %u frames at %08X. The engine compares against a byte "
             "immediate, so 127 frames is the hard ceiling. With no configured frame rate that "
             "is one second only while the game runs at 127 frames a second; at 240 it is 0.53 s "
             "and at 450 it is 0.28 s, so an off-screen emitter sleeps earlier than the original "
             "intended. Set TargetFps to a real cap if that matters more than the frame rate.",
             DORMANCY_RETAIL_FRAMES, frames, (unsigned)immediate);
}

/* ============================================================================================
 * The compensation must not follow frame-TIME NOISE.
 *
 * The damping law k' = k^(dt*30) is a statement about the frame RATE, not about how long one
 * particular frame happened to take. Driving it from the instantaneous dt was wrong twice over:
 *
 *   * at a vsynced 60 fps the measured dt swings roughly 15.5..17.6 ms, so s = dt*30 swings
 *     0.466..0.529 and the camera's easing factor wobbled with the measurement noise EVERY frame,
 *     which is visible as a shimmer on everything the camera carries;
 *   * and because the five factors are IMMEDIATE OPERANDS in .text, every wobble rewrote live
 *     code and made two FlushInstructionCache calls per frame, inside the frame loop.
 *
 * So: low-pass the frame time, quantise the result, and only touch .text when the quantised value
 * really moves. At a steady frame rate that is once, at startup.
 * ============================================================================================ */
#define SMOOTHING_FACTOR   0.0625f   /* 1/16 per frame settles in about 0.25 s at 60 fps */
#define SCALE_QUANTISATION 256.0f
#define MAX_PLAUSIBLE_DELTA 0.25f

static void on_frame(void)
{
    float frame_delta;
    float scale;

    if (framerate_state.frame_delta == NULL) {
        return;
    }
    frame_delta = *framerate_state.frame_delta;

    /* A zero or absurd frame delta is not an invitation to invent one. Substituting 1/30 here
     * made the camera take a full 30 Hz-sized step on a frame that took no time at all, pure
     * jitter at a high frame rate. Skipping is correct: the constants simply keep the value they
     * had, which is what a zero-length frame deserves. */
    if (!(frame_delta > 0.0f) || frame_delta > MAX_PLAUSIBLE_DELTA) {
        return;                                    /* also catches NaN */
    }

    if (framerate_state.smoothed_delta <= 0.0f) {
        framerate_state.smoothed_delta = frame_delta;
    } else {
        framerate_state.smoothed_delta +=
            (frame_delta - framerate_state.smoothed_delta) * SMOOTHING_FACTOR;
    }

    if (framerate_state.config.compensate_camera) {
        scale = framerate_state.smoothed_delta * 30.0f;
        scale = (float)((int32_t)(scale * SCALE_QUANTISATION + 0.5f)) / SCALE_QUANTISATION;
        camera_compensation_update(scale);
    }

    /* THE ANIMATION CLOCK, and it is a genuine choice rather than a fix.
     * g_clockTicks is an INTEGER counter that the water wave consumes as
     * (float)(uint32_t)(ticks * rate) degrees, so its resolution is one tick, whatever we do.
     * (Not the UV scroll: see the corrected census at SIG_CLOCK_TICKS_INCREMENT. The surface UV
     * animation runs on world+0x50, the per-substep simulation clock, and is already correct.)
     *   mode 1 (default): ticks = elapsed * 30. The AUTHORED speed at any frame rate, but the
     *                     phase visibly advances 30 times a second. It also pins the engine's own
     *                     frame-rate readout at exactly 30.0, because that readout divides this
     *                     counter's movement by real seconds and the counter is now synthetic. The
     *                     debug page is therefore not a second opinion on the frame rate while
     *                     this mode is on; the statistics window is.
     *   mode 0:           leave the engine's per-frame increment alone. Smooth, but the water and
     *                     the scrolling textures run at fps/30 times their authored speed.
     * There is no third option: the consumer truncates to uint32 before it converts to float, so
     * a fractional clock cannot be expressed. */
    if (framerate_state.config.compensate_animation &&
        framerate_state.config.animation_clock_mode == 1 &&
        framerate_state.clock_ticks != NULL) {
        framerate_state.clock_accumulator += (double)frame_delta * 30.0;
        *framerate_state.clock_ticks = (uint32_t)framerate_state.clock_accumulator;
    }

    framerate_stats_sample(frame_delta);
    mover_interpolation_sample();
    sim_clock_sample();
}

/* ============================================================================================ */
/* Lifts the whole process above ordinary background work.
 *
 * A scheduling repair rather than a performance one, and it treats a symptom the engine cannot
 * defend against itself. The game is single threaded and saturates one core: measurements put it
 * between 92 and 103 per cent of a core while drawing. On a machine with many cores that shows in
 * the task manager as a low total, so a second process busy on the same core looks harmless while
 * it is in fact taking the game's time slice away. Every slice the drawing thread loses is a
 * frame delivered late, and with vertical sync a late frame does not arrive late by the amount it
 * was delayed, it misses the refresh and arrives a whole refresh interval late instead.
 *
 * Off by default, because raising a process above normal is a decision about the whole machine.
 * High is offered; real time deliberately is not, because that would starve the very drivers
 * that deliver the input this is meant to protect.
 *
 * It has NOT been shown to repair anything. A deliberate load test, twenty eight busy workers on
 * a twenty eight processor machine with this switched off, produced a measurable rise in late
 * frames and nothing a player could see. Treat it as a precaution, not as a fix. */
static void apply_process_priority(void)
{
    DWORD       priority;
    const char *name;

    switch (framerate_state.config.process_priority) {
    case 0:
        return;
    case 1:
        priority = ABOVE_NORMAL_PRIORITY_CLASS;
        name     = "above normal";
        break;
    case 2:
        priority = HIGH_PRIORITY_CLASS;
        name     = "high";
        break;
    default:
        log_warning("ProcessPriority=%d is not one of 0, 1 or 2, so the priority is left alone",
                    framerate_state.config.process_priority);
        return;
    }

    if (!SetPriorityClass(GetCurrentProcess(), priority)) {
        log_warning("the process priority could not be raised to %s, so it stays at normal and a "
                    "busy machine can still take the drawing thread's time slice away", name);
        return;
    }
    log_info("process priority raised to %s. The game is single threaded and saturates one core, "
             "so an equally ranked background process competes with it directly even while the "
             "task manager shows a low total across all cores. A lost time slice is a late frame, "
             "and with vertical sync a late frame costs a whole refresh interval rather than the "
             "delay itself.", name);
}

void framerate_fix_install(void)
{
    log_init("framerate_fix", false);

    if (framerate_state.installed) {
        return;
    }
    if (!host_image_resolve()) {
        log_error("no 32-bit host image, the frame rate is NOT patched");
        return;
    }

    load_config();
    if (!framerate_state.config.enabled) {
        log_info("Enabled=0, the 30 Hz cap and every compensation stay off");
        return;
    }

    /* Before any patching, because it touches no engine memory and a failure here must not
     * leave a half patched image behind. */
    apply_process_priority();

    signature_resolve_table(sites, SITE_COUNT);

    /* FIRST: the simulation must not move, whatever else we do. */
    pin_simulation_rate();
    patch_render_cap();
    patch_emitter_dormancy();

    if (framerate_state.config.compensate_camera) {
        camera_compensation_install(framerate_state.config.compensate_camera_anchor);
    } else {
        log_info("CompensateCamera=0, the camera will feel rigid above 30 fps");
    }

    if (framerate_state.config.interpolate_pitch_roll) {
        draw_interpolation_install_euler();
    } else {
        log_info("InterpolatePitchRoll=0, drawn pitch and roll keep stepping at 32 Hz");
    }
    face_latch_install(framerate_state.config.face_latch_yield);

    /* The frame delta detour goes on before the particle and mover work, because both of those
     * publish a value derived from the substep alpha and the alpha is only as good as the period
     * the wait measured. */
    frame_delta_install(framerate_state.config.precise_frame_time);
    particle_clock_install(framerate_state.config.interpolate_particles);
    sim_clock_install(framerate_state.config.rebase_sim_clock);
    mover_interpolation_install(framerate_state.config.interpolate_movers,
                                framerate_state.config.mover_travel_limit);

    if (framerate_state.config.pose_per_frame) {
        draw_interpolation_install_pose_throttle();
    } else {
        log_info("PosePerFrame=0, joint matrices keep rebuilding only on substep boundaries, so "
                 "animation steps at 32 Hz above 32 fps");
    }

    framerate_state.installed = true;

    if (!frame_hook_add(on_frame)) {
        log_warning("no per-frame hook, the camera compensation and the animation clock do NOT "
                    "run. The render cap, the pinned simulation rate, the emitter dormancy and "
                    "both draw patches are already in place and stay in place.");
        return;
    }

    resolve_globals();
    framerate_stats_install(framerate_state.config.stats_frame_interval,
                            framerate_state.config.stats_player_frames);
}
