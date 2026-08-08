/* diag_flow.h: observers for level flow, the player mode, dialogue and effects.
 *
 * These four share one signature table because they share one shape: a thin hook that calls the
 * original and reports what it observed. The world simulation observers (movers, the AI state
 * machine) live in diag_world.c, which has its own table for the same reason.
 */
#ifndef DIAG_FLOW_H
#define DIAG_FLOW_H

int diag_level_install(int level_level);
int diag_player_install(int player_level);
int diag_dialogue_install(int dialogue_level);
int diag_fx_install(int fx_level);

#endif /* DIAG_FLOW_H */
