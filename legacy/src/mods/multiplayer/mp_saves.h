/* mp_saves.h: the savegames this installation holds, and the one thing a lobby needs out of each.
 *
 * Layer 1. It reads files and nothing else, no engine address, no engine header, so the header
 * parser is driven in a test against bytes the test writes itself.
 *
 * ======================================= What a save says =====================================
 *
 * A .SAV opens with a tag of twenty four bytes, "ZANZIBAR", a length and a version, and behind
 * it a small fixed header before the tagged subsystem blocks. Two of its fields are all a lobby
 * wants:
 *
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
 * The index was not assumed. The eleven shipped snapshots carry 0x18 = 1..10, which alone could
 * be a slot as well as an index; the player's own saves settle it (the Otoh Gunga save reads 2,
 * the palace garden save 3, the Trade Federation station save 0, the swamp save 1 and the Mos
 * Espa save 5, as the load screen names them in the installation's own language), and those are
 * the table's own rows for gunga, garden, fedship, swamp and espa: five agreements with the
 * level table in a language the table is not written in.
 *
 * The word at +0x14 is not a field, though it reads like one. The engine builds the tag on its
 * stack and writes the name, the length and the version; the two words behind them, at +0x10
 * and +0x14, go into the file as they lay there. In the shipped snapshots that is the folder
 * string's address and the slot number, left over from the call that built the file's name. A
 * save written under another stack holds something else: one of this installation's own reads
 * an address at +0x10 and nought at +0x14, and is slot twelve. So nothing is decided by that
 * word. Its low byte is kept as `slot` for the name a save without one is given, and for the
 * report.
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
    uint8_t slot;                      /* the low byte at +0x14, which is no field: see above */
    uint8_t level_index;               /* which level it plays in: what the clients load */
} mp_save_t;

/* What a look at a file, or at the bytes of one, found. The three answers that are not a save
 * are kept apart because they call for different things. A save of a level that was loaded by
 * its path carries no index into the table, and whoever was sent such a file loads the level
 * itself. A file that did not open, or opened and is no savegame at all, says nothing about the
 * level: a file on the disk is looked at again, and nobody begins a level on the strength of
 * it. */
typedef enum mp_saves_look {
    MP_SAVES_LOOK_SAVE = 0,     /* a save of a level of the game's own table */
    MP_SAVES_LOOK_NO_LEVEL,     /* a save, and the level it names is not in the table */
    MP_SAVES_LOOK_NOT_A_SAVE,   /* read, and it is no savegame: short, or another magic */
    MP_SAVES_LOOK_UNREADABLE    /* it did not open, or did not hold a header's worth of bytes */
} mp_saves_look_t;

/* Reads the fields out of a header and says what it is. Pure. `out` is filled for a save
 * alone; a name byte that is not printable ASCII is dropped from it. Never
 * MP_SAVES_LOOK_UNREADABLE: bytes in hand were read. */
mp_saves_look_t mp_saves_judge_header(const uint8_t *bytes, size_t length, mp_save_t *out);

/* Walks the save folder. Answers how many were found; safe to call again. */
size_t mp_saves_scan(void);

size_t           mp_saves_count(void);
const mp_save_t *mp_saves_at(size_t index);

/* The save with this file name, or NULL. */
const mp_save_t *mp_saves_find(const char *file);

/* Reads one file's header, catalogue or not, and says what the file is. What the start module
 * and the follower ask of a file that is not a choice, the received one above all: it is a
 * save if its header says so, wherever it came from. The reason is part of the answer because
 * a caller does something else when the file did not open than when it opened and names no
 * level. `out` is filled for a save alone. */
mp_saves_look_t mp_saves_look(const char *file, mp_save_t *out);

void mp_saves_report(void);

#endif /* MULTIPLAYER_MP_SAVES_H */
