#ifndef RAW_MOUSE_H
#define RAW_MOUSE_H

#include <stdbool.h>

/* The mouse, read from the device instead of from the pointer.
 *
 * ==============================================================================================
 * Where in the engine this sits
 *
 * The device layer is one cluster, `0x0048D080` to `0x0048EA1E`, 34 functions, and the binding
 * layer above it is another, 44 functions. Neither has a single function reconstructed as readable
 * source, so every claim below is read out of the instruction stream, and every claim that says
 * "nothing else does X" rests on a sweep of the whole image rather than on the two call sites that
 * happened to be open at the time.
 *
 * ==============================================================================================
 * The reason this exists, and the wrong reason, named because a reader reaches for it first
 *
 * The engine opens its DirectInput mouse non-exclusive. `stdControl_openMouse` at `0x0048DA7C` sets
 * the cooperative level, and the flag word is the one number the argument turns on:
 *
 *     0048DAEA  6A 06              push 6
 *     0048DAEC  E8 76 AE 00 00     call 0x00498967        ; the window handle getter
 *     0048DAF1  50                 push eax               ; hwnd
 *     0048DAF2  A1 C0 19 86 00     mov  eax, [0x008619C0]
 *     0048DAF7  50                 push eax               ; this
 *     0048DAF8  8B 0D C0 19 86 00  mov  ecx, [0x008619C0]
 *     0048DAFE  8B 11              mov  edx, [ecx]
 *     0048DB00  FF 52 34           call dword [edx+0x34]  ; SetCooperativeLevel
 *
 * 6 is `DISCL_NONEXCLUSIVE | DISCL_FOREGROUND`. Exclusive would have been 5. The engine also warps
 * the pointer back to the middle of its window on every mouse message, in `control_recentreMouse`
 * from `0x0046A115`, which compares the client position against `0x140` and `0xF0` and then pushes
 * that same pair into `SetCursorPos` at `0x0046A13E`. An exclusive device would need none of that,
 * and the warp target is the centre of a 640x480 window rather than of a modern one, so the travel
 * available before the pointer reaches an edge is not symmetric.
 *
 * From those two facts it is tempting to conclude that the number the engine reads is pointer
 * travel, carrying the Windows acceleration curve, the control panel's pointer speed and a
 * quantisation to whole screen pixels. That conclusion is not established, and it is written down
 * here only because it is the version a reader will reach for. The documented behaviour of a
 * DirectInput mouse is that the control panel's speed and acceleration do not affect its data, and
 * the image agrees with the documentation rather than with the guess: the axis cell has exactly one
 * writer and it is the store from `GetDeviceState`, while the pointer warp's own product is read
 * nowhere outside the window procedure. The two paths share no storage. Whatever else turns out to
 * be true, that story is not supported by anything in here.
 *
 * ==============================================================================================
 * What is established, and what this exists for
 *
 *   - `GetDeviceState` answers a sum. The call is at `0x0048E6FD` inside `stdControl_pollMouse`
 *     `0x0048E6C7`, and it hands over the counts accumulated since the previous call while
 *     destroying the structure they arrived in. Nothing downstream can then tell three device
 *     reports in a drain from four. That structure is the one correction that matters here, and
 *     Unreal has shipped it for twenty years: divide by the number of reports the interval
 *     contained rather than by its duration. It cannot be done from a sum.
 *   - the simulation consumes at a fixed 32 Hz, so a device reporting at 125 Hz delivers three or
 *     four whole reports per consumed interval no matter what the render rate does. That is a
 *     frame-rate-invariant wobble, which is exactly the shape the field reported, and it is the one
 *     surviving candidate that a frame rate cap could not have removed.
 *   - and the report rate itself is not knowable any other way. Counting packets here measures it
 *     directly, which turns the remaining question from an argument into a number.
 *
 * So this is the reader every game has used since the middle of the last decade, taken for what it
 * actually buys: the device's own timing, preserved rather than summed away.
 *
 * ==============================================================================================
 * What this does not change, and what it does not settle
 *
 * The pointer is still warped by the engine and still drives the menus. Nothing here touches the
 * cursor, the window procedure or the engine's own device. This is a second, independent reader
 * that the view turn is taken from, and if it ever stops answering, the engine's own reader is
 * still there and is switched back to.
 *
 * Two things are open and are worth saying rather than papering over. Whether `DISCL_NONEXCLUSIVE`
 * on a current Windows means the pointer path in the way the paragraph above describes is a
 * platform contract and not an instruction in this image: the `push 6` is proven, the consequence
 * is documented behaviour, and the field test is the feature itself. And the device's own report
 * rate is not recoverable from anything in the engine, while it bounds how smooth the result can
 * be at all, which is the second reason the packet count is exported below.
 */

/* Starts the reader: a message-only window on its own thread, and a raw input registration for the
 * mouse against it. Returns false when any step fails, in which case nothing has been left running
 * and the caller keeps using the engine's reader. Safe to call twice. */
bool raw_mouse_install(void);

/* What one drain of the reader collected. The counts alone would be a sum with its timing thrown
 * away, which is the very thing the engine's own reader does and the reason this exists. */
typedef struct raw_mouse_sample {
    float    axis;              /* the counts, in the engine reader's own units                */
    unsigned packets;           /* how many device reports carried them                        */
    float    span_seconds;      /* from the previously taken report to the newest one          */
} raw_mouse_sample_t;

/* This frame's motion. Consumes: what it returns has been taken out of the accumulator, so it must
 * be called exactly once per rendered frame.
 *
 * The engine's reader is not destructive and can be read as often as anyone likes; this one cannot.
 * That difference is the reason this function is not simply installed as the reader pointer. */
void raw_mouse_take(raw_mouse_sample_t *out);

/* How many raw packets have arrived since the start. The caller uses it to notice a registration
 * that succeeded and then delivered nothing, which is the one failure here that would leave the
 * player with no mouse at all. */
unsigned raw_mouse_packet_count(void);

/* A second drain, for the menu cursor, in raw counts and on both axes.
 *
 * `raw_mouse_take` above is destructive and therefore has exactly one consumer, the view turn. The
 * cursor needs the same packets without taking them away from it, so the packet path adds to two
 * accumulators and each consumer clears its own. Calling this one does not affect the other and the
 * view turn did not have to change.
 *
 * The vertical axis exists only here: the view turn has no use for it. */
void raw_mouse_take_cursor(long *out_dx, long *out_dy);

/* True once the reader is registered and a packet has actually arrived. The cursor path must not
 * take the engine's own pointer motion away on the strength of a registration alone: a silent
 * reader would leave the player with a frozen cursor, which is worse than the stepping it
 * replaces. */
bool raw_mouse_is_delivering(void);

#endif /* RAW_MOUSE_H */
