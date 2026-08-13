#ifndef MOUSE_RATE_H
#define MOUSE_RATE_H

#include <stdbool.h>

/* Reconstructing a hand's speed from a device that reports at its own rate, whatever that rate is.
 *
 * ==============================================================================================
 * The error this removes
 *
 * The simulation consumes at a fixed 32 Hz. A device reports at R Hz. Summing the counts that fall
 * inside each consumed interval therefore collects R/32 reports, and when that is not a whole
 * number the sum alternates between the floor and the ceiling of it. A hand moving at a constant
 * speed then produces a turn that jumps by a fraction of itself several times a second, and the
 * camera draws exactly what it is given: the rendered yaw is a straight line while the per-step
 * increment is constant and a polyline the moment it is not.
 *
 * The repair is to divide the counts not by the length of the interval they were collected in but
 * by the time the reports that carried them actually represent. It then no longer matters how many
 * landed inside a step: three reports and four reports give the same rate for the same hand. It
 * costs no latency at all, because it changes what is divided rather than delaying anything.
 *
 * ==============================================================================================
 * Why the per-substep increment is the whole of it, out of the engine's own bytes
 *
 * The substep is exactly 1/32 s, and `sys_runSubsteps` writes the constant itself:
 *
 *     00475740  C7 05 14 87 86 00 00 00 00 3D   mov dword [0x00868714], 0x3D000000   ; 1/32
 *     0047574C  C7 05 14 87 86 00 00 00 80 3C   mov dword [0x00868714], 0x3C800000   ; 1/64
 *
 * The second store is reached only when the engine's sixty-frames flag at `0x00882294` is set, and
 * the loop `while (simTime < simTarget)` at `0x00475756` to `0x004757FA` runs whole substeps only.
 *
 * The camera interpolates the heading pair across one substep of game time, and the alpha it uses
 * is built at the tail of that same loop:
 *
 *     004757FF  fld  [0x00869294]      ; simTarget
 *     00475805  fsub [0x00868728]      ; minus simTime
 *     0047580B  fadd [0x00868714]      ; plus one step
 *     00475811  fdiv [0x00868714]      ; over one step
 *     00475817  fstp [0x0086871C]      ; the substep alpha
 *
 * The loop exits having overshot by at most one step, so alpha lies in (0,1]. `bapview_updateCam`
 * reads it once at its top into [ebp-0x3C], and the interpolation itself is at `0x004185E5`:
 *
 *     004185E5  push [0x008A0074]      ; head
 *     004185EC  push [0x008A0070]      ; the previous head
 *     004185F2  call 0x004941C0        ; angle difference, into (-180,180]
 *     004185FD  fmul [ebp-0x3C]        ; times alpha
 *     00418600  fadd [0x008A0070]      ; plus the previous head
 *
 * The pair is rotated in exactly one place, `bapview_setCamTarget` at `0x004184CC`, once per
 * substep. So within a substep the alpha sweeps 0 to 1 against a frozen pair and the rendered yaw
 * is a straight line whose slope is (head minus previous head) times 32. The per-substep increment,
 * and only the per-substep increment, decides whether the view looks smooth.
 *
 * The engine's own reader then destroys the one thing that would fix it: `GetDeviceState` at
 * `0x0048E6FD`, inside `stdControl_pollMouse` at `0x0048E6C7`, hands over a sum with the report
 * timing already discarded.
 *
 * ==============================================================================================
 * The quantisation, in numbers
 *
 * Reports per consumed step is R/32. When that is not a whole number the sum alternates between its
 * floor and its ceiling, peak to peak is 32/R, the coefficient of variation is sqrt(f(1-f))/(R/32)
 * with f the fractional part of R/32, and the beat frequency is 32 times the smaller of f and 1-f.
 *
 *     device     reports per step   peak to peak      cv      beat
 *     62.5 Hz         1 or 2           51.2 %       10.8 %   1.5 Hz
 *     104 Hz          3 or 4           30.8 %       13.3 %   8.0 Hz
 *     125 Hz          3 or 4           25.6 %        7.5 %   3.0 Hz
 *     250 Hz          7 or 8           12.8 %        5.0 %   6.0 Hz
 *     500 Hz         15 or 16           6.4 %        3.1 %  12.0 Hz
 *     1000 Hz        31 or 32           3.2 %        1.4 %   8.0 Hz
 *
 * That is two shapes and not one. At 62.5 Hz it is a fifty per cent lurch one and a half times a
 * second; at 104 Hz it is an eight hertz shimmer.
 *
 * While the counts are collected on a frame hook and drained per substep there is a second term,
 * because the boundary snaps to a frame first and to a report second, giving 32/fps + 32/R. At
 * 125 Hz and 91.5 frames per second that is 76.8 per cent. It is also why a field test that capped
 * the game at 64 frames per second came back unchanged, and why that null result is evidence rather
 * than a dead end: at 64 fps the frame grid is a subgrid of the substep grid, so the frame term
 * vanishes exactly and what was left over was the report term.
 *
 * ==============================================================================================
 * The clock is the packet count, not the arrival times
 *
 * This used to divide by the arrival span, first report of the interval to the last, which is the
 * obvious reading of "the time the reports spanned" and is the wrong divisor. A device reports at
 * its own fixed rate, so N packets represent N intervals whatever their arrival stamps look like,
 * and the rate is the counts over the packet count times the report interval. Dividing by a
 * measured span puts the sampling boundary straight back into the estimate: six reports in a frame
 * instead of five carry a sixth more counts across a span that did not grow by a sixth.
 *
 * Mean error in counts per frame against a hand that accelerates and reverses, worst single frame
 * in brackets, modelled offline:
 *
 *                                          1000 Hz      500 Hz       250 Hz       125 Hz
 *     arrival span, stamped at the device  1.04 (2.44)  3.25 (6.40)  5.05 (10.15) 5.76 (12.72)
 *     arrival span, stamped at the pump    1.90 (10.03) 4.51 (9.75)  7.53 (17.87) 12.65 (26.25)
 *     packet count as the clock            1.04 (2.47)  3.22 (6.07)  4.99 (9.60)  5.60 (13.13)
 *
 * The third row is one row rather than two because the two arrival models give it identical
 * answers, and a unit test asserts that equality. On a hand at 125 Hz the arrival-span version
 * delivered more than twice the unevenness of this one.
 *
 * A false reason for the same conclusion is worth naming, because it was believed for a while and
 * it sent one investigation after a defect that was not there. The claim was that raw input arrives
 * in a burst when the game empties its message queue once per rendered frame, so the span measured
 * is the frame's rather than the reports'. That is not what happens here: the packets are received
 * on a thread of our own whose window has its own blocking pump, so they are drained as they arrive
 * and the game's frame pacing never touches them. The conclusion survives on the arithmetic above;
 * the story about bunched delivery does not, and it is exactly the belief that makes a delivery cap
 * look plausible.
 *
 * What survives the packet clock is the report clock's own unevenness, and the only way to remove
 * that is to average, which is delay. That is what the time constant buys and it is the only thing
 * in here that costs anything.
 *
 * ==============================================================================================
 * Why the time constant is measured rather than configured
 *
 * A fixed setting in milliseconds cannot be right at two different report rates: what is barely
 * enough at 100 Hz is a quarter of a second of mush at 1000. So the filter's time constant is a
 * number of report intervals, and the report interval is measured from the stream itself. A fast
 * device is smoothed for a few milliseconds and nobody can feel it; a slow one is smoothed for as
 * long as it has to be. The player configures a ceiling on that, not the value.
 *
 * ==============================================================================================
 * The rule that makes two consumers safe, and it is the one this file exists to enforce
 *
 * Everything that arrives is banked, and a take removes what it delivers. Nothing is ever delivered
 * that has not arrived, and the bank can never change sign. That is what lets two consumers at
 * different cadences share one reconstruction: whatever the first takes, the second finds gone, and
 * the total delivered over any interval is exactly the total the hand produced.
 *
 * The previous version of this file allowed a take to run ahead of the bank by one report interval,
 * on the argument that the interval between two reports is genuinely unknown. It is, but the
 * allowance was granted per call rather than per unit of time, so two consumers in one frame each
 * took it, the bank went negative, and the ledger then paid the negative back as motion in the
 * opposite direction. A camera that walks backwards after the hand stops is worse than a camera
 * that is one report behind.
 *
 * ==============================================================================================
 * The prior art, with the formulas, because three of the four are the wrong tool here
 *
 *   - Quake 1 and 3, `m_filter`, default off: mouse_x = (mx + old_mouse_x) * 0.5, a two tap box,
 *     group delay half a sample, which against a 32 Hz consumer is 15.6 ms of delay.
 *   - Doom 3, `m_smooth`, default 1, which is the identity. A box of length N costs (N-1)/2
 *     samples of delay and does nothing about which interval a report belonged to.
 *   - Source: no filter in the default path at all. Accumulate on events, drain once per fixed
 *     tick, which is the arrangement that produces the alternation described above.
 *   - Unreal, `SmoothMouse`, and it is the one that matches this engine. Epic's own comment is
 *     that mouse sampling does not match up with tick time, and the load-bearing line divides by
 *     the number of device reports the frame contained rather than by the frame's duration. That
 *     is the boundary correction, and it cannot be done from a sum.
 */

typedef struct mouse_rate {
    float bank;                 /* arrived and not yet delivered, in axis units               */
    float counts_per_second;    /* the smoothed hand rate                                     */
    float report_seconds;       /* the measured mean interval between device reports          */
    float idle_seconds;         /* since the last report that carried anything                */
    bool  primed;               /* a report interval has been measured at least once          */

    /* The window the report interval is measured over. It has to be long compared with a frame,
     * because within one frame the packet count is a small whole number and its rounding is the
     * whole of the error being removed. */
    float    window_seconds;
    unsigned window_packets;
    bool     window_ready;      /* a full window has closed, so the estimate is the window's   */
} mouse_rate_t;

void mouse_rate_reset(mouse_rate_t *rate);

/* One drain of the reader, once per rendered frame.
 *
 * `packets` is how many device reports the counts arrived in, and `span_seconds` is the wall time
 * those reports spanned, measured from the previously consumed report to the newest one. A reader
 * that cannot say passes one packet and the frame's own duration, which loses the boundary
 * correction and keeps everything else. A reader that can say and had nothing to report this frame
 * passes zero packets, and that is not the same thing: an interval with no report in it says
 * nothing about the hand, and feeding it in as a zero is what makes a slow device look like a
 * stopping one.
 *
 * `frame_seconds` is used only to age the estimate on a frame that carried no report at all, which
 * is the normal case whenever the device reports more slowly than the game draws.
 *
 * `max_time_constant_seconds` is the player's ceiling on how much delay may be spent smoothing. */
void mouse_rate_observe(mouse_rate_t *rate, float counts, unsigned packets, float span_seconds,
                        float frame_seconds, float max_time_constant_seconds);

/* The counts to deliver over `dt` seconds, removed from the bank. Never more than has arrived and
 * never against the bank's own direction. */
float mouse_rate_take(mouse_rate_t *rate, float dt, float max_time_constant_seconds);

/* The time constant the filter is currently running at, for the log. Zero before the first
 * measurement. */
float mouse_rate_time_constant(const mouse_rate_t *rate, float max_time_constant_seconds);

/* What is still banked, for the log. Reports only; it takes nothing. */
float mouse_rate_banked(const mouse_rate_t *rate);

#endif /* MOUSE_RATE_H */
