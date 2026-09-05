/* video_overlay.c: finds the game's own window, creates a borderless popup owned by it, and plays
 * one file into that popup through libVLC, in-process, blocking until it ends.
 *
 * ==============================================================================================
 * The full path is here, because none of the earlier steps were WRONG so much as INCOMPLETE
 *
 * Design 1 played movies through Windows' own Media Foundation (MFPlay), in-process, in a window
 * owned by the game. It flickered black for the whole length of every movie. Five fixes for that,
 * each chasing a real hypothesis, each verified insufficient on its own: re-asserting
 * WS_EX_TOPMOST every 200 ms; minimizing the game window outright; a real WM_ERASEBKGND bug (the
 * overlay's own background fill racing EVR's Direct3D presentation on the same window; fixed, not
 * the cause); dropping WS_EX_TOPMOST and all Z-order reassertion; filtering the game window's own
 * messages out of the shared message loop.
 *
 * Design 2 moved MFPlay into a separate process, on the theory that MFPlay's Direct3D9Ex/EVR
 * device sharing a process with the DirectDraw-to-Direct3D9 translation layer this whole redesign
 * exists to route around was the constant across all five in-process fixes. Getting the overlay to
 * render above the game at all across a process boundary needed the game's HWND passed over as the
 * overlay's OWNER (Windows keeps an owned window above its owner automatically), which surfaced a
 * real, independent bug: blocking the game's own thread on WaitForSingleObject(..., INFINITE)
 * while another process created a window owned by its window deadlocked the whole desktop
 * compositor once, not just the game. Once that was fixed the overlay rendered on top, but taking
 * foreground on a window sized to the whole monitor made Windows' shell treat it as switching to a
 * different fullscreen app and auto-minimize the game underneath, and the game's own WndProc
 * fighting to restore itself raced that minimize, which is what the earlier flicker had actually
 * been. Dropping SetForegroundWindow, then WS_EX_NOACTIVATE, each closed part of that gap without
 * closing it. Disabling Windows' Fullscreen Optimizations for the executable, a genuinely
 * different and independently testable theory, changed nothing, which ruled out DWM's own
 * exclusive-fullscreen heuristics specifically.
 *
 * Along the way MFPlay itself was replaced with libVLC. Standalone VLC, playing the exact same
 * converted file on the reporting machine, showed zero flicker from the start, which MFPlay never
 * managed even isolated in its own process. That swap alone fixed the flicker, and it was a
 * single-variable change: same process, same window, same file, different decoder. The minimize
 * was still separate, and reading the translation layer's own source settled why: a real
 * exclusive-mode Direct3D9 device, which is what this game gets by default, translated from its
 * own DirectDraw DDSCL_EXCLUSIVE request, auto-minimizes on WM_ACTIVATEAPP(deactivate) as a
 * fundamental, decades-old part of the D3D9 runtime, unrelated to and unaffected by Fullscreen
 * Optimizations. That layer has a windowed-mode override which avoids this by never requesting
 * exclusive mode at all, confirmed against the real install to eliminate both symptoms, but at a
 * real cost: this game switches its own internal DirectDraw resolution between menus (640x480) and
 * gameplay constantly, something true exclusive fullscreen never had to expose because the GPU
 * always scales the backbuffer to fill the screen; windowed mode instead physically resizes the
 * actual window on every such change, landing on stale intermediate sizes. A structural mismatch
 * with this game, not a misconfiguration, and it was reverted.
 *
 * Which leaves the design here. WM_ACTIVATEAPP is a CROSS-PROCESS signal: Windows sends it when a
 * window belonging to a DIFFERENT process becomes relevant, which a separate host process always
 * was, regardless of WS_EX_NOACTIVATE (which governs keyboard activation, not process identity).
 * Moving the overlay back in-process, now that libVLC rather than MFPlay renders into it, removes
 * that signal at the root: Windows does not raise WM_ACTIVATEAPP for a window becoming topmost
 * within its OWN process.
 *
 * That is ONE reason, and this file used to claim two. The second was that excluding the game's
 * window from the message loop's dispatch would stop WM_ACTIVATEAPP reaching its window procedure.
 * It cannot: WM_ACTIVATEAPP is a sent message, delivered by the system straight to the target
 * window procedure from inside PeekMessageW itself, and never appears in the MSG that the loop
 * filters on. An Alt-Tab during a movie therefore does reach the engine's window procedure, on the
 * thread parked inside this call. What the exclusion genuinely does is keep the game's POSTED
 * traffic, input, timers and paints alike, out of its window procedure while the movie plays,
 * worth having on its own terms and is what it is documented as now.
 * ============================================================================================== */
#include "video_overlay.h"

#include "menu_cursor_cells.h"
#include "vlc_playback.h"

#include "common/logging.h"

#include <windows.h>

#include <stdbool.h>
#include <string.h>
#include <wchar.h>

#define OVERLAY_CLASS_NAME L"OpenPhantomFmvPlayerOverlay"

/* The engine's smallest display mode is 640x480 and its menus run at exactly that, so anything
 * much smaller is not the game. The floor is set well below it rather than at it, because the
 * translation layer's windowed mode does briefly resize the real window while the engine switches
 * its own internal resolution, and a movie that started during one of those moments should still
 * find the right window. What this is really for is the windows a process owns without ever
 * showing anything in them: input method windows, tooltips, and the invisible message-only
 * helpers a graphics wrapper leaves lying around. */
#define MIN_GAME_WINDOW_WIDTH  320
#define MIN_GAME_WINDOW_HEIGHT 240

typedef struct overlay_state {
    bool    class_registered;
    HMODULE module;
} overlay_state_t;

static overlay_state_t overlay_state;

/* ============================================================================================ */
static LRESULT CALLBACK overlay_window_proc(HWND window, UINT message, WPARAM wparam,
                                            LPARAM lparam)
{
    if (message == WM_ERASEBKGND) {
        /* A GDI fill here would race libVLC's own presentation on the same window; see the design
         * history above (design 1, fix 3) for the version of this bug that cost a real debugging
         * session to find the first time. The one moment that needs a manual black fill is handled
         * once, directly, after the window is shown. */
        return 1;
    }
    if (message == WM_SETCURSOR) {
        /* No pointer over a cutscene. The engine's own movie window procedure does exactly this,
         * and it is one of the things that made the overlay feel like a different program: the
         * retail path hides the cursor for the length of a movie and this did not. */
        SetCursor(NULL);
        return TRUE;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

/* This DLL's own module handle, taken from the address of a function inside it rather than from
 * DllMain, so nothing has to be threaded through from the entry point.
 *
 * The class is registered against this rather than against the executable because the window
 * procedure lives here: a class that names the executable while its procedure sits in a DLL is a
 * dangling pointer the moment that DLL goes away. Nothing unregisters it, and nothing needs to -
 * see dll_main.c for why this feature gives none of its process-global state back. */
static HMODULE own_module(void)
{
    if (overlay_state.module == NULL) {
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCWSTR)(void *)&overlay_window_proc, &overlay_state.module);
    }
    return overlay_state.module;
}

/* ============================================================================================ */
typedef struct window_search {
    HWND found;
} window_search_t;

static bool is_our_overlay(HWND window)
{
    wchar_t class_name[64];

    if (GetClassNameW(window, class_name, ARRAYSIZE(class_name)) == 0) {
        return false;
    }
    return wcscmp(class_name, OVERLAY_CLASS_NAME) == 0;
}

/* The first window, in Z-order, that could be the game: this process's, visible, owned by nobody,
 * and big enough to be a display rather than a tooltip.
 *
 * The owner test is the one that matters and the one this file used to be missing. The overlay
 * created below is owned by the game window and is visible while it exists, so without that test a
 * second movie starting while an overlay is still up would size itself against the overlay rather
 * than against the game. The class-name test is a second, cheaper answer to the same question and
 * costs nothing. */
static BOOL CALLBACK pick_window(HWND window, LPARAM user_data)
{
    window_search_t *search = (window_search_t *)user_data;
    DWORD            process_id = 0;
    RECT             bounds;

    GetWindowThreadProcessId(window, &process_id);
    if (process_id != GetCurrentProcessId()) {
        return TRUE;
    }
    if (!IsWindowVisible(window)) {
        return TRUE;
    }
    if (GetWindow(window, GW_OWNER) != NULL || is_our_overlay(window)) {
        return TRUE;
    }
    if (!GetWindowRect(window, &bounds)) {
        return TRUE;
    }
    if (bounds.right - bounds.left < MIN_GAME_WINDOW_WIDTH ||
        bounds.bottom - bounds.top < MIN_GAME_WINDOW_HEIGHT) {
        return TRUE;
    }

    search->found = window;
    return FALSE;
}

static HWND find_game_window(void)
{
    window_search_t search = { NULL };

    EnumWindows(pick_window, (LPARAM)&search);
    if (search.found == NULL) {
        log_warning("no visible unowned top-level window of at least %dx%d belongs to this "
                    "process, so there is nothing to lay a movie over",
                    MIN_GAME_WINDOW_WIDTH, MIN_GAME_WINDOW_HEIGHT);
    }
    return search.found;
}

/* ============================================================================================ */
static bool register_overlay_class_once(void)
{
    WNDCLASSW window_class;

    if (overlay_state.class_registered) {
        return true;
    }

    ZeroMemory(&window_class, sizeof window_class);
    window_class.lpfnWndProc   = overlay_window_proc;
    window_class.hInstance     = own_module();
    window_class.lpszClassName = OVERLAY_CLASS_NAME;
    window_class.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    /* Deliberately no class cursor: WM_SETCURSOR above hides it instead, the way the engine's own
     * movie window procedure does. A class cursor here would put an arrow back on top of every
     * cutscene. */
    window_class.hCursor       = NULL;

    if (RegisterClassW(&window_class) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        log_error("the overlay window class could not be registered (error %u)",
                  (unsigned)GetLastError());
        return false;
    }
    overlay_state.class_registered = true;
    return true;
}

bool video_overlay_start_async_init(void)
{
    if (!register_overlay_class_once()) {
        return false;
    }
    /* Best effort and deliberately not fatal: without it the movies still play, only the drawn
     * menu cursor is left where the engine's own queued messages put it. */
    (void)menu_cursor_cells_resolve();
    vlc_playback_init_async();
    return true;
}

bool video_overlay_is_ready(void)
{
    return vlc_playback_is_ready();
}

bool video_overlay_is_still_loading(void)
{
    return vlc_playback_is_still_loading();
}

/* ============================================================================================ */
/* See the header. SURFACE_POPUP is what shipped and stays the default. */
typedef enum {
    SURFACE_POPUP = 0,
    SURFACE_CHILD,
    SURFACE_GAME
} surface_mode_t;

static surface_mode_t surface_mode = SURFACE_POPUP;

void video_overlay_set_surface_mode(const char *mode)
{
    if (mode == NULL) {
        return;
    }
    if (_stricmp(mode, "popup") == 0) {
        surface_mode = SURFACE_POPUP;
    } else if (_stricmp(mode, "child") == 0) {
        surface_mode = SURFACE_CHILD;
        log_info("the movie surface is a child of the game's window rather than a window of its "
                 "own. This is the Wine default: a top level window is mapped as an X11 window, "
                 "the window manager focuses it, and Wine then holds the focus for a window that "
                 "refuses activation, so no key reaches the game until the movie ends. See "
                 "video_overlay.h");
    } else if (_stricmp(mode, "game") == 0) {
        surface_mode = SURFACE_GAME;
        log_info("libVLC is given the game's own window and no movie window is created. This is "
                 "the last resort Wine setting: see video_overlay.h");
    } else {
        log_warning("MovieSurface=%s is not one of popup, child or game, so the shipped popup is "
                    "kept", mode);
    }
}

/* The one moment that needs a manual black fill: the gap between the window appearing and libVLC's
 * first decoded frame reaching it.
 *
 * It has to happen AFTER ShowWindow. A window created without WS_VISIBLE has no visible region, so
 * GDI clips every drawing operation through its device context away entirely; the fill this file
 * used to do before showing the window was a no-op, and what filled that gap instead was whatever
 * a fresh DWM redirection surface happens to contain, which is not something this code gets to
 * decide. Doing it once here rather than from WM_ERASEBKGND is deliberate; see the window
 * procedure above for why repeating it was a real bug. */
static void fill_black_once(HWND window)
{
    HDC  paint_dc = GetDC(window);
    RECT client_rect;

    if (paint_dc == NULL) {
        log_warning("no device context for the overlay, so the first frames may show whatever was "
                    "on screen before");
        return;
    }
    GetClientRect(window, &client_rect);
    FillRect(paint_dc, &client_rect, (HBRUSH)GetStockObject(BLACK_BRUSH));
    ReleaseDC(window, paint_dc);
}

/* The whole monitor the game is on, which is what a movie has to cover.
 *
 * The client rect was the obvious answer and it was the wrong one. The engine's own movie path
 * FORCES the display to its minimum mode before it plays anything and puts the previous mode back
 * afterwards, so "the size of the game's window while a movie is starting" is not the size of the
 * game, it is 640x480. Sizing to it produced a small picture in a corner of a black screen. The
 * monitor does not move, and it is also the honest reading of "full screen".
 *
 * The window is not the game's, so this asks which monitor the game is on rather than assuming the
 * primary one: a player with two screens should get the movie on the one the game is on. */
static bool monitor_rect_of(HWND window, RECT *out_rect)
{
    HMONITOR     monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    MONITORINFO  info;

    ZeroMemory(&info, sizeof info);
    info.cbSize = sizeof info;
    if (monitor == NULL || !GetMonitorInfo(monitor, &info)) {
        log_error("the monitor the game is on could not be identified (error %u)",
                  (unsigned)GetLastError());
        return false;
    }

    *out_rect = info.rcMonitor;
    return true;
}

/* The surface libVLC is given. See the header for what each one is for and why the choice exists
 * at all. Returns NULL in SURFACE_GAME, where NULL is the answer rather than a failure, so the
 * caller tests the mode and not just the handle. */
static HWND create_surface(HWND game_window)
{
    RECT rect;

    if (surface_mode == SURFACE_GAME) {
        return NULL;
    }

    if (surface_mode == SURFACE_CHILD) {
        /* The game's client area, not the monitor. A child is positioned inside its parent, so its
         * origin is 0,0 by definition. The engine forces the display to its minimum mode while a
         * movie plays, as monitor_rect_of's own note explains, so this can be 640x480: that is the
         * whole of the game's window at that moment, which is the whole of the screen, so the
         * picture still covers everything the player can see. */
        if (!GetClientRect(game_window, &rect)) {
            log_error("the game window's client area could not be read (error %u)",
                      (unsigned)GetLastError());
            return NULL;
        }
        return CreateWindowExW(WS_EX_NOACTIVATE, OVERLAY_CLASS_NAME, L"", WS_CHILD,
                               0, 0, rect.right - rect.left, rect.bottom - rect.top,
                               game_window, NULL, own_module(), NULL);
    }

    if (!monitor_rect_of(game_window, &rect)) {
        return NULL;
    }

    /* Owned by the game window, so Windows keeps it above its owner with no WS_EX_TOPMOST and no
     * Z-order reassertion, and WS_EX_NOACTIVATE because it never needs keyboard focus: Escape is
     * read as physical key state inside vlc_playback.c rather than as per-window input. Both were
     * already true of the separate-process design and neither was ever the problem; being
     * in-process is what is different here. */
    return CreateWindowExW(WS_EX_NOACTIVATE, OVERLAY_CLASS_NAME, L"", WS_POPUP,
                           rect.left, rect.top,
                           rect.right - rect.left, rect.bottom - rect.top,
                           game_window, NULL, own_module(), NULL);
}

bool video_overlay_play_blocking(const wchar_t *file_path)
{
    HWND  game_window;
    HWND  overlay_window;
    HWND  render_window;
    bool  played;

    if (file_path == NULL) {
        return false;
    }

    game_window = find_game_window();
    if (game_window == NULL) {
        return false;
    }

    overlay_window = create_surface(game_window);
    if (overlay_window == NULL && surface_mode != SURFACE_GAME) {
        log_error("the overlay window could not be created (error %u)", (unsigned)GetLastError());
        return false;
    }
    render_window = (overlay_window != NULL) ? overlay_window : game_window;

    if (overlay_window != NULL) {
        ShowWindow(overlay_window, SW_SHOW);
        fill_black_once(overlay_window);
        UpdateWindow(overlay_window);
    }

    played = vlc_playback_play_blocking(render_window, file_path, game_window);

    if (overlay_window != NULL) {
        DestroyWindow(overlay_window);
    }

    /* The pump above is scoped to the overlay, so the game window's own mouse messages were never
     * touched while the movie ran: they simply queued up. Left alone, that whole backlog would be
     * delivered to the engine's accumulator the moment the normal pump resumes, adding a burst of
     * stale deltas AFTER the deliberate recentring below. Draining it first is what makes that
     * write the last thing to touch the drawn cursor before the player's own next real move.
     *
     * Only the mouse range is drained, and only for the game's window. What is lost is the pointer
     * travel that happened while a full-screen movie covered the picture, which is not information
     * anybody can act on, and a button press made against a movie the player could not see. */
    {
        MSG stale;
        while (PeekMessageW(&stale, game_window, WM_MOUSEFIRST, WM_MOUSELAST, PM_REMOVE)) {
            /* discarded on purpose */
        }
    }
    menu_cursor_cells_recentre();

    /* Field-confirmed repair for a stray OS "loading" cursor left on screen after the intro
     * movies. Not a hang: the process showed as Running throughout, never Not Responding, which
     * rules out the shell's own "still starting" heuristic. What explains it is that the RETAIL
     * Bink path visibly flashes the screen several times before the game settles, real
     * minimize/restore cycles, consistent with this game's exclusive-mode Direct3D9 device
     * reacting to however Bink touches the display, while this DLL shows none of that flashing,
     * which is it doing exactly what it was built to do. Something early in start-up leaves the OS
     * cursor undone, and the retail path's incidental flashing was re-triggering a full activation
     * handshake and curing it before anyone saw it. A smooth window removes that accidental cure
     * along with the flicker, which is why the symptom only ever appeared with this DLL installed
     * even though its cause is not in this file. This does not repair that cause; it replaces the
     * accidental cure with a deliberate one, once per movie.
     *
     * Both calls are needed. SetForegroundWindow asks Windows to reconsider which window owns the
     * foreground but does not itself force a cursor update; SetCursor(NULL) hides it immediately,
     * on this thread, which is the same answer the overlay's own window procedure gives
     * WM_SETCURSOR. */
    SetForegroundWindow(game_window);
    SetCursor(NULL);

    return played;
}
