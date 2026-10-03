/* mp_saves.c: the savegame list. See the header. */
#include "mp_saves.h"
#include "mp_text.h"

#include "common/logging.h"
#include "common/text.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* The level table holds eleven rows, so an index past that is not a level (mp_levels). */
#define LEVEL_TABLE_ENTRIES 11u

typedef struct saves_state {
    mp_save_t save[MP_SAVES_MAX];
    size_t    count;
    size_t    refused;   /* files in the folder that were not saves, or not readable */
    bool      scanned;
} saves_state_t;

static saves_state_t saves;

mp_saves_look_t mp_saves_judge_header(const uint8_t *bytes, size_t length, mp_save_t *out)
{
    size_t i;

    if (bytes == NULL || out == NULL || length < MP_SAVES_HEADER_BYTES) {
        return MP_SAVES_LOOK_NOT_A_SAVE;
    }
    if (memcmp(bytes, MP_SAVES_MAGIC, MP_SAVES_MAGIC_BYTES) != 0) {
        return MP_SAVES_LOOK_NOT_A_SAVE;
    }
    memset(out, 0, sizeof *out);

    /* The level index is a dword in the file and a byte here, so one that does not fit names no
     * row. The word in front of it is not asked: the engine never writes it, so what it holds
     * says nothing about the file.
     *
     * A level index that is no row of the table is still a save: a level loaded by its path is
     * saved with minus one there, all four bytes set. It names no level, so nobody could be told
     * which one to load, and that is its own answer, apart from a file that is no save. */
    if (bytes[MP_SAVES_OFF_LEVEL + 1u] != 0u || bytes[MP_SAVES_OFF_LEVEL + 2u] != 0u ||
        bytes[MP_SAVES_OFF_LEVEL + 3u] != 0u) {
        return MP_SAVES_LOOK_NO_LEVEL;
    }
    out->slot        = bytes[MP_SAVES_OFF_SLOT];
    out->level_index = bytes[MP_SAVES_OFF_LEVEL];
    if (out->level_index >= LEVEL_TABLE_ENTRIES) {
        return MP_SAVES_LOOK_NO_LEVEL;
    }

    {
        size_t at = 0;

        for (i = 0; at + 1u < sizeof out->name && MP_SAVES_OFF_NAME + i < length; ++i) {
            char c = (char)bytes[MP_SAVES_OFF_NAME + i];

            if (c == '\0') {
                break;
            }
            /* The name is drawn with the engine's own font and written into a log; a byte with no
             * glyph is dropped rather than passed on. A player's own save carries the level's
             * localised name, which is where the bytes with no glyph come from. */
            if (c >= 0x20 && c <= 0x7E) {
                out->name[at++] = c;
            }
        }
        out->name[at] = '\0';
    }
    if (out->name[0] == '\0') {
        text_format(out->name, sizeof out->name, mp_text(MP_TEXT_SAVE_FALLBACK_NAME),
                    (unsigned)out->slot);
    }
    return MP_SAVES_LOOK_SAVE;
}

/* A file that does not open is not a file that is no save. Another process may hold it for a
 * moment: a second copy of the game in the same folder writes the received savegame under the
 * same name and shares it with nobody while it does. */
static mp_saves_look_t look_at_header(const char *path, mp_save_t *out)
{
    uint8_t header[MP_SAVES_HEADER_BYTES];
    HANDLE  file;
    DWORD   got = 0;
    bool    ok;

    file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return MP_SAVES_LOOK_UNREADABLE;
    }
    ok = ReadFile(file, header, (DWORD)sizeof header, &got, NULL) != 0 &&
         got == (DWORD)sizeof header;
    CloseHandle(file);
    return ok ? mp_saves_judge_header(header, sizeof header, out) : MP_SAVES_LOOK_UNREADABLE;
}

static bool read_header(const char *path, mp_save_t *out)
{
    return look_at_header(path, out) == MP_SAVES_LOOK_SAVE;
}

static void keep_the_file_name(const char *file, mp_save_t *out)
{
    memset(out->file, 0, sizeof out->file);
    memcpy(out->file, file, strlen(file) < sizeof out->file ? strlen(file)
                                                            : sizeof out->file - 1u);
}

size_t mp_saves_scan(void)
{
    WIN32_FIND_DATAA found;
    HANDLE           search;

    saves.count   = 0;
    saves.refused = 0;
    saves.scanned = true;

    search = FindFirstFileA(MP_SAVES_PATTERN, &found);
    if (search == INVALID_HANDLE_VALUE) {
        return 0;
    }
    do {
        mp_save_t entry;
        char      path[MP_SAVES_FILE_MAX + 1u];   /* a byte over, so a cut path fills it */

        if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0u) {
            continue;
        }
        if (_stricmp(found.cFileName, MP_SAVES_JOIN_FILE) == 0) {
            continue;   /* somebody else's game, received for one session; never a choice */
        }
        if (text_format(path, sizeof path, "%s\\%s", MP_SAVES_FOLDER, found.cFileName) >=
            MP_SAVES_FILE_MAX) {
            ++saves.refused;
            continue;
        }
        if (!read_header(path, &entry)) {
            ++saves.refused;
            continue;
        }
        if (saves.count >= MP_SAVES_MAX) {
            break;
        }
        memcpy(entry.file, path, strlen(path) + 1u);
        saves.save[saves.count++] = entry;
    } while (FindNextFileA(search, &found) != 0);
    FindClose(search);
    return saves.count;
}

size_t mp_saves_count(void)
{
    return saves.count;
}

const mp_save_t *mp_saves_at(size_t index)
{
    return index < saves.count ? &saves.save[index] : NULL;
}

mp_saves_look_t mp_saves_look(const char *file, mp_save_t *out)
{
    mp_saves_look_t look;

    if (file == NULL || out == NULL) {
        return MP_SAVES_LOOK_UNREADABLE;
    }
    look = look_at_header(file, out);
    if (look == MP_SAVES_LOOK_SAVE) {
        keep_the_file_name(file, out);
    }
    return look;
}

const mp_save_t *mp_saves_find(const char *file)
{
    size_t i;

    if (file == NULL || file[0] == '\0') {
        return NULL;
    }
    for (i = 0; i < saves.count; ++i) {
        if (_stricmp(saves.save[i].file, file) == 0) {
            return &saves.save[i];
        }
    }
    return NULL;
}

void mp_saves_report(void)
{
    size_t i;

    if (!saves.scanned) {
        return;
    }
    log_info("  the savegames: %u readable, %u refused", (unsigned)saves.count,
             (unsigned)saves.refused);
    for (i = 0; i < saves.count; ++i) {
        log_info("    %-22s  slot %-3u level %-2u  %s", saves.save[i].file,
                 (unsigned)saves.save[i].slot, (unsigned)saves.save[i].level_index,
                 saves.save[i].name);
    }
}
