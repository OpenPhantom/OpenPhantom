/* frame_cap.h: the render cap, and why it has to match the display.
 *
 * What the cap is for, and it is not what it looks like. The engine limits its own frame rate with
 * a busy wait against an authored 1/30, and this rewrites that immediate. A player picking a
 * number for it is choosing how fast frames are PRODUCED, and nothing in the shipped stack ties
 * that to how fast they are SHOWN: the DirectDraw flip the game presents through used to be
 * scheduled for the next vertical retrace, and the wrapper that now translates it builds its
 * device with an immediate presentation interval instead.
 *
 * So a produced rate below the refresh rate means the display shows some frames and repeats
 * others, on an irregular pattern, and the motion judders while the frame counter reads perfectly
 * steady. Measured on a 144 Hz screen: a cap of 100 leaves 44 refreshes a second repeating a
 * frame, and a cap of 144 leaves none. On a 90 Hz Steam Deck OLED a cap of 60 leaves 30. Both
 * were mistaken for a fault in this DLL's interpolation, and an evening went into eliminating
 * seven innocent hypotheses before the cap was suspected.
 *
 * The wrapper CAN synchronise. Its EnableVSync key sets the interval on a copy of the device
 * parameters, so the DirectDraw log line that prints the original always reads IMMEDIATE and the
 * first test of the key believed it; the shipped configuration had vsync forced off. With it on,
 * a frame is only ever shown at a retrace, and a cap of half the refresh locks to exactly two
 * retraces a frame. Without it, half the refresh tears on every second retrace, which a large
 * object crossing the screen shows as judder. The installer ships it on from 1.4.4.
 *
 * Uncapped is smooth as well, but by brute force rather than by pacing: measured at 256 to 310
 * frames a second, so the display always has a recent one to show. It costs a fully loaded core
 * for a 1999 game, and it is not the same thing as being paced, which matters because the frame
 * times themselves swing by a factor of three at that rate.
 *
 * Hence MatchDisplayRefresh. The cap is set from the display rather than from a number somebody
 * had to guess, which is the one configuration that is both smooth and cheap, and it follows the
 * screen if the player changes it.
 *
 * And hence the divisor. The refresh rate is only the right cap while the machine can hold it,
 * and a 240 Hz screen on a modest machine, or a wide cutscene shot on any machine, is where it
 * cannot: measured, an in-game cutscene at 16:9 costs 8.5 ms of work a frame against the 6.94 ms
 * a 144 Hz screen allows, so a third of its refreshes repeated a frame. With the display
 * synchronised, the caps that are still even are the refresh and its integer fractions, 144, 72,
 * 48, so when the work will not fit the cap steps to the next fraction down,
 * where every frame is shown exactly twice and the motion is even again, and steps back up only
 * after several clear seconds in a row, so a doorway cannot make it flap and a heavy shot costs
 * the rate it needs and no longer. Holding the lower rate for the whole level was tried and felt
 * wrong: a cutscene had halved the rate and the level after it was played at half. A level
 * opening starts it at the refresh again. The player can pin the fraction instead, or turn the
 * matching off and type a number. Smooth needs both halves: the synchronised display and a cap
 * that divides its rate.
 */
#ifndef FRAME_CAP_H
#define FRAME_CAP_H

#include <stdbool.h>
#include <stdint.h>

/* The furthest the cap may step below the refresh, and the rate it will not step below whatever
 * the screen: a quarter of 144 is 36, a quarter of 60 would be 15 and is refused. */
#define FRAME_CAP_DIVISOR_MAX 4
#define FRAME_CAP_FLOOR_FPS   30

/* One second of frames as the hooks counted them. `work` is the time from the end of the frame
 * wait to the moment the drawn frame is handed to the display, which is the frame with the cap's
 * own idling and the display's own wait both taken out, and the only number that says whether
 * there is room. Under vertical sync the present blocks until the retrace, and counting that
 * would read a frame that fits comfortably as one that overran. */
typedef struct frame_cap_second {
    unsigned frames;
    unsigned over_budget;     /* frames whose work alone overran the cap in force */
    unsigned over_faster;     /* frames whose work would crowd the next faster cap */
    float    longest_work;    /* seconds */
} frame_cap_second_t;

/* The cap to actually use, given what the settings asked for and what the display reports.
 *
 * Pure, and separated because it is the one decision here with more than one way to be wrong.
 * `configured` is TargetFps, 0 for uncapped. `refresh_hz` is 0 when the display will not say,
 * and then the configured number stands: a match that cannot be performed must not silently
 * uncap the game or cap it at nothing. `divisor` is the fraction of the refresh in force, 1 for
 * the refresh itself.
 */
int frame_cap_effective(int configured, bool match_refresh, int refresh_hz, int divisor);

/* The largest divisor a screen allows: FRAME_CAP_DIVISOR_MAX, or fewer when a fraction would fall
 * under FRAME_CAP_FLOOR_FPS. */
int frame_cap_divisor_limit(int refresh_hz);

/* One second's verdict, pure. Steps down one fraction when more than a tenth of the second's
 * frames overran the budget, so a single hiccup never moves it; steps back up one fraction after
 * FRAME_CAP_CLEAN_SECONDS consecutive seconds in which almost every frame had room at the faster
 * cap, so a doorway cannot make it flap. A second with fewer than FRAME_CAP_JUDGE_FRAMES frames,
 * or one holding a frame of FRAME_CAP_STALL_SECONDS or more of work, is a menu, a pause or a
 * level load and decides nothing. `clean_seconds` is the run of clear seconds so far and is kept
 * by the caller. Returns the divisor to use from now on. */
#define FRAME_CAP_CLEAN_SECONDS 5u
#define FRAME_CAP_JUDGE_FRAMES  20u
#define FRAME_CAP_STALL_SECONDS 0.05f
int frame_cap_step(int divisor, unsigned *clean_seconds, const frame_cap_second_t *second,
                   int refresh_hz);

/* The display's refresh rate in Hz, or 0 when it cannot be had.
 *
 * VREFRESH answers 0 or 1 for a driver reporting a hardware default rather than a rate, and
 * neither is a rate anybody can cap to, so both come back as unknown. Rates outside 20 to 1000
 * are refused for the same reason.
 */
int frame_cap_refresh_hz(void);

/* Remembers the site and checks everything that will later be written, so a live change cannot
 * discover a bad byte halfway. False when the site is unusable, and then nothing is ever written.
 *
 * `wait_site` is sys_waitForFrame's start, resolved by the caller, which owns the signature table.
 */
bool frame_cap_install(uintptr_t wait_site);

/* The settings, applied. Called at install and from the once-a-second poll, and cheap when nothing
 * has moved. `pinned_divisor` is RefreshDivisor: 0 lets the cap step by itself, 1 to
 * FRAME_CAP_DIVISOR_MAX holds it at that fraction. With `match_refresh` off the divisor is
 * meaningless and `configured` is the cap, as before. */
void frame_cap_configure(int configured, bool match_refresh, int pinned_divisor);

/* The cap in force, in frames a second, or 0 while the game is uncapped or nothing has been
 * applied. The statistics window measures long frames against it. */
int frame_cap_applied(void);

/* A level has opened: the fraction starts at the refresh again. Called by sim_clock, which is
 * where the clock going backwards is read. */
void frame_cap_level_opened(void);

/* The two ends of a frame's work. The wait hook says when the wait has ended, which is where the
 * frame's work starts; the frame end hook, before the present, says when the drawing is done. The
 * work times are counted into the current second and the second's verdict is taken there. */
void frame_cap_wait_ends(void);
void frame_cap_frame_drawn(void);

#endif /* FRAME_CAP_H */
