/* movie_close.c: the close box during a movie, on a real window's message queue.
 *
 * The module looks at one window's queue and takes, puts back or posts messages on it, and a test
 * process can own such a window and read its queue afterwards. So this is the real case with a
 * window of the test's own standing in for the game's.
 *
 * What is pinned: without a session the request ends the movie and goes back on the queue as it
 * came, which is how the movie player has always behaved; in a session it ends nothing, is taken
 * and counted, and once the movie is over it is passed on exactly once, however often the box was
 * pressed, as WM_SYSCOMMAND with SC_CLOSE, the command a completed click produces.
 */
#include "unittest.h"

#include "movie_close.h"
#include "movie_rule.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static HWND make_window(void)
{
    WNDCLASSW window_class;

    memset(&window_class, 0, sizeof window_class);
    window_class.lpfnWndProc   = DefWindowProcW;
    window_class.hInstance     = GetModuleHandleW(NULL);
    window_class.lpszClassName = L"MovieCloseTest";
    (void)RegisterClassW(&window_class);
    return CreateWindowExW(0, L"MovieCloseTest", L"", WS_OVERLAPPEDWINDOW, 0, 0, 200, 200, NULL,
                           NULL, window_class.hInstance, NULL);
}

/* How many messages of one kind lie on the window's queue, taken off in counting. */
static unsigned take_queued(HWND window, UINT message, WPARAM wparam)
{
    MSG      found;
    unsigned count = 0u;

    while (PeekMessageW(&found, window, message, message, PM_REMOVE)) {
        if (found.wParam == wparam) {
            ++count;
        }
    }
    return count;
}

static void press_the_close_box(HWND window)
{
    (void)PostMessageW(window, WM_NCLBUTTONDOWN, HTCLOSE, MAKELPARAM(180, 10));
}

static void check_without_a_session(HWND window)
{
    movie_loop_t loop;

    ut_section("without a session the close box ends the movie and goes back as it came");

    memset(&loop, 0, sizeof loop);
    loop.exits = movie_rule_exits(MOVIE_ROLE_FREE);
    press_the_close_box(window);
    ut_check(movie_close_requested(window, &loop), "the request ends the movie");
    ut_check(loop.counts.closes_refused == 0u, "and nothing is counted as refused");
    ut_check(take_queued(window, WM_NCLBUTTONDOWN, HTCLOSE) == 1u,
             "the click is back on the queue, once, for the game's own pump");
    ut_check(!movie_close_pass_on(), "and there is nothing left to pass on afterwards");
    ut_check(take_queued(window, WM_SYSCOMMAND, SC_CLOSE) == 0u, "so nothing more is posted");
}

static void check_in_a_session(HWND window)
{
    movie_loop_t loop;

    ut_section("in a session the close box ends nothing and is passed on once, afterwards");

    memset(&loop, 0, sizeof loop);
    loop.exits = movie_rule_exits(MOVIE_ROLE_HOST);
    press_the_close_box(window);
    ut_check(!movie_close_requested(window, &loop), "the host's movie is not ended by it");
    ut_check(loop.counts.closes_refused == 1u, "the refusal is counted");
    ut_check(!movie_close_requested(window, &loop),
             "and the click is not found again on the next turn");
    ut_check(loop.counts.closes_refused == 1u, "so it is counted once");

    loop.exits = movie_rule_exits(MOVIE_ROLE_LOCKED);
    press_the_close_box(window);
    ut_check(!movie_close_requested(window, &loop), "a held client's movie is not ended by it");
    ut_check(loop.counts.closes_refused == 2u, "the second press is counted too");
    ut_check(take_queued(window, WM_NCLBUTTONDOWN, HTCLOSE) == 0u,
             "and neither click is left on the queue while the movie plays");

    ut_check(movie_close_pass_on(), "once the movie is over, the request is passed on");
    ut_check(take_queued(window, WM_SYSCOMMAND, SC_CLOSE) == 1u,
             "exactly once, for two presses, as the command a completed click produces");
    ut_check(take_queued(window, WM_NCLBUTTONDOWN, HTCLOSE) == 0u,
             "and not as the click, which DefWindowProc would take for a button still held");
    ut_check(!movie_close_pass_on(), "a second pass finds nothing to pass on");
    ut_check(take_queued(window, WM_SYSCOMMAND, SC_CLOSE) == 0u, "and posts nothing");
}

static void check_what_is_not_a_close(HWND window)
{
    movie_loop_t loop;

    ut_section("only the close box is a close request");

    memset(&loop, 0, sizeof loop);
    loop.exits = movie_rule_exits(MOVIE_ROLE_LOCKED);
    (void)PostMessageW(window, WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(50, 10));
    ut_check(!movie_close_requested(window, &loop), "a click on the caption is not one");
    ut_check(loop.counts.closes_refused == 0u, "and is not counted");
    ut_check(take_queued(window, WM_NCLBUTTONDOWN, HTCAPTION) == 1u,
             "it is left where it lies, for the game's own pump");
    ut_check(!movie_close_pass_on(), "and there is nothing to pass on");
    ut_check(!movie_close_requested(NULL, &loop) && !movie_close_requested(window, NULL),
             "no window and no loop ask nothing");
}

int main(void)
{
    HWND window = make_window();

    ut_check(window != NULL, "a window of the test's own stands in for the game's");
    if (window == NULL) {
        return ut_summary("movie_close");
    }
    check_without_a_session(window);
    check_in_a_session(window);
    check_what_is_not_a_close(window);
    DestroyWindow(window);
    return ut_summary("movie_close");
}
