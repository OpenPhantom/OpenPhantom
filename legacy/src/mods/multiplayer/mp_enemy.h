/* mp_enemy.h: the engine's enemies, seen from the outside.
 *
 * This module deliberately does almost nothing: it can stop the AI on one machine and it can say
 * how many actors are alive. Both answers came before anything larger was designed, and neither can
 * be had from reading the binary.
 *
 * The reason the switch comes first is that the whole enemy design rests on one claim: that a
 * machine which is not the owner of an enemy can be told to stop simulating it, with one dword and
 * without side effects worth naming. The AI pass reads that dword before it does anything else,
 * so it also stops the AI flag timers, the activation scan and the use latch countdown. Which of
 * those matters in play is a question no disassembly answers.
 *
 * And the count is the number the whole budget rests on. A full enemy state is around 37 to 43
 * bytes and the packet budget is 1200, so whether the map holds eleven live actors or thirty seven
 * decides whether every actor can travel every substep or whether the rate has to be cut.
 *
 * Two things this deliberately leaves alone. It suspends no single actor: the cell is all or
 * nothing, and per actor suspension needs a detour on the script runner, a resolved site with no
 * caller yet. And it does not touch the activation scan, which turns back when no player is
 * alive; with two players "the player" there is always the local one, so a machine whose local
 * player is dead spawns and despawns nothing while the far player keeps going. That is a defect
 * of its own and is not addressed here.
 */
#ifndef MULTIPLAYER_MP_ENEMY_H
#define MULTIPLAYER_MP_ENEMY_H

#include <stdbool.h>
#include <stdint.h>

/* Both instances share one ini, so a switch that has to differ between them cannot be a plain
 * boolean. The third value is the one the field runs use: it lets the ini say "the client stops
 * its enemies" once, for both processes. */
typedef enum mp_enemy_suspend_mode {
    MP_ENEMY_SUSPEND_OFF = 0,
    MP_ENEMY_SUSPEND_ALWAYS = 1,
    MP_ENEMY_SUSPEND_WHEN_CLIENT = 2
} mp_enemy_suspend_mode_t;

/* Reads the three cells and says whether the module can do anything at all. Safe to call on a
 * build where none of them resolved: it logs once and every entry point below turns back. */
bool mp_enemy_install(mp_enemy_suspend_mode_t mode);

/* Which side this instance is. The suspend decision needs it and the bridge is the only thing that
 * knows it, so it arrives from there rather than being guessed here. */
void mp_enemy_set_client(bool is_client);

/* Once per substep: puts the switch where it should be and samples the two counters. Cheap, and
 * the write only happens when the cell disagrees with what this side wants. */
void mp_enemy_tick(void);

/* The live count and the engine's own high water mark, as last sampled. */
uint32_t mp_enemy_live(void);
uint32_t mp_enemy_live_peak(void);

void mp_enemy_report(void);

#endif /* MULTIPLAYER_MP_ENEMY_H */
