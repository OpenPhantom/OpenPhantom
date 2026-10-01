/* movie_close.c: the close box while a movie is on screen. See the header. */
#include "movie_close.h"

#include "common/logging.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* True for the posted message that BEGINS a close by mouse, as opposed to WM_QUIT, the message a
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
 * It was wrong about the consequence. The game IGNORES WM_CLOSE at all times: its window
 * procedure at 0x0049905E takes case 0x10 in the switch, sets the result to zero and breaks, so
 * DefWindowProcA never runs and the default destroy never happens, and the chained handlers
 * never see it either. Only WM_DESTROY calls PostQuitMessage, raised by the game's own quit
 * path. Confirmed in play: Alt+F4 does nothing during ordinary gameplay with no movie involved.
 *
 * So there was no close being lost here to restore. Detecting the combination properly was tried,
 * and it ended the movie and then posted a close the engine discarded. Making Alt+F4 genuinely
 * close the game would override a decision the engine took for itself, which is a behaviour change
 * rather than a repair and does not belong in the movie player.
 *
 * All of that is the engine's own window. enhanced_resolution, once it gives the window a frame
 * with a close box, answers WM_CLOSE by shutting the game down, and then the close box and Alt+F4
 * do close it; a request dropped here would be a close the player asked for and did not get. */
static bool is_close_request(const MSG *message)
{
    return message->message == WM_NCLBUTTONDOWN && message->wParam == HTCLOSE;
}

/* The request remembered in a session, until the movie and its wait are over. Module state because
 * one movie is on screen at a time, on the game's thread. */
typedef struct close_state {
    bool     pending;
    HWND     window;    /* the game's window the request was addressed to */
    uint32_t presses;   /* how often the box was pressed while it waited */
} close_state_t;

static close_state_t close_state;

static void remember(const MSG *message)
{
    close_state.pending = true;
    close_state.window  = message->hwnd;
    ++close_state.presses;
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
 * PM_NOREMOVE answers the same question without that cost: the close request is recognised where
 * it lies, and only IT is then removed, so everything else stays exactly where it was, in order.
 *
 * The range filter keeps this cheap: the request is one non-client mouse message, so nothing else
 * is even looked at. The table has one row now that the keyboard range is gone. */
bool movie_close_requested(HWND game_window, movie_loop_t *loop)
{
    static const struct { UINT first; UINT last; } ranges[] = {
        { WM_NCLBUTTONDOWN,  WM_NCLBUTTONDOWN  }
    };
    MSG    message;
    size_t index;

    if (game_window == NULL || loop == NULL) {
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
        /* In a session the close box ends no movie. The request is taken, so it is not found
         * again on the next turn, and remembered for movie_close_pass_on once the movie is over. */
        if (!loop->exits.close_ends) {
            if (PeekMessageW(&message, game_window, ranges[index].first, ranges[index].last,
                             PM_REMOVE)) {
                remember(&message);
                ++loop->counts.closes_refused;
            }
            return false;
        }
        /* Take this one and put it straight back. Removing it first stops this returning true
         * again on the next turn before the game's pump has had a chance to run; re-posting it
         * unchanged lets the request survive to be honoured a moment later, by the engine's own
         * window procedure, on a thread that is no longer parked inside a movie. */
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

/* SC_CLOSE and not the click itself, because the click is a request only while its button is down.
 * A WM_NCLBUTTONDOWN on HTCLOSE handed to DefWindowProc after the button is up enters the modal
 * tracking of the caption button and does not come back until real mouse input arrives: measured
 * in a test window, still inside DefWindowProc two and a half seconds later, with neither SC_CLOSE
 * nor WM_CLOSE sent, and posted mouse moves and a posted button up did not release it. A posted
 * WM_SYSCOMMAND with SC_CLOSE produced exactly one WM_CLOSE. The game's window procedure hands
 * WM_SYSCOMMAND through its handlers to DefWindowProc, which sends WM_CLOSE, which is what the
 * close box would have ended in. */
bool movie_close_pass_on(void)
{
    if (!close_state.pending) {
        return false;
    }
    close_state.pending = false;
    if (close_state.window == NULL ||
        !PostMessageW(close_state.window, WM_SYSCOMMAND, SC_CLOSE, 0)) {
        log_warning("the close box was pressed during the session's movie, and the request could "
                    "not be passed on (error %lu)", (unsigned long)GetLastError());
        close_state.presses = 0u;
        return false;
    }
    log_info("the close box was pressed %u time(s) during the session's movie; the movie was not "
             "ended for it, and now that it is over the request is passed on once, so the game "
             "closes as it would have without a movie", (unsigned)close_state.presses);
    close_state.presses = 0u;
    return true;
}
