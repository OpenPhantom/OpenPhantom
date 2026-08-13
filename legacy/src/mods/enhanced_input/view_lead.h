#ifndef VIEW_LEAD_H
#define VIEW_LEAD_H

#include <stdbool.h>

/* The view turn the camera has already been shown and the body has not yet made.
 *
 * ==============================================================================================
 * Where the view angle is produced, and why that is the defect
 *
 * Every address in this file and in view_lead.c was read out of retail WMAIN.EXE, 829,952 bytes,
 * ImageBase 0x400000. The view angle has exactly one producer, inside Plr_Integrate, phase 7 of
 * the player phase table:
 *
 *     0044A692  fld  [eax + 0x2A4]      turnWheel, degrees per second
 *     0044A69E  fmul [ecx + 0x74]       the substep length
 *     0044A6A7  fadd [edx + 0x2A0]      the heading
 *     0044A6B2  fstp [eax + 0x2A0]
 *     0044A6C5  call 0x004940AB         the wrap
 *
 * So the view angle advances once per simulation step, 32 times a second, and never between
 * steps. The camera draws the frames in between by interpolating the two most recently published
 * values. A held direction key asks for a constant number of degrees per step, so that
 * interpolation draws a straight line and the turn is smooth at any frame rate. The mouse does
 * not: what the hand did between two steps is one number that varies, so five consecutive frames
 * are drawn at one turn rate and then it changes. That change of slope, 32 times a second, is what
 * a player reads as stepping.
 *
 * The same listing carries the double integration this DLL already subtracts out in phase 7. A
 * mouse count reaches the heading as `count * 0.3 * 6.0 * dt`, because the count is added to a
 * rate at 0x0044A068, which is the value the substep length is then multiplied into here.
 *
 * A mouse is an odometer and a key is a speedometer. The count a mouse reports is a displacement
 * that has already been integrated over whatever interval elapsed, so it belongs on the render
 * clock and must never be multiplied by a time; a key reports only that it is held, so the
 * displacement it implies has to be built from a rate and an elapsed time and belongs on whatever
 * clock does the integrating. Every engine that got this right treats the two differently, and
 * this one treats them the same.
 *
 * So the mouse gets its own clock here. Each rendered frame banks that frame's degrees, each
 * simulation step takes the whole bank and hands it to the body as a displacement, and the camera
 * is shown the sum of what is still banked and the part of the last step's mouse turn that the
 * engine's interpolation has not paid out yet. Those two terms move in opposite directions by the
 * same amount, so during a steady sweep their sum is constant and the drawn angle advances by
 * exactly one frame's worth of hand movement on every frame.
 *
 * What must not be compensated is the key, and leaving it out is not an omission. Its increment is
 * already constant, so the engine's interpolation is exactly right for it, and cancelling that
 * interpolation would turn the one input that is smooth today into a staircase. The same goes for
 * every turn the body makes that this module did not ask for: a scripted turn, a knockback, the
 * damped turn free look drives. All of them ride the engine's own ramp, untouched.
 *
 * ==============================================================================================
 * The arithmetic, with the numbers
 *
 * `h` is the hand's movement per rendered frame, five frames per simulation step, so a step
 * publishes `5h`. `H` is the heading after that step, measured from the same origin throughout,
 * and the engine's interpolation weight walks 0.2, 0.4, 0.6, 0.8, 1.0.
 *
 *     frame                     alpha   interp  banked  unpaid  drawn    turn drawn
 *                                                       5h(1-alpha)      this frame
 *     the one that ran the step  0.2    H - 4h     h      4h    H +  h
 *     +1                         0.4    H - 3h    2h      3h    H + 2h      +h
 *     +2                         0.6    H - 2h    3h      2h    H + 3h      +h
 *     +3                         0.8    H -  h    4h       h    H + 4h      +h
 *     +4                         1.0    H         5h       0    H + 5h      +h
 *     the next step's frame      0.2    H +  h     h      4h    H + 6h      +h
 *
 * ==============================================================================================
 * The variant that looks right, is one line shorter, and turns the camera backwards
 *
 * Showing the camera only what is banked, without cancelling the interpolation, double counts. The
 * interpolation is already paying out the previous step's mouse turn as a ramp while the bank is
 * refilling with the next one, so the two add:
 *
 *     frame                     alpha   interp  banked  drawn    turn drawn
 *                                                                this frame
 *     the one that ran the step  0.2    H - 4h     h    H - 3h
 *     +1                         0.4    H - 3h    2h    H -  h     +2h
 *     +2                         0.6    H - 2h    3h    H +  h     +2h
 *     +3                         0.8    H -  h    4h    H + 3h     +2h
 *     +4                         1.0    H         5h    H + 5h     +2h
 *     the next step's frame      0.2    H +  h     h    H + 2h     -3h
 *
 * The total over the step is right and every frame inside it is wrong: the drawn angle advances at
 * twice the hand's speed for four frames and then falls back by three frames' worth when the bank
 * resets. At 300 degrees per second that is a 5.6 degree backwards jerk 32 times a second, which
 * is worse than the defect it was written to remove. The unit test carries that sequence and
 * asserts the reversal, so the shorter form cannot be rediscovered as a simplification.
 * ============================================================================================ */

/* ==============================================================================================
 * The pure half: no engine, no configuration, checkable without the game.
 * ============================================================================================ */

/* The degrees to add to the yaw the engine has just composed.
 *
 * `pending_degrees`       what the hand has done since the last simulation step took the bank.
 * `substep_mouse_degrees` what that step was handed, which is the only part of the body's turn the
 *                         engine's interpolation must not be allowed to pay out a second time.
 * `alpha`                 how far this frame sits between the last two steps, the engine's own
 *                         interpolation weight, clamped here into [0, 1].
 *
 * Answers zero for any input that is not a finite number, so a poisoned bank cannot reach the
 * camera. */
float view_lead_degrees(float pending_degrees, float substep_mouse_degrees, float alpha);

/* ==============================================================================================
 * The engine half.
 * ============================================================================================ */

/* Reads NewMouseInput. Safe before anything is resolved. */
void view_lead_load_config(void);

/* Arms the feature, or declines it and says which dependency was missing.
 *
 * `bank_is_live`      the mouse is collected once per rendered frame. Without it there is no bank
 *                     to drain per frame, and reading the axis a second time would hand the same
 *                     sample to two consumers and double the turn.
 * `yaw_arm_is_forced` the engine is being told its target heading did not move, which makes it
 *                     take the plain heading-plus-offset arm. The other arm reads back the yaw
 *                     written on the previous frame, so a lead added to that yaw would feed into
 *                     the next frame's and compound. The two arms are disassembled in view_lead.c
 *                     above the install function.
 *
 * Idempotent, and it writes not one byte of the host: everything this feature does is done from a
 * detour that stands whether it is on or off. */
void view_lead_install(bool bank_is_live, bool yaw_arm_is_forced);

/* Whether NewMouseInput is on, before any dependency is looked at. For the one caller that has to
 * explain that a missing camera has taken the feature away with it. */
bool view_lead_is_enabled(void);

/* Whether the lead is actually being applied: the key is on and every dependency answered. */
bool view_lead_is_active(void);

/* The two clocks, and which is which is the whole feature.
 *
 * `view_lead_add_frame` is called once per rendered frame with that frame's mouse degrees, from
 * inside the camera update, which is the one instant at which the frame's own interpolation inputs
 * already exist and nothing of ours can be reordered against the bank's filler.
 *
 * `view_lead_take_substep` is called once per simulation step, at the top of the steering phase
 * and before any gate, exactly where the engine's own per-step drain is called and for the same
 * reason: taking is what marks a step as having consumed. It is passed what that drain already
 * handed over, because the total of the two is what the body is about to turn by and therefore
 * what the camera must stop drawing. It returns the banked degrees, for the caller to add to its
 * own step, and answers zero while the feature is off.
 *
 * A step that then declines to turn the body is not a special case and needs no signal. The bank
 * is empty either way, and the last step's mouse turn is paid back out by the interpolation over
 * the following step, so the camera glides back onto the body instead of snapping onto it. */
void  view_lead_add_frame(float degrees);
float view_lead_take_substep(float degrees_taken_from_the_bank);

/* Drops everything banked. Called on every frame the camera is not ours: a cutscene, a scripted
 * camera, an authored camera region, a load, a parked player, or free look taking the mouse. There
 * is nothing to restore, because the engine recomposes the drawn yaw from its own cells on every
 * frame and this feature never writes to any of them. */
void view_lead_release(void);

/* The degrees to add to the drawn yaw this frame, for the engine's own interpolation weight. */
float view_lead_current(float alpha);

/* ==============================================================================================
 * The measurement, which is what decides whether any of this worked.
 *
 * It runs whether the feature is on or off, because the number it produces only means anything
 * against the same sweep with the key at the other setting.
 * ============================================================================================ */

/* The yaw the frame is about to draw, sampled once per rendered frame after the camera update has
 * composed it and after any lead has been added. */
void view_lead_census_sample(float drawn_yaw);

#endif /* VIEW_LEAD_H */
