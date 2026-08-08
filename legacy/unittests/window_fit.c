/* window_fit.c: which monitor the game lands on, checked without a second display.
 *
 * This is the rule that was wrong in the field. "The smallest monitor that still fits" sent a
 * 640x480 window from a 2560x1440 primary onto a 1920x1080 secondary at -1920,0, and the log
 * recorded it doing so. Every case below is one sentence of the corrected rule, and the first
 * group is the regression itself.
 */
#include "unittest.h"

#include "window_fit.h"

#include <stdbool.h>
#include <stdint.h>

/* The desktop from the field report: a 2560x1440 primary at 0,0 and a 1920x1080 secondary whose
 * origin is NEGATIVE, which is what put the engine's fixed screen point 320,240 on the wrong
 * display once the window had been moved there. */
static const window_fit_monitor_t field_desktop[] = {
    {     0, 0, 2560, 1440 },
    { -1920, 0, 1920, 1080 }
};
#define FIELD_PRIMARY   0
#define FIELD_SECONDARY 1

static void test_the_regression(void)
{
    ut_check(window_fit_choose_monitor(field_desktop, 2, FIELD_PRIMARY, 640, 480) == FIELD_PRIMARY,
          "a 640x480 mode stays on the 1440p monitor the window is already on");

    ut_check(window_fit_choose_monitor(field_desktop, 2, FIELD_PRIMARY, 2560, 1440) == FIELD_PRIMARY,
          "a mode that fills the current monitor stays on it");

    ut_check(window_fit_choose_monitor(field_desktop, 2, FIELD_SECONDARY, 640, 480) == FIELD_SECONDARY,
          "a window already on the secondary is not dragged back to the primary");

    /* The one case in which moving IS right: the monitor the window is on cannot show the mode. */
    ut_check(window_fit_choose_monitor(field_desktop, 2, FIELD_SECONDARY, 2560, 1440) == FIELD_PRIMARY,
          "a 1440p mode moves off the 1080p monitor, because that one cannot show it");
}

static void test_without_a_current_monitor(void)
{
    /* Before the window exists there is no "already on", and the old preferences take over. */
    ut_check(window_fit_choose_monitor(field_desktop, 2, WINDOW_FIT_NO_MONITOR, 1920, 1080)
              == FIELD_SECONDARY,
          "with no current monitor an exact size match wins");

    ut_check(window_fit_choose_monitor(field_desktop, 2, WINDOW_FIT_NO_MONITOR, 800, 600)
              == FIELD_SECONDARY,
          "with no current monitor and no exact match, the smallest that fits wins");

    ut_check(window_fit_choose_monitor(field_desktop, 2, WINDOW_FIT_NO_MONITOR, 3840, 2160)
              == WINDOW_FIT_NO_MONITOR,
          "a mode no monitor can show reports exactly that");
}

static void test_exact_match_does_not_beat_the_current_monitor(void)
{
    /* A 1920x1080 mode on a 1440p primary: the secondary matches it exactly, and the window still
     * does not move. Being teleported to another display is a bigger surprise to the player than a
     * window that is not flush with the edges of its own monitor. */
    ut_check(window_fit_choose_monitor(field_desktop, 2, FIELD_PRIMARY, 1920, 1080) == FIELD_PRIMARY,
          "an exact match elsewhere does NOT pull the window off the monitor it is on");
}

static void test_degenerate_input(void)
{
    static const window_fit_monitor_t one[] = { { 0, 0, 1024, 768 } };

    ut_check(window_fit_choose_monitor(NULL, 2, 0, 640, 480) == WINDOW_FIT_NO_MONITOR,
          "no monitor array is not a crash");
    ut_check(window_fit_choose_monitor(one, 0, 0, 640, 480) == WINDOW_FIT_NO_MONITOR,
          "an empty monitor list is not a crash");
    ut_check(window_fit_choose_monitor(one, 1, 0, 0, 480) == WINDOW_FIT_NO_MONITOR,
          "a zero width is refused rather than treated as 'fits anywhere'");
    ut_check(window_fit_choose_monitor(one, 1, 0, 640, 0) == WINDOW_FIT_NO_MONITOR,
          "a zero height is refused rather than treated as 'fits anywhere'");

    /* An out-of-range index must not be believed and must not be read from either. */
    ut_check(window_fit_choose_monitor(one, 1, 7, 640, 480) == 0,
          "a current index past the end of the list falls back to the normal rule");
    ut_check(window_fit_choose_monitor(one, 1, -3, 640, 480) == 0,
          "a negative current index other than the named constant is treated the same way");
}

static void test_the_single_monitor_desktop(void)
{
    static const window_fit_monitor_t one[] = { { 0, 0, 1920, 1080 } };

    ut_check(window_fit_choose_monitor(one, 1, 0, 640, 480) == 0,
          "one monitor, a smaller mode: that monitor");
    ut_check(window_fit_choose_monitor(one, 1, 0, 1920, 1080) == 0,
          "one monitor, its own size: that monitor");
    ut_check(window_fit_choose_monitor(one, 1, 0, 2560, 1440) == WINDOW_FIT_NO_MONITOR,
          "one monitor, a larger mode: nothing can show it, and that is reported");
}

int main(void)
{
    test_the_regression();
    test_without_a_current_monitor();
    test_exact_match_does_not_beat_the_current_monitor();
    test_degenerate_input();
    test_the_single_monitor_desktop();

    return ut_summary("window_fit");
}
