/* draw_table.h: move g_drawEntry out of the BSS into a buffer with a guard page behind it.
 *
 * WHY: the cell table's end IS the start of the bucket list heads, and gatherCell only checks its
 * limit on entry (see cell_watchdog.h for the full account). Lowering the limit is one answer and
 * costs view distance. This is the other: give the table a much larger buffer with a PAGE_NOACCESS
 * page behind it, then the limit may RISE (32768 instead of 8192) and the overflow becomes
 * structurally unreachable.
 *
 * ORDERING CONSTRAINT, and it is not a matter of taste:
 *   cell_watchdog_install() runs the cross-check `table + 0x2000*12 == bucket` against exactly the
 *   operands this module rewrites. If the relocation ran first, the watchdog would read our
 *   buffer address, the cross-check would fail, and it would switch itself off together with the
 *   view distance. So: watchdog first, relocation second.
 *
 * draw_table_restore() belongs at DLL_PROCESS_DETACH.
 */
#ifndef DRAW_TABLE_H
#define DRAW_TABLE_H

#include <stdbool.h>
#include <stdint.h>

/* All-or-nothing. Writes nothing at all unless every gate passes, and rolls back completely if a
 * write fails midway. Idempotent: a second call is ignored. */
void draw_table_relocate(void);

void draw_table_restore(void);

bool     draw_table_is_active(void);
uint32_t draw_table_limit(void);   /* the new cell limit, or the retail one when inactive */

#endif /* DRAW_TABLE_H */
