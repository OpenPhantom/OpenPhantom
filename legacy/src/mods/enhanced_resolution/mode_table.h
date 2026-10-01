/* mode_table.h: the engine's raw display mode list, and the menu list built from it.

 * Lifted out of enhanced_resolution.c, which had reached the 900 line hard limit exactly. The seam
 * is a whole responsibility rather than a slice: reading g_aRawMode and g_numRawModes out of the
 * instructions around the aspect gate, dumping that table on request, and capping what
 * graphics_enumModes hands the options screen. None of it is touched by the resolution forcing,
 * the window mode or the menu gates that stayed behind, and the 4 KB scratch array it needs
 * belongs to it alone.
 *
 * The site table that resolves these two functions stays in enhanced_resolution.c, so both
 * addresses arrive as arguments; one table means one block of resolution logging.
 */
#ifndef ENHANCED_RESOLUTION_MODE_TABLE_H
#define ENHANCED_RESOLUTION_MODE_TABLE_H

#include <stdbool.h>
#include <stdint.h>

/* push ebp; mov ebp,esp; sub esp,0x808. Nine bytes and an exact instruction boundary. Declared
 * here because the entry that resolves the function is in enhanced_resolution.c while the detour
 * that overwrites it is here, and one number has to serve both. */
#define ENUM_MODES_PROLOGUE_SIZE 9u

/* Reads g_aRawMode, g_numRawModes and the engine's own free() out of the instructions in front of
 * the aspect gate, whose address is the only thing this needs from the caller. Every one is
 * checked against its opcode bytes before the operand is believed, so a build that moved them
 * leaves the table unresolved and the dump simply does not run. */
void mode_table_resolve(uintptr_t aspect_gate_site);

/* Caps the list graphics_enumModes hands the options screen, which holds 64 slots and does not
 * check. Also dumps the raw table on the first enumeration when `log_table` is set, since the
 * table is empty until stdDisplay_startup has run. */
void mode_table_install_cap(uintptr_t enum_modes_site, int max_menu_modes, bool log_table);

/* The two table pointers, for window_fit, which measures a requested mode against the same
 * list. Both are NULL until mode_table_resolve has run, and stay NULL on a build where the
 * opcode checks refused what they found, so a caller must expect that. */
const uint8_t  *mode_table_raw_modes(void);
const uint32_t *mode_table_raw_mode_count(void);

#endif /* ENHANCED_RESOLUTION_MODE_TABLE_H */
