/* diag_world.h: observers for the world simulation: movers and the AI state machine.
 *
 * Level flow, the player mode, dialogue and effects live in diag_flow.h.
 */
#ifndef DIAG_WORLD_H
#define DIAG_WORLD_H

int diag_trigger_install(int trigger_level);
int diag_fsm_install(int fsm_level);

#endif /* DIAG_WORLD_H */
