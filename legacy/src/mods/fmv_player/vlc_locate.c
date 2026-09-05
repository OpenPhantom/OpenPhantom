/* vlc_locate.c: see vlc_locate.h. */
#include "vlc_locate.h"

#include "common/host_image.h"
#include "common/logging.h"

#include <windows.h>

#include <stdio.h>

/* Relative to the game folder, and the same path the installer writes. Nothing else in the
 * process knows it, so it is spelled once. */
#define BUNDLED_RUNTIME_SUBDIRECTORY L"mods\\fmv"

#define VLC_REGISTRY_KEY L"SOFTWARE\\VideoLAN\\VLC"

/* ============================================================================================ */
/* True when `directory` holds a libvlc.dll. The check is the file rather than the directory,
 * because a leftover empty VideoLAN folder is a real thing to find on a machine where VLC was
 * uninstalled, and reporting that as a hit would turn a clear "not installed" into a confusing
 * "found it, could not load it". */
static bool holds_libvlc(const wchar_t *directory)
{
    wchar_t probe[MAX_PATH];

    if (_snwprintf(probe, ARRAYSIZE(probe) - 1, L"%s\\libvlc.dll", directory) < 0) {
        return false;
    }
    probe[ARRAYSIZE(probe) - 1] = L'\0';
    return GetFileAttributesW(probe) != INVALID_FILE_ATTRIBUTES;
}

static bool accept(const wchar_t *directory, wchar_t *out_dir, size_t out_capacity,
                   const char *which)
{
    if (!holds_libvlc(directory)) {
        log_info("no libvlc.dll in %ls (%s)", directory, which);
        return false;
    }
    if (_snwprintf(out_dir, out_capacity - 1, L"%s", directory) < 0) {
        log_warning("the path of the %s libVLC does not fit in %u characters, ignoring it",
                    which, (unsigned)out_capacity);
        return false;
    }
    out_dir[out_capacity - 1] = L'\0';
    log_info("using the %s libVLC in %ls", which, out_dir);
    return true;
}

/* ============================================================================================ */
static bool try_bundled(wchar_t *out_dir, size_t out_capacity)
{
    wchar_t candidate[MAX_PATH];
    wchar_t game_directory[MAX_PATH];

    /* host_directory() answers with an empty string when it could not work out where the
     * executable is. Carrying on would build a RELATIVE path and ask the file system about
     * whatever the current directory happens to be, which is a different question. */
    if (host_directory()[0] == '\0') {
        log_warning("the game directory is not known, so the bundled libVLC cannot be looked for");
        return false;
    }
    if (MultiByteToWideChar(CP_ACP, 0, host_directory(), -1, game_directory,
                            ARRAYSIZE(game_directory)) == 0) {
        log_warning("the game directory could not be converted to wide characters, so the "
                    "bundled libVLC cannot be looked for");
        return false;
    }
    /* host_directory() ends in a backslash, so the subdirectory is appended without one. */
    if (_snwprintf(candidate, ARRAYSIZE(candidate) - 1, L"%s%s", game_directory,
                   BUNDLED_RUNTIME_SUBDIRECTORY) < 0) {
        return false;
    }
    candidate[ARRAYSIZE(candidate) - 1] = L'\0';

    return accept(candidate, out_dir, out_capacity, "bundled");
}

static bool try_program_files(wchar_t *out_dir, size_t out_capacity)
{
    wchar_t program_files[MAX_PATH];
    wchar_t candidate[MAX_PATH];

    if (GetEnvironmentVariableW(L"ProgramFiles(x86)", program_files,
                                ARRAYSIZE(program_files)) == 0) {
        /* Absent on 32-bit Windows, where ProgramFiles is already the 32-bit one. */
        if (GetEnvironmentVariableW(L"ProgramFiles", program_files,
                                    ARRAYSIZE(program_files)) == 0) {
            log_info("neither ProgramFiles(x86) nor ProgramFiles is set");
            return false;
        }
    }
    if (_snwprintf(candidate, ARRAYSIZE(candidate) - 1, L"%s\\VideoLAN\\VLC",
                   program_files) < 0) {
        return false;
    }
    candidate[ARRAYSIZE(candidate) - 1] = L'\0';

    return accept(candidate, out_dir, out_capacity, "default-location");
}

/* ============================================================================================ */
/* Reads one value out of an already-open key as a wide string.
 *
 * Two things RegQueryValueExW does not do for you, and both are quiet when they go wrong.
 * It does not guarantee a terminating null: a value written without one comes back without one,
 * and every wcs* call after that reads past the buffer. And it does not expand REG_EXPAND_SZ,
 * which is a real spelling for an install path, so accepting that type without expanding it
 * produces a directory name with a literal %ProgramFiles% in it that matches nothing. */
static bool read_registry_string(HKEY key, const wchar_t *value_name, wchar_t *out,
                                 size_t out_capacity)
{
    wchar_t raw[MAX_PATH];
    DWORD   size = sizeof raw - sizeof raw[0];   /* leave room for a terminator of our own */
    DWORD   type = 0;
    DWORD   expanded;
    LSTATUS status;

    status = RegQueryValueExW(key, value_name, NULL, &type, (LPBYTE)raw, &size);
    if (status != ERROR_SUCCESS) {
        return false;
    }
    if (type != REG_SZ && type != REG_EXPAND_SZ) {
        log_warning("the VLC registry value is of type %u, which is not a string", (unsigned)type);
        return false;
    }
    raw[size / sizeof raw[0]] = L'\0';

    if (type == REG_SZ) {
        if (_snwprintf(out, out_capacity - 1, L"%s", raw) < 0) {
            return false;
        }
        out[out_capacity - 1] = L'\0';
        return true;
    }

    /* ExpandEnvironmentStringsW answers with the size it WOULD have needed and writes nothing
     * useful when that is more than it was given, so the return value has to be range-checked
     * rather than only tested against zero. */
    expanded = ExpandEnvironmentStringsW(raw, out, (DWORD)out_capacity);
    if (expanded == 0 || expanded > out_capacity) {
        log_warning("the VLC registry value did not expand into %u characters",
                    (unsigned)out_capacity);
        return false;
    }
    return true;
}

/* Cuts a trailing file name off, so "D:\VLC\vlc.exe" becomes "D:\VLC". Used only on the key's
 * unnamed default value, which VLC's installer writes as the path of vlc.exe rather than as the
 * directory: reading it as a directory is what made the earlier version of this fallback look for
 * "D:\VLC\vlc.exe\libvlc.dll" and never find anything. */
static void strip_file_name(wchar_t *path)
{
    wchar_t *cursor;
    wchar_t *last_separator = NULL;

    for (cursor = path; *cursor != L'\0'; ++cursor) {
        if (*cursor == L'\\' || *cursor == L'/') {
            last_separator = cursor;
        }
    }
    if (last_separator != NULL) {
        *last_separator = L'\0';
    }
}

/* The installer's own key. A 32-bit installer writes it through to WOW6432Node on 64-bit Windows,
 * and this 32-bit process reads it through the same redirection, so both sides land in the same
 * place with no KEY_WOW64_* flag needed. That symmetry is also what keeps a 64-bit VLC out of the
 * answer, which is correct here rather than a limitation.
 *
 * InstallDir is read first because it is the value that holds a directory. The unnamed default
 * value is tried afterwards for the older installers that wrote only that one, with its file name
 * cut off. */
static bool try_registry(wchar_t *out_dir, size_t out_capacity)
{
    HKEY    key;
    wchar_t candidate[MAX_PATH];
    bool    found = false;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, VLC_REGISTRY_KEY, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        log_info("no HKLM\\%ls, so no VLC was installed by its own installer for 32-bit "
                 "applications", VLC_REGISTRY_KEY);
        return false;
    }

    if (read_registry_string(key, L"InstallDir", candidate, ARRAYSIZE(candidate))) {
        found = accept(candidate, out_dir, out_capacity, "registered");
    }
    if (!found && read_registry_string(key, NULL, candidate, ARRAYSIZE(candidate))) {
        strip_file_name(candidate);
        found = accept(candidate, out_dir, out_capacity, "registered (from the default value)");
    }
    if (!found) {
        log_info("HKLM\\%ls exists but names no directory holding libvlc.dll",
                 VLC_REGISTRY_KEY);
    }

    RegCloseKey(key);
    return found;
}

/* ============================================================================================ */
bool vlc_locate_directory(wchar_t *out_dir, size_t out_capacity)
{
    if (out_dir == NULL || out_capacity == 0) {
        return false;
    }

    if (try_bundled(out_dir, out_capacity)) {
        return true;
    }
    if (try_program_files(out_dir, out_capacity)) {
        return true;
    }
    if (try_registry(out_dir, out_capacity)) {
        return true;
    }

    log_warning("no 32-bit libVLC found in \"%s%ls\", in Program Files (x86), or through the VLC "
                "registry key. Every movie keeps using the retail Bink path. A 64-bit VLC does "
                "not count: this is a 32-bit process and cannot load a 64-bit DLL at all.",
                host_directory(), BUNDLED_RUNTIME_SUBDIRECTORY);
    return false;
}
