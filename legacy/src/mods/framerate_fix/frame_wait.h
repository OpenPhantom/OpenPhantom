/* frame_wait.h: an optional sleep ahead of the engine's frame wait.
 *
 * The engine waits for its cap by spinning: pump the message queue, read the clock, compare with
 * the cap, Sleep(0), and round again, for the whole idle part of every frame. That holds one core
 * at full load while the game has nothing to do. On one machine that is a hot core and not much
 * worse. With three instances of the game on one machine, which is how the multiplayer is tested,
 * the instances that are idling hold cores, hyperthread siblings and turbo budget that the busy
 * one could use.
 *
 * This sleeps the bulk of that idle time on a high resolution waitable timer and hands the rest to
 * the engine's own spin, which runs exactly as before and still ends the wait on the deadline. The
 * sleep stops a margin short of the deadline because a timer wakes late by an amount nobody can
 * promise: measured on the test machine, a sleep of one millisecond on this kind of timer took
 * 1.5 ms at the median, 2.3 at the 90th percentile and 7.3 at worst. So the margin is learned from
 * the overshoot the timer actually shows on the machine it runs on, the 99th percentile of the last
 * window of sleeps plus a quarter of a millisecond, and it starts at two milliseconds before
 * anything has been measured. A frame with no more than the margin plus a millisecond left does
 * not sleep at all, and nothing sleeps at a cap above 200 frames a second, where the whole budget
 * of five milliseconds is under that worst case.
 *
 * The sleep also ends for any message, so input, the multiplayer's pump timer and anything sent to
 * the window are served the moment they arrive, as the spin served them.
 *
 * Off by default, FrameWaitSleep=0: the margin is not settled, and a frame that wakes past its
 * deadline leaves the wait late. A line a minute says what the sleep did and how many frames it
 * made late, with the margin it has learned on the line after; the late count has to be zero in
 * the field before this can be on by default.
 *
 * What it cannot do: a message already waiting in the queue ends the sleep before it starts, so a
 * window that is receiving input every millisecond, a moving mouse on the instance in front, sleeps
 * little. The instances behind it, which receive nothing, are the ones it is for, and the line's
 * count of sleeps woken early by a message says how it went.
 */
#ifndef FRAME_WAIT_H
#define FRAME_WAIT_H

#include <stdbool.h>
#include <stdint.h>

/* Above this cap nothing sleeps: the budget is five milliseconds or less. */
#define FRAME_WAIT_MAX_FPS         200
/* The margin before anything has been measured, and the pad above the measured percentile. */
#define FRAME_WAIT_MARGIN_START_US 2000u
#define FRAME_WAIT_MARGIN_PAD_US   250u
/* What must be left above the margin for a sleep to be worth waking from. */
#define FRAME_WAIT_MIN_SLEEP_US    1000u
/* Overshoots per margin update, and the percentile taken of them. The 99th of 256 is the third
 * largest, so two late wakes in a window pass it by; the minute's late count shows them. */
#define FRAME_WAIT_WINDOW          256u
#define FRAME_WAIT_PERCENTILE      99u

typedef struct frame_wait_margin {
    uint32_t window[FRAME_WAIT_WINDOW];   /* the current window's overshoots, in us */
    uint32_t filled;
    uint32_t margin_us;
    uint32_t windows;                     /* windows learned since the last reset */
} frame_wait_margin_t;

/* How long to sleep ahead of the spin, in microseconds, or 0 for not at all. `cap_fps` is the cap
 * in force, 0 while the game is uncapped or nothing was applied; `elapsed_us` is the time since the
 * last wait ended; `margin_us` is what is left to the spin. Pure. */
uint32_t frame_wait_sleep_us(int cap_fps, uint32_t elapsed_us, uint32_t margin_us);

/* Back to the starting margin with nothing learned. */
void frame_wait_margin_reset(frame_wait_margin_t *margin);

/* One overshoot, in microseconds. After every FRAME_WAIT_WINDOW of them the margin becomes their
 * FRAME_WAIT_PERCENTILE percentile plus FRAME_WAIT_MARGIN_PAD_US, saturating rather than wrapping,
 * and the next window starts empty. Pure. */
void frame_wait_margin_note(frame_wait_margin_t *margin, uint32_t overshoot_us);

uint32_t frame_wait_margin_us(const frame_wait_margin_t *margin);
uint32_t frame_wait_margin_windows(const frame_wait_margin_t *margin);

/* The histogram bin a value falls in: `value_us / bin_us`, with everything past the last bin kept
 * in the last bin. Pure. */
uint32_t frame_wait_histogram_bin(uint32_t value_us, uint32_t bin_us, uint32_t bin_count);

/* The nearest-rank `percent` percentile of `samples` values counted into `bins`, answered as the
 * upper edge of the bin it falls in, so it reads as "at most". A percentile in the last bin has no
 * upper edge and answers UINT32_MAX; the caller caps that with the largest value it saw. No
 * samples answers 0. Pure. */
uint32_t frame_wait_histogram_percentile(const uint32_t *bins, uint32_t bin_count, uint32_t bin_us,
                                         uint32_t samples, uint32_t percent);

/* Arms the sleep. `enabled` is FrameWaitSleep; `hooked` says whether the wait hook this runs from
 * is in place, which it is not when PreciseFrameTime=0 or sys_waitForFrame did not resolve. Says in
 * the log which it did, and why when it did not. */
void frame_wait_install(bool enabled, bool hooked);

/* Called from the wait hook ahead of the engine's own wait, on the game thread. `previous_end` is
 * the performance counter at the end of the last wait. Returns at once while the sleep is not
 * armed. */
void frame_wait_before_spin(int64_t previous_end);

#endif /* FRAME_WAIT_H */
