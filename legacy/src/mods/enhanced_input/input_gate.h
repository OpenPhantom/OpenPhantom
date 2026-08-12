/* input_gate.h: is the engine reading input at all?
 *
 * The original locks nothing in a menu, a dialogue or a cutscene. It does not have to: it reads
 * the mouse inside Plr_Steer, and Plr_Steer is one of the player phases, dispatched from the
 * phase table through `call dword ptr [ecx*4 + g_plrPhases]`. Its entry at 0x00449EE8 has no E8
 * caller anywhere in the image, which is what a table dispatch looks like. In those three states
 * the engine simply does not run the phases, so the steer never runs and nothing turns.
 *
 * Anything in this DLL that turns the view on the RENDER clock is outside that pipeline and would
 * otherwise keep turning where the engine had stopped. So the phase thunks stamp this gate every
 * time the engine runs a phase, and the drains ask it before they deliver anything.
 * ============================================================================================ */
#ifndef INPUT_GATE_H
#define INPUT_GATE_H

#include <stdbool.h>

/* Called from the phase thunks, every time the engine runs a player phase. */
void input_gate_note_phase_ran(void);

/* True while the engine is running the player phases, which is the only state in which it reads
 * input itself. False in a menu, a dialogue and a cutscene, and false before the first phase has
 * ever run, which is the front end.
 *
 * A caller that sees false must deliver nothing AND drop whatever it had banked. The engine never
 * saw that movement, so there is nothing to pay out when the phases start again; banking it would
 * swing the view by everything the hand did during a cutscene the moment the cutscene ended. */
bool input_gate_is_open(void);

/* Says once, the first time the gate is found shut, that this is what is happening. Kept apart
 * from the test so a caller can ask as often as it likes without deciding when to log. */
void input_gate_note_closed(void);

#endif /* INPUT_GATE_H */
