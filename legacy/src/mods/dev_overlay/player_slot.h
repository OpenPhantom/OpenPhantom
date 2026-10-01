/* player_slot.h: the engine's own pointer at the player record, found once and shared.
 *
 * Five cheats and the input freeze all read the player through one global cell. It was written
 * down as an address, which proved nothing about the build the DLL was running in: a recompile
 * that kept the cell elsewhere would have been read at the old place, and what sits there in
 * that build is anybody's guess. The cell is read out of an operand now, from the engine's own
 * "is the player suspended" predicate, whose first instruction is that one load.
 *
 * Internal to dev_overlay.
 */
#ifndef DEV_OVERLAY_PLAYER_SLOT_H
#define DEV_OVERLAY_PLAYER_SLOT_H

#include <stdbool.h>
#include <stdint.h>

/* Finds the cell. Idempotent, logs once, and answers false when the predicate did not resolve or
 * its operand does not name a cell inside the image. Nothing that reads the player installs
 * without it. */
bool player_slot_resolve(void);

/* The cell itself, or NULL before a successful resolve. The engine writes it on every level, so
 * a reader that wants the player must go through the cell every time rather than keep the
 * pointer it found. */
void *const volatile *player_slot(void);

/* What the cell holds right now: the player record, or NULL between levels and before the first
 * one, and NULL when the cell was never found. */
void *player_slot_current(void);

/* Where the cell was found, for a caller that has its own evidence for the same cell and wants
 * to check the two agree. 0 before a successful resolve. */
uintptr_t player_slot_address(void);

#endif /* DEV_OVERLAY_PLAYER_SLOT_H */
