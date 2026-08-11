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
 * root: Windows does not send WM_ACTIVATEAPP for a window becoming topmost within its OWN process,
 * and that is now the only line of defense needed. vlc_playback_play_blocking() used to also be
 * handed the game window as `exclude_from_dispatch`, pumping with hWnd=NULL and dropping whatever
 * arrived for the game window unread - a second, redundant guard against the same cross-process
 * signal this in-process design already cannot produce. It cost more than it bought: PM_REMOVE
 * takes a message off the queue whether or not it is then dispatched, so every WM_SETCURSOR,
 * WM_ACTIVATE and WM_MOUSEMOVE that arrived for the game window during a movie was silently
 * discarded rather than deferred - which is what a stray OS "loading" cursor left on screen after
 * the intro movies turned out to be. vlc_playback_play_blocking() now pumps only the overlay
 * window's own messages, so the game window's queue is untouched and whatever arrived for it is
 * still there, in order, once this call returns.
 * ============================================================================================== */
#include "video_overlay.h"

#include "vlc_playback.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"

#include <windows.h>

#include <stdbool.h>
#include <stdint.h>

#define OVERLAY_CLASS_NAME L"OpenPhantomFmvPlayerOverlay"

/* ==============================================================================================
 * THE DRAWN MENU CURSOR'S POSITION, VERIFIED LIVE IN GHIDRA AGAINST THE RUNNING RETAIL IMAGE
 *
 * modal_window_wndproc_handler's WM_MOUSEMOVE (0x200) case, at 0x00460BCC once
 * control_recentreMouse (0x0046A115) has confirmed the message is a real movement, not the echo
 * of the engine's own recentring warp:
 *
 *   00460BCC  0F BF 55 14           movsx edx, word ptr [ebp+0x14]     ; client x
 *   00460BD0  8B 05 98 6C 4B 00     mov   eax,[004B6C98]               ; g_menuCursorX
 *   00460BD5  8D 0C 10              lea   ecx,[eax+edx-0x140]          ; += client_x - 320
 *   00460BDC  89 0D 98 6C 4B 00     mov   [004B6C98],ecx
 *   ... the identical shape for Y, 004B6C9C, -0xF0 (240) ...
 *   00460C04  A1 58 FD 6C 00        mov   eax,[006CFD58]               ; g_menuOriginX
 *   00460C0C  ...                   clamp g_menuCursorX to [originX, originX+0x25F]
 *   00460C41  A1 5C FD 6C 00        mov   eax,[006CFD5C]               ; g_menuOriginY
 *   00460C4A  ...                   clamp g_menuCursorY to [originY, originY+0x1BF]
 *
 * g_menuCursorX/Y (0x004B6C98 / 0x004B6C9C) are an ACCUMULATOR, not an absolute position: every
 * real WM_MOUSEMOVE ADDS (client_x - 320, client_y - 240) to whatever they already held, then
 * clamps. g_menuOriginX/Y (0x006CFD58 / 0x006CFD5C) are the same cells pointer_cage.c already
 * documents and repoints, cross-confirming both pairs of addresses.
 *
 * This is why warping the REAL OS cursor to any particular point and letting the resulting
 * synthetic WM_MOUSEMOVE feed through this accumulator does NOT reliably put the drawn cursor
 * anywhere in particular: the result depends on whatever g_menuCursorX/Y already held going in,
 * which this file has no way to know. Two things were tried that way and neither was reliable
 * for exactly that reason - see the design-history comment above and the fmv_player README.
 *
 * The fix writes g_menuCursorX/Y DIRECTLY instead, bypassing the accumulator entirely: no message
 * has to arrive, no prior value matters, and the result is deterministic. Sanity-bounded before
 * the write - a build where these cells hold something implausible is a build these addresses do
 * not actually describe, and writing into it blind would be worse than doing nothing. */
#define ENGINE_MENU_CURSOR_X 0x004B6C98u
#define ENGINE_MENU_CURSOR_Y 0x004B6C9Cu
#define ENGINE_MENU_ORIGIN_X 0x006CFD58u
#define ENGINE_MENU_ORIGIN_Y 0x006CFD5Cu

/* The 640x480 menu space's own half-extent, the same 0x140/0xF0 the accumulator above measures
 * every delta from - the middle of the clamp range [origin, origin+0x25F]x[origin, origin+0x1BF]
 * (607x447: 640/480 less the 33-pixel margin pointer_cage.c's own header explains), comfortably
 * clear of either edge. */
#define ENGINE_MENU_HALF_WIDTH  320
#define ENGINE_MENU_HALF_HEIGHT 240

/* Plausibility bound for an origin cell: (W-640)/2 for any display mode this engine will ever be
 * handed. Not a real limit, just wide enough that a garbage read (a wrong build, a moved data
 * section) cannot pass it by accident. */
#define ENGINE_MENU_ORIGIN_MAX 8192

static void recentre_drawn_menu_cursor(void)
{
    uint32_t origin_x = 0;
    uint32_t origin_y = 0;

    if (!memory_read_u32(ENGINE_MENU_ORIGIN_X, &origin_x) ||
        !memory_read_u32(ENGINE_MENU_ORIGIN_Y, &origin_y)) {
        return;                        /* not readable at all - say nothing, this is best-effort */
    }
    if (origin_x > ENGINE_MENU_ORIGIN_MAX || origin_y > ENGINE_MENU_ORIGIN_MAX) {
        log_warning("the menu origin read back as %u,%u, outside any plausible display mode - not "
                    "recentring the drawn menu cursor this time", (unsigned)origin_x,
                    (unsigned)origin_y);
        return;
    }

    (void)patch_write_u32(ENGINE_MENU_CURSOR_X, origin_x + ENGINE_MENU_HALF_WIDTH);
    (void)patch_write_u32(ENGINE_MENU_CURSOR_Y, origin_y + ENGINE_MENU_HALF_HEIGHT);
}

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
    if (message == WM_SETCURSOR) {
        /* The game's own window procedure answers this unconditionally with SetCursor(NULL) - see
         * enhanced_resolution.c - because the front end draws its own cursor and there is no screen
         * on which the OS pointer should show. This window sits on top of that same picture while a
         * movie plays and was falling through to DefWindowProcW instead, which paints the class
         * cursor (plain IDC_ARROW) any time the real pointer is over it. Matching the game's own
         * rule here means the OS pointer is never visible over either window. */
        SetCursor(NULL);
        return TRUE;
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
bool video_overlay_start_async_init(void)
{
    if (!register_overlay_class_once()) {
        return false;
    }
    vlc_playback_init_async();
    return true;
}

bool video_overlay_is_ready(void)
{
    return vlc_playback_is_ready();
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

    played_without_error = vlc_playback_play_blocking(overlay_window, file_path);

    DestroyWindow(overlay_window);

    /* vlc_playback.c's message pump is scoped to the overlay window's own handle (see its header
     * comment), so the game window's own WM_MOUSEMOVE messages are not eaten while a movie plays,
     * they simply queue up, unprocessed, for as long as the movie runs. Left alone, that whole
     * backlog would be delivered to the engine's own accumulator (see recentre_drawn_menu_cursor()
     * above) the moment the normal pump resumes, adding a burst of stale deltas on top of - and
     * after - the deliberate write below. Draining it first means that write is the last thing
     * that touches the drawn cursor's position before the player's own next real move does. */
    {
        MSG stale;
        while (PeekMessageW(&stale, game_window, WM_MOUSEFIRST, WM_MOUSELAST, PM_REMOVE)) {
            /* discarded - stale positions from while the overlay owned the screen */
        }
    }
    recentre_drawn_menu_cursor();

    /* FIELD-CONFIRMED FIX for a stray OS "loading" cursor left on screen after the intro movies.
     * Not a hang: Task Manager showed the game's process as Running, not Not Responding, the whole
     * time the cursor was stuck, which ruled out Windows' own "still starting" shell heuristic
     * directly. What actually explained it: the RETAIL Bink path (this DLL disabled) visibly
     * flashes the screen several times before the game settles - real minimize/restore cycles,
     * consistent with this game's exclusive-mode Direct3D9 device reacting to however Bink touches
     * the display - while this DLL enabled shows none of that flashing, which is it doing exactly
     * what it was built to do (see the fmv_player README's "no flicker, no minimize" result).
     * Something early in startup, before either playback path ever runs, leaves the OS cursor
     * undone; the retail path's own incidental flashing was quietly re-triggering a full activation
     * handshake a few times before the player ever reached the menu, curing it before it was ever
     * seen. This DLL's smooth window removes that incidental cure along with the flicker it was
     * curing a symptom of, which is why the cursor problem only ever showed up with this DLL
     * installed even though its root cause is not in this file. This does not repair that root
     * cause - it replaces the accidental cure with a deliberate one, once per movie (at least twice
     * a session: the logo, then whatever plays after it). SetForegroundWindow() alone was tried
     * first and was not enough on its own - it asks Windows to reconsider which window owns the
     * foreground, but does not itself force a cursor update. SetCursor(NULL) does not wait on that:
     * it hides the cursor immediately, on this same thread, the same call the game's own window
     * procedure answers WM_SETCURSOR with. Both together are what field-testing confirmed fixed. */
    SetForegroundWindow(game_window);
    SetCursor(NULL);

    return played_without_error;
}
