/* video_overlay.c: finds the game's own window, creates a borderless popup owned by it, and plays
 * one file into that popup through libVLC (vlc_playback.c), in-process, blocking until it ends.
 *
 * ==============================================================================================
 * THE FULL PATH HERE, BECAUSE NONE OF THE EARLIER STEPS WERE WRONG SO MUCH AS INCOMPLETE
 *
 * Design 1 played movies through Windows' own Media Foundation (MFPlay), in-process, in a window
 * owned by the game. It flickered black for the whole length of every movie. Five fixes for that,
 * each chasing a real hypothesis, each verified insufficient on its own: re-asserting
 * WS_EX_TOPMOST every 200 ms; minimizing the game window outright; a real WM_ERASEBKGND bug (the
 * overlay's own background fill racing EVR's Direct3D presentation on the same window - fixed, not
 * the cause); dropping WS_EX_TOPMOST and all Z-order reassertion; filtering the game window's own
 * messages out of the shared message loop.
 *
 * Design 2 moved MFPlay into fmv_player_host.exe, a separate process, on the theory that MFPlay's
 * Direct3D9Ex/EVR device sharing a process with dxwrapper (the DirectDraw-to-Direct3D9 translation
 * layer this whole redesign already exists to route around) was the constant across all five
 * in-process fixes. Getting the overlay to render above the game at all in a separate process
 * needed the game window's HWND passed across the process boundary as the overlay's OWNER (Windows
 * keeps an owned window above its owner in Z-order automatically) - which surfaced a real,
 * independent bug: blocking the game's own thread on WaitForSingleObject(..., INFINITE) while
 * another process created a window owned by its window deadlocked the whole desktop compositor
 * once, not just the game (fixed by pumping the game thread's queue via
 * MsgWaitForMultipleObjects instead of blocking it dead). Once that was fixed the overlay rendered
 * on top, but taking foreground on a window sized to the whole monitor made Windows' shell treat it
 * as switching to a different fullscreen app and auto-minimize the game underneath it - and the
 * game's own WndProc fighting to restore itself (now that its queue was being pumped again) raced
 * that minimize, which is what the earlier flicker had actually been. Dropping
 * SetForegroundWindow, then WS_EX_NOACTIVATE outright, each closed part of that gap without closing
 * it - the minimize kept recurring, "random" and then, on one launch, immediate. Disabling Windows'
 * Fullscreen Optimizations for WMAIN.EXE - a genuinely different, independently testable theory -
 * changed nothing, which ruled out DWM's own exclusive-fullscreen heuristics specifically.
 *
 * Along the way, MFPlay itself was replaced with libVLC (vlc_playback.c) - standalone VLC, playing
 * the exact same converted file on the reporting machine, showed zero flicker from the start, which
 * MFPlay never managed even isolated in its own process. That swap alone fixed the flicker. The
 * minimize was still separate, and reading dxwrapper's own source (it is open source; this was
 * checked directly, not guessed) settled why: a real exclusive-mode Direct3D9 device - which is
 * what this game gets by default, translated from its own DirectDraw DDSCL_EXCLUSIVE request -
 * auto-minimizes on WM_ACTIVATEAPP(deactivate) as a fundamental, decades-old part of the D3D9
 * runtime, unrelated to and unaffected by Fullscreen Optimizations. dxwrapper has a windowed-mode
 * override (EnableWindowMode) that avoids this by never requesting exclusive mode at all - tried
 * directly against the real install, confirmed via its own log to eliminate both the minimize and
 * the flicker, but at a real cost: this game switches its own internal DirectDraw resolution
 * between menus (640x480) and gameplay (its full configured resolution) constantly, something true
 * exclusive fullscreen never had to expose because the GPU always scales whatever the backbuffer is
 * to fill the physical screen - dxwrapper's windowed mode instead physically resizes the actual
 * window on every such change, landing on stale intermediate sizes. That is a structural mismatch
 * with this game, not a misconfiguration, and it was reverted.
 *
 * Which leaves the design here. WM_ACTIVATEAPP is specifically a CROSS-PROCESS signal - Windows
 * sends it when a window belonging to a DIFFERENT process becomes relevant, which
 * fmv_player_host.exe, a genuinely separate process, always was, regardless of WS_EX_NOACTIVATE
 * (which governs keyboard activation, not process identity). Moving the overlay back in-process,
 * now that libVLC rather than MFPlay is what actually renders into it, removes that signal at the
 * root: Windows does not send WM_ACTIVATEAPP for a window becoming topmost within its OWN process.
 * As a second, independent line of defense - cross-thread SendMessage delivery (which is how
 * WM_ACTIVATEAPP reaches a window's WndProc) requires the TARGET thread to be pumping its queue,
 * and the game's own thread is frozen inside this exact call for as long as a movie plays -
 * vlc_playback_play_blocking() is handed the game window as `exclude_from_dispatch`, so even if
 * Windows tries to deliver that message here, it is never actually dispatched to the game's WndProc
 * at all. Two independent reasons this should hold, not one.
 * ============================================================================================== */
#include "video_overlay.h"

#include "vlc_playback.h"

#include <windows.h>

#include <stdbool.h>

#define OVERLAY_CLASS_NAME L"OpenPhantomFmvPlayerOverlay"

/* ============================================================================================ */
static LRESULT CALLBACK overlay_window_proc(HWND window, UINT message, WPARAM wparam,
                                            LPARAM lparam)
{
    if (message == WM_ERASEBKGND) {
        /* A GDI fill here would race libVLC's own presentation on the same window - see the design
         * history above (design 1, fix 3) for the version of this bug that cost a real debugging
         * session to find the first time. The one moment that needs a manual black fill is handled
         * once, directly, right after CreateWindowExW below. */
        return 1;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

static BOOL CALLBACK find_own_window_proc(HWND window, LPARAM out_window)
{
    DWORD window_process_id = 0;

    GetWindowThreadProcessId(window, &window_process_id);
    if (window_process_id == GetCurrentProcessId() && IsWindowVisible(window)) {
        *(HWND *)out_window = window;
        return FALSE;   /* found it, stop enumerating */
    }
    return TRUE;
}

/* The game's own top-level window, found by asking Windows rather than by resolving any
 * engine-specific signature: the first visible top-level window this process owns. */
static HWND find_game_window(void)
{
    HWND game_window = NULL;
    EnumWindows(find_own_window_proc, (LPARAM)&game_window);
    return game_window;
}

static bool register_overlay_class_once(void)
{
    static bool registered;
    WNDCLASSW window_class;

    if (registered) {
        return true;
    }

    ZeroMemory(&window_class, sizeof window_class);
    window_class.lpfnWndProc   = overlay_window_proc;
    window_class.hInstance     = GetModuleHandleW(NULL);
    window_class.lpszClassName = OVERLAY_CLASS_NAME;
    window_class.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    window_class.hCursor       = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);

    if (RegisterClassW(&window_class) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }
    registered = true;
    return true;
}

/* ============================================================================================ */
bool video_overlay_init(void)
{
    return register_overlay_class_once() && vlc_playback_init();
}

bool video_overlay_play_blocking(const wchar_t *file_path)
{
    HWND    game_window;
    RECT    overlay_rect;
    POINT   client_origin;
    HWND    overlay_window;
    bool    played_without_error;

    if (file_path == NULL) {
        return false;
    }

    game_window = find_game_window();
    if (game_window == NULL || !GetClientRect(game_window, &overlay_rect)) {
        return false;
    }
    client_origin.x = 0;
    client_origin.y = 0;
    ClientToScreen(game_window, &client_origin);
    OffsetRect(&overlay_rect, client_origin.x, client_origin.y);

    /* Owned by the game window (Windows keeps an owned window above its owner in Z-order
     * automatically, no WS_EX_TOPMOST needed) and WS_EX_NOACTIVATE (this window never needs
     * keyboard focus - Escape-to-skip reads GetAsyncKeyState inside vlc_playback_play_blocking(),
     * physical key state rather than per-window input). Both were already true of design 2's
     * overlay and neither was ever the problem; being IN-PROCESS is what's different here - see the
     * header comment above. */
    overlay_window = CreateWindowExW(WS_EX_NOACTIVATE, OVERLAY_CLASS_NAME, L"", WS_POPUP,
                                     overlay_rect.left, overlay_rect.top,
                                     overlay_rect.right - overlay_rect.left,
                                     overlay_rect.bottom - overlay_rect.top,
                                     game_window, NULL, GetModuleHandleW(NULL), NULL);
    if (overlay_window == NULL) {
        return false;
    }

    /* The one moment that needs a manual black fill: the gap between the window existing and the
     * first decoded frame reaching the screen. Exactly once, here, never again from inside
     * WM_ERASEBKGND - see overlay_window_proc() for why repeating it there was a real bug once. */
    {
        HDC paint_dc = GetDC(overlay_window);
        if (paint_dc != NULL) {
            RECT client_rect;
            GetClientRect(overlay_window, &client_rect);
            FillRect(paint_dc, &client_rect, (HBRUSH)GetStockObject(BLACK_BRUSH));
            ReleaseDC(overlay_window, paint_dc);
        }
    }

    ShowWindow(overlay_window, SW_SHOW);
    UpdateWindow(overlay_window);

    played_without_error = vlc_playback_play_blocking(overlay_window, file_path, game_window);

    DestroyWindow(overlay_window);

    return played_without_error;
}
