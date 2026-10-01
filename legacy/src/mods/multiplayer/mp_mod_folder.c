/* mp_mod_folder.c: which modules of this process came out of the mods folder. See the header.
 */
#include "mp_mod_folder.h"

#include "common/host_image.h"
#include "common/ini.h"
#include "common/text.h"

#include <windows.h>
#include <tlhelp32.h>

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

_Static_assert(MP_MOD_FOLDER_PATH_MAX == MAX_PATH, "the walk's paths are no longer MAX_PATH");

/* The loader's own section and key, read the way the loader reads them. */
#define LOADER_SECTION        "loader"
#define DEFAULT_MOD_DIRECTORY "mods"

/* The system answers ERROR_BAD_LENGTH while another thread loads or unloads a module; the list is
 * asked again that often before the walk reads the folder instead. */
#define SNAPSHOT_TRIES 3u

/* How many steps a plain path holds; a path of MAX_PATH has fewer. */
#define PATH_STEPS_MAX 160u

static bool is_separator(char c)
{
    return c == '\\' || c == '/';
}

/* `path` written plainly into `out`: every separator a '\', none doubled and none at the end, and
 * every "." and ".." step resolved. False when it does not fit. */
static bool plain_path(const char *path, char *out, size_t capacity)
{
    size_t starts[PATH_STEPS_MAX];
    size_t steps = 0u;
    size_t used = 0u;
    size_t at = 0u;

    if (path == NULL || capacity < 2u) {
        return false;
    }
    if (is_separator(path[0])) {
        out[used++] = '\\';
    }
    while (path[at] != '\0') {
        size_t start;
        size_t length;
        size_t separator;

        while (is_separator(path[at])) {
            ++at;
        }
        start = at;
        while (path[at] != '\0' && !is_separator(path[at])) {
            ++at;
        }
        length = at - start;
        if (length == 0u || (length == 1u && path[start] == '.')) {
            continue;
        }
        if (length == 2u && path[start] == '.' && path[start + 1u] == '.') {
            used = steps != 0u ? starts[--steps] : used;
            continue;
        }
        separator = (used != 0u && out[used - 1u] != '\\') ? 1u : 0u;
        if (steps == PATH_STEPS_MAX || used + separator + length + 1u > capacity) {
            return false;
        }
        starts[steps++] = used;
        if (separator != 0u) {
            out[used++] = '\\';
        }
        memcpy(out + used, path + start, length);
        used += length;
    }
    out[used] = '\0';
    return true;
}

mp_mod_place_t mp_mod_folder_place(const char *path, const char *folder)
{
    char        file[MAX_PATH];
    char        plain_folder[MAX_PATH];
    size_t      length;
    const char *last;

    if (!plain_path(path, file, sizeof file) ||
        !plain_path(folder, plain_folder, sizeof plain_folder)) {
        return MP_MOD_PLACE_ELSEWHERE;
    }
    length = strlen(plain_folder);
    if (length == 0u || _strnicmp(file, plain_folder, length) != 0 || file[length] != '\\') {
        return MP_MOD_PLACE_ELSEWHERE;
    }
    last = strrchr(file, '\\');
    return (size_t)(last - file) == length ? MP_MOD_PLACE_DIRECTLY : MP_MOD_PLACE_BELOW;
}

/* The folder the loader loads from, written plainly. */
static void resolve_folder(char *folder, size_t capacity)
{
    char  configured[MAX_PATH];
    char  joined[MAX_PATH];
    char  full[MAX_PATH];
    DWORD length;

    (void)ini_read_string(LOADER_SECTION, "ModDirectory", DEFAULT_MOD_DIRECTORY, configured,
                          sizeof configured);
    (void)text_format(joined, sizeof joined, "%s%s", host_directory(), configured);
    length = GetFullPathNameA(joined, sizeof full, full, NULL);
    if (length == 0u || length >= sizeof full || !plain_path(full, folder, capacity)) {
        (void)text_format(folder, capacity, "%s", joined);
    }
}

static const char *file_name_of(const char *path)
{
    const char *name = path;

    for (; *path != '\0'; ++path) {
        if (is_separator(*path)) {
            name = path + 1;
        }
    }
    return name;
}

/* What runs: every module of the process, with the path it was loaded from. False, with the
 * reason, when the list cannot be read. */
static bool walk_module_list(mp_mod_folder_each_fn each, mp_mod_folder_walk_t *out)
{
    HANDLE        snapshot = INVALID_HANDLE_VALUE;
    MODULEENTRY32 entry;
    unsigned      tries;

    for (tries = 0; tries < SNAPSHOT_TRIES && snapshot == INVALID_HANDLE_VALUE; ++tries) {
        snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, 0u);
        if (snapshot == INVALID_HANDLE_VALUE && GetLastError() != ERROR_BAD_LENGTH) {
            break;
        }
    }
    if (snapshot == INVALID_HANDLE_VALUE) {
        out->list_error = GetLastError();
        return false;
    }
    memset(&entry, 0, sizeof entry);
    entry.dwSize = sizeof entry;
    if (Module32First(snapshot, &entry)) {
        do {
            mp_mod_place_t place = mp_mod_folder_place(entry.szExePath, out->folder);

            if (place == MP_MOD_PLACE_DIRECTLY) {
                each(entry.hModule, file_name_of(entry.szExePath));
            } else if (place == MP_MOD_PLACE_BELOW) {
                ++out->below;
            }
        } while (Module32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return true;
}

/* The module loaded under `name` when it came out of the folder; a DLL of the same name can be
 * loaded from elsewhere, the system's own for one, and that is not the file in the folder. */
static HMODULE loaded_from_folder(const char *name, const char *folder)
{
    HMODULE module = GetModuleHandleA(name);
    char    path[MAX_PATH];

    if (module == NULL || GetModuleFileNameA(module, path, sizeof path) == 0u) {
        return NULL;
    }
    path[sizeof path - 1u] = '\0';
    return mp_mod_folder_place(path, folder) == MP_MOD_PLACE_DIRECTLY ? module : NULL;
}

static bool ends_with_dll(const char *name)
{
    size_t length = strlen(name);

    return length > 4u && _stricmp(name + length - 4u, ".dll") == 0;
}

/* What lies: every DLL in the folder, as the loader reads it. With `each` every one that is loaded
 * is handed over, which is the way without the module list; the rest are counted either way. */
static void walk_folder(mp_mod_folder_each_fn each, mp_mod_folder_walk_t *out)
{
    char             pattern[MAX_PATH];
    WIN32_FIND_DATAA entry;
    HANDLE           search;

    (void)text_format(pattern, sizeof pattern, "%s\\*.dll", out->folder);
    search = FindFirstFileA(pattern, &entry);
    if (search == INVALID_HANDLE_VALUE) {
        return;
    }
    out->folder_found = true;
    do {
        HMODULE module;

        /* The long name decides, as it does for the loader: the pattern also matches short names,
         * and FEATUR~1.DLL is how a renamed feature.dll.disabled comes back from it. */
        if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0u ||
            !ends_with_dll(entry.cFileName)) {
            continue;
        }
        module = loaded_from_folder(entry.cFileName, out->folder);
        if (module == NULL) {
            ++out->not_loaded;
        } else if (each != NULL) {
            each(module, entry.cFileName);
        }
    } while (FindNextFileA(search, &entry));
    FindClose(search);
}

void mp_mod_folder_walk(bool module_list, mp_mod_folder_each_fn each, mp_mod_folder_walk_t *out)
{
    memset(out, 0, sizeof *out);
    resolve_folder(out->folder, sizeof out->folder);
    out->list_asked = module_list;
    out->from_list  = module_list && walk_module_list(each, out);
    walk_folder(out->from_list ? NULL : each, out);
}
