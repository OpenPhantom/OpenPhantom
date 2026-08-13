/* vlc_playback.c: loads a 32-bit libVLC and drives it through the small, long-stable subset of its
 * C API this file actually needs. See vlc_playback.h for the contract and vlc_locate.c for where
 * the DLL comes from.
 *
 * WHY THE API IS DECLARED BY HAND
 * The functions below are the oldest core of libvlc's public C API - libvlc_new and _release,
 * media and media-player lifetime, set_hwnd, play, stop, is_playing - all present with this exact
 * __cdecl C signature since libVLC's earliest 2.x releases. Nothing here reaches into libVLC's
 * event or state-enum APIs, whose numeric values this file has no local header to check against,
 * which is the same rule the rest of this project follows about never trusting a constant it has
 * not seen for itself.
 *
 * resolve_exports() requires all ten to be PRESENT. That is a check on names and nothing more: it
 * catches a libVLC that removed or renamed one of them, and it cannot catch one that kept the name
 * and changed the signature, because there is nothing at run time to compare a signature against.
 * What makes that acceptable is not the check, it is the choice of functions - this is the oldest
 * and least-touched corner of the API - together with the installer shipping a known 3.x runtime
 * that vlc_locate.c looks at before anything else on the machine.
 */
#include "vlc_playback.h"

#include "vlc_locate.h"

#include "common/logging.h"

#include <process.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* How long libVLC gets to actually begin decoding before this gives up and hands the movie back to
 * the retail path. It exists because a file libVLC can open but not decode would otherwise park
 * the game's thread here forever. The cost of the wait is visible - the overlay is on screen and
 * black for that long before the Bink version starts - so it is short. */
#define START_TIMEOUT_MS 5000u

/* ============================================================================================
 * The hand-declared slice of libvlc's C API. Opaque handles, never dereferenced here, only handed
 * back to libvlc's own functions, plus __cdecl function-pointer typedefs matching libVLC's own
 * declarations, which use plain C linkage with no stdcall or WINAPI decoration anywhere.
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
/* Takes a string like "16:9", or NULL to go back to the source's own shape. */
typedef void (__cdecl *libvlc_video_set_aspect_ratio_fn)(libvlc_media_player_t *player,
                                                          const char *aspect);

typedef struct vlc_api {
    HMODULE                               core_module;
    HMODULE                               module;
    libvlc_new_fn                         new_instance;
    libvlc_release_fn                     release;
    libvlc_media_new_path_fn              media_new_path;
    libvlc_media_release_fn               media_release;
    libvlc_media_player_new_from_media_fn player_new_from_media;
    libvlc_media_player_release_fn        player_release;
    libvlc_media_player_set_hwnd_fn       player_set_hwnd;
    libvlc_media_player_play_fn           player_play;
    libvlc_media_player_stop_fn           player_stop;
    libvlc_media_player_is_playing_fn     player_is_playing;
    libvlc_video_set_aspect_ratio_fn      video_set_aspect_ratio;   /* optional, see resolve */
    libvlc_instance_t                    *instance;
    bool                                  plugin_path_set;
} vlc_api_t;

static vlc_api_t vlc_api;

/* Set once from the ini before any movie plays. Kept here so this layer takes one input and has no
 * opinion about where it came from. */
static bool vlc_stretch_to_window;

void vlc_playback_set_stretch(bool stretch)
{
    vlc_stretch_to_window = stretch;
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

/* ============================================================================================ */
static bool init_worker(void)
{
    wchar_t vlc_dir[MAX_PATH];
    wchar_t core_path[MAX_PATH];
    wchar_t main_path[MAX_PATH];
    wchar_t plugin_path[MAX_PATH];
    static const char *const instance_args[] = { "--quiet", "--no-video-title-show" };

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

    vlc_api.instance = vlc_api.new_instance((int)ARRAYSIZE(instance_args), instance_args);
    if (vlc_api.instance == NULL) {
        log_error("libvlc_new refused, most often because the plugins folder under %ls is missing "
                  "or incomplete", vlc_dir);
        unload();
        return false;
    }

    log_info("libVLC ready, plugins from %ls", plugin_path);
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
 * that comes along before the load finishes plays through the retail Bink path instead - the same
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

/* ============================================================================================ */
/* Escape only counts as a skip on the edge from up to down, and only while a window of this
 * process has the foreground.
 *
 * GetAsyncKeyState reports the physical key, which is right for a window that never takes keyboard
 * focus but wrong in two ways on its own: a key still held from the previous movie would skip the
 * next one the moment it starts, and a key pressed in somebody else's window would skip this one.
 * Both were real behaviour before these two tests. */
static bool foreground_belongs_to_us(void)
{
    DWORD process_id = 0;

    GetWindowThreadProcessId(GetForegroundWindow(), &process_id);
    return process_id == GetCurrentProcessId();
}

static bool escape_pressed_now(bool *was_down)
{
    bool down = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
    bool fresh = down && !*was_down && foreground_belongs_to_us();

    *was_down = down;
    return fresh;
}

/* True for the two posted messages that BEGIN a close, as opposed to WM_QUIT, which is what a close
 * has already turned into.
 *
 * This distinction is the whole reason this function exists. Alt+F4 arrives as WM_SYSKEYDOWN with
 * VK_F4, and the close box as WM_NCLBUTTONDOWN on hit-test area HTCLOSE. Neither is a close yet:
 * DefWindowProc is what turns them into WM_SYSCOMMAND, then WM_CLOSE, then eventually
 * PostQuitMessage. The overlay never takes activation, so the focus window during a movie is the
 * game's, so both of these are addressed to exactly the window whose messages this loop drops -
 * which means DefWindowProc never runs and no WM_QUIT is ever produced for the branch below to
 * catch. Handling only WM_QUIT preserves a quit somebody else raised and loses every quit the
 * player raises, which is the wrong half. */
static bool is_close_request(const MSG *message)
{
    if (message->message == WM_SYSKEYDOWN && message->wParam == VK_F4) {
        return true;
    }
    return message->message == WM_NCLBUTTONDOWN && message->wParam == HTCLOSE;
}

/* Does the player want out? Answered by LOOKING at the game window's queue without emptying it.
 *
 * This used to be answered on the way past, by peeking the whole thread queue with PM_REMOVE and
 * dropping whatever belonged to the game window. That is what the peek below deliberately no
 * longer does, and the reason is worth keeping: PM_REMOVE takes a message off the queue whether or
 * not it is then dispatched, so retrieving the game window's posted traffic and declining to
 * dispatch it did not DEFER that traffic, it DISCARDED it. Every WM_MOUSEMOVE the player made
 * during a movie was thrown away rather than delivered late, and the engine came out of each movie
 * having silently missed all of it.
 *
 * PM_NOREMOVE answers the same question without that cost: the two close requests are recognised
 * where they lie, and only THEY are then removed and re-posted, so the game's own pump finds them
 * once this call returns. Everything else stays exactly where it was, in order.
 *
 * The range filter is what makes this cheap: both requests live in the keyboard and non-client
 * mouse ranges, so nothing else is even looked at. */
static bool close_was_requested(HWND game_window)
{
    static const struct { UINT first; UINT last; } ranges[] = {
        { WM_SYSKEYDOWN,     WM_SYSKEYDOWN     },
        { WM_NCLBUTTONDOWN,  WM_NCLBUTTONDOWN  }
    };
    MSG    message;
    size_t index;

    if (game_window == NULL) {
        return false;
    }
    for (index = 0; index < ARRAYSIZE(ranges); ++index) {
        if (!PeekMessageW(&message, game_window, ranges[index].first, ranges[index].last,
                          PM_NOREMOVE)) {
            continue;
        }
        if (!is_close_request(&message)) {
            continue;
        }
        /* Take this one and put it straight back. Removing it first is what stops this returning
         * true again on the next turn before the game's pump has had a chance to run; re-posting
         * it unchanged is what makes the request survive to be honoured a moment later, by the
         * engine's own window procedure, on a thread that is no longer parked inside a movie. */
        if (PeekMessageW(&message, game_window, ranges[index].first, ranges[index].last,
                         PM_REMOVE)) {
            PostMessageW(message.hwnd, message.message, message.wParam, message.lParam);
        }
        log_info("the player asked to close the game during playback, ending the movie and "
                 "passing the request on");
        return true;
    }
    return false;
}

/* One turn of the message pump. Returns false when the player wants out, which ends playback rather
 * than swallowing the request.
 *
 * The peek is scoped to `window`, the overlay's own handle, and not NULL. An unscoped peek runs
 * the whole thread queue, and this loop runs in-process on the game's own thread, so the game
 * window's posted traffic is on that same queue. See close_was_requested() above for what taking
 * it and not dispatching it actually did.
 *
 * One consequence of the scoping, stated rather than discovered later: a filtered peek does not
 * retrieve thread messages either, and WM_QUIT is a thread message with no window. It therefore
 * stays on the queue for the game's own pump, which is the right place for it and the reason this
 * function no longer has a WM_QUIT branch. */
static bool pump_once(HWND window, HWND game_window)
{
    MSG message;

    if (close_was_requested(game_window)) {
        return false;
    }

    if (!PeekMessageW(&message, window, 0, 0, PM_REMOVE)) {
        Sleep(1);
        return true;
    }

    TranslateMessage(&message);
    DispatchMessageW(&message);
    return true;
}

/* Letterbox needs no call: libVLC keeps the source's own shape by default, so doing nothing is
 * already the conservative answer.
 *
 * Stretch is told the WINDOW's ratio rather than a fixed "16:9". The overlay covers the screen, so
 * claiming the picture has the window's shape makes it fill the window exactly, and it stays exact
 * on a 16:10, a 21:9 or a rotated panel where 16:9 would bar one axis and crop the other. A zero or
 * negative client rectangle, which a minimised or not yet shown window reports, is left alone. */
static void apply_scaling(libvlc_media_player_t *player, HWND window)
{
    RECT client;
    char aspect[32];

    if (!vlc_stretch_to_window || vlc_api.video_set_aspect_ratio == NULL) {
        return;
    }
    if (!GetClientRect(window, &client)) {
        return;
    }
    if (client.right - client.left <= 0 || client.bottom - client.top <= 0) {
        return;
    }

    if (_snprintf_s(aspect, sizeof aspect, _TRUNCATE, "%ld:%ld",
                    (long)(client.right - client.left),
                    (long)(client.bottom - client.top)) < 0) {
        return;
    }
    vlc_api.video_set_aspect_ratio(player, aspect);
}

bool vlc_playback_play_blocking(HWND window, const wchar_t *file_path, HWND game_window)
{
    char                   utf8_path[MAX_PATH * 3];   /* worst-case UTF-8 expansion of MAX_PATH */
    libvlc_media_t        *media;
    libvlc_media_player_t *player;
    bool                   started = false;
    bool                   escape_was_down;
    DWORD                  start_deadline;

    if (vlc_api.instance == NULL || window == NULL || file_path == NULL) {
        log_error("playback was asked for without an instance, a window or a file");
        return false;
    }
    /* libvlc_media_new_path documents its argument as UTF-8, not the local code page. */
    if (WideCharToMultiByte(CP_UTF8, 0, file_path, -1, utf8_path, sizeof utf8_path, NULL,
                            NULL) == 0) {
        log_error("%ls could not be converted to UTF-8 (error %u)", file_path,
                  (unsigned)GetLastError());
        return false;
    }

    media = vlc_api.media_new_path(vlc_api.instance, utf8_path);
    if (media == NULL) {
        log_error("libVLC refused to open %ls", file_path);
        return false;
    }
    player = vlc_api.player_new_from_media(media);
    vlc_api.media_release(media);   /* the player takes its own reference once created */
    if (player == NULL) {
        log_error("libVLC could not create a player for %ls", file_path);
        return false;
    }

    vlc_api.player_set_hwnd(player, (void *)window);
    apply_scaling(player, window);
    if (vlc_api.player_play(player) != 0) {
        log_error("libVLC refused to start %ls", file_path);
        vlc_api.player_release(player);
        return false;
    }

    /* GetTickCount wraps roughly every 49.7 days; comparing the difference as a signed value is
     * the standard way to stay correct across that wrap. */
    start_deadline = GetTickCount() + START_TIMEOUT_MS;
    escape_was_down = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;

    for (;;) {
        if (escape_pressed_now(&escape_was_down)) {
            log_info("Escape ended playback");
            break;
        }

        if (started) {
            if (!vlc_api.player_is_playing(player)) {
                break;   /* stopped on its own: the end of the file, or a rare error */
            }
            /* Losing the foreground ends the movie, which is what the engine's own movie window
             * procedure does on WM_ACTIVATE. Retail does not leave a cutscene running behind
             * somebody else's window, and a movie that carried on playing inaudibly under another
             * program was one of the things that made this feel like a separate application. The
             * test is only applied once playback is under way, because the foreground has not
             * necessarily settled in the moment the overlay appears. */
            if (!foreground_belongs_to_us()) {
                log_info("the game lost the foreground during playback, ending the movie the way "
                         "the engine's own player does");
                break;
            }
        } else if (vlc_api.player_is_playing(player)) {
            started = true;
        } else if ((int32_t)(GetTickCount() - start_deadline) >= 0) {
            log_warning("libVLC did not start decoding %ls within %u ms, giving the movie back to "
                        "the retail path", file_path, (unsigned)START_TIMEOUT_MS);
            vlc_api.player_stop(player);
            vlc_api.player_release(player);
            return false;
        }

        if (!pump_once(window, game_window)) {
            break;
        }
    }

    vlc_api.player_stop(player);
    vlc_api.player_release(player);
    return true;
}
