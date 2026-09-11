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
 * Uncapped is smooth as well, but by brute force rather than by pacing: measured at 256 to 310
 * frames a second, so the display always has a recent one to show. It costs a fully loaded core
 * for a 1999 game, and it is not the same thing as being paced, which matters because the frame
 * times themselves swing by a factor of three at that rate.
 *
 * Hence MatchDisplayRefresh. The cap is set from the display rather than from a number somebody
 * had to guess, which is the one configuration that is both smooth and cheap, and it follows the
 * screen if the player changes it.
 */
#ifndef FRAME_CAP_H
#define FRAME_CAP_H

#include <stdbool.h>
#include <stdint.h>

/* The cap to actually use, given what the settings asked for and what the display reports.
 *
 * Pure, and separated because it is the one decision here with more than one way to be wrong.
 * `configured` is TargetFps, 0 for uncapped. `refresh_hz` is 0 when the display will not say,
 * and then the configured number stands: a match that cannot be performed must not silently
 * uncap the game or cap it at nothing.
 */
int frame_cap_effective(int configured, bool match_refresh, int refresh_hz);

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

/* Applies a cap, or clears the limiter for 0. Safe to call every second and cheap when the value
 * has not changed, which is the normal case.
 *
 * Both immediates are written, the authored 1/30 and the "60fps" cheat arm, because the cheat
 * could otherwise undo the cap from inside the game. Going from uncapped back to capped also puts
 * the limiter flag back, since clearing it is how uncapped is expressed and no other code
 * restores it.
 */
void frame_cap_apply(int fps);


#endif /* FRAME_CAP_H */
