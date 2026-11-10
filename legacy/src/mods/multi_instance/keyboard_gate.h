/* keyboard_gate.h: the keyboard reaches only the copy in front.
 *
 * stdControl_openKeyboard (retail 0x0048D9E8) never sets a cooperative level, so DirectInput's
 * default applies: non-exclusive and BACKGROUND. Every copy on the machine reads every key. While
 * this copy's window is behind, the buffered events are drained before the engine's poll sees
 * them, and keys still down when the focus left are released once, so none sticks.
 *
 * The drain runs before the original poll rather than after it, so controller_input's background
 * roll, which writes its keys after the poll, keeps working whichever detour is outermost.
 */
#ifndef MULTI_INSTANCE_KEYBOARD_GATE_H
#define MULTI_INSTANCE_KEYBOARD_GATE_H

void keyboard_gate_install(void);

#endif /* MULTI_INSTANCE_KEYBOARD_GATE_H */
