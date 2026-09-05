/* vlc_playback.c: plays one file in one window until it ends, is skipped, or is closed.
 *
 * The other half of this, finding a libVLC on the machine and resolving its exports, is
 * vlc_runtime.c. They were one file and they change for different reasons: that half moves when
 * libVLC's packaging or its export names move, this one when the game's window handling or the
 * skip keys do. The table vlc_runtime_api() hands back is the seam between them.
 *
 * See vlc_playback.h for the contract and vlc_locate.c for where the DLL comes from.
 */
#include "vlc_playback.h"

#include "vlc_runtime.h"


#include "vlc_locate.h"

#include "common/logging.h"
#include "common/platform.h"

#include <process.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* How long libVLC gets to actually begin decoding before this gives up and hands the movie back to
 * the retail path. It exists because a file libVLC can open but not decode would otherwise park
 * the game's thread here forever. The cost of the wait is visible, since the overlay is on screen
 * and black for that long before the Bink version starts, so it is short. */
#define START_TIMEOUT_MS 5000u

/* Set once from the ini before any movie plays. Kept here so this layer takes one input and has no
 * opinion about where it came from. */
static bool vlc_stretch_to_window;

void vlc_playback_set_stretch(bool stretch)
{
    vlc_stretch_to_window = stretch;
}

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

/* Why the test above is ENOUGH, including under Wine, and what it cost to learn that.
 *
 * There was a period where a real Escape did not skip a movie under Wine while the controller's
 * Start button did. Start works because controller_input.c synthesises Escape with SendInput, which
 * writes straight into Wine's own input state; a key pressed by a hand has to arrive from X11
 * first, and it was not arriving at all. Two readings of the key were built on that evidence and
 * Both FAILED in the field: the game window's message queue, and raw input on a message-only window
 * with RIDEV_INPUTSINK. Neither could have worked, and the reason names the real cause.
 *
 * The key was never reaching the PROCESS. Escape opened the menus normally during play, so Wine was
 * delivering it perfectly well; it stopped only while a movie was on screen, which was the only
 * time video_overlay.c put a TOP LEVEL window up. An X11 window manager focuses a newly mapped top
 * level window, and the HWND behind that one is WS_EX_NOACTIVATE, so Wine had been handed the focus
 * for a window that refuses activation and the key went nowhere. Nothing this file could read would
 * have found it, which is why three read paths all failed the same way.
 *
 * The fix is in video_overlay.c and it is a window, not a key: under Wine the movie is drawn into a
 * CHILD of the game's own window, so no X11 window is ever created and the focus never moves.
 * FIELD CONFIRMED on Linux Mint. GetAsyncKeyState then answers exactly as it does on Windows, which
 * is why there is one test here again and no Wine-only path at all. */


/* True for the posted message that BEGINS a close by mouse, as opposed to WM_QUIT, which is what a
 * close has already turned into.
 *
 * The close box arrives as WM_NCLBUTTONDOWN on hit-test area HTCLOSE, and it is not a close yet:
 * DefWindowProc is what turns it into WM_SYSCOMMAND, then WM_CLOSE, then eventually
 * PostQuitMessage. The overlay never takes activation, so the focus window during a movie is the
 * game's, so it is addressed to exactly the window whose messages this loop drops, which means
 * DefWindowProc never runs and no WM_QUIT is ever produced. Handling only WM_QUIT would preserve a
 * quit somebody else raised and lose every quit the player raises, which is the wrong half.
 *
 * Alt+F4 is NOT handled here, and could not usefully be. WM_SYSKEYDOWN with VK_F4 used to be in
 * the range list below and could never fire: a filtered peek returns the FIRST message in its
 * range, Alt+F4 queues VK_MENU before VK_F4, and holding Alt autorepeats more behind it, so the F4
 * was never examined. An external audit found that and was right about the code.
 *
 * It was wrong about the consequence. The game IGNORES WM_CLOSE at all times: its window procedure
 * at 0x0049905E takes case 0x10 in the switch, sets the result to zero and breaks, so DefWindowProcA
 * never runs and the default destroy never happens, and the chained handlers never see it either.
 * Only WM_DESTROY calls PostQuitMessage, raised by the game's own quit path. Confirmed in play:
 * Alt+F4 does nothing during ordinary gameplay with no movie involved.
 *
 * So there was no close being lost here to restore. Detecting the combination properly was tried,
 * and it ended the movie and then posted a close the engine discarded. Making Alt+F4 genuinely
 * close the game would override a decision the engine took for itself, which is a behaviour change
 * rather than a repair and does not belong in the movie player. */
static bool is_close_request(const MSG *message)
{
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

    if (!vlc_stretch_to_window || vlc_runtime_api()->video_set_aspect_ratio == NULL) {
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
    vlc_runtime_api()->video_set_aspect_ratio(player, aspect);
}

bool vlc_playback_play_blocking(HWND window, const wchar_t *file_path, HWND game_window)
{
    char                   utf8_path[MAX_PATH * 3];   /* worst-case UTF-8 expansion of MAX_PATH */
    libvlc_media_t        *media;
    libvlc_media_player_t *player;
    bool                   started = false;
    bool                   escape_was_down;
    DWORD                  start_deadline;

    if (vlc_runtime_api()->instance == NULL || window == NULL || file_path == NULL) {
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

    media = vlc_runtime_api()->media_new_path(vlc_runtime_api()->instance, utf8_path);
    if (media == NULL) {
        log_error("libVLC refused to open %ls", file_path);
        return false;
    }
    player = vlc_runtime_api()->player_new_from_media(media);
    vlc_runtime_api()->media_release(media);   /* the player takes its own reference once created */
    if (player == NULL) {
        log_error("libVLC could not create a player for %ls", file_path);
        return false;
    }

    vlc_runtime_api()->player_set_hwnd(player, (void *)window);
    apply_scaling(player, window);
    if (vlc_runtime_api()->player_play(player) != 0) {
        log_error("libVLC refused to start %ls", file_path);
        vlc_runtime_api()->player_release(player);
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
            if (!vlc_runtime_api()->player_is_playing(player)) {
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
        } else if (vlc_runtime_api()->player_is_playing(player)) {
            started = true;
        } else if ((int32_t)(GetTickCount() - start_deadline) >= 0) {
            log_warning("libVLC did not start decoding %ls within %u ms, giving the movie back to "
                        "the retail path", file_path, (unsigned)START_TIMEOUT_MS);
            vlc_runtime_api()->player_stop(player);
            vlc_runtime_api()->player_release(player);
            return false;
        }

        if (!pump_once(window, game_window)) {
            break;
        }
    }

    vlc_runtime_api()->player_stop(player);
    vlc_runtime_api()->player_release(player);
    return true;
}

