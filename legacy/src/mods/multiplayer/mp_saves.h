/* mp_saves.h: the savegames this installation holds, and the one thing a lobby needs out of each.
 *
 * Layer 1. It reads files and nothing else, no engine address, no engine header, so the header
 * parser is driven in a test against bytes the test writes itself.
 *
 * ======================================= What a save says =====================================
 *
 * A .SAV opens with "ZANZIBAR" and a small fixed header before the tagged subsystem blocks.
 * Three of its fields are all a lobby wants:
 *
 *   +0x14  the slot number, which is also the number in the file name
 *   +0x18  THE LEVEL INDEX, 0..10, into the game's own level table
 *   +0x20  the name the load screen shows, as plain text
 *
 * The level index names the level a save sits in, which is what the lobby shows and what its setup
 * note carries. The host restores the save, and every client is sent the same file before the
 * start and restores it the same way (mp_bridge_savefile.h), so each machine begins in the host's
 * level with the host's campaign. What happens after that reaches the clients as the host's
 * snapshots, which is the authority model this feature settled on: the host simulates and the
 * clients replicate.
 *
 * The index was not assumed. The slot at 0x14 is the only dword in the first 0x400 bytes that
 * runs 1 to 11 across the eleven shipped snapshots, and those carry 0x18 = 1..10 beside it, which
 * alone cannot tell a slot from an index; the player's own saves separate them (the Otoh Gunga
 * save reads 2, the palace garden save 3, the Trade Federation station save 0, the swamp save 1
 * and the Mos Espa save 5, as the load screen names them in the installation's own language),
 * and those are the table's own rows for gunga, garden, fedship, swamp and espa: five agreements
 * with the level table in a language the table is not written in.
 */
#ifndef MULTIPLAYER_MP_SAVES_H
#define MULTIPLAYER_MP_SAVES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MP_SAVES_MAX       32u
#define MP_SAVES_NAME_MAX  32u   /* the display name, as the header holds it */
#define MP_SAVES_FILE_MAX  64u   /* "save\\Zanzi03.sav", what the engine's loader is handed */

/* Where they live and what they are called, as the engine's own format string spells it: its
 * folder string is `.\save\` and its file format is `%sZanzi%02d.sav`, relative to the game's
 * working folder. */
#define MP_SAVES_FOLDER  "save"
#define MP_SAVES_PATTERN "save\\*.sav"

/* The file a client receives from its host is written under this name and restored from it
 * (mp_bridge_savefile). It is never a choice: the scan leaves it out, because a machine that was
 * a client last week would otherwise offer somebody else's game as one of its own, under the
 * name the other host's save carried. */
#define MP_SAVES_JOIN_FILE "MPJOIN.SAV"
#define MP_SAVES_JOIN_PATH "save\\MPJOIN.SAV"

/* The three fields, and the least a file must hold to carry them. */
#define MP_SAVES_MAGIC        "ZANZIBAR"
#define MP_SAVES_MAGIC_BYTES  8u
#define MP_SAVES_OFF_SLOT     0x14u
#define MP_SAVES_OFF_LEVEL    0x18u
#define MP_SAVES_OFF_NAME     0x20u
#define MP_SAVES_HEADER_BYTES 0x40u

typedef struct mp_save {
    char    file[MP_SAVES_FILE_MAX];   /* relative to the game's working folder */
    char    name[MP_SAVES_NAME_MAX];   /* what the header calls it; never empty */
    uint8_t slot;
    uint8_t level_index;               /* which level it plays in: what the clients load */
} mp_save_t;

/* Reads the fields out of a header. False for anything that is not a save: a short buffer, a
 * wrong magic, a level index past the table, or a name that is not printable ASCII. Pure. */
bool mp_saves_parse_header(const uint8_t *bytes, size_t length, mp_save_t *out);

/* Walks the save folder. Answers how many were found; safe to call again. */
size_t mp_saves_scan(void);

size_t           mp_saves_count(void);
const mp_save_t *mp_saves_at(size_t index);

/* The save with this file name, or NULL. */
const mp_save_t *mp_saves_find(const char *file);

/* Reads one file's header, catalogue or not. What the start module asks of a file that is not a
 * choice, the received one above all: it is a save if its header says so, wherever it came from. */
bool mp_saves_read(const char *file, mp_save_t *out);

void mp_saves_report(void);

#endif /* MULTIPLAYER_MP_SAVES_H */
