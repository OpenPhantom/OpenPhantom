/* xinput_joy.h: an XInput pad presented as the WinMM joystick the engine reads.
 *
 * Windows hands an Xbox pad's WinMM/DirectInput reports only to the process in front; behind,
 * every axis reads centred and no button is down. XInput has no such gate. Answering the engine's
 * joyGetPosEx and joyGetDevCapsA from XInput keeps the copy behind the other one playable.
 *
 * The layout is the one the pad gives through WinMM in front ("Microsoft PC-joystick driver"):
 * X/Y left stick, Z both triggers, R/U right stick vertical/horizontal, buttons A B X Y LB RB
 * View Menu LS RS as 1 to 10, the d-pad as a POV in hundredths of a degree (caps say 4-direction).
 */
#ifndef MULTI_INSTANCE_XINPUT_JOY_H
#define MULTI_INSTANCE_XINPUT_JOY_H

#include <windows.h>
#include <mmsystem.h>
#include <xinput.h>

/* Fills every field of `out` from `pad`; dwSize and dwFlags are left as the caller passed them. */
void xinput_joy_position(const XINPUT_GAMEPAD *pad, JOYINFOEX *out);

/* The capabilities the real pad reports through WinMM, so the engine's ranges and deadzone match. */
void xinput_joy_caps(JOYCAPSA *out);

/* `slot` 0 to 3. Replaces the game's joyGetPosEx and joyGetDevCapsA imports. False if it could not. */
BOOL xinput_joy_install(int slot);

#endif /* MULTI_INSTANCE_XINPUT_JOY_H */
