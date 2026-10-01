/* unittests/mp_mod_census_fixture.c: a mods folder of a census test's own. See the header. */
#include "mp_mod_census_fixture.h"

#include "common/host_image.h"
#include "common/ini.h"
#include "common/text.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* The probe DLL's path comes from the build (MP_MOD_CENSUS_PROBE). */
#ifndef MP_MOD_CENSUS_PROBE
#error "the build names the probe DLL in MP_MOD_CENSUS_PROBE"
#endif

#define LOADED_MAX 8u

static struct {
    char    folder[MAX_PATH];   /* beside the program, with no separator at its end */
    bool    ini_existed;
    HMODULE loaded[LOADED_MAX];
    size_t  loaded_count;
} fixture;

/* Deletes every file below `path`, then the folders, then `path` itself. */
static void remove_tree(const char *path)
{
    char             pattern[MAX_PATH];
    WIN32_FIND_DATAA entry;
    HANDLE           search;

    (void)text_format(pattern, sizeof pattern, "%s\\*", path);
    search = FindFirstFileA(pattern, &entry);
    if (search != INVALID_HANDLE_VALUE) {
        do {
            char child[MAX_PATH];

            if (strcmp(entry.cFileName, ".") == 0 || strcmp(entry.cFileName, "..") == 0) {
                continue;
            }
            (void)text_format(child, sizeof child, "%s\\%s", path, entry.cFileName);
            if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0u) {
                remove_tree(child);
            } else {
                (void)DeleteFileA(child);
            }
        } while (FindNextFileA(search, &entry));
        FindClose(search);
    }
    (void)RemoveDirectoryA(path);
}

static void clear_keys(void)
{
    (void)WritePrivateProfileStringA("loader", "ModDirectory", NULL, ini_path());
    (void)WritePrivateProfileStringA("multiplayer", "AllowMods", NULL, ini_path());
}

bool fixture_begin(const char *folder)
{
    char configured[MAX_PATH];
    char sub[MAX_PATH];

    memset(&fixture, 0, sizeof fixture);
    fixture.ini_existed = GetFileAttributesA(ini_path()) != INVALID_FILE_ATTRIBUTES;
    (void)text_format(fixture.folder, sizeof fixture.folder, "%s%s", host_directory(), folder);
    remove_tree(fixture.folder);
    clear_keys();
    (void)text_format(configured, sizeof configured, "%s\\", folder);
    (void)text_format(sub, sizeof sub, "%s\\sub", fixture.folder);
    return ini_write_string("loader", "ModDirectory", configured) &&
           ini_write_string("multiplayer", "AllowMods", "") &&
           CreateDirectoryA(fixture.folder, NULL) && CreateDirectoryA(sub, NULL);
}

bool fixture_copy(const char *relative)
{
    char to[MAX_PATH];

    (void)text_format(to, sizeof to, "%s\\%s", fixture.folder, relative);
    return CopyFileA(MP_MOD_CENSUS_PROBE, to, FALSE) != 0;
}

bool fixture_load(const char *relative)
{
    char    path[MAX_PATH];
    HMODULE module;

    if (fixture.loaded_count == LOADED_MAX) {
        return false;
    }
    /* The loader joins the host's directory and ModDirectory, which ends in a separator here, to
     * the name with one more, so the path it loads from has two in a row. */
    (void)text_format(path, sizeof path, "%s\\\\%s", fixture.folder, relative);
    module = LoadLibraryA(path);
    if (module == NULL) {
        return false;
    }
    fixture.loaded[fixture.loaded_count++] = module;
    return true;
}

bool fixture_rename(const char *from, const char *to)
{
    char old_path[MAX_PATH];
    char new_path[MAX_PATH];

    (void)text_format(old_path, sizeof old_path, "%s\\%s", fixture.folder, from);
    (void)text_format(new_path, sizeof new_path, "%s\\%s", fixture.folder, to);
    return MoveFileA(old_path, new_path) != 0;
}

bool fixture_allow(const char *list)
{
    return ini_write_string("multiplayer", "AllowMods", list);
}

void fixture_end(void)
{
    size_t i;

    for (i = 0; i < fixture.loaded_count; ++i) {
        (void)FreeLibrary(fixture.loaded[i]);
    }
    fixture.loaded_count = 0u;
    remove_tree(fixture.folder);
    clear_keys();
    if (!fixture.ini_existed) {
        (void)DeleteFileA(ini_path());
    }
}
