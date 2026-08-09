#include "framerate_stats.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* --- 0x004757DB  sys_runSubsteps tail: the two numbers a measurement needs ------------------- *
 * The signature below is the LOOP TAIL, 31 bytes, 0x004757DB..0x004757F9:
 *   A1 60884B00        mov  eax,[g_tickCounter]  -> address at +0x01   (one per SUBSTEP)
 *   83 C0 01           add  eax,1
 *   A3 60884B00        mov  [g_tickCounter],eax
 *   D9 05 28878600     fld  [g_simTime]                                (0x00868728)
 *   D8 05 14878600     fadd [g_frameTime]                              (0x00868714 = the substep dt
 *                                                                       while the loop is running)
 *   D9 1D 28878600     fstp [g_simTime]          <- simTime += dt
 *
 * +0x3E reaches PAST the signature, over the 5-byte `E9 jmp 0x00475756` at 0x004757FA and the four
 * x87 instructions of the loop EXIT arm, and lands on the disp32 of
 *   0x00475817  D9 1D 1C878600  fstp [g_substepAlpha]   -> its address at +0x3E   (0x0086871C)
 * The opcode is verified before the operand is read.
 *
 * CORRECTED 2026-08-07 (control pass). The three x87 lines above previously read
 * `fld [g_simTarget] / fsub [g_simTime] / fstp [g_substepAlpha]`, which is not what these bytes
 * are; that is a garbled copy of the EXIT arm at 0x004757FF. The exit arm is actually
 *   alpha = (g_simTarget, g_simTime + dt) / dt
 * a four-instruction sequence (fld/fsub/fadd/fdiv), and it lives after the jmp, not inside the
 * signature. Consequence worth keeping: at loop exit the invariant is
 * 0 <= g_simTime, g_simTarget < dt, so alpha is always in (0,1] whenever sys_runSubsteps runs at
 * all. INCLUDING when the loop body iterated zero times. A window that reports alpha constant
 * 0.000 therefore does not mean "the substep loop did not iterate"; it means sys_runSubsteps was
 * never CALLED in that window (the only other writer of 0x0086871C is `mov [0x0086871C],0` at
 * 0x00475ACB, the msg-1 init arm of the time module 0x00475AB0). In the field log that is exactly
 * what a menu window looks like.                                                                */
static const uint8_t SIG_SUBSTEP_TAIL[] = {
    0xA1, 0x60, 0x88, 0x4B, 0x00, 0x83, 0xC0, 0x01, 0xA3, 0x60, 0x88, 0x4B, 0x00,
    0xD9, 0x05, 0x28, 0x87, 0x86, 0x00, 0xD8, 0x05, 0x14, 0x87, 0x86, 0x00,
    0xD9, 0x1D, 0x28, 0x87, 0x86, 0x00
};
#define OFFSET_TICK_COUNTER   0x01u
#define OFFSET_SUBSTEP_ALPHA  0x3Eu   /* past the jmp; the opcode is verified before use */

/* --- 0x0044C06B  Plr_CommitPose: the handle on the PLAYER RECORD ----------------------------- *
 *   55 8B EC              prologue
 *   A1 20524B00           mov eax,[pPlayer]   <- the pointer's address at +0x04
 *   83 B8 A0000000 00     cmp [eax+0xA0], 0   (movedThisFrame)
 *   74 28                 je ...                                                                */
static const uint8_t SIG_PLAYER_COMMIT_POSE[] = {
    0x55, 0x8B, 0xEC, 0xA1, 0x20, 0x52, 0x4B, 0x00,
    0x83, 0xB8, 0xA0, 0x00, 0x00, 0x00, 0x00, 0x74, 0x28
};
#define OFFSET_PLAYER_POINTER 0x04u

enum {
    SITE_SUBSTEP_TAIL,
    SITE_PLAYER_COMMIT_POSE,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY("substep_tail",        SIG_SUBSTEP_TAIL),
    SIGNATURE_ENTRY("player_commit_pose",  SIG_PLAYER_COMMIT_POSE)
};

/* bapObj offsets the player dump reads. */
#define PLAYER_ACTOR_HANDLE   0x0C
#define OBJECT_POSITION       0x18
#define OBJECT_ROTATION       0x3C
#define OBJECT_PREVIOUS_POS   0x54
#define OBJECT_PREVIOUS_ROT   0x60
#define OBJECT_THING          0x9C
#define THING_PUPPET          0x18
#define PUPPET_FIRST_TRACK    0x08
#define TRACK_STRIDE         0x14C
#define TRACK_FLAGS          0x000
#define TRACK_CLIP_FPS       0x010
#define TRACK_TIME_FRAMES    0x120

/* The engine's own ceiling on a frame delta. A window whose SMALLEST sample already sits here has
 * nothing left to say about frame time, and the log must admit that rather than divide by it. */
#define FRAME_DELTA_CLAMP_SECONDS 0.1f

typedef struct framerate_stats_state {
    int              frame_interval;
    uint32_t         frames_left_for_player;

    const uint32_t  *tick_counter;
    const float     *substep_alpha;
    uint8_t        **player_pointer;

    uint32_t         frames_in_window;
    uint32_t         ticks_at_window_start;
    float            delta_minimum;
    float            delta_maximum;
    float            delta_sum;
    float            alpha_minimum;
    float            alpha_maximum;

    /* The wall clock, which nothing in the engine can clamp. */
    LARGE_INTEGER    clock_frequency;
    LARGE_INTEGER    clock_at_window_start;
} framerate_stats_state_t;

static framerate_stats_state_t stats_state;

/* ============================================================================================ */
static void resolve_substep_globals(void)
{
    uintptr_t site = sites[SITE_SUBSTEP_TAIL].address;
    uint32_t  address;
    uint8_t   opcode_high;
    uint8_t   opcode_low;

    if (site == 0) {
        return;
    }

    if (memory_read_u32(site + OFFSET_TICK_COUNTER, &address) &&
        memory_is_inside_image(address, sizeof(uint32_t))) {
        stats_state.tick_counter = (const uint32_t *)(uintptr_t)address;
    }

    /* The alpha store sits past a rel32 jmp, so verify the `fstp dword [abs32]` opcode pair
     * before believing the operand. */
    if (memory_read_u8(site + OFFSET_SUBSTEP_ALPHA - 2, &opcode_high) &&
        memory_read_u8(site + OFFSET_SUBSTEP_ALPHA - 1, &opcode_low) &&
        opcode_high == 0xD9 && opcode_low == 0x1D &&
        memory_read_u32(site + OFFSET_SUBSTEP_ALPHA, &address) &&
        memory_is_inside_image(address, sizeof(float))) {
        stats_state.substep_alpha = (const float *)(uintptr_t)address;
    }
}

static void resolve_player_pointer(void)
{
    uintptr_t site = sites[SITE_PLAYER_COMMIT_POSE].address;
    uint32_t  address;

    if (site == 0) {
        return;
    }
    if (memory_read_u32(site + OFFSET_PLAYER_POINTER, &address) &&
        memory_is_inside_image(address, sizeof(uint32_t))) {
        stats_state.player_pointer = (uint8_t **)(uintptr_t)address;
    }
}

void framerate_stats_install(int frame_sample_interval, int player_frames)
{
    if (frame_sample_interval <= 0 && player_frames <= 0) {
        return;                                    /* nothing on: resolve nothing, log nothing */
    }

    stats_state.frame_interval = frame_sample_interval;
    stats_state.frames_left_for_player = (player_frames > 0) ? (uint32_t)player_frames : 0;

    if (!QueryPerformanceFrequency(&stats_state.clock_frequency)) {
        stats_state.clock_frequency.QuadPart = 0;
        log_warning("no performance counter, the frame window can only report what the ENGINE "
                    "believes the frame time was, and that number is clamped at %.0f ms. Treat a "
                    "steady %.0f fps in the lines below as 'at least this slow', not as a "
                    "measurement.",
                    (double)(FRAME_DELTA_CLAMP_SECONDS * 1000.0f),
                    (double)(1.0f / FRAME_DELTA_CLAMP_SECONDS));
    }

    signature_resolve_table(sites, SITE_COUNT);
    resolve_substep_globals();
    resolve_player_pointer();

    /* The first window has no previous window to open it, so it is stamped here. Every later
     * one is stamped by log_frame_window as it closes. */
    stats_state.ticks_at_window_start =
        (stats_state.tick_counter != NULL) ? *stats_state.tick_counter : 0;
    if (!QueryPerformanceCounter(&stats_state.clock_at_window_start)) {
        stats_state.clock_at_window_start.QuadPart = 0;
    }

    if (frame_sample_interval > 0) {
        log_info("frame statistics every %d frames (tickCounter=%08X substepAlpha=%08X)",
                 frame_sample_interval, (unsigned)(uintptr_t)stats_state.tick_counter,
                 (unsigned)(uintptr_t)stats_state.substep_alpha);
    }
    if (player_frames > 0) {
        log_info("player draw dump armed for %d frames (pPlayer=%08X)",
                 player_frames, (unsigned)(uintptr_t)stats_state.player_pointer);
    }
}

/* ============================================================================================ */
/* The wall clock, and why this function exists at all.
 *
 * This window used to report its elapsed time as the sum of the engine's own frame deltas. That is
 * circular, and it fails in exactly the situation the measurement is for: the engine clamps its
 * delta to a tenth of a second, so once frames get slower than that, every sample reads the clamp,
 * the sum is `frames * 0.1 s`, and the line reports a steady ten frames a second no matter how bad
 * it really is. A field log showed five consecutive windows of "60 frames in 6.000 s, dt min = avg
 * = max = 100.000, jitter 1.0x" while the game was being played at a small fraction of that. The
 * instrument was reporting its own ceiling.
 *
 * So the elapsed time now comes from the performance counter, which nothing in the engine can
 * clamp, and the engine's delta is reported BESIDE it rather than instead of it. When the two
 * disagree the difference is the finding: `dt` is what the simulation believes, `real` is what
 * happened, and their ratio is how much slower than real time the game is running. */
static double window_seconds_from_wall_clock(void)
{
    LARGE_INTEGER now;

    if (stats_state.clock_frequency.QuadPart == 0 || !QueryPerformanceCounter(&now)) {
        return 0.0;
    }
    return (double)(now.QuadPart - stats_state.clock_at_window_start.QuadPart)
         / (double)stats_state.clock_frequency.QuadPart;
}

static void log_frame_window(void)
{
    uint32_t ticks = (stats_state.tick_counter != NULL)
                   ? (*stats_state.tick_counter - stats_state.ticks_at_window_start)
                   : 0;
    double   real  = window_seconds_from_wall_clock();
    double   rate  = (real > 0.0) ? (double)stats_state.frames_in_window / real : 0.0;
    float    simulated = stats_state.delta_sum;

    log_info("%u frames in %.3f s REAL | fps %.1f | the engine believes %.3f s (%.2fx real time, "
             "so the game runs %s) | dt ms min %.3f avg %.3f max %.3f (jitter %.1fx)%s | "
             "substeps %u (%.1f Hz, %.2f frames/substep) | alpha %.3f..%.3f",
             stats_state.frames_in_window, real, rate, (double)simulated,
             (real > 0.0) ? (double)simulated / real : 0.0,
             (real > 0.0 && (double)simulated < real * 0.95) ? "slower than real time"
                                                            : "at real time",
             (double)(stats_state.delta_minimum * 1000.0f),
             (double)(simulated / (float)stats_state.frames_in_window * 1000.0f),
             (double)(stats_state.delta_maximum * 1000.0f),
             (double)((stats_state.delta_minimum > 0.0f)
                      ? stats_state.delta_maximum / stats_state.delta_minimum : 0.0f),
             /* The clamp is the engine's own ceiling on a frame delta. Once every sample in a
              * window sits on it, the dt figures carry no information at all and saying so is the
              * whole point of printing them. */
             (stats_state.delta_minimum >= FRAME_DELTA_CLAMP_SECONDS)
                 ? ". every sample is on the clamp, dt carries no information here" : "",
             ticks, (real > 0.0) ? (double)ticks / real : 0.0,
             (double)((ticks != 0) ? (float)stats_state.frames_in_window / (float)ticks : 0.0f),
             (double)stats_state.alpha_minimum, (double)stats_state.alpha_maximum);

    /* The next window starts here, not on its first sample.
     *
     * Stamping the marks when the first frame of a window arrives measures N-1 intervals while
     * counting N frames, so everything derived from wall time came out N/(N-1) too high. At the
     * shipped interval of 60 that is 1.7 percent, which is why every window in every log reported
     * a frame rate just above the cap and a flat "1.02x real time". Closing one window and opening
     * the next at the same instant makes the windows contiguous, makes the interval count match
     * the frame count, and stops the substep total losing an interval as well. */
    stats_state.ticks_at_window_start =
        (stats_state.tick_counter != NULL) ? *stats_state.tick_counter : 0;
    if (!QueryPerformanceCounter(&stats_state.clock_at_window_start)) {
        stats_state.clock_at_window_start.QuadPart = 0;
    }
}

static void sample_frame_window(float frame_delta)
{
    if (stats_state.frames_in_window == 0) {
        stats_state.delta_minimum = frame_delta;
        stats_state.delta_maximum = frame_delta;
        stats_state.delta_sum     = 0.0f;
        stats_state.alpha_minimum = 1.0f;
        stats_state.alpha_maximum = 0.0f;
        /* The wall clock and the substep mark were stamped when the previous window closed, so the
         * measured interval count matches the frame count. The first window has no previous one,
         * and its marks were stamped at install. */
    }

    if (frame_delta < stats_state.delta_minimum) { stats_state.delta_minimum = frame_delta; }
    if (frame_delta > stats_state.delta_maximum) { stats_state.delta_maximum = frame_delta; }
    stats_state.delta_sum += frame_delta;

    if (stats_state.substep_alpha != NULL) {
        float alpha = *stats_state.substep_alpha;
        if (alpha < stats_state.alpha_minimum) { stats_state.alpha_minimum = alpha; }
        if (alpha > stats_state.alpha_maximum) { stats_state.alpha_maximum = alpha; }
    }

    ++stats_state.frames_in_window;
    if (stats_state.frames_in_window >= (uint32_t)stats_state.frame_interval) {
        log_frame_window();
        stats_state.frames_in_window = 0;
    }
}

/* ============================================================================================
 * The player draw, recomputed from the outside.
 *
 * bapobj_drawAll builds the player's draw transform as (byte-read at 0x4112xx):
 *     p   = prevPos + (pos, prevPos) * substepAlpha
 *     yaw = prevRot.y + angle_diff(prevRot.y, rot.y) * alpha
 * so logging alpha together with pos/prevPos/rot reproduces exactly what the renderer put on
 * screen, without touching the renderer. If `draw` walks smoothly and the picture does not, the
 * shake is not in the body transform; if `draw` itself stutters, the prev/cur pair is not two
 * clean substep samples and the interpolation has nothing to work with.
 * ============================================================================================ */
static void dump_animation_clock(const uint8_t *object)
{
    const uint8_t *thing;
    const uint8_t *puppet;
    const uint8_t *track;

    if (!memory_read(( uintptr_t)(object + OBJECT_THING), &thing, sizeof(thing)) ||
        thing == NULL) {
        return;
    }
    if (!memory_read((uintptr_t)(thing + THING_PUPPET), &puppet, sizeof(puppet)) ||
        puppet == NULL) {
        return;
    }

    track = puppet + PUPPET_FIRST_TRACK;
    if (!memory_is_readable_range((uintptr_t)track, 4 * TRACK_STRIDE)) {
        return;
    }

    /* track+0x120 is the time IN FRAMES, +0x010 the clip's own fps. If t climbs by
     * clipFps/renderFps every frame the clock is continuous and any stepping is elsewhere. */
    log_info("track t=%.4f/%.4f/%.4f/%.4f  fps=%.1f/%.1f/%.1f/%.1f  flags=%X/%X/%X/%X",
             (double)*(const float *)(track + 0 * TRACK_STRIDE + TRACK_TIME_FRAMES),
             (double)*(const float *)(track + 1 * TRACK_STRIDE + TRACK_TIME_FRAMES),
             (double)*(const float *)(track + 2 * TRACK_STRIDE + TRACK_TIME_FRAMES),
             (double)*(const float *)(track + 3 * TRACK_STRIDE + TRACK_TIME_FRAMES),
             (double)*(const float *)(track + 0 * TRACK_STRIDE + TRACK_CLIP_FPS),
             (double)*(const float *)(track + 1 * TRACK_STRIDE + TRACK_CLIP_FPS),
             (double)*(const float *)(track + 2 * TRACK_STRIDE + TRACK_CLIP_FPS),
             (double)*(const float *)(track + 3 * TRACK_STRIDE + TRACK_CLIP_FPS),
             *(const uint32_t *)(track + 0 * TRACK_STRIDE + TRACK_FLAGS),
             *(const uint32_t *)(track + 1 * TRACK_STRIDE + TRACK_FLAGS),
             *(const uint32_t *)(track + 2 * TRACK_STRIDE + TRACK_FLAGS),
             *(const uint32_t *)(track + 3 * TRACK_STRIDE + TRACK_FLAGS));
}

static void dump_player_draw(void)
{
    const uint8_t *record;
    const uint8_t *object;
    const float   *position;
    const float   *previous_position;
    const float   *rotation;
    const float   *previous_rotation;
    float          alpha;

    if (stats_state.player_pointer == NULL || stats_state.substep_alpha == NULL) {
        stats_state.frames_left_for_player = 0;
        return;
    }

    record = *stats_state.player_pointer;
    if (record == NULL ||
        !memory_read((uintptr_t)(record + PLAYER_ACTOR_HANDLE), &object, sizeof(object)) ||
        object == NULL) {
        return;
    }

    position          = (const float *)(object + OBJECT_POSITION);
    rotation          = (const float *)(object + OBJECT_ROTATION);
    previous_position = (const float *)(object + OBJECT_PREVIOUS_POS);
    previous_rotation = (const float *)(object + OBJECT_PREVIOUS_ROT);
    alpha             = *stats_state.substep_alpha;

    dump_animation_clock(object);

    log_info("player a=%.4f prev(%.4f %.4f %.4f) cur(%.4f %.4f %.4f) draw(%.4f %.4f %.4f) "
             "yaw prev=%.3f cur=%.3f pitch=%.3f roll=%.3f",
             (double)alpha,
             (double)previous_position[0], (double)previous_position[1],
             (double)previous_position[2],
             (double)position[0], (double)position[1], (double)position[2],
             (double)(previous_position[0] + (position[0] - previous_position[0]) * alpha),
             (double)(previous_position[1] + (position[1] - previous_position[1]) * alpha),
             (double)(previous_position[2] + (position[2] - previous_position[2]) * alpha),
             (double)previous_rotation[1], (double)rotation[1],
             (double)rotation[0], (double)rotation[2]);

    --stats_state.frames_left_for_player;
}

void framerate_stats_sample(float frame_delta)
{
    if (stats_state.frame_interval > 0) {
        sample_frame_window(frame_delta);
    }
    if (stats_state.frames_left_for_player != 0) {
        dump_player_draw();
    }
}
