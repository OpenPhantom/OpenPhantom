/* pad_stick.h: the left stick, read as a direction and a magnitude, because the engine's own path
 * destroys both.
 *
 * ==============================================================================================
 * Why this exists at all, and it is not a preference
 *
 * The engine reads a pad through WinMM into two INDEPENDENT scalars: control function 0 (turn) and
 * control function 1 (forward and back). There is no sideways axis and no vector anywhere. Three
 * things then happen to those two numbers, and each one loses information the one before it kept.
 *
 * FIRST, a thirty per cent SQUARE deadzone, cut per axis, with no rescale. stdControl_readAxis
 * (0x0048D38D) drops any sample inside the deadzone stored in the axis record, and
 * joystick_init_query_caps (0x0048D7C2) sets that from a fraction of 0.3. Being per axis it is
 * square, so a diagonal push is dead well past the point a straight push is live; and being
 * uncut it is a step, so the first live sample arrives at 0.30 rather than near zero.
 *
 * SECOND, the shipped default binds each stick axis to its function TWICE. control_defaultJoyEntry
 * writes the descriptor pairs {2,1} and {1,2} for X, which are the two half axes; both resolve to
 * control function 0 with the INVERT flag, and control_bindAxisShared (0x00464E6E) appends without
 * deduplicating. control_readAxis (0x0046507E) then SUMS the bindings. The axis is doubled.
 *
 * THIRD, every binding is clamped to plus or minus one before the sum, and the sum is clamped
 * again. So the doubled value saturates at half the stick's travel.
 *
 * The three compose into this, per axis, against the fraction of real stick travel:
 *
 *     below 0.30      reads 0
 *     0.30 to 0.50    jumps to 0.60, then climbs to 1.0
 *     above 0.50      pinned at 1.0
 *
 * The whole analogue range is a fifth of the stick, entered by a jump to sixty per cent, and
 * everything past halfway is saturated. Past halfway a diagonal reads (1, 1) whatever direction it
 * is really pointing, so the DIRECTION is gone, not merely coarse. This was read out of the retail
 * image and then confirmed against a real install's obi.ini, whose X0JOY/Y0JOY rows are the shipped
 * defaults; a player who has rebound the pad in the controls screen may not have the doubling, so
 * nothing here may ASSUME it, which is the other reason to stop reading that path.
 *
 * FOURTH, and this part was ours rather than the engine's: the sideways walk took the analogue
 * value for the sideways component but only the move BIT for the forward one, so every diagonal
 * was pulled toward forward. A true forty five degree push came out at thirty five degrees, and
 * at half deflection at nineteen.
 *
 * ==============================================================================================
 * What this does instead
 *
 * Reads the left stick from XInput, which is where it already comes from for the right stick in
 * controller_input.dll, and applies one radial deadzone. That gives an honest direction over the
 * whole stick and an honest magnitude, both of which a camera-relative walk and a walk/run
 * threshold need.
 *
 * It does NOT touch the engine's own reading. Nothing here disables an axis, clears a binding or
 * writes an engine input cell. The phase 2 thunk runs AFTER the engine's own steer, so it simply
 * overwrites the movement fields with values built from this vector, and a frame where this reader
 * has nothing to say leaves the engine's own numbers exactly as they were. That keeps the
 * keyboard, and a pad the player has bound by hand, working unchanged.
 */
#ifndef ENHANCED_INPUT_PAD_STICK_H
#define ENHANCED_INPUT_PAD_STICK_H

#include <stdbool.h>
#include <stdint.h>

/* `deadzone` is the radial fraction, and `walk_run_threshold` the magnitude at or above which a
 * push means run rather than walk. Both come from the ini. */
void pad_stick_configure(bool enabled, int controller_index, float deadzone,
                         float walk_run_threshold, float threshold_hysteresis);

/* One read, once per substep, before anything asks what the stick says. Cheap: a single
 * XInputGetState against one slot. */
void pad_stick_poll(void);

/* True when the stick is outside the deadzone and this reader is the one to believe this substep.
 * False means the caller must leave the engine's own input alone rather than substitute a zero. */
bool pad_stick_is_active(void);

/* Sideways, positive RIGHT. */
float pad_stick_x(void);

/* Forward, positive FORWARD. XInput reports Y positive up, and up is forward, so the value passes
 * through with no sign change; the right stick in controller_input.dll is the one that flips it,
 * because there up has to become a downward mouse movement. */
float pad_stick_y(void);

/* 0 to 1, the deadzone already taken out and the remainder rescaled, so it starts at nearly zero
 * rather than at the deadzone's own value. */
float pad_stick_magnitude(void);

/* The pad's whole contribution to one steer substep, answered in one call so the phase 2 thunk
 * does not have to know how a stick becomes a sideways value and a forward one.
 *
 * Polls, and answers false when the stick is centred or the player is not in Stand, in which
 * case both outputs are untouched and the caller must leave the engine's own input alone rather
 * than substitute a zero. Answers true having written the move bits and the drive from the
 * vector, replacing what the engine's own read of the same stick put there a moment earlier.
 *
 * `out_forward` carries a DEADBAND against the back-pedal: a stick held horizontally is never
 * exactly horizontal, and its forward component wanders either side of centre. Downstream that
 * sign is taken twice over, once for the backward move bit and once for the drive sign that
 * negates the travel angle, so an unguarded hair below centre flips the player between walking
 * left and back-pedalling right several times a second. A keyboard never reaches this: its
 * forward component is exactly 0, +1 or -1 and never sits near the boundary. Use pad_stick_y()
 * where the RAW component is wanted instead, as free look's own travel angle does, since a body
 * that faces its travel has no backward case to protect. */
bool pad_stick_take_substep(uint8_t *record, bool stand_mode, bool strafe_invert,
                            bool sideways_walk, float *out_strafe, float *out_forward);

/* The same stick, spent the way the SHIPPED GAME spends it: sideways turns, forward walks.
 *
 * For where the level author has placed a camera. Our own scheme reads the stick as a
 * direction and relies on free look to turn the body to face it, and free look lets go of an
 * authored camera. What was left in that case was a body lean clamped at ninety degrees and
 * nothing at all that rotates the player, which is how a balcony in the palace became a place
 * you could walk down to and not get out of.
 *
 * `out_turn` is the raw sideways deflection, for the caller to spend on the view the way it
 * spends the keyboard's turn. Only the forward and back move bits are written here, with no
 * sideways component at all, exactly what the engine's own scheme puts in them.
 *
 * The stick is still read from XInput rather than handed back to the engine's own joystick
 * path, and that is deliberate: on a pad that reaches XInput but never WinMM, handing it back
 * would hand back nothing and the player would still be stuck. This restores the BEHAVIOUR the
 * shipped game has, from a reading that works. */
bool pad_stick_take_handback(uint8_t *record, bool stand_mode, float *out_turn,
                             float *out_forward);

/* True while the push is hard enough to mean run. Hysteresis is applied inside, so the answer does
 * not chatter while the stick sits on the boundary. */
bool pad_stick_wants_run(void);

#endif /* ENHANCED_INPUT_PAD_STICK_H */
