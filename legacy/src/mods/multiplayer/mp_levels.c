/* mp_levels.c: the level list, out of the game's own table and its level folder. See the header. */
#include "mp_levels.h"

#include "mp_cells.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/text.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* The table the campaign loop indexes: eleven rows, twelve bytes each, and the imul that proves
 * the stride is part of the site the base is read from. Row: {b3d path, display name, movie}.
 * The table sits at 004AE388 in the retail image and is read through its cell, which comes out
 * of the campaign loop's own load branch, 0x33 into that site, with the `imul edx,0xC` three
 * bytes in front of it; the number and its justification cannot drift apart. Three of the eleven
 * rows carry no movie pointer. */
#define TABLE_ROWS   11u
#define TABLE_STRIDE 12u

/* The third pointer of a row: the movie the level opens with, or an empty string for none. */
#define TABLE_MOVIE_OFFSET 8u

/* Where the loose levels live, under the data root. */
#define LEVEL_FOLDER "level"

typedef struct levels_state {
    mp_level_t level[MP_LEVELS_MAX];
    size_t     count;
    size_t     shipped;    /* how many came out of the table */
    size_t     custom;     /* how many came out of the folder */
    bool       scanned;
    bool       table_missing_logged;
} levels_state_t;

static levels_state_t levels;

/* ==============================================================================================
 * Pure.
 * ============================================================================================ */

static char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static bool same_path(const char *a, const char *b)
{
    size_t i;

    for (i = 0; i < MP_LOBBY_LEVEL_MAX; ++i) {
        char ca = lower(a[i]);
        char cb = lower(b[i]);

        if (ca == '/') {
            ca = '\\';   /* the engine takes either; two spellings are still one file */
        }
        if (cb == '/') {
            cb = '\\';
        }
        if (ca != cb) {
            return false;
        }
        if (ca == '\0') {
            return true;
        }
    }
    return true;
}

bool mp_levels_title_from_file(const char *file_name, char *out, size_t out_size)
{
    size_t length;
    size_t at = 0;
    size_t i;

    if (file_name == NULL || out == NULL || out_size == 0u) {
        return false;
    }
    out[0] = '\0';
    length = strlen(file_name);
    if (length < 5u) {
        return false;
    }
    /* It has to be a .b3d, and the extension is compared without regard to case, the way the
     * engine's own resource lookup compares a name. */
    if (lower(file_name[length - 4u]) != '.' || lower(file_name[length - 3u]) != 'b' ||
        lower(file_name[length - 2u]) != '3' || lower(file_name[length - 1u]) != 'd') {
        return false;
    }
    length -= 4u;
    for (i = 0; i < length && at + 1u < out_size; ++i) {
        char c = file_name[i];

        if (c < 0x20 || c > 0x7E) {
            continue;
        }
        if (at == 0u) {
            /* One capital. */
            out[at++] = (char)((c >= 'a' && c <= 'z') ? c - 'a' + 'A' : c);
        } else {
            out[at++] = lower(c);
        }
    }
    out[at] = '\0';
    return at != 0u;
}

bool mp_levels_join(const char *root, const char *relative, char *out, size_t out_size)
{
    size_t root_length;
    size_t total;

    if (root == NULL || relative == NULL || out == NULL || out_size == 0u) {
        return false;
    }
    out[0] = '\0';
    root_length = strlen(root);
    while (root_length > 0u && (root[root_length - 1u] == '\\' || root[root_length - 1u] == '/')) {
        --root_length;   /* exactly one separator, whatever the root ends with */
    }
    while (*relative == '\\' || *relative == '/') {
        ++relative;
    }
    if (relative[0] == '\0') {
        return false;
    }
    total = root_length + 1u + strlen(relative);
    if (total + 1u > out_size) {
        return false;
    }
    memcpy(out, root, root_length);
    out[root_length] = '\\';
    memcpy(out + root_length + 1u, relative, strlen(relative) + 1u);
    return true;
}

/* ==============================================================================================
 * The engine's side.
 * ============================================================================================ */

/* A NUL terminated string out of the engine's data, copied into our own field. Refuses anything
 * that is not printable ASCII, because it goes on a wire and onto a screen. */
static bool read_engine_string(uintptr_t address, char *out, size_t out_size)
{
    size_t i;

    if (address == 0u || out == NULL || out_size == 0u) {
        return false;
    }
    for (i = 0; i + 1u < out_size; ++i) {
        uint8_t byte = 0;

        if (!memory_try_read(address + i, &byte, sizeof byte)) {
            return false;
        }
        if (byte == 0u) {
            out[i] = '\0';
            return i != 0u;
        }
        if (byte < 0x20u || byte > 0x7Eu) {
            return false;
        }
        out[i] = (char)byte;
    }
    return false;   /* no terminator inside the field */
}

/* The data root is the engine's own cell at 00881480, which the game fills from the registry
 * value `CD Path` with `\gamedata\` appended, and which the command line can override. It is
 * per installation, which is why the wire carries a relative path and each machine prefixes its
 * own. */
static const char *data_root(void)
{
    static char root[144];
    uintptr_t   cell = mp_cells_address(MP_CELL_DATA_ROOT);

    if (cell == 0u || !read_engine_string(cell, root, sizeof root)) {
        return NULL;
    }
    return root;
}

static mp_level_t *take_slot(void)
{
    if (levels.count >= MP_LEVELS_MAX) {
        return NULL;
    }
    return &levels.level[levels.count++];
}

static void add_shipped(void)
{
    uintptr_t table = mp_cells_address(MP_CELL_LEVEL_TABLE);
    size_t    row;

    if (table == 0u) {
        if (!levels.table_missing_logged) {
            levels.table_missing_logged = true;
            log_warning("the level table did not resolve: only the levels lying in the folder can "
                        "be offered, and none of them by the game's own name");
        }
        return;
    }
    for (row = 0; row < TABLE_ROWS; ++row) {
        uintptr_t   entry = table + row * TABLE_STRIDE;
        uint32_t    path_pointer = 0;
        uint32_t    title_pointer = 0;
        mp_level_t *level;

        if (!memory_read_u32(entry, &path_pointer) ||
            !memory_read_u32(entry + 4u, &title_pointer)) {
            continue;
        }
        level = take_slot();
        if (level == NULL) {
            return;
        }
        memset(level, 0, sizeof *level);
        if (!read_engine_string(path_pointer, level->path, sizeof level->path)) {
            --levels.count;   /* a row that reads as nothing is a row this build cannot offer */
            continue;
        }
        if (!read_engine_string(title_pointer, level->title, sizeof level->title)) {
            (void)mp_levels_title_from_file(level->path, level->title, sizeof level->title);
        }
        level->index = (uint8_t)row;
        ++levels.shipped;
    }
}

static void add_folder(void)
{
    char             pattern[MP_LEVELS_ABSOLUTE_MAX];
    char             relative[MP_LOBBY_LEVEL_MAX + 1u];   /* a byte over, so a cut path fills it */
    WIN32_FIND_DATAA found;
    HANDLE           search;
    const char      *root = data_root();

    if (root == NULL) {
        return;
    }
    if (!mp_levels_join(root, LEVEL_FOLDER "\\*.b3d", pattern, sizeof pattern)) {
        return;
    }
    search = FindFirstFileA(pattern, &found);
    if (search == INVALID_HANDLE_VALUE) {
        return;
    }
    do {
        mp_level_t *level;

        if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0u) {
            continue;
        }
        if (text_format(relative, sizeof relative, "%s\\%s", LEVEL_FOLDER, found.cFileName) >=
            MP_LOBBY_LEVEL_MAX) {
            continue;
        }
        if (mp_levels_find(relative) != NULL) {
            continue;   /* the table already named it, and the table's name for it is better */
        }
        level = take_slot();
        if (level == NULL) {
            break;
        }
        memset(level, 0, sizeof *level);
        memcpy(level->path, relative, strlen(relative) + 1u);
        if (!mp_levels_title_from_file(found.cFileName, level->title, sizeof level->title)) {
            --levels.count;
            continue;
        }
        level->index = (uint8_t)MP_LOBBY_LEVEL_CUSTOM;
        ++levels.custom;
    } while (FindNextFileA(search, &found) != 0);
    FindClose(search);
}

size_t mp_levels_scan(void)
{
    levels.count   = 0;
    levels.shipped = 0;
    levels.custom  = 0;
    add_shipped();
    add_folder();
    levels.scanned = true;
    return levels.count;
}

size_t mp_levels_count(void)
{
    return levels.count;
}

const mp_level_t *mp_levels_at(size_t index)
{
    return index < levels.count ? &levels.level[index] : NULL;
}

const mp_level_t *mp_levels_find(const char *path)
{
    size_t i;

    if (path == NULL || path[0] == '\0') {
        return NULL;
    }
    for (i = 0; i < levels.count; ++i) {
        if (same_path(levels.level[i].path, path)) {
            return &levels.level[i];
        }
    }
    return NULL;
}

const mp_level_t *mp_levels_shipped(uint8_t index)
{
    size_t i;

    for (i = 0; i < levels.count; ++i) {
        if (levels.level[i].index == index && index != (uint8_t)MP_LOBBY_LEVEL_CUSTOM) {
            return &levels.level[i];
        }
    }
    return NULL;
}

bool mp_levels_movie_of(uint32_t index, char *out, size_t out_size)
{
    uintptr_t table   = mp_cells_address(MP_CELL_LEVEL_TABLE);
    uint32_t  pointer = 0;

    if (out == NULL || out_size == 0u) {
        return false;
    }
    out[0] = '\0';
    if (table == 0u || index >= TABLE_ROWS ||
        !memory_read_u32(table + index * TABLE_STRIDE + TABLE_MOVIE_OFFSET, &pointer)) {
        return false;
    }
    return read_engine_string(pointer, out, out_size);   /* false for the empty "none" */
}

bool mp_levels_absolute(const char *relative, char *out, size_t out_size)
{
    const char *root = data_root();

    return root != NULL && mp_levels_join(root, relative, out, out_size);
}

/* The guard that has to exist. The campaign loop's by-name branch sets the level index cell at
 * 0088136C to -1 and calls the loader at 0043F70A with the path at 00881374, and tests nothing
 * the loader answers; a path that names nothing leaves the world pointer null and the next
 * frame walks into it. */
bool mp_levels_present(const char *relative)
{
    char  absolute[MP_LEVELS_ABSOLUTE_MAX];
    DWORD attributes;

    if (!mp_levels_absolute(relative, absolute, sizeof absolute)) {
        return false;
    }
    attributes = GetFileAttributesA(absolute);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0u;
}

void mp_levels_report(void)
{
    size_t i;

    if (!levels.scanned) {
        return;
    }
    log_info("  the levels: %u in the list, %u from the game's table, %u lying in the folder",
             (unsigned)levels.count, (unsigned)levels.shipped, (unsigned)levels.custom);
    for (i = 0; i < levels.count; ++i) {
        log_info("    %-24s  %-22s  %s", levels.level[i].path, levels.level[i].title,
                 levels.level[i].index == (uint8_t)MP_LOBBY_LEVEL_CUSTOM ? "own" : "shipped");
    }
}
