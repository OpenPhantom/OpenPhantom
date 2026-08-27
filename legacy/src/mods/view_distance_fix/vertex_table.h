/* vertex_table.h: move the vertex cache out of the BSS into a buffer with a guard page behind it.
 *
 * WHY: cell_watchdog.h already names this "WALL 2, and it is the nastier one" - 16384 slots of
 * 0x40 bytes, and unlike the cell table's silent corruption this one aborts cleanly but leaves
 * torn geometry that survives until the level reloads. Raising the view distance runs into this
 * wall before the cell table's own (relocated) one. This is the same answer draw_table.c already
 * gave the cell table: a much larger buffer with a PAGE_NOACCESS page behind it, so the three
 * gates cell_watchdog.h documents may be raised together and the abort becomes something normal
 * play does not reach, rather than something a longer view distance walks straight into.
 *
 * ORDERING CONSTRAINT, same shape as draw_table.h's: cell_watchdog_install() resolves the vertex
 * counter against operand +0x08 of gate 1, an operand this module never touches, so in principle
 * the order does not matter for THAT read. It is kept second anyway, after cell_watchdog and after
 * draw_table, so every relocation in this DLL follows one rule rather than one rule for some and a
 * different one remembered specially for this file.
 *
 * vertex_table_restore() belongs at DLL_PROCESS_DETACH, same as draw_table_restore().
 */
#ifndef VERTEX_TABLE_H
#define VERTEX_TABLE_H

#include <stdbool.h>
#include <stdint.h>

/* All-or-nothing. Writes nothing at all unless every gate passes, and rolls back completely if a
 * write fails midway. Idempotent: a second call is ignored. */
void vertex_table_relocate(void);

void vertex_table_restore(void);

bool     vertex_table_is_active(void);
uint32_t vertex_table_limit(void);   /* the new vertex-cache limit, or the retail one when inactive */

#endif /* VERTEX_TABLE_H */
