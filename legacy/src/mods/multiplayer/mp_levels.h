/* mp_levels.h: which levels this installation can load, the shipped ones and the player's own.
 *
 * Two sources, one list:
 *
 *   The game's own table. Eleven rows of three pointers at the address the campaign loop's load
 *   branch names (mp_cells: MP_CELL_LEVEL_TABLE): the .b3d path, the name the level is known by
 *   ("Naboo Swamp", "The Underwater City"), and its cutscene. The names are the game's, not this
 *   feature's, which is why they are read rather than written down.
 *
 *   The level folder. The eleven ship as LOOSE FILES under the data root, so anything else lying
 *   beside them is a level this installation can load too. That is the whole of the "own maps"
 *   support: put a .b3d in the folder and it is in the list.
 *
 * A path here is relative ("level\\swamp.b3d") and that is what goes on the wire, because the data
 * root is read out of the registry and differs per installation. The absolute path is built where
 * it is needed, against this machine's own root.
 *
 * The loader does not check. The branch that takes a path verbatim ignores the loader's answer,
 * so a path that names nothing walks into a null world pointer. Nothing here hands a path on
 * without mp_levels_present having said the file is there.
 */
#ifndef MULTIPLAYER_MP_LEVELS_H
#define MULTIPLAYER_MP_LEVELS_H

#include "mp_lobby.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Eleven shipped plus room for a folder full of the player's own. */
#define MP_LEVELS_MAX 64u

/* An absolute path: the data root is a 128 byte field in the engine, and a relative path is 64. */
#define MP_LEVELS_ABSOLUTE_MAX 224u

typedef struct mp_level {
    char    path[MP_LOBBY_LEVEL_MAX];    /* relative, as the wire carries it and the loader
                                          * takes it */
    char    title[MP_LOBBY_TITLE_MAX];   /* the game's own name for it, or the file's stem */
    uint8_t index;                       /* 0..10 for a shipped level, else MP_LOBBY_LEVEL_CUSTOM */
} mp_level_t;

/* Reads the table and scans the folder. Answers how many levels are in the list; safe to call
 * again, and does, because a player may drop a file in while the game is running. Zero means the
 * table did not resolve AND the folder had nothing, which is a build that cannot host. */
size_t mp_levels_scan(void);

size_t            mp_levels_count(void);
const mp_level_t *mp_levels_at(size_t index);

/* The level with this relative path, compared the way the engine compares a file name: without
 * regard to case. NULL for one that is not in the list. */
const mp_level_t *mp_levels_find(const char *path);

/* The shipped level with this table index, or NULL. */
const mp_level_t *mp_levels_shipped(uint8_t index);

/* The movie that opens the shipped level with this table index, as the table names it
 * ("movie\scene1"). False for an index past the table, for the three levels that open with none,
 * and when the table did not resolve; `out` is empty then. */
bool mp_levels_movie_of(uint32_t index, char *out, size_t out_size);

/* This machine's root joined to a relative path. False when the root did not resolve or the
 * result would not fit. */
bool mp_levels_absolute(const char *relative, char *out, size_t out_size);

/* Whether the file is really on disk. The guard in front of every load. */
bool mp_levels_present(const char *relative);

/* ---------------------------------------------------------------------------------------------
 * Pure, so the awkward halves are driven in a test without a game.
 * ------------------------------------------------------------------------------------------- */

/* The name to show for a file nobody named: "ARENA.B3D" becomes "Arena". Cuts to the title field,
 * keeps printable ASCII, and answers false for a name that is not a .b3d at all. */
bool mp_levels_title_from_file(const char *file_name, char *out, size_t out_size);

/* Joins a root and a relative path with exactly one separator between them. */
bool mp_levels_join(const char *root, const char *relative, char *out, size_t out_size);

void mp_levels_report(void);

#endif /* MULTIPLAYER_MP_LEVELS_H */
