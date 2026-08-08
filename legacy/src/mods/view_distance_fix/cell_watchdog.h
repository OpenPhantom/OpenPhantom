/* cell_watchdog.h: measure the two walls that a longer view distance runs into, and back off.
 *
 * ==============================================================================================
 * WALL 1: THE CELL TABLE
 *
 * bapdraw_gatherCell checks its 8192-entry limit EXACTLY ONCE, at function entry
 * (0x4064B8 `cmp eax,0x2000`). The face loop afterwards runs unchecked to 0x406815, and
 * bapdraw_gatherCellMovers hangs on with NO limit at all, a single mover carries up to 855
 * faces (FINAL, measured). And the end of the table IS the start of the bucket list heads:
 *
 *     0x8A4880 + 0x2000*0x0C = 0x8BC880
 *
 * Raising the view distance therefore drives towards a silent memory corruption. It happened:
 * an access violation in the draw function 0x4199B0 with esi and edi inside the bucket array
 * (008BC978 / 008BC970) and ebp = 0x1F, an overwritten list head the renderer read as a
 * pointer. Entry number 8193 landed on g_bucket[3], and the values loaded from it, 0x5C0D5E31 and
 * 0x5E335E32, are byte-exactly the corner indices of GARDEN cell 7406, slots 5 and 7. Each of
 * those values occurs exactly ONCE in all 334,396 faces of the game.
 *
 * How hard it presses depends on the level, not only on the scale: the same setting ran through
 * a small level and broke in a large one. A fixed cap cannot solve that; it would be either too
 * tight or too loose. So we measure.
 *
 * ==============================================================================================
 * WALL 2: THE VERTEX CACHE, and it is the nastier one
 *
 * 16384 slots of 0x40 bytes = exactly 1 MiB. If gate 1 (0x41A0DF) trips, the counter is left at
 * exactly 0x4000, the counter is written BEFORE the branch, and the other two gates then trip
 * on the FIRST face of the next call. Worse, gate 2 jumps BEHIND the `touched` reset loop, which
 * iterates over the CACHE SLOTS. A vertex skipped because of `touched` never gets a slot again
 * and is therefore never reset until the level is reloaded. That is the cause of the stretched
 * triangles seen after raising the view distance: the damage is permanent, not transient.
 *
 * Unlike the cell table the vertex cache aborts CLEANLY (all three gates branch to a return
 * before every write), so the draw function returns normally and a measurement at frame end SEES
 * it. That was not true for the cell table, where the draw function dies first.
 */
#ifndef CELL_WATCHDOG_H
#define CELL_WATCHDOG_H

#include <stdbool.h>
#include <stdint.h>

/* Resolves the counters. `relocation_active` tells it not to lower the gate itself, because the
 * draw-table relocation sets that same word and two writers on one word would be a bet on who
 * comes last. Returns false when the watchdog cannot run, the caller must then hold the view
 * scale at 1.0, because an overflow would otherwise go unnoticed. */
bool cell_watchdog_install(bool lower_cell_limit, bool relocation_active);

/* Called once per frame. Lowers the effective view scale when a wall comes close, and never
 * raises it again: better a permanently shorter view than a crash ten minutes later. */
void cell_watchdog_on_frame(float *effective_view_scale);

/* Adopts the larger limit after a successful draw-table relocation. */
void cell_watchdog_set_limit(uint32_t new_limit);

/* The cell budget as a multiple of the shipped 8192, for the radius cap. */
float cell_watchdog_budget(void);

#endif /* CELL_WATCHDOG_H */
