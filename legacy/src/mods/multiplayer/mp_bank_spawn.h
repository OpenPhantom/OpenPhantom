/* mp_bank_spawn.h: the spawn's window, the half that knows the cells.
 *
 * mp_bank owns the window and its order. Before it swaps in for a spawn it asks this file for a
 * snapshot, and in the swap out, before the pointers go back, it has this file give back. What is
 * kept, and why, is in mp_bank_keep.h: the six inventory and key bytes, the four hero records and
 * the current player index, which the engine's hero spawn writes outside the hero block.
 *
 * This file finds those cells, proves once that status_setActivePlayer names the same ones, takes
 * and gives back, and counts. The proof has a second user: the carry of mp_hero_carry reads and
 * writes the same cells when the player's own hero changes, and takes them from here rather than
 * proving them a second time.
 *
 * The bank's own status copy still carries this player's six bytes after a spawn, because the
 * spawn's store half writes them there. Nothing that runs a bank reaches status_setActivePlayer,
 * so they are never read.
 */
#ifndef MULTIPLAYER_MP_BANK_SPAWN_H
#define MULTIPLAYER_MP_BANK_SPAWN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The snapshot, taken before the swap in. False, counted and said once, when the cells cannot be
 * proven or when the status pointer (read from `status_cell`) and the current player index do not
 * both name `local_hero`: the window would then put them back onto two different heroes, and a
 * window it cannot give back from would take this player's keys. */
bool mp_bank_spawn_take(uintptr_t status_cell, size_t index, int32_t local_hero);

/* Puts back every region the spawn moved, then reads it all again. Called only by mp_bank's swap
 * out, after the spawn's last engine writer. */
void mp_bank_spawn_give_back(void);

void mp_bank_spawn_report(void);

/* The cells status_setActivePlayer names, as the proof found them: the status pointer, the six
 * inventory and key bytes, hero 0's record and the current player index. */
typedef struct mp_bank_spawn_cells {
    uintptr_t status_cell;
    uintptr_t flags_cell;
    uintptr_t records_cell;
    uintptr_t current_cell;
} mp_bank_spawn_cells_t;

/* Runs the proof if nobody has yet, and hands out the proven cells. False when the proof failed;
 * the refusal is said once for both users and kept for the process. */
bool mp_bank_spawn_cells(mp_bank_spawn_cells_t *out);

#endif /* MULTIPLAYER_MP_BANK_SPAWN_H */
