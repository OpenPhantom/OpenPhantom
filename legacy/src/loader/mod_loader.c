#include "mod_loader.h"

#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/mod_entry.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define LOADER_SECTION        "loader"
#define DEFAULT_MOD_DIRECTORY "mods"
#define MAX_MODS              64

typedef void (__cdecl *engine_fix_install_fn_t)(void);

static bool loader_has_run;

typedef struct mod_list {
    char   names[MAX_MODS][MAX_PATH];
    size_t count;
    size_t skipped;
} mod_list_t;

/* Case-insensitive, so the order does not depend on how the files happen to be capitalised. */
static void insert_sorted(mod_list_t *list, const char *name)
{
    size_t position;
    size_t index;

    if (list->count >= MAX_MODS) {
        ++list->skipped;
        return;
    }

    for (position = 0; position < list->count; ++position) {
        if (_stricmp(name, list->names[position]) < 0) {
            break;
        }
    }
    for (index = list->count; index > position; --index) {
        memcpy(list->names[index], list->names[index - 1], MAX_PATH);
    }

    strncpy(list->names[position], name, MAX_PATH - 1);
    list->names[position][MAX_PATH - 1] = '\0';
    ++list->count;
}

static bool collect_mods(const char *directory, mod_list_t *list)
{
    WIN32_FIND_DATAA entry;
    HANDLE           search;
    char             pattern[MAX_PATH];

    _snprintf(pattern, sizeof(pattern), "%s\\*.dll", directory);
    pattern[sizeof(pattern) - 1] = '\0';

    search = FindFirstFileA(pattern, &entry);
    if (search == INVALID_HANDLE_VALUE) {
        return false;
    }

    do {
        if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            continue;
        }
        insert_sorted(list, entry.cFileName);
    } while (FindNextFileA(search, &entry));

    FindClose(search);
    return true;
}

static void load_one(const char *directory, const char *name)
{
    char                    path[MAX_PATH];
    HMODULE                 module;
    engine_fix_install_fn_t install;

    _snprintf(path, sizeof(path), "%s\\%s", directory, name);
    path[sizeof(path) - 1] = '\0';

    module = LoadLibraryA(path);
    if (module == NULL) {
        log_error("mod %-24s LoadLibrary failed (%lu)", name, (unsigned long)GetLastError());
        return;
    }

    install = (engine_fix_install_fn_t)GetProcAddress(module, ENGINE_FIX_ENTRY_NAME);
    if (install == NULL) {
        log_info("mod %-24s loaded at %08X, no %s export, left to its own DllMain",
                 name, (unsigned)(uintptr_t)module, ENGINE_FIX_ENTRY_NAME);
        return;
    }

    log_info("mod %-24s loaded at %08X, calling %s",
             name, (unsigned)(uintptr_t)module, ENGINE_FIX_ENTRY_NAME);
    install();
}

void mod_loader_run_once(void)
{
    char       configured[MAX_PATH];
    char       directory[MAX_PATH];
    mod_list_t list;
    size_t     index;

    if (loader_has_run) {
        return;
    }
    loader_has_run = true;

    host_image_resolve();
    log_init("loader", true);

    {
        char host_path[MAX_PATH];
        host_path[0] = '\0';
        GetModuleFileNameA(NULL, host_path, MAX_PATH);
        host_path[MAX_PATH - 1] = '\0';
        log_info("host %s", host_path);
        log_info("ini  %s", ini_path());
    }

    /* Every feature falls back to its built-in defaults when the file is absent. That is a
     * legitimate way to run, but it must not look like the settings were read. */
    if (GetFileAttributesA(ini_path()) == INVALID_FILE_ATTRIBUTES) {
        log_warning("there is no configuration file at that path. Every feature is running on its "
                    "built-in defaults. Copy dist/engine_fixes.ini next to WMAIN.EXE to change "
                    "anything.");
    }

    if (!ini_read_bool(LOADER_SECTION, "Enabled", true)) {
        log_warning("Enabled=0 in [%s] - no mod is loaded, the game runs exactly as before",
                    LOADER_SECTION);
        return;
    }

    ini_read_string(LOADER_SECTION, "ModDirectory", DEFAULT_MOD_DIRECTORY,
                    configured, sizeof(configured));
    _snprintf(directory, sizeof(directory), "%s%s", host_directory(), configured);
    directory[sizeof(directory) - 1] = '\0';

    memset(&list, 0, sizeof(list));
    if (!collect_mods(directory, &list)) {
        log_warning("no mod directory at %s, nothing to load. Create it and put the feature "
                    "DLLs in it.", directory);
        return;
    }
    if (list.skipped != 0) {
        log_warning("%s holds more than %u DLLs; %u were not loaded",
                    directory, (unsigned)MAX_MODS, (unsigned)list.skipped);
    }

    log_info("loading %u mod(s) from %s", (unsigned)list.count, directory);
    for (index = 0; index < list.count; ++index) {
        load_one(directory, list.names[index]);
    }
    log_info("--- all mods loaded ---");
}
