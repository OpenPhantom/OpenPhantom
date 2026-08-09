/* vlc_playback.c: finds a 32-bit libVLC install, loads it dynamically, and drives it through the
 * small, long-stable subset of its C API this file actually needs.
 *
 * WHY DYNAMIC LOADING INSTEAD OF LINKING AN SDK
 * There is no libVLC SDK anywhere in this project's build, on purpose: LoadLibraryW/GetProcAddress
 * against a libvlc.dll found on the machine at RUNTIME means nobody building this project needs
 * libVLC headers or an import library installed, and the DLL degrades to the existing Bink fallback
 * exactly like every other "thing wasn't there" case already in this codebase (no converted file,
 * no fmv_player_host.exe, host process exits non-zero) if no compatible libVLC turns up. The
 * functions declared below by hand (typedefs + GetProcAddress, not a vendored header) are the
 * oldest, most stable core of libvlc's public C API - libvlc_new/_release, basic media and media
 * player lifetime, libvlc_media_player_set_hwnd, play, stop, is_playing - all present, with this
 * exact __cdecl C signature, since libVLC's earliest 2.x releases and never removed since; nothing
 * here reaches for anything from libVLC's event or state-enum APIs, whose exact numeric values this
 * file has no local SDK header to verify against, matching this whole project's own rule of never
 * trusting a byte or a constant it has not checked directly.
 *
 * WHY A 32-BIT INSTALL SPECIFICALLY
 * This whole project is 32-bit only (the game itself is a 32-bit process), and a 32-bit process
 * cannot load a 64-bit DLL under any circumstances - not a version mismatch, a hard architecture
 * wall. Most current VLC downloads default to 64-bit; this file only ever looks in the places a
 * SEPARATE 32-bit VLC install would put itself (Program Files (x86), or its own registry key, which
 * a 32-bit installer writes through to WOW6432Node on 64-bit Windows the same way this 32-bit
 * process reads from it - no special redirection handling needed, both sides get redirected
 * symmetrically), and never touches a 64-bit install even if that is the only one present.
 */
#include "vlc_playback.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* ============================================================================================
 * The hand-declared slice of libvlc's C API. Opaque handle types (never dereferenced here, only
 * passed back to libvlc's own functions) plus __cdecl function-pointer typedefs matching libVLC's
 * own header declarations, which use plain C linkage with no stdcall/WINAPI decoration anywhere.
 * ============================================================================================ */
typedef struct libvlc_instance_t libvlc_instance_t;
typedef struct libvlc_media_t libvlc_media_t;
typedef struct libvlc_media_player_t libvlc_media_player_t;

typedef libvlc_instance_t *(__cdecl *libvlc_new_fn)(int argc, const char *const *argv);
typedef void (__cdecl *libvlc_release_fn)(libvlc_instance_t *instance);
typedef libvlc_media_t *(__cdecl *libvlc_media_new_path_fn)(libvlc_instance_t *instance,
                                                            const char *path);
typedef void (__cdecl *libvlc_media_release_fn)(libvlc_media_t *media);
typedef libvlc_media_player_t *(__cdecl *libvlc_media_player_new_from_media_fn)(libvlc_media_t *media);
typedef void (__cdecl *libvlc_media_player_release_fn)(libvlc_media_player_t *player);
typedef void (__cdecl *libvlc_media_player_set_hwnd_fn)(libvlc_media_player_t *player,
                                                         void *drawable);
typedef int (__cdecl *libvlc_media_player_play_fn)(libvlc_media_player_t *player);
typedef void (__cdecl *libvlc_media_player_stop_fn)(libvlc_media_player_t *player);
typedef int (__cdecl *libvlc_media_player_is_playing_fn)(libvlc_media_player_t *player);

typedef struct vlc_api {
    HMODULE                             core_module;
    HMODULE                             module;
    libvlc_new_fn                       new_instance;
    libvlc_release_fn                   release;
    libvlc_media_new_path_fn            media_new_path;
    libvlc_media_release_fn             media_release;
    libvlc_media_player_new_from_media_fn player_new_from_media;
    libvlc_media_player_release_fn      player_release;
    libvlc_media_player_set_hwnd_fn     player_set_hwnd;
    libvlc_media_player_play_fn         player_play;
    libvlc_media_player_stop_fn         player_stop;
    libvlc_media_player_is_playing_fn   player_is_playing;
    libvlc_instance_t                   *instance;
} vlc_api_t;

static vlc_api_t vlc_api;

/* ============================================================================================ */
static bool probe_libvlc_at(const wchar_t *directory, wchar_t *out_dir, size_t out_capacity)
{
    wchar_t probe[MAX_PATH];

    if (_snwprintf(probe, ARRAYSIZE(probe), L"%s\\libvlc.dll", directory) <= 0) {
        return false;
    }
    if (GetFileAttributesW(probe) == INVALID_FILE_ATTRIBUTES) {
        return false;
    }
    return _snwprintf(out_dir, out_capacity, L"%s", directory) > 0;
}

/* Program Files (x86) first: where a 32-bit VLC installer puts itself by default on 64-bit
 * Windows, and checkable with no registry knowledge at all. The installer's own registry key is
 * the fallback, for anyone who installed to a non-default location; its default (unnamed) value
 * holds the install directory, matching VLC's NSIS installer script. */
static bool find_vlc_directory(wchar_t *out_dir, size_t out_capacity)
{
    wchar_t program_files_x86[MAX_PATH];
    wchar_t candidate[MAX_PATH];

    if (GetEnvironmentVariableW(L"ProgramFiles(x86)", program_files_x86,
                                ARRAYSIZE(program_files_x86)) != 0 &&
        _snwprintf(candidate, ARRAYSIZE(candidate), L"%s\\VideoLAN\\VLC",
                  program_files_x86) > 0 &&
        probe_libvlc_at(candidate, out_dir, out_capacity)) {
        return true;
    }

    {
        HKEY  key;
        bool  found = false;

        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\VideoLAN\\VLC", 0, KEY_READ,
                          &key) == ERROR_SUCCESS) {
            wchar_t value[MAX_PATH];
            DWORD   size = sizeof value;
            DWORD   type = 0;

            if (RegQueryValueExW(key, NULL, NULL, &type, (LPBYTE)value, &size) == ERROR_SUCCESS &&
                (type == REG_SZ || type == REG_EXPAND_SZ)) {
                found = probe_libvlc_at(value, out_dir, out_capacity);
            }
            RegCloseKey(key);
        }
        return found;
    }
}

static bool resolve_exports(void)
{
    vlc_api.new_instance = (libvlc_new_fn)GetProcAddress(vlc_api.module, "libvlc_new");
    vlc_api.release = (libvlc_release_fn)GetProcAddress(vlc_api.module, "libvlc_release");
    vlc_api.media_new_path =
        (libvlc_media_new_path_fn)GetProcAddress(vlc_api.module, "libvlc_media_new_path");
    vlc_api.media_release =
        (libvlc_media_release_fn)GetProcAddress(vlc_api.module, "libvlc_media_release");
    vlc_api.player_new_from_media = (libvlc_media_player_new_from_media_fn)GetProcAddress(
        vlc_api.module, "libvlc_media_player_new_from_media");
    vlc_api.player_release = (libvlc_media_player_release_fn)GetProcAddress(
        vlc_api.module, "libvlc_media_player_release");
    vlc_api.player_set_hwnd = (libvlc_media_player_set_hwnd_fn)GetProcAddress(
        vlc_api.module, "libvlc_media_player_set_hwnd");
    vlc_api.player_play =
        (libvlc_media_player_play_fn)GetProcAddress(vlc_api.module, "libvlc_media_player_play");
    vlc_api.player_stop =
        (libvlc_media_player_stop_fn)GetProcAddress(vlc_api.module, "libvlc_media_player_stop");
    vlc_api.player_is_playing = (libvlc_media_player_is_playing_fn)GetProcAddress(
        vlc_api.module, "libvlc_media_player_is_playing");

    return vlc_api.new_instance != NULL && vlc_api.release != NULL &&
          vlc_api.media_new_path != NULL && vlc_api.media_release != NULL &&
          vlc_api.player_new_from_media != NULL && vlc_api.player_release != NULL &&
          vlc_api.player_set_hwnd != NULL && vlc_api.player_play != NULL &&
          vlc_api.player_stop != NULL && vlc_api.player_is_playing != NULL;
}

/* ============================================================================================ */
bool vlc_playback_init(void)
{
    wchar_t vlc_dir[MAX_PATH];
    wchar_t core_path[MAX_PATH];
    wchar_t main_path[MAX_PATH];
    wchar_t plugin_path[MAX_PATH];
    static const char *const instance_args[] = { "--quiet", "--no-video-title-show" };

    if (!find_vlc_directory(vlc_dir, ARRAYSIZE(vlc_dir))) {
        return false;
    }
    if (_snwprintf(core_path, ARRAYSIZE(core_path), L"%s\\libvlccore.dll", vlc_dir) <= 0 ||
        _snwprintf(main_path, ARRAYSIZE(main_path), L"%s\\libvlc.dll", vlc_dir) <= 0 ||
        _snwprintf(plugin_path, ARRAYSIZE(plugin_path), L"%s\\plugins", vlc_dir) <= 0) {
        return false;
    }

    /* libvlc.dll depends on libvlccore.dll; loading it explicitly first, from the SAME directory
     * that was just confirmed to hold a 32-bit libvlc.dll, means the loader never has to guess
     * whether an unrelated libvlccore.dll elsewhere on the search path is compatible. */
    vlc_api.core_module = LoadLibraryW(core_path);
    if (vlc_api.core_module == NULL) {
        return false;
    }
    vlc_api.module = LoadLibraryW(main_path);
    if (vlc_api.module == NULL) {
        return false;
    }

    /* Points libVLC's own plugin loader at this specific install's plugins folder explicitly,
     * rather than relying on its relative-to-DLL auto-detection to land on the right one. */
    SetEnvironmentVariableW(L"VLC_PLUGIN_PATH", plugin_path);

    if (!resolve_exports()) {
        return false;
    }

    vlc_api.instance = vlc_api.new_instance((int)ARRAYSIZE(instance_args), instance_args);
    return vlc_api.instance != NULL;
}

bool vlc_playback_play_blocking(HWND window, const wchar_t *file_path, HWND exclude_from_dispatch)
{
    char                   utf8_path[MAX_PATH * 3];   /* worst-case UTF-8 expansion of MAX_PATH */
    libvlc_media_t         *media;
    libvlc_media_player_t  *player;
    bool                    started = false;
    DWORD                   start_deadline;

    if (vlc_api.instance == NULL || window == NULL || file_path == NULL) {
        return false;
    }
    /* libvlc_media_new_path documents its path argument as UTF-8, not the local ANSI code page. */
    if (WideCharToMultiByte(CP_UTF8, 0, file_path, -1, utf8_path, sizeof utf8_path, NULL,
                            NULL) == 0) {
        return false;
    }

    media = vlc_api.media_new_path(vlc_api.instance, utf8_path);
    if (media == NULL) {
        return false;
    }
    player = vlc_api.player_new_from_media(media);
    vlc_api.media_release(media);   /* the player takes its own reference once created */
    if (player == NULL) {
        return false;
    }

    vlc_api.player_set_hwnd(player, (void *)window);
    if (vlc_api.player_play(player) != 0) {
        vlc_api.player_release(player);
        return false;
    }

    /* Ten seconds to actually begin decoding before giving up - protects against hanging forever
     * if libVLC silently never starts (a bad codec, a file it can open but not decode), the same
     * class of unbounded-wait bug this project's own history already cost a full desktop hang to
     * learn the hard way. GetTickCount() wraps roughly every 49.7 days; the signed-subtraction
     * comparison below is the standard wraparound-safe way to compare against it. */
    start_deadline = GetTickCount() + 10000;
    for (;;) {
        MSG message;

        if ((GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0) {
            break;
        }

        if (started) {
            if (!vlc_api.player_is_playing(player)) {
                break;   /* stopped on its own - reached the end (or, rarely, errored) */
            }
        } else if (vlc_api.player_is_playing(player)) {
            started = true;
        } else if ((int32_t)(GetTickCount() - start_deadline) >= 0) {
            vlc_api.player_stop(player);
            vlc_api.player_release(player);
            return false;
        }

        if (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
            if (exclude_from_dispatch == NULL || message.hwnd != exclude_from_dispatch) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        } else {
            Sleep(1);
        }
    }

    vlc_api.player_stop(player);
    vlc_api.player_release(player);
    return true;
}
