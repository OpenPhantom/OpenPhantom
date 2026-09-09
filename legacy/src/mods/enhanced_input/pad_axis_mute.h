/* pad_axis_mute.h: one pad axis taken away from the engine's own reading of the stick.
 *
 * The right stick walked the player forward and back, and none of it was synthesised here. The
 * game reads the pad itself through WinMM, pad_stick.h sets out why nothing in this module uses
 * that reading for a direction, and the shipped bindings put the right stick on the same control
 * as the left. Straight out of a retail install's obi.ini, decoded as (control, half axis):
 *
 *     Y0JOY=0x0304   control 3, half axis 4      the left stick, forward
 *     Y1JOY=0x0403   control 4, half axis 3      the left stick, back
 *     R0JOY=0x0307   control 3, half axis 7      the RIGHT stick, forward
 *     R2JOY=0x0408   control 4, half axis 8      the RIGHT stick, back
 *
 * control_readAxis at 0x0046507E sums every binding on a control, so both sticks drive the walk
 * and either one alone is enough.
 *
 * Which axis is which is the engine's own, stated twice in its own code.
 * joystick_init_query_caps walks JOYCAPS in the order X, Y, Z, R, U, V and fills the axis table in
 * that order. options_controls_configure_joystick then reads each one back to find out what the
 * player is moving, and asks for index 3 only when wCaps carries JOYCAPS_HASR, so index 3 is the R
 * axis by the engine's own test. The same function records that
 * axis under input ids 7 and 8, the two the R rows above name, which is the other half of the
 * decode confirmed from the engine rather than inferred. R on an Xbox pad seen through WinMM is the
 * right stick's vertical.
 *
 * All of that is still a reading. What settles it is a measurement, taken in play: with the right
 * stick's own synthesised vertical already switched off, JOYENABLE=0 in obi.ini stopped the
 * walking, and JOYENABLE switches off nothing except the engine's own joystick reading.
 *
 * Why the left stick's own path never covered this. pad_stick_take_substep returns early while the
 * stick sits inside its deadzone and writes no movement at all, deliberately, so that the keyboard
 * and a pad somebody has bound by hand go on working. A centred left stick therefore leaves
 * whatever the engine read standing, and what the engine read was the other stick.
 *
 * What this does. Chains stdControl_readAxis and answers a flat zero for one axis, passing every
 * other axis through untouched. That is the narrowest place to stand: the keyboard, the face
 * buttons, the POV hat, the left stick and the other five axes are not involved, and a build where
 * the site does not resolve loses this alone.
 *
 * It is a setting because it is not free, and it costs two things. Somebody who bound that axis on
 * purpose loses what they bound it to, and there is no way from here to tell a deliberate binding
 * from the shipped one. And the joystick binding screen finds out what the player is moving by
 * reading these same axes, so while this is on, pushing the right stick up or down on that screen
 * registers as nothing and the axis cannot be given a new binding. An existing binding is not
 * touched, since that is read from the binding table rather than from the axis.
 *
 * The default takes the axis away anyway, because the shipped binding fights the stick the player
 * is steering with and does it in a way that reads as the game being broken, and because binding
 * that axis is a thing almost nobody does.
 *
 * This is the first thing in enhanced_input to interfere with the engine's own pad reading.
 * pad_stick.h promises not to, and that promise still holds where it was made: nothing here
 * disables a binding, clears an axis record or writes an engine input cell. The reading is
 * answered rather than altered, and the answer is given back to a single axis index.
 */
#ifndef ENHANCED_INPUT_PAD_AXIS_MUTE_H
#define ENHANCED_INPUT_PAD_AXIS_MUTE_H

#include <stdbool.h>

/* The right stick's vertical, which is the axis the shipped bindings put on walking. */
#define PAD_AXIS_RIGHT_STICK_VERTICAL 3

/* Optional. A failure is a named degraded mode rather than a refusal: everything else about the
 * pad goes on working and the right stick goes on walking the player. */
bool pad_axis_mute_install(int axis);

#endif /* ENHANCED_INPUT_PAD_AXIS_MUTE_H */
