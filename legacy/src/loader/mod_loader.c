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

/* WHICH BUILD IS THIS, answered on the line that announces the module rather than by asking
 * somebody to look at file properties.
 *
 * The cost of not having this was three rounds of a field investigation. A tester reported a
 * crash, a fix was built and sent, the crash was reported again, and the only way to tell
 * whether the fix was even in the process was that one DLL happened to have changed a log
 * message. The one whose constant had changed logged nothing different at all, so its build was
 * unknowable from a log; a fix was briefly credited to the wrong DLL because of it.
 *
 * THE PE TIMESTAMP AND NOT THE FILE DATE. IMAGE_FILE_HEADER.TimeDateStamp is the link time,
 * written into the bytes of the file, so it survives copying, zipping, emailing and anything
 * else that happens between a build and a tester. A file date is metadata and any of those can
 * reset it. It is read out of the already mapped headers, so this costs no file I/O.
 *
 * Both forms are printed. The local time is for a human comparing against a build they have,
 * and the raw hex is the identity itself: unambiguous across time zones and the thing to quote
 * when asking whether two people are running the same binary.
 *
 * A build with /Brepro writes a content hash here instead of a time, which reads as a date far
 * outside any plausible range. That is why the range is tested rather than trusted, and why the
 * hex is printed either way: a hash is still a perfectly good identity, it is just not a date. */
static void describe_build(HMODULE module, char *out, size_t size)
{
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)module;
    const IMAGE_NT_HEADERS *nt;
    ULONGLONG               hundred_ns;
    FILETIME                utc, local;
    SYSTEMTIME              when;
    DWORD                   stamp;

    out[0] = '\0';

    if (dos == NULL || dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return;
    }
    nt = (const IMAGE_NT_HEADERS *)((const char *)module + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        return;
    }
    stamp = nt->FileHeader.TimeDateStamp;

    /* 1970 in hundred nanosecond units since 1601, which is what a FILETIME counts. */
    hundred_ns = 116444736000000000ULL + (ULONGLONG)stamp * 10000000ULL;
    utc.dwLowDateTime  = (DWORD)hundred_ns;
    utc.dwHighDateTime = (DWORD)(hundred_ns >> 32);

    if (stamp > 0x40000000u && stamp < 0x80000000u &&
        FileTimeToLocalFileTime(&utc, &local) && FileTimeToSystemTime(&local, &when)) {
        _snprintf(out, size, ", built %04u-%02u-%02u %02u:%02u (%08X)",
                  when.wYear, when.wMonth, when.wDay, when.wHour, when.wMinute,
                  (unsigned)stamp);
    } else {
        _snprintf(out, size, ", build id %08X", (unsigned)stamp);
    }
    out[size - 1] = '\0';
}

static void load_one(const char *directory, const char *name)
{
    char                    path[MAX_PATH];
    char                    build[64];
    HMODULE                 module;
    engine_fix_install_fn_t install;

    _snprintf(path, sizeof(path), "%s\\%s", directory, name);
    path[sizeof(path) - 1] = '\0';

    module = LoadLibraryA(path);
    if (module == NULL) {
        log_error("mod %-24s LoadLibrary failed (%lu)", name, (unsigned long)GetLastError());
        return;
    }

    describe_build(module, build, sizeof(build));

    install = (engine_fix_install_fn_t)GetProcAddress(module, ENGINE_FIX_ENTRY_NAME);
    if (install == NULL) {
        log_info("mod %-24s loaded at %08X%s, no %s export, left to its own DllMain",
                 name, (unsigned)(uintptr_t)module, build, ENGINE_FIX_ENTRY_NAME);
        return;
    }

    log_info("mod %-24s loaded at %08X%s, calling %s",
             name, (unsigned)(uintptr_t)module, build, ENGINE_FIX_ENTRY_NAME);
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
                    "built-in defaults. Put engine_fixes.ini next to WMAIN.EXE to change "
                    "anything.");
    }

    if (!ini_read_bool(LOADER_SECTION, "Enabled", true)) {
        log_warning("Enabled=0 in [%s]; no mod is loaded, the game runs exactly as before",
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
