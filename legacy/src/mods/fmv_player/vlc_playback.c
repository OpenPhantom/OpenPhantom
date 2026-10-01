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

#include "movie_close.h"
#include "movie_rule.h"
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

static bool escape_pressed_now(bool *was_down)
{
    bool down = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
    bool fresh = down && !*was_down && platform_foreground_is_ours();

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
 * have found it, and three read paths all failed the same way.
 *
 * The fix is in video_overlay.c and it is a window, not a key: under Wine the movie is drawn into a
 * CHILD of the game's own window, so no X11 window is ever created and the focus never moves.
 * Field confirmed on Linux Mint. GetAsyncKeyState then answers exactly as it does on Windows, so
 * there is one test here again and no Wine-only path at all. */


/* How many timer messages one turn takes at most. The session's timer runs every thirty
 * milliseconds and a turn lasts about one, so one is the usual count; the bound only keeps a timer
 * that is always due from holding the turn. */
#define THREAD_TIMERS_PER_TURN 8u

/* The session's thread timer, while a multiplayer session runs. It is a timer with no window, so
 * the overlay-scoped peek below never takes it, and without this the session is not serviced for
 * the length of the movie: a movie longer than the session's timeout ends the session. The retail
 * player's own pump dispatches every message of the thread, this one included.
 *
 * Only a timer with no window, the session's, and the overlay's own are dispatched. A due tick of
 * any other window of the thread is taken and dropped, because it cannot be left: a filtered peek
 * returns the first due timer of its range, and a foreign tick left in place is returned again on
 * every turn with the thread timer behind it never reached. Measured with a window timer and a
 * thread timer both at ten milliseconds: taken and dropped, the thread timer ran 30 times in half
 * a second; left in place, once. A peek scoped to (HWND)-1 returns no thread timer at all.
 * Dropping a tick loses nothing a timer does not lose anyway, since its next one comes at its next
 * interval, and the engine sets no timer of its own.
 *
 * A timer with no window is not necessarily the session's. The Miles sound library sets one of its
 * own on one of its driver paths, and a timer procedure of that kind runs here as the retail
 * player's pump would run it. So the count is of thread timers: nought means the session was not
 * pumped, and more than nought does not prove that it was; the multiplayer's own report says that,
 * as its timer pump count. */
void vlc_playback_pump_timers(HWND window, movie_loop_t *loop)
{
    MSG      message;
    unsigned taken;

    if (loop == NULL || !loop->exits.pump_session) {
        return;
    }
    for (taken = 0; taken < THREAD_TIMERS_PER_TURN; ++taken) {
        if (!PeekMessageW(&message, NULL, WM_TIMER, WM_TIMER, PM_REMOVE)) {
            return;
        }
        if (message.hwnd == NULL) {
            ++loop->counts.thread_timers;
            DispatchMessageW(&message);
        } else if (message.hwnd == window) {
            DispatchMessageW(&message);
        }
    }
}

/* One turn of the message pump. Returns false when the player wants out, which ends playback rather
 * than swallowing the request.
 *
 * The peek is scoped to `window`, the overlay's own handle, and not NULL. An unscoped peek runs
 * the whole thread queue, and this loop runs in-process on the game's own thread, so the game
 * window's posted traffic is on that same queue. See movie_close.c for what taking it and not
 * dispatching it actually did.
 *
 * One consequence of the scoping, stated rather than discovered later: a filtered peek does not
 * retrieve thread messages either, and WM_QUIT is a thread message with no window. It therefore
 * stays on the queue for the game's own pump, which is the right place for it and the reason this
 * function no longer has a WM_QUIT branch. */
static bool pump_once(HWND window, HWND game_window, movie_loop_t *loop)
{
    MSG message;

    if (movie_close_requested(game_window, loop)) {
        return false;
    }
    vlc_playback_pump_timers(window, loop);

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

/* The question a held movie asks every turn, after the turn has pumped: a gate is filed from the
 * session's timer, and asking before that timer has had its turn would read a gate that could not
 * have moved. True when the host is done, which ends the movie as the host's; an answer that lets
 * the player go turns the loop into this side's own movie. */
static bool host_decides(movie_loop_t *loop)
{
    movie_verdict_t verdict;

    if (loop->poll == NULL) {
        return false;
    }
    verdict = loop->poll();
    if (verdict == MOVIE_VERDICT_HOST_DONE) {
        loop->end = MOVIE_END_HOST;
        return true;
    }
    if (verdict != MOVIE_VERDICT_GO_ON) {
        movie_rule_let_go(loop);
    }
    return false;
}

/* False when the foreground is lost and that ends the movie. A loss that ends nothing is counted
 * once, on its edge. The line keeps the words it has always had, although the retail player ends
 * no movie on a lost foreground, because a comparison against older field logs knows the end by
 * them, and on a held client they are the failure the run is read for. */
static bool foreground_holds(movie_loop_t *loop, bool *was_foreground)
{
    bool foreground = platform_foreground_is_ours();

    if (!foreground && loop->exits.focus_ends) {
        log_info("the game lost the foreground during playback, ending the movie the way "
                 "the engine's own player does");
        loop->end = MOVIE_END_FOCUS;
        return false;
    }
    if (!foreground && *was_foreground) {
        ++loop->counts.focus_losses_refused;
    }
    *was_foreground = foreground;
    return true;
}

bool vlc_playback_play_blocking(HWND window, const wchar_t *file_path, HWND game_window,
                                movie_loop_t *loop)
{
    char                   utf8_path[MAX_PATH * 3];   /* worst-case UTF-8 expansion of MAX_PATH */
    libvlc_media_t        *media;
    libvlc_media_player_t *player;
    bool                   started = false;
    bool                   escape_was_down;
    bool                   was_foreground = true;
    DWORD                  start_deadline;

    if (vlc_runtime_api()->instance == NULL || window == NULL || file_path == NULL ||
        loop == NULL) {
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
            if (loop->exits.escape_ends) {
                log_info("Escape ended playback");
                loop->end = MOVIE_END_ESCAPE;
                break;
            }
            ++loop->counts.escapes_refused;
        }

        if (started) {
            if (!vlc_runtime_api()->player_is_playing(player)) {
                loop->end = MOVIE_END_NATURAL;
                break;   /* stopped on its own: the end of the file, or a rare error */
            }
            /* Losing the foreground ends the movie outside a session. The retail player does
             * not: it ends a movie at its own end or on a key and on nothing else. This player
             * does it by its own choice, because a movie that carried on playing inaudibly under
             * another program was one of the things that made it feel like a separate
             * application, and it is what every installation has been tested with. In a session
             * a lost foreground ends nothing and is counted, since a click into a fellow player's
             * window would otherwise end everybody's movie. The test is only applied once
             * playback is under way, because the foreground has not necessarily settled in the
             * moment the overlay appears. */
            if (!foreground_holds(loop, &was_foreground)) {
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

        if (!pump_once(window, game_window, loop)) {
            loop->end = MOVIE_END_CLOSE;
            break;
        }
        if (host_decides(loop)) {
            break;
        }
    }

    vlc_runtime_api()->player_stop(player);
    vlc_runtime_api()->player_release(player);
    return true;
}

void vlc_playback_hold_blocking(HWND window, HWND game_window, movie_loop_t *loop)
{
    bool escape_was_down;
    bool was_foreground = platform_foreground_is_ours();

    if (window == NULL || loop == NULL) {
        return;
    }
    escape_was_down = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
    for (;;) {
        movie_verdict_t verdict;
        bool            foreground;

        /* Pumped first and asked second, as in the movie's own loop above. */
        (void)pump_once(window, game_window, loop);   /* a close request is remembered there */
        verdict = loop->poll != NULL ? loop->poll() : MOVIE_VERDICT_NOT_RUNNING;
        if (verdict == MOVIE_VERDICT_HOST_DONE) {
            loop->end = MOVIE_END_HOST;
            return;
        }
        if (verdict != MOVIE_VERDICT_GO_ON) {
            loop->end = MOVIE_END_ALONE;
            return;
        }
        if (escape_pressed_now(&escape_was_down)) {
            ++loop->counts.escapes_refused;
        }
        foreground = platform_foreground_is_ours();
        if (!foreground && was_foreground) {
            ++loop->counts.focus_losses_refused;
        }
        was_foreground = foreground;
    }
}

