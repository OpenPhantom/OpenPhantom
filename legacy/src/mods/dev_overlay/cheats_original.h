/* cheats_original.h: the eleven toggles the shipped game already has.
 *
 * They are not reimplemented here. The engine keeps eleven int32 of runtime state, one per code,
 * and its own console flips a row with `^ 1`; this reads that same array and writes it the same
 * way, so a row switched from our panel is indistinguishable from one typed into the console. Every
 * consumer stays the engine's own and none of them is patched.
 *
 * The name table and the flag array are resolved out of the image rather than written down as
 * constants. The parallel table of message ids beside them is deliberately not used: it exists to
 * confirm a typed code on screen, and a panel that shows the state has nothing to confirm.
 * cheats_original.c carries the bytes.
 */
#ifndef CHEATS_ORIGINAL_H
#define CHEATS_ORIGINAL_H

#include <stdbool.h>
#include <stdint.h>

/* Eleven, and the count is the image's: the pointer table is terminated by a NULL dword and the
 * console's own loop is bounded by the literal 0xb. Both agree. */
#define CHEATS_ORIGINAL_MAX 11u

/* Resolves the pointer table and the flag array. Answers false and logs which of the two was not
 * found, leaving the feature inactive rather than writing into an address it guessed. */
bool cheats_original_resolve(void);

/* How many rows really resolved. Zero before cheats_original_resolve has succeeded. */
uint32_t cheats_original_count(void);

/* The code as the game spells it, for example "turntables". NULL for an index out of range. The
 * string belongs to the image and outlives everything here. */
const char *cheats_original_name(uint32_t index);

/* Whether that row is on right now. Read fresh from the engine on every call, because the console
 * and a save game can both change it behind us. False for an index out of range. */
bool cheats_original_is_on(uint32_t index);

/* Flips one row, exactly as the console's `^ 1` does, and answers the new state. A row that is out
 * of range is refused and answers false. */
bool cheats_original_toggle(uint32_t index);

#endif /* CHEATS_ORIGINAL_H */
