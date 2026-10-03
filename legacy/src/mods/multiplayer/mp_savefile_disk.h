/* mp_savefile_disk.h: the received savegame on the disk: whether it is there already, and
 * putting it there.
 *
 * Layer 1. It opens, reads and writes one file and knows nothing else: no engine, no session, no
 * address, so a test drives it against a file it writes itself.
 *
 * A client writes the host's savegame under one fixed name and the engine restores it from there.
 * Several copies of the game can run out of one folder, and then every one of them receives the
 * same file and writes it under that same name. Each write used to truncate the file and write it
 * again, sharing it with nobody while it did, so a neighbour that looked at the file in that moment
 * could not open it. The bytes on the disk are compared first here: a file that already holds
 * exactly what would be written is left alone and counts as written. One copy writes, the others
 * find it.
 *
 * What is left of the race: nothing stands between the comparison and the write. A copy that
 * compares while a neighbour still writes cannot open the file, takes that as not known to be
 * the same, and writes the same bytes once more when the neighbour has let go. For those
 * milliseconds the file is shared with nobody again, and a third copy whose engine opens it for
 * a restore just then is refused by the system. It is the old fault at a far smaller size, and
 * writing beside the file and renaming over it would end it.
 *
 * The write itself shares the file with nobody, and that has to stay. The engine keeps the file
 * open for the whole of a restore and lets others read and write beside it, so a write that asked
 * for no sharing is what the system refuses while a restore reads. A write that shared would
 * truncate the file under the restore.
 */
#ifndef MULTIPLAYER_MP_SAVEFILE_DISK_H
#define MULTIPLAYER_MP_SAVEFILE_DISK_H

#include <stdbool.h>
#include <stdint.h>

/* Whether the file at `path` holds exactly these bytes. False when it does not open, has another
 * length or differs anywhere; nothing is written either way. */
bool mp_savefile_disk_holds(const char *path, const uint8_t *bytes, uint32_t total);

/* Writes the file whole, replacing what was there. False when it could not be opened for writing
 * or the write came up short; `error` then holds the system's code and is otherwise nought. */
bool mp_savefile_disk_write(const char *path, const uint8_t *bytes, uint32_t total,
                            uint32_t *error);

#endif /* MULTIPLAYER_MP_SAVEFILE_DISK_H */
