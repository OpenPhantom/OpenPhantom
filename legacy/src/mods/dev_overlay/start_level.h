/* start_level.h: a new game that begins at a level of your choosing, with nothing carried in.
 *
 * The engine has this already, as a command line option: `level <n>` sets the front end's
 * "start at" index, and New Game then loads that row of the eleven-row level table, plays its
 * intro movie, shows its title and stamps the checkpoint, exactly as reaching it in play would.
 * What it cannot do is survive the front end: the campaign loop zeroes the index at the end of
 * every level, before the front end comes back, so a value set from inside the game is gone by
 * the time New Game reads it. And the developer panel is not drawn in the front end at all.
 *
 * So this sits on campaign_loadLevel instead, the one call every level entry goes through, and
 * redirects the one that is a NEW GAME: the campaign index is 0 (the table's first row), the
 * level outcome is below 4 (4 to 10 are the fail variants, and a restart of level one after a
 * death carries one), and the restore flag is clear (a save being loaded copies its own block over
 * the flow first, and that block carries the flag set). Then the campaign index is written to
 * the row chosen and the original is called with that row's path, so everything the loop does
 * after the load, the movie, the title, the checkpoint, reads the new index and is right.
 *
 * What the player has on arrival is what a new campaign gives plus what the level's own script
 * hands out, not what the shipped saves carry from the levels before. That is the point: a
 * level loads as itself, so a modded one can be played from its own first frame with no save
 * from the unmodded game in the way.
 */
#ifndef DEV_OVERLAY_START_LEVEL_H
#define DEV_OVERLAY_START_LEVEL_H

#include <stdbool.h>
#include <stdint.h>

#define START_LEVEL_COUNT 11

/* Resolves the four sites and places the detour. False, with a log line, when any did not
 * resolve; the row then reads unavailable. */
bool start_level_install(void);
bool start_level_is_available(void);

/* The level a new game starts at, 1 to START_LEVEL_COUNT, or 0 for the game's own first level.
 * The set clamps. */
int  start_level_get(void);
void start_level_set(int level);

/* The file stem of level `level` (1 based), "espa" for level\espa.b3d, read out of the engine's
 * own table; "" when it does not read. `out` is always terminated. */
void start_level_stem(int level, char *out, uint32_t out_size);

#endif /* DEV_OVERLAY_START_LEVEL_H */
