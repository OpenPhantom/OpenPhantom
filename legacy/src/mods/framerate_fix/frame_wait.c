/* frame_wait.c: see frame_wait.h.
 *
 * Two halves. The rules at the top are pure and tested: when a frame may sleep and for how long,
 * how the margin is learned, and the histogram the minute's line is read from. The half below them
 * is the timer and the line, and it only runs on the game thread, from the wait hook in
 * frame_delta.c, ahead of the engine's own wait.
 *
 * The one ordering a maintainer must not break: the sleep comes BEFORE the engine's wait and the
 * engine's wait always runs after it. The engine's loop is what pumps the messages, reads the clock
 * the frame delta is taken from and ends the wait on the deadline; this only shortens how long it
 * spins. A sleep that replaced the loop, or ran after it, would change what the engine measures.
 */
#include "frame_wait.h"

#include "frame_cap.h"

#include "common/logging.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION came with the Windows 10 1803 SDK; named here so an older
 * SDK still builds, and the call itself fails cleanly on a system that does not know the flag. */
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

/* The minute's overshoot histogram: 10 us bins up to a little over 10 ms, and everything later in
 * the last bin. The worst is kept exactly beside it. */
#define FRAME_WAIT_BIN_US 10u
#define FRAME_WAIT_BINS   1024u

#define REPORT_SECONDS 60

/* The wait's own timeout, beyond the sleep asked for. The timer ends the wait long before this;
 * it is only there so a timer that never fires costs a late frame rather than a hung game. */
#define BACKSTOP_MS 16u

/* ============================================================================================ *
 * The rules
 * ============================================================================================ */

uint32_t frame_wait_sleep_us(int cap_fps, uint32_t elapsed_us, uint32_t margin_us)
{
    uint32_t budget_us;
    uint32_t left_us;

    if (cap_fps <= 0 || cap_fps > FRAME_WAIT_MAX_FPS) {
        return 0u;
    }
    budget_us = 1000000u / (uint32_t)cap_fps;
    if (elapsed_us >= budget_us) {
        return 0u;
    }
    left_us = budget_us - elapsed_us;

    /* In 64 bits, so the largest margin there is cannot wrap round into a small one when the
     * millisecond is added. */
    if ((uint64_t)left_us <= (uint64_t)margin_us + FRAME_WAIT_MIN_SLEEP_US) {
        return 0u;
    }
    return left_us - margin_us;
}

/* The nearest rank, 1-based: the smallest rank at or above `percent` of `count`. Never 0, so the
 * 0th percentile is the smallest value rather than nothing. */
static uint64_t nearest_rank(uint64_t count, uint32_t percent)
{
    uint64_t rank = (count * percent + 99u) / 100u;

    return (rank == 0u) ? 1u : rank;
}

static int compare_u32(const void *left, const void *right)
{
    uint32_t a = *(const uint32_t *)left;
    uint32_t b = *(const uint32_t *)right;

    return (a < b) ? -1 : (a > b) ? 1 : 0;
}

void frame_wait_margin_reset(frame_wait_margin_t *margin)
{
    memset(margin, 0, sizeof(*margin));
    margin->margin_us = FRAME_WAIT_MARGIN_START_US;
}

void frame_wait_margin_note(frame_wait_margin_t *margin, uint32_t overshoot_us)
{
    uint32_t percentile;

    margin->window[margin->filled++] = overshoot_us;
    if (margin->filled < FRAME_WAIT_WINDOW) {
        return;
    }

    /* Sorted in place: the window is spent either way, and a full sort of 256 values once every
     * 256 sleeps is a few microseconds taken out of idle time. */
    qsort(margin->window, FRAME_WAIT_WINDOW, sizeof(margin->window[0]), compare_u32);
    /* The rank is at most the window's length, so the index fits whatever the rank's type. */
    percentile = margin->window[(size_t)(nearest_rank(FRAME_WAIT_WINDOW, FRAME_WAIT_PERCENTILE) -
                                         1u)];
    margin->margin_us = (percentile > UINT32_MAX - FRAME_WAIT_MARGIN_PAD_US)
                      ? UINT32_MAX : percentile + FRAME_WAIT_MARGIN_PAD_US;
    margin->filled = 0u;
    ++margin->windows;
}

uint32_t frame_wait_margin_us(const frame_wait_margin_t *margin)
{
    return margin->margin_us;
}

uint32_t frame_wait_margin_windows(const frame_wait_margin_t *margin)
{
    return margin->windows;
}

uint32_t frame_wait_histogram_bin(uint32_t value_us, uint32_t bin_us, uint32_t bin_count)
{
    uint32_t bin;

    if (bin_us == 0u || bin_count == 0u) {
        return 0u;
    }
    bin = value_us / bin_us;
    return (bin >= bin_count) ? bin_count - 1u : bin;
}

uint32_t frame_wait_histogram_percentile(const uint32_t *bins, uint32_t bin_count, uint32_t bin_us,
                                         uint32_t samples, uint32_t percent)
{
    uint64_t rank;
    uint64_t seen = 0u;
    uint32_t bin;

    if (bins == NULL || bin_count == 0u || samples == 0u) {
        return 0u;
    }
    rank = nearest_rank(samples, percent);
    for (bin = 0u; bin < bin_count; ++bin) {
        seen += bins[bin];
        if (seen >= rank) {
            uint64_t edge = (uint64_t)(bin + 1u) * bin_us;

            if (bin + 1u == bin_count || edge > UINT32_MAX) {
                return UINT32_MAX;
            }
            return (uint32_t)edge;
        }
    }
    return UINT32_MAX;    /* the bins hold fewer samples than were claimed */
}

/* ============================================================================================ *
 * The timer and the line
 * ============================================================================================ */

typedef struct frame_wait_minute {
    uint32_t bins[FRAME_WAIT_BINS];
    uint32_t samples;
    uint32_t worst_us;
    uint32_t slept;
    uint64_t asked_us;
    uint32_t woken;
    uint32_t late;
} frame_wait_minute_t;

typedef struct frame_wait_state {
    bool                active;
    HANDLE              timer;
    LARGE_INTEGER       frequency;
    int64_t             minute_started;
    frame_wait_margin_t margin;
    frame_wait_minute_t minute;
} frame_wait_state_t;

static frame_wait_state_t wait_state;

static uint32_t ticks_to_us(int64_t ticks)
{
    uint64_t us;

    if (ticks <= 0) {
        return 0u;
    }
    us = (uint64_t)ticks * 1000000u / (uint64_t)wait_state.frequency.QuadPart;
    return (us > UINT32_MAX) ? UINT32_MAX : (uint32_t)us;
}

static int64_t us_to_ticks(uint32_t us)
{
    return (int64_t)((uint64_t)us * (uint64_t)wait_state.frequency.QuadPart / 1000000u);
}

/* Said once. The sleep then stays off for the rest of the process and the engine spins as it did
 * before, which is the one outcome that cannot make a frame late. */
static void give_up(const char *what, DWORD error)
{
    wait_state.active = false;
    log_warning("the frame wait stops sleeping ahead of the spin: %s (error %lu). The engine spins "
                "through the whole of each frame's idle time as it did before",
                what, (unsigned long)error);
}

static void note_overshoot(int64_t ticks)
{
    frame_wait_minute_t *minute = &wait_state.minute;
    uint32_t             us     = ticks_to_us(ticks);

    ++minute->bins[frame_wait_histogram_bin(us, FRAME_WAIT_BIN_US, FRAME_WAIT_BINS)];
    ++minute->samples;
    if (us > minute->worst_us) {
        minute->worst_us = us;
    }
    frame_wait_margin_note(&wait_state.margin, us);
}

static uint32_t minute_percentile(uint32_t percent)
{
    const frame_wait_minute_t *minute = &wait_state.minute;
    uint32_t value = frame_wait_histogram_percentile(minute->bins, FRAME_WAIT_BINS,
                                                     FRAME_WAIT_BIN_US, minute->samples, percent);

    return (value > minute->worst_us) ? minute->worst_us : value;
}

static void report_minute(void)
{
    const frame_wait_minute_t *minute = &wait_state.minute;

    log_info("the frame wait: %u frame(s) slept before the spin, %u ms asked in all; the timer "
             "overshot by %u us at the median, %u at the 99th percentile, %u at worst; %u woken "
             "early by a message; %u frame(s) left the wait later than the cap allowed",
             minute->slept, (unsigned)(minute->asked_us / 1000u), minute_percentile(50u),
             minute_percentile(99u), minute->worst_us, minute->woken, minute->late);
    log_info("the frame wait's margin left to the spin is %u us, learned over %u window(s) of %u "
             "sleep(s)",
             frame_wait_margin_us(&wait_state.margin),
             frame_wait_margin_windows(&wait_state.margin), FRAME_WAIT_WINDOW);
}

/* A line a minute of wall time, and the margin beside it, zeros included: a minute in which nothing
 * slept is itself the answer when the cap is high or the frames are full. Printed ahead of the
 * engine's wait, so the write comes out of idle time rather than out of a frame's work. */
static void report_if_a_minute_passed(int64_t now)
{
    if (wait_state.minute_started == 0) {
        wait_state.minute_started = now;
        return;
    }
    if (now - wait_state.minute_started < wait_state.frequency.QuadPart * REPORT_SECONDS) {
        return;
    }
    report_minute();
    memset(&wait_state.minute, 0, sizeof(wait_state.minute));
    wait_state.minute_started = now;
}

static void sleep_on_the_timer(int64_t start, uint32_t sleep_us, int64_t previous_end, int cap_fps)
{
    frame_wait_minute_t *minute = &wait_state.minute;
    int64_t              target = start + us_to_ticks(sleep_us);
    LARGE_INTEGER        due;
    LARGE_INTEGER        woke;
    DWORD                result;

    /* Relative, so negative, in the timer's units of 100 ns. */
    due.QuadPart = -(LONGLONG)sleep_us * 10;
    if (!SetWaitableTimer(wait_state.timer, &due, 0, NULL, NULL, FALSE)) {
        give_up("the timer could not be set", GetLastError());
        return;
    }

    /* Any message ends the sleep, and MWMO_INPUTAVAILABLE counts one that is already queued and
     * merely seen, so nothing waits in the queue behind the sleep: the engine's own loop pumps it
     * straight after, as it would have from its spin. */
    result = MsgWaitForMultipleObjectsEx(1u, &wait_state.timer, sleep_us / 1000u + BACKSTOP_MS,
                                         QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    if (!QueryPerformanceCounter(&woke)) {
        woke.QuadPart = target;
    }
    ++minute->slept;
    minute->asked_us += sleep_us;

    if (result == WAIT_OBJECT_0 + 1u) {
        /* Setting the timer again resets it, so a sleep cut short leaves nothing signalled for
         * the next one; cancelling only spares the timer firing into nobody. */
        ++minute->woken;
        (void)CancelWaitableTimer(wait_state.timer);
    } else if (result == WAIT_OBJECT_0 || result == WAIT_TIMEOUT) {
        note_overshoot(woke.QuadPart - target);
    } else {
        give_up("the wait on the timer failed", GetLastError());
        return;
    }

    if (woke.QuadPart - previous_end > wait_state.frequency.QuadPart / cap_fps) {
        ++minute->late;
    }
}

void frame_wait_before_spin(int64_t previous_end)
{
    LARGE_INTEGER now;
    int           cap_fps;
    uint32_t      sleep_us;

    if (!wait_state.active || !QueryPerformanceCounter(&now)) {
        return;
    }
    report_if_a_minute_passed(now.QuadPart);
    if (now.QuadPart <= previous_end) {
        return;
    }

    /* The cap in force now, which the cap's own stepping may have moved since the last frame. */
    cap_fps  = frame_cap_applied();
    sleep_us = frame_wait_sleep_us(cap_fps, ticks_to_us(now.QuadPart - previous_end),
                                   frame_wait_margin_us(&wait_state.margin));
    if (sleep_us == 0u) {
        return;
    }
    sleep_on_the_timer(now.QuadPart, sleep_us, previous_end, cap_fps);
}

void frame_wait_install(bool enabled, bool hooked)
{
    if (!enabled) {
        log_info("FrameWaitSleep=0, the frame wait spins through the whole of each frame's idle "
                 "time as the engine wrote it");
        return;
    }
    if (!hooked) {
        log_warning("FrameWaitSleep=1, but the sleep runs from the wait hook that "
                    "PreciseFrameTime=0 or an unresolved sys_waitForFrame leaves out, so the frame "
                    "wait spins as the engine wrote it");
        return;
    }
    if (!QueryPerformanceFrequency(&wait_state.frequency) ||
        wait_state.frequency.QuadPart <= 0) {
        log_warning("FrameWaitSleep=1, but there is no performance counter to time a sleep "
                    "against, so the frame wait spins as the engine wrote it");
        return;
    }

    /* Created once and kept for the life of the process, like every other resource this DLL
     * holds: the DLL is never unloaded. */
    wait_state.timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                              TIMER_MODIFY_STATE | SYNCHRONIZE);
    if (wait_state.timer == NULL) {
        log_warning("FrameWaitSleep=1, but this system has no high resolution waitable timer "
                    "(error %lu); it came with Windows 10 version 1803. The frame wait spins as "
                    "the engine wrote it", (unsigned long)GetLastError());
        return;
    }

    frame_wait_margin_reset(&wait_state.margin);
    wait_state.active = true;
    log_info("FrameWaitSleep=1: while the cap is %d fps or less, a frame with more than the "
             "margin plus %u us of its budget left sleeps on a high resolution timer down to the "
             "margin, and the engine spins the rest as before. The margin starts at %u us and then "
             "follows the %uth percentile of the timer's overshoot plus %u us, learned over every "
             "%u sleeps. A message ends the sleep at once. A line a minute says what it did, with "
             "the margin it has learned beside it",
             FRAME_WAIT_MAX_FPS, FRAME_WAIT_MIN_SLEEP_US, FRAME_WAIT_MARGIN_START_US,
             FRAME_WAIT_PERCENTILE, FRAME_WAIT_MARGIN_PAD_US, FRAME_WAIT_WINDOW);
}
