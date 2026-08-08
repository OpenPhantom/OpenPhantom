/* enhanced_input.h: mouse look and strafing: tank controls become shooter controls.
 *
 * Produces: enhanced_input.dll
 */
#ifndef ENHANCED_INPUT_H
#define ENHANCED_INPUT_H

#include <stdbool.h>

void enhanced_input_install(void);

/* True when both player phases are ours. A control that drives nothing must not be offered. */
bool enhanced_input_is_active(void);

/* The live setting, which the check box on the controls screen both seeds from and writes to. */
bool enhanced_input_strafe_enabled(void);

/* Takes effect at once and is saved at once. Refuses, and says so, when sideways walking
 * cannot run in this session, so a switch can never claim more than the DLL can deliver. */
void enhanced_input_set_strafe(bool enabled);

/* Whether free look CAN run in this session: the follow camera in this build was recognised and its
 * update is detoured. It says nothing about whether the feature is switched on. A screen must not
 * offer a switch for it while this is false, because that switch could never do anything. */
bool enhanced_input_free_look_available(void);

/* The live control mode. True means the mouse turns the camera; false means it turns the body. */
bool enhanced_input_free_look_enabled(void);

/* Takes effect on the next substep and is saved at once. Refuses, and says so, when the camera
 * machinery is not installed, exactly as the strafe setter refuses when its axis is missing. */
void enhanced_input_set_free_look(bool enabled);

#endif /* ENHANCED_INPUT_H */
