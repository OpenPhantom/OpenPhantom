/* joystick_select.h: which WinMM joystick this copy of the game reads.
 *
 * The engine's whole controller surface is three WinMM calls, joyGetNumDevs, joyGetPosEx and
 * joyGetDevCapsA. At startup it walks the joystick ids and keeps the first one joyGetPosEx answers
 * for, in one global (retail 0x008619DC), and every later read passes that id. Two copies on one
 * machine therefore both take the first pad. Answering every id the game asks about with the one
 * configured here makes the walk settle on it, and from then on this copy reads that pad and no
 * other, whichever window is in front: WinMM has no notion of focus.
 *
 * Only this module's import table entries for the game are replaced; nothing in the engine is
 * patched, and other DLLs' own WinMM calls are untouched.
 */
#ifndef MULTI_INSTANCE_JOYSTICK_SELECT_H
#define MULTI_INSTANCE_JOYSTICK_SELECT_H

/* `joystick` is a WinMM id, 0 to 15. Below zero leaves the game's own choice alone. */
void joystick_select_install(int joystick);

#endif /* MULTI_INSTANCE_JOYSTICK_SELECT_H */
