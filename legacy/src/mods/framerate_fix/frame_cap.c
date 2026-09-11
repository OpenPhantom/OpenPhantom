/* frame_cap.c: see frame_cap.h. */
#include "frame_cap.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"

#include <windows.h>

/* --- 0x00475B75  sys_waitForFrame (function start) ------------------------------------------- *
 *   +0x10 : the 1/30 immediate (0x3D088889)
 *   +0x16 : `mov dword ptr [ebp-4],0x3C888889`, the "60fps" cheat arm, whose immediate is +0x19
 *   +0x39 : `cmp [g_frameLimiterOn], 0`, its absolute operand at +0x3B
 *
 * The site itself is resolved by framerate_fix.c, which owns the signature table for this
 * function: the render cap, the frame delta repair and the Sleep push all live on it, and one
 * owner for the pattern keeps them from each other's throats. */
#define OFFSET_CAP_30_IMMEDIATE 0x10u
#define OFFSET_CAP_60_MOV       0x16u
#define OFFSET_CAP_60_IMMEDIATE 0x19u
#define OFFSET_LIMITER_CMP      0x39u
#define OFFSET_LIMITER_ADDRESS  0x3Bu

/* The whole seven byte instruction rather than its immediate alone. Checking four bytes would
 * accept the right immediate in the wrong instruction, which on a build that diverges after the
 * matched prologue is a write into the middle of something else. */
static const uint8_t EXPECTED_CAP_60_MOV[7] = { 0xC7, 0x45, 0xFC, 0x89, 0x88, 0x88, 0x3C };

/* The limiter flag ships as 1. Clearing it is how uncapped is expressed, and putting it back is
 * how a live change returns from uncapped to capped. */
#define LIMITER_ON  1u
#define LIMITER_OFF 0u

/* VREFRESH, from wingdi.h. Named here rather than included for, because this is the only thing
 * this file wants out of it. */
#define DEVICE_CAP_VREFRESH 116

/* A rate outside this is a driver reporting something other than a rate. 0 and 1 both mean "the
 * hardware default" and neither is a number to cap to. */
#define REFRESH_MINIMUM 20
#define REFRESH_MAXIMUM 1000

/* The step policy's numbers. A tenth of a second's frames overrunning is a tenth of the refreshes
 * repeating a frame, which is the point at which it reads as judder rather than as a hiccup; the
 * one or two long frames a second that any machine produces are well under it. Room at the next
 * faster cap means the work fitting four fifths of that budget, so the step up does not land on a
 * cap the very next second overruns, and a fiftieth of the frames may miss that without spoiling
 * the second, which is the hiccup allowance again. */
#define OVERRUN_DIVISOR       10u
#define FASTER_MARGIN         0.8f
#define CROWDED_DIVISOR       50u


static uintptr_t cap_site;
static uintptr_t cap_limiter_address;
static bool      cap_usable;
static int       cap_applied = -1;      /* -1 means nothing has been applied yet */

/* What the settings asked for, kept so a second's verdict can be applied without the caller. */
static int       cap_configured;
static bool      cap_match_refresh;
static int       cap_pinned_divisor;
static int       cap_refresh_hz;
static int       cap_divisor = 1;
static unsigned  cap_clean_seconds;

/* The second being counted. */
static LARGE_INTEGER       clock_frequency;
static LARGE_INTEGER       work_started;          /* end of the last wait */
static LARGE_INTEGER       second_started;
static bool                have_work_start;
static frame_cap_second_t  second;

int frame_cap_effective(int configured, bool match_refresh, int refresh_hz, int divisor)
{
    if (configured < 0) {
        configured = 0;
    }
    if (configured > REFRESH_MAXIMUM) {
        configured = REFRESH_MAXIMUM;
    }
    if (!match_refresh) {
        return configured;
    }
    if (refresh_hz < REFRESH_MINIMUM || refresh_hz > REFRESH_MAXIMUM) {
        /* Asked to match and cannot. The configured number stands, because the alternatives are
         * to uncap the game or to cap it at nothing, and both are worse than the answer the
         * player already gave. */
        return configured;
    }
    if (divisor < 1) {
        divisor = 1;
    }
    if (divisor > frame_cap_divisor_limit(refresh_hz)) {
        divisor = frame_cap_divisor_limit(refresh_hz);
    }
    /* Rounded to the nearest whole frame a second: 144/4 is 36 exactly, 90/4 is 22.5 and is
     * refused by the limit above, 75/2 is 37.5 and rounds to 38, half a percent off even. */
    return (refresh_hz + divisor / 2) / divisor;
}

int frame_cap_divisor_limit(int refresh_hz)
{
    int divisor = FRAME_CAP_DIVISOR_MAX;

    while (divisor > 1 && refresh_hz / divisor < FRAME_CAP_FLOOR_FPS) {
        --divisor;
    }
    return divisor;
}

int frame_cap_step(int divisor, unsigned *clean_seconds, const frame_cap_second_t *second_counts,
                   int refresh_hz)
{
    int limit = frame_cap_divisor_limit(refresh_hz);

    if (divisor < 1) {
        divisor = 1;
    }
    if (divisor > limit) {
        divisor = limit;
    }
    if (second_counts == NULL || second_counts->frames < FRAME_CAP_JUDGE_FRAMES ||
        second_counts->longest_work >= FRAME_CAP_STALL_SECONDS) {
        /* Too few frames to be gameplay, or a stall inside it: a menu, a pause, a level coming
         * up. The first auto run stepped down on a level load and on a second of eight frames,
         * and neither was the machine failing to hold the cap. */
        return divisor;
    }

    if (second_counts->over_budget > second_counts->frames / OVERRUN_DIVISOR) {
        *clean_seconds = 0;
        return (divisor < limit) ? divisor + 1 : divisor;
    }

    if (divisor > 1 && second_counts->over_faster <= second_counts->frames / CROWDED_DIVISOR) {
        if (++*clean_seconds >= FRAME_CAP_CLEAN_SECONDS) {
            *clean_seconds = 0;
            return divisor - 1;
        }
    } else {
        *clean_seconds = 0;
    }
    return divisor;
}

int frame_cap_refresh_hz(void)
{
    HDC dc;
    int hz;

    dc = GetDC(NULL);
    if (dc == NULL) {
        return 0;
    }
    hz = GetDeviceCaps(dc, DEVICE_CAP_VREFRESH);
    (void)ReleaseDC(NULL, dc);

    if (hz < REFRESH_MINIMUM || hz > REFRESH_MAXIMUM) {
        return 0;
    }
    return hz;
}

bool frame_cap_install(uintptr_t wait_site)
{
    uint8_t  cmp_opcode[2];
    uint32_t limiter_address;

    cap_usable = false;
    if (wait_site == 0) {
        return false;
    }

    /* Everything a later write will touch is checked now, so a change made from the dev panel a
     * quarter of an hour into a session cannot be the thing that discovers a bad byte. */
    if (!patch_validate_bytes(wait_site + OFFSET_CAP_60_MOV, EXPECTED_CAP_60_MOV,
                              sizeof EXPECTED_CAP_60_MOV)) {
        log_warning("the 60fps cheat arm is not the expected mov [ebp-4],1/60 at %08X, so the "
                    "render cap is left alone entirely rather than half applied",
                    (unsigned)(wait_site + OFFSET_CAP_60_MOV));
        return false;
    }
    if (!memory_read(wait_site + OFFSET_LIMITER_CMP, cmp_opcode, sizeof cmp_opcode) ||
        cmp_opcode[0] != 0x83 || cmp_opcode[1] != 0x3D) {
        log_warning("the limiter cmp [imm32],0 shape is not at %08X, so the render cap is left "
                    "alone", (unsigned)(wait_site + OFFSET_LIMITER_CMP));
        return false;
    }
    if (!memory_read_u32(wait_site + OFFSET_LIMITER_ADDRESS, &limiter_address) ||
        !memory_is_inside_image(limiter_address, sizeof(uint32_t))) {
        log_warning("the limiter address %08X is outside the image, so the render cap is left "
                    "alone", (unsigned)limiter_address);
        return false;
    }

    if (!QueryPerformanceFrequency(&clock_frequency)) {
        clock_frequency.QuadPart = 0;
    }
    cap_site            = wait_site;
    cap_limiter_address = (uintptr_t)limiter_address;
    cap_usable          = true;
    return true;
}

static void frame_cap_apply(int fps);

/* The cap that follows from the settings and the fraction in force, applied. */
static void apply_current(void)
{
    frame_cap_apply(frame_cap_effective(cap_configured, cap_match_refresh, cap_refresh_hz,
                                        cap_divisor));
}

void frame_cap_configure(int configured, bool match_refresh, int pinned_divisor)
{
    int refresh = frame_cap_refresh_hz();

    if (pinned_divisor < 0) {
        pinned_divisor = 0;
    }
    if (pinned_divisor > FRAME_CAP_DIVISOR_MAX) {
        pinned_divisor = FRAME_CAP_DIVISOR_MAX;
    }

    if (pinned_divisor != cap_pinned_divisor || refresh != cap_refresh_hz ||
        match_refresh != cap_match_refresh) {
        /* A new screen, a new pin or a new mode starts the stepping afresh: what the last screen
         * could hold says nothing about this one. */
        cap_divisor       = (pinned_divisor > 0) ? pinned_divisor : 1;
        cap_clean_seconds = 0;
        have_work_start   = false;
    }
    cap_configured     = configured;
    cap_match_refresh  = match_refresh;
    cap_pinned_divisor = pinned_divisor;
    cap_refresh_hz     = refresh;
    apply_current();
}

/* Applies a cap, or clears the limiter for 0. Cheap when the value has not changed, which is the
 * normal case. Both immediates are written, the authored 1/30 and the "60fps" cheat arm, because
 * the cheat could otherwise undo the cap from inside the game. Going from uncapped back to capped
 * also puts the limiter flag back, since clearing it is how uncapped is expressed and no other
 * code restores it. */
static void frame_cap_apply(int fps)
{
    float cap;

    if (!cap_usable || fps == cap_applied) {
        return;
    }
    if (fps < 0) {
        fps = 0;
    }

    if (fps > 0) {
        cap = 1.0f / (float)fps;
        patch_write_f32(cap_site + OFFSET_CAP_30_IMMEDIATE, cap);
        patch_write_f32(cap_site + OFFSET_CAP_60_IMMEDIATE, cap);
        patch_write_u32(cap_limiter_address, LIMITER_ON);
        log_info("render cap %d fps (%.8f s) at %08X and %08X, limiter [%08X] on",
                 fps, (double)cap,
                 (unsigned)(cap_site + OFFSET_CAP_30_IMMEDIATE),
                 (unsigned)(cap_site + OFFSET_CAP_60_IMMEDIATE),
                 (unsigned)cap_limiter_address);
    } else {
        patch_write_u32(cap_limiter_address, LIMITER_OFF);
        log_info("render cap removed, g_frameLimiterOn [%08X] cleared. The game will draw as fast "
                 "as the machine manages, which is smooth because the display always has a recent "
                 "frame rather than because anything is paced, and it loads one core fully",
                 (unsigned)cap_limiter_address);
    }
    cap_applied = fps;
}

int frame_cap_applied(void)
{
    return (cap_applied > 0) ? cap_applied : 0;
}

void frame_cap_level_opened(void)
{
    if (cap_pinned_divisor != 0 || cap_divisor == 1) {
        return;
    }
    cap_divisor       = 1;
    cap_clean_seconds = 0;
    memset(&second, 0, sizeof(second));
    have_work_start = false;
    log_info("a level has opened, so the cap starts at the refresh again, %d fps",
             frame_cap_effective(cap_configured, true, cap_refresh_hz, 1));
    apply_current();
}

/* Whether the fraction is this file's to move: matching a screen that answered, with no pin. */
static bool stepping_is_live(void)
{
    return cap_usable && cap_match_refresh && cap_pinned_divisor == 0 &&
           cap_refresh_hz >= REFRESH_MINIMUM && cap_refresh_hz <= REFRESH_MAXIMUM &&
           clock_frequency.QuadPart > 0;
}

static void close_second(void)
{
    int      before = cap_divisor;
    unsigned clean  = cap_clean_seconds;
    int      after  = frame_cap_step(before, &clean, &second, cap_refresh_hz);

    cap_clean_seconds = clean;
    if (after != before) {
        int   from = frame_cap_effective(cap_configured, true, cap_refresh_hz, before);
        int   to   = frame_cap_effective(cap_configured, true, cap_refresh_hz, after);
        float budget_ms = 1000.0f * (float)before / (float)cap_refresh_hz;

        cap_divisor = after;
        if (after > before) {
            log_info("the cap steps down from %d to %d fps (%d Hz over %d): %u of the last "
                     "second's %u frames needed more than the %.2f ms of work the cap allowed, "
                     "the longest %.1f ms. On a synchronised display every frame is now shown "
                     "%d times over rather than some twice and some once",
                     from, to, cap_refresh_hz, after, second.over_budget, second.frames,
                     (double)budget_ms, (double)(second.longest_work * 1000.0f), after);
        } else {
            log_info("the cap steps back up from %d to %d fps (%d Hz over %d) after %u seconds "
                     "with room for it, the last second's longest frame %.1f ms of work",
                     from, to, cap_refresh_hz, after, FRAME_CAP_CLEAN_SECONDS,
                     (double)(second.longest_work * 1000.0f));
        }
        apply_current();
    }
    memset(&second, 0, sizeof(second));
}

void frame_cap_frame_drawn(void)
{
    LARGE_INTEGER now;
    float         work;
    float         budget;

    if (!stepping_is_live() || !have_work_start || !QueryPerformanceCounter(&now)) {
        return;
    }
    have_work_start = false;                /* one measurement per wait */
    work = (float)((double)(now.QuadPart - work_started.QuadPart) /
                   (double)clock_frequency.QuadPart);
    if (!(work >= 0.0f)) {
        return;
    }

    budget = (float)cap_divisor / (float)cap_refresh_hz;
    ++second.frames;
    if (work > budget) {
        ++second.over_budget;
    }
    if (cap_divisor > 1 &&
        work > FASTER_MARGIN * (float)(cap_divisor - 1) / (float)cap_refresh_hz) {
        ++second.over_faster;
    }
    if (work > second.longest_work) {
        second.longest_work = work;
    }

    if (second.frames == 1u) {
        second_started = now;
    } else if (now.QuadPart - second_started.QuadPart >= clock_frequency.QuadPart) {
        close_second();
    }
}

void frame_cap_wait_ends(void)
{
    if (!stepping_is_live()) {
        have_work_start = false;
        return;
    }
    if (QueryPerformanceCounter(&work_started)) {
        have_work_start = true;
    }
}
