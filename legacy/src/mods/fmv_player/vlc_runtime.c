/* vlc_runtime.c: see vlc_runtime.h. */
#include "vlc_runtime.h"

#include "vlc_playback.h"
#include "vlc_locate.h"

#include "common/logging.h"
#include "common/platform.h"

#include <process.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static vlc_api_t vlc_api;

const vlc_api_t *vlc_runtime_api(void)
{
    return &vlc_api;
}


/* ============================================================================================ */
/* Resolves one export and says which one was missing rather than only that something was. A
 * missing name here means the libVLC on this machine is not the API this file was written
 * against, and that is worth naming in the log by the name that was not there. */
static void *resolve(const char *name)
{
    void *address = (void *)GetProcAddress(vlc_api.module, name);

    if (address == NULL) {
        log_error("libvlc.dll has no %s, so this libVLC is not the API this DLL speaks", name);
    }
    return address;
}

static bool resolve_exports(void)
{
    vlc_api.new_instance = (libvlc_new_fn)resolve("libvlc_new");
    vlc_api.release = (libvlc_release_fn)resolve("libvlc_release");
    vlc_api.media_new_path = (libvlc_media_new_path_fn)resolve("libvlc_media_new_path");
    vlc_api.media_release = (libvlc_media_release_fn)resolve("libvlc_media_release");
    vlc_api.player_new_from_media =
        (libvlc_media_player_new_from_media_fn)resolve("libvlc_media_player_new_from_media");
    vlc_api.player_release = (libvlc_media_player_release_fn)resolve("libvlc_media_player_release");
    vlc_api.player_set_hwnd = (libvlc_media_player_set_hwnd_fn)resolve("libvlc_media_player_set_hwnd");
    vlc_api.player_play = (libvlc_media_player_play_fn)resolve("libvlc_media_player_play");
    vlc_api.player_stop = (libvlc_media_player_stop_fn)resolve("libvlc_media_player_stop");
    vlc_api.player_is_playing =
        (libvlc_media_player_is_playing_fn)resolve("libvlc_media_player_is_playing");

    /* OPTIONAL, so not through resolve(), which would call a libVLC lacking it the wrong API. It
     * only forces the picture to fill the window; without it every movie still plays letterboxed,
     * and failing the whole load over a preference would be the worse trade. */
    vlc_api.video_set_aspect_ratio =
        (libvlc_video_set_aspect_ratio_fn)GetProcAddress(vlc_api.module,
                                                         "libvlc_video_set_aspect_ratio");
    if (vlc_api.video_set_aspect_ratio == NULL) {
        log_warning("this libVLC has no libvlc_video_set_aspect_ratio, so movies always keep their "
                    "own shape and Scaling=stretch cannot be honoured");
    }

    return vlc_api.new_instance != NULL && vlc_api.release != NULL &&
           vlc_api.media_new_path != NULL && vlc_api.media_release != NULL &&
           vlc_api.player_new_from_media != NULL && vlc_api.player_release != NULL &&
           vlc_api.player_set_hwnd != NULL && vlc_api.player_play != NULL &&
           vlc_api.player_stop != NULL && vlc_api.player_is_playing != NULL;
}

/* Unwinds what init_worker has done so far, and is only ever called from its FAILURE paths.
 * An initialisation that gives up halfway must not leave two loaded modules and a changed
 * environment variable behind for the rest of the session: the next thing to load a libvlccore.dll
 * would get ours without having asked for it.
 *
 * The environment variable is REMOVED, not restored. No original is saved, so a process that had
 * VLC_PLUGIN_PATH exported before this DLL ran does not get it back. That is a deliberate limit
 * rather than an oversight: this is a 1999 game launched from a shortcut, the variable is not one
 * it or any of its other components sets, and saving and restoring it would add a state to carry
 * for a case nobody has seen. It is written down because "puts the process back exactly as it was
 * found" is what this comment used to claim, and that was not true.
 *
 * There is deliberately no success-path counterpart to this. Once libVLC is up it stays up for the
 * life of the process, because the only place a feature DLL could unwind it is DllMain, and
 * FreeLibrary and a thread-joining libvlc_release do not belong under the loader lock. dll_main.c
 * carries that reasoning. */
static void unload(void)
{
    if (vlc_api.plugin_path_set) {
        SetEnvironmentVariableW(L"VLC_PLUGIN_PATH", NULL);
    }
    if (vlc_api.module != NULL) {
        FreeLibrary(vlc_api.module);
    }
    if (vlc_api.core_module != NULL) {
        FreeLibrary(vlc_api.core_module);
    }
    ZeroMemory(&vlc_api, sizeof vlc_api);
}

/* --vout=NAME, assembled once and held for the life of the process because libVLC keeps the
 * pointers it is given. Empty means the argument is not passed at all, which is not the same as
 * passing an empty one: libVLC then chooses, exactly as it did before this existed. */
static char vout_argument[64] = "";

void vlc_playback_set_video_output(const char *name)
{
    if (name == NULL || name[0] == '\0') {
        vout_argument[0] = '\0';
        return;
    }
    if (_snprintf_s(vout_argument, sizeof vout_argument, _TRUNCATE, "--vout=%s", name) < 0) {
        vout_argument[0] = '\0';
        log_warning("VideoOutput=%s does not fit, libVLC will choose its own video output", name);
    }
}

/* ============================================================================================ */
static bool init_worker(void)
{
    wchar_t vlc_dir[MAX_PATH];
    wchar_t core_path[MAX_PATH];
    wchar_t main_path[MAX_PATH];
    wchar_t plugin_path[MAX_PATH];
    const char *instance_args[3];
    int         instance_arg_count = 0;
    /* CONFIRMED FIELD FIX for a ROG Ally X that reported silent movies with DSOAL installed.
     *
     * --aout=mmdevice (forcing WASAPI) was the first fix tried, on the theory that libVLC's own
     * auto-probe was falling back to DirectSound and DSOAL (a DirectSound-to-OpenAL layer for the
     * retail game's own EAX effects) was swallowing the audio from there. FIELD-TESTED and it did
     * NOT fix it: the movies stayed silent even with WASAPI forced and DirectSound removed from
     * the path entirely, which ruled out DirectSound/DSOAL as the actual cause.
     *
     * --aout=waveout (winmm) is what's forced now, and it FIXED the reported system: the oldest
     * Windows audio output there is, older than DirectSound and WASAPI both, with no exclusive-
     * mode negotiation, no audio session machinery and no device enumeration through the MMDevice
     * COM API that WASAPI and mmdevice both depend on. Whatever was actually wrong on that system
     * (handheld audio middleware, a spatial audio APO, a COM initialisation quirk from running
     * inside an injected DLL) never got the chance to interfere with winmm's much smaller
     * surface. The specific mechanism was never pinned down beyond that, and does not need to be:
     * the fix is confirmed on the machine that reported the bug, which is the bar every other
     * field-tested fix in this project is held to. */

    instance_args[instance_arg_count++] = "--no-video-title-show";
    instance_args[instance_arg_count++] = "--aout=waveout";
    if (vout_argument[0] != '\0') {
        instance_args[instance_arg_count++] = vout_argument;
    }

    if (!vlc_locate_directory(vlc_dir, ARRAYSIZE(vlc_dir))) {
        return false;   /* vlc_locate.c has already said which places it looked in */
    }
    if (_snwprintf(core_path, ARRAYSIZE(core_path) - 1, L"%s\\libvlccore.dll", vlc_dir) < 0 ||
        _snwprintf(main_path, ARRAYSIZE(main_path) - 1, L"%s\\libvlc.dll", vlc_dir) < 0 ||
        _snwprintf(plugin_path, ARRAYSIZE(plugin_path) - 1, L"%s\\plugins", vlc_dir) < 0) {
        log_error("the libVLC paths under %ls do not fit in %u characters", vlc_dir,
                  (unsigned)ARRAYSIZE(core_path));
        return false;
    }
    core_path[ARRAYSIZE(core_path) - 1] = L'\0';
    main_path[ARRAYSIZE(main_path) - 1] = L'\0';
    plugin_path[ARRAYSIZE(plugin_path) - 1] = L'\0';

    /* libvlc.dll depends on libvlccore.dll. Loading the core explicitly first, out of the same
     * directory that was just confirmed to hold a libvlc.dll, means the loader never has to decide
     * whether some other libvlccore.dll on the search path is the compatible one. */
    vlc_api.core_module = LoadLibraryW(core_path);
    if (vlc_api.core_module == NULL) {
        log_error("%ls could not be loaded (error %u). The usual cause is a 64-bit VLC: this is a "
                  "32-bit process and cannot load a 64-bit DLL at all.", core_path,
                  (unsigned)GetLastError());
        unload();
        return false;
    }
    vlc_api.module = LoadLibraryW(main_path);
    if (vlc_api.module == NULL) {
        log_error("%ls could not be loaded (error %u)", main_path, (unsigned)GetLastError());
        unload();
        return false;
    }

    /* Points libVLC's own plugin loader at this install's plugins folder rather than relying on
     * its relative-to-DLL detection to land on the right one, which matters most for the bundled
     * runtime, where there is no VLC installation around it to fall back on. */
    if (!SetEnvironmentVariableW(L"VLC_PLUGIN_PATH", plugin_path)) {
        log_error("VLC_PLUGIN_PATH could not be set (error %u)", (unsigned)GetLastError());
        unload();
        return false;
    }
    vlc_api.plugin_path_set = true;

    if (!resolve_exports()) {
        unload();
        return false;
    }

    vlc_api.instance = vlc_api.new_instance(instance_arg_count, instance_args);
    if (vlc_api.instance == NULL) {
        log_error("libvlc_new refused, most often because the plugins folder under %ls is missing "
                  "or incomplete", vlc_dir);
        unload();
        return false;
    }

    log_info("libVLC ready, plugins from %ls, audio output forced to waveout (confirmed field fix "
             "for silent movies on at least one system), video output %s", plugin_path,
             (vout_argument[0] != '\0') ? vout_argument : "left to libVLC");
    return true;
}

/* ============================================================================================
 * WHY THE LOAD ABOVE RUNS ON ITS OWN THREAD
 *
 * init_worker() walks the registry and the disk for a 32-bit VLC, loads two DLLs out of it and
 * then calls libvlc_new, which initialises VLC's entire plugin system. None of that is fast, and
 * on the game's own thread it is time in which nothing pumps the game window's messages. Doing it
 * at install time, which is where it has to happen if the first movie is not to wait for it, puts
 * that stall in the middle of the engine's own start-up.
 *
 * So it runs on a thread of its own, started as early as this DLL is loaded, and nothing on the
 * game's thread ever waits for it. vlc_playback_is_ready() is a non-blocking poll, and a movie
 * that comes along before the load finishes plays through the retail Bink path instead, the same
 * fallback used when there is no converted file at all. The first movie in this game plays almost
 * immediately, so that fallback is not hypothetical; it is the expected outcome for the logo on a
 * cold start, and the movies after it get the overlay.
 *
 * The event is the entire synchronisation. init_succeeded is written on the init thread before
 * SetEvent and read only after a wait on that event has already returned signalled, so there is a
 * release and an acquire around it and no lock is needed on top.
 * ============================================================================================ */
static HANDLE init_thread;
static HANDLE init_done_event;   /* manual-reset, signalled once, after every vlc_api write */
static bool   init_succeeded;

static unsigned __stdcall init_thread_proc(void *unused)
{
    (void)unused;

    init_succeeded = init_worker();
    if (!init_succeeded) {
        log_warning("libVLC did not finish loading in the background (no 32-bit install found, or "
                    "an export or libvlc_new() itself failed), every movie keeps using the retail "
                    "Bink path");
    }
    SetEvent(init_done_event);
    return 0;
}

void vlc_playback_init_async(void)
{
    uintptr_t handle;

    if (init_thread != NULL) {
        return;                                     /* already started, idempotent */
    }

    init_done_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (init_done_event == NULL) {
        log_error("libVLC: could not create the init-done event (error %u), every movie keeps "
                  "using the retail Bink path", (unsigned)GetLastError());
        return;
    }

    handle = _beginthreadex(NULL, 0, init_thread_proc, NULL, 0, NULL);
    if (handle == 0) {
        log_error("libVLC: the background init thread could not start, every movie keeps using "
                  "the retail Bink path");
        CloseHandle(init_done_event);
        init_done_event = NULL;
        return;
    }
    /* The handle is kept rather than closed, and never joined. This DLL is loaded once and never
     * freed (see dll_main.c), so there is no shutdown for a join to belong to, and holding the
     * handle keeps the thread object addressable for a debugger looking at a stalled load. */
    init_thread = (HANDLE)handle;

    log_info("libVLC is loading on its own thread so the first movie never has to block the "
             "game's own thread waiting for it; a movie that wants to play before this finishes "
             "falls through to the retail Bink path instead of waiting.");
}

bool vlc_playback_is_ready(void)
{
    if (init_done_event == NULL) {
        return false;                                /* never started, or failed to start */
    }
    if (WaitForSingleObject(init_done_event, 0) != WAIT_OBJECT_0) {
        return false;                                /* still loading */
    }
    return init_succeeded;
}

bool vlc_playback_is_still_loading(void)
{
    /* Only the one temporary answer. A load that never started, and a load that finished and
     * failed, are both permanent for this session and are not this. */
    return init_done_event != NULL &&
           WaitForSingleObject(init_done_event, 0) != WAIT_OBJECT_0;
}
