/* start_level_row.h: the Level selection group's setting for the level a new game starts at.
 *
 * Reads and writes [dev_overlay] NewGameStartsAt, 0 for the game's own first level and 1 to 11
 * for a row of the level table, and hands the value to start_level.c. The chip shows the level's
 * file stem beside the number.
 */
#ifndef DEV_OVERLAY_START_LEVEL_ROW_H
#define DEV_OVERLAY_START_LEVEL_ROW_H

#include <stdbool.h>
#include <stdint.h>

/* Reads the setting and hands it to start_level.c. Once, after start_level_install(). */
void start_level_row_load(void);

int  start_level_row_get(void);

/* Writes a choice, 0 to 11; anything else is clamped by the setter. False on a failed write. */
bool start_level_row_set(int level);

/* The chip: "Off" for 0, else the number and the file stem, "6 espa". */
void start_level_row_value(char *out, uint32_t out_size);

#endif /* DEV_OVERLAY_START_LEVEL_ROW_H */
