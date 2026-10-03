/* mp_bridge_savefile_look.c: what the host's savegame is, asked before a client restores it. See
 * the header and the declaration in mp_bridge_savefile.h.
 *
 * The answer used to be read off the disk alone, and a file that did not open was answered like a
 * file that names no level. For the second the right thing is to load the level itself: a save
 * of a level loaded by its path carries the host's own path. For the first it is not, because a
 * client that begins the level fresh beside a host that restored a savegame plays a campaign of
 * its own, and nothing on its screen says so. Three clients run out of one folder showed the
 * difference: all three wrote the received file under its one name, each sharing it with nobody
 * while it wrote, and the one that looked at the header in that moment began its level fresh.
 */
#include "mp_bridge_savefile_look.h"

#include "mp_bridge_savefile.h"
#include "mp_saves.h"

#include "common/logging.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* How long the received file may fail to read before the write is forgotten and made again, and
 * how many lines say so in one process. Three seconds is longer than a neighbour's write holds
 * the file, and short enough that a player waiting at the title is not left there. */
#define UNREAD_REWRITE_MS 3000u
#define UNREAD_LINES_MAX  8u

/* The looks of one wait follow each other frame by frame. A look that comes this long after the
 * last one that did not read begins a wait of its own: the earlier one ended some other way, the
 * host left or the session did, and its stamp is not this wait's. */
#define UNREAD_RUN_GAP_MS 1000u

typedef struct look_state {
    bool     stamped;        /* a run of looks that did not read is under way */
    uint32_t stamped_id;     /* the file that run is about */
    uint32_t since_ms;       /* when its first look was */
    uint32_t last_ms;        /* and its latest */
    uint32_t from_memory;    /* looks answered out of the assembled bytes */
    uint32_t from_disk;      /* looks answered out of the file */
    uint32_t unread;         /* looks at a file that did not open or was no savegame */
    uint32_t no_savegame;    /* looks at bytes that arrived whole and are no savegame */
    uint32_t forgets;        /* times the write was forgotten, to be made again */
    uint32_t lines;
} look_state_t;

static look_state_t ls;

/* The file on the disk did not read, or read as no savegame. The first such look of a run stamps
 * the time; three seconds of them forget the write, so the transfer writes the file again from
 * the bytes it holds, or asks the host for them again where it holds none. */
static void the_file_did_not_read(mp_saves_look_t look, uint32_t save_id, uint32_t system_error)
{
    uint32_t now   = mp_bridge_savefile_now_ms();
    bool     fresh = !ls.stamped || ls.stamped_id != save_id ||
                     (uint32_t)(now - ls.last_ms) > UNREAD_RUN_GAP_MS;

    ++ls.unread;
    ls.last_ms = now;
    if (fresh) {
        ls.stamped    = true;
        ls.stamped_id = save_id;
        ls.since_ms   = now;
        if (ls.lines >= UNREAD_LINES_MAX) {
            return;
        }
        ++ls.lines;
        if (look == MP_SAVES_LOOK_UNREADABLE) {
            log_warning("the host's savegame is on disk as %s and could not be opened or read "
                        "just now (the system's last error %lu); this side waits and looks again, "
                        "and begins no level without it", MP_SAVES_JOIN_PATH,
                        (unsigned long)system_error);
        } else {
            log_warning("the host's savegame is on disk as %s and does not read as a savegame "
                        "just now; this side waits and looks again, and begins no level without "
                        "it", MP_SAVES_JOIN_PATH);
        }
        return;
    }
    if ((uint32_t)(now - ls.since_ms) < UNREAD_REWRITE_MS) {
        return;
    }
    ls.stamped = false;
    ++ls.forgets;
    mp_bridge_savefile_forget_the_write();
    if (ls.lines < UNREAD_LINES_MAX) {
        ++ls.lines;
        log_warning("the host's savegame on disk has not read for %u ms, so what was written is "
                    "forgotten: the file is written again from the bytes this side holds, or "
                    "asked of the host again", (unsigned)UNREAD_REWRITE_MS);
    }
}

/* Bytes that hash to the host's file and are no savegame: shorter than a header, or opening with
 * another magic. A host names a savegame only after its engine has restored it or its own scan
 * has read it, so this is a file the two builds read differently. Waiting changes nothing, no
 * later look reads the same bytes another way, so the caller is told what it is told for a save
 * that names no level, and loads the level itself. The line says which of the two it was. */
static mp_saves_look_t the_bytes_are_no_savegame(void)
{
    ++ls.no_savegame;
    if (ls.lines < UNREAD_LINES_MAX) {
        ++ls.lines;
        log_warning("the host's savegame arrived whole and does not read as a savegame here "
                    "(shorter than its header, or another magic); it is answered as a savegame "
                    "that names no level, so this side loads the level itself");
    }
    return MP_SAVES_LOOK_NO_LEVEL;
}

mp_saves_look_t mp_bridge_savefile_look(uint32_t save_id, uint32_t save_bytes, mp_save_t *out)
{
    const mp_savefile_assembly_t *assembly = mp_bridge_savefile_assembly();
    mp_saves_look_t               look;
    uint32_t                      system_error;

    if (out == NULL) {
        return MP_SAVES_LOOK_UNREADABLE;
    }
    /* The bytes this side put together hash to the file's name, so they are the file. Asked of
     * them, the question cannot fail for a reason of the disk. */
    if (assembly->complete && assembly->file_id == save_id && assembly->total == save_bytes) {
        ++ls.from_memory;
        ls.stamped = false;
        look = mp_saves_judge_header(assembly->bytes, assembly->total, out);
        if (look == MP_SAVES_LOOK_SAVE) {
            memset(out->file, 0, sizeof out->file);
            memcpy(out->file, MP_SAVES_JOIN_PATH, sizeof MP_SAVES_JOIN_PATH);
        }
        return look == MP_SAVES_LOOK_NOT_A_SAVE ? the_bytes_are_no_savegame() : look;
    }
    /* A file this process wrote earlier and whose bytes it no longer holds: the disk is all
     * there is to ask. */
    ++ls.from_disk;
    look         = mp_saves_look(MP_SAVES_JOIN_PATH, out);
    system_error = (uint32_t)GetLastError();
    if (look == MP_SAVES_LOOK_SAVE || look == MP_SAVES_LOOK_NO_LEVEL) {
        ls.stamped = false;
        return look;
    }
    the_file_did_not_read(look, save_id, system_error);
    return look;
}

void mp_bridge_savefile_look_report(void)
{
    log_info("  the look at the host's savegame before a restore: %u answered out of the bytes "
             "this side assembled, %u out of the file; %u look(s) at a file that did not read "
             "(none of them may end in a fresh level), the write forgotten %u time(s) to be made "
             "again; %u look(s) at bytes that arrived whole and are no savegame",
             (unsigned)ls.from_memory, (unsigned)ls.from_disk, (unsigned)ls.unread,
             (unsigned)ls.forgets, (unsigned)ls.no_savegame);
}
