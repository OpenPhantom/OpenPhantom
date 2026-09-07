/* window_mode.c: what shape the window is asked for, checked without a window.
 *
 * Everything this feature decides is one function. The rest is SetWindowLong and SetWindowPos,
 * which cannot be tested without a desktop, so the whole of the policy is here and none of the
 * plumbing is.
 */
#include "unittest.h"

#include "window_mode.h"

#include <stdbool.h>
#include <stdint.h>

/* A 2560x1440 primary at the origin, and a secondary at a NEGATIVE origin, which is the desktop
 * that broke the monitor rule once already and is the one worth carrying here too. */
static const window_mode_rect_t primary   = {     0, 0, 2560, 1440 };
static const window_mode_rect_t secondary = { -1920, 0, 1920, 1080 };

static void test_authentic_decides_nothing(void)
{
    window_mode_rect_t out = { 1, 2, 3, 4 };

    ut_check(!window_mode_client_rect(WINDOW_MODE_AUTHENTIC, &primary, 1920, 1080, 0, 0, &out),
          "the authentic mode asks for nothing, so there is no rectangle to apply");
    ut_check(out.left == 1 && out.top == 2 && out.width == 3 && out.height == 4,
          "and it leaves the caller's rectangle untouched rather than zeroing it");
    ut_check(window_mode_style(WINDOW_MODE_AUTHENTIC) == 0u,
          "the authentic mode wants no style word, which is the signal to leave GWL_STYLE alone");
}

static void test_borderless_is_the_monitor(void)
{
    window_mode_rect_t out;

    ut_check(window_mode_client_rect(WINDOW_MODE_BORDERLESS, &primary, 1920, 1080, 0, 0, &out),
          "the borderless mode always has an answer on a real monitor");
    ut_check(out.left == 0 && out.top == 0 && out.width == 2560 && out.height == 1440,
          "borderless is the whole monitor, not the display mode the engine is rendering");

    ut_check(window_mode_client_rect(WINDOW_MODE_BORDERLESS, &secondary, 1920, 1080, 0, 0, &out),
          "and it answers on a monitor whose origin is negative");
    ut_check(out.left == -1920 && out.top == 0 && out.width == 1920 && out.height == 1080,
          "borderless takes that monitor's own origin, so the window lands on it and not at 0,0");
}

static void test_windowed_defaults_to_the_render_size(void)
{
    window_mode_rect_t out;

    ut_check(window_mode_client_rect(WINDOW_MODE_WINDOWED, &primary, 1280, 720, 0, 0, &out),
          "a windowed mode with no explicit size still has an answer");
    ut_check(out.width == 1280 && out.height == 720,
          "with no explicit size the client is the display mode, so nothing is scaled");
    ut_check(out.left == (2560 - 1280) / 2 && out.top == (1440 - 720) / 2,
          "and it is centred on the monitor the window is on");
}

static void test_windowed_takes_an_explicit_size(void)
{
    window_mode_rect_t out;

    ut_check(window_mode_client_rect(WINDOW_MODE_WINDOWED, &primary, 1920, 1080, 800, 600, &out),
          "an explicit size is accepted");
    ut_check(out.width == 800 && out.height == 600,
          "an explicit size beats the display mode, which is what the keys are for");

    ut_check(window_mode_client_rect(WINDOW_MODE_WINDOWED, &primary, 1920, 1080, 800, 0, &out),
          "one axis given and the other left at zero is still answerable");
    ut_check(out.width == 800 && out.height == 1080,
          "each axis falls back on its own, so half a setting is not half a window");
}

static void test_windowed_is_clamped_to_the_monitor(void)
{
    window_mode_rect_t out;

    ut_check(window_mode_client_rect(WINDOW_MODE_WINDOWED, &secondary, 3840, 2160, 0, 0, &out),
          "a display mode larger than the monitor is still answerable");
    ut_check(out.width == 1920 && out.height == 1080,
          "a window is never larger than the monitor, or its caption is off the top of the screen "
          "once it is centred and it cannot be dragged back");
    ut_check(out.left == -1920 && out.top == 0,
          "and clamping to exactly the monitor centres it at that monitor's origin");
}

static void test_the_size_bounds(void)
{
    window_mode_rect_t out;

    ut_check(window_mode_client_rect(WINDOW_MODE_WINDOWED, &primary, 1920, 1080, 63, 63, &out),
          "a size one under the minimum is refused rather than applied");
    ut_check(out.width == 1920 && out.height == 1080,
          "and it falls back to the display mode, not to the number that was refused");

    ut_check(window_mode_client_rect(WINDOW_MODE_WINDOWED, &primary, 1920, 17000, 0, 0, &out),
          "one implausible axis of the display mode is still answerable");
    ut_check(out.width == 1920 && out.height == 1440,
          "and only that axis falls back to the monitor: a bad number does not throw away the "
          "good one beside it");

    ut_check(window_mode_client_rect(WINDOW_MODE_WINDOWED, &primary, 1920, 1080, 64, 64, &out),
          "the minimum itself is accepted");
    ut_check(out.width == 64 && out.height == 64,
          "sixty four is a size, so it is used");

    ut_check(window_mode_client_rect(WINDOW_MODE_WINDOWED, &primary, 16385, 16385, 0, 0, &out),
          "a display mode one over the maximum is answerable");
    ut_check(out.width == 2560 && out.height == 1440,
          "an implausible mode size falls back to the monitor rather than to an invented number");
}

static void test_the_refusals(void)
{
    window_mode_rect_t out;

    ut_check(!window_mode_client_rect(WINDOW_MODE_WINDOWED, &primary, 1920, 1080, 0, 0, NULL),
          "a null destination is refused rather than written through");
    ut_check(!window_mode_client_rect(WINDOW_MODE_WINDOWED, NULL, 1920, 1080, 0, 0, &out),
          "a null monitor is refused, because there is nothing to centre on");
    ut_check(!window_mode_client_rect(WINDOW_MODE_COUNT, &primary, 1920, 1080, 0, 0, &out),
          "a mode past the end of the list is refused rather than treated as the last one");
    ut_check(!window_mode_client_rect((window_mode_kind_t)-1, &primary, 1920, 1080, 0, 0, &out),
          "a negative mode is refused too, because the setting comes from a file and nothing "
          "between the file and here narrows it to the enumeration");

    {
        const window_mode_rect_t empty = { 0, 0, 0, 0 };

        ut_check(!window_mode_client_rect(WINDOW_MODE_BORDERLESS, &empty, 1920, 1080, 0, 0, &out),
              "a monitor with no area is refused, so a failed measurement cannot become a "
              "zero-sized window");
    }
}

static void test_the_style_words(void)
{
    ut_check(window_mode_style(WINDOW_MODE_BORDERLESS) == 0x10000000u,
          "borderless is WS_VISIBLE alone, which is the style the engine already runs with, so "
          "that mode changes the geometry and nothing else");

    ut_check((window_mode_style(WINDOW_MODE_WINDOWED) & 0x00040000u) == 0u,
          "the windowed style does NOT set WS_THICKFRAME: the window procedure handles no WM_SIZE "
          "and the class has no CS_HREDRAW, so a drag-resize would repaint nothing");
    ut_check((window_mode_style(WINDOW_MODE_WINDOWED) & 0x00010000u) == 0u,
          "and it does not set WS_MAXIMIZEBOX, for the same reason");
    ut_check((window_mode_style(WINDOW_MODE_WINDOWED) & 0x00C00000u) == 0x00C00000u,
          "it does set WS_CAPTION, which is the whole point of the mode");
    ut_check((window_mode_style(WINDOW_MODE_WINDOWED) & 0x10000000u) == 0x10000000u,
          "and WS_VISIBLE, because nothing calls ShowWindow again after the window is created");
}

int main(void)
{
    ut_section("the authentic mode");
    test_authentic_decides_nothing();

    ut_section("borderless is the monitor the window is on");
    test_borderless_is_the_monitor();

    ut_section("windowed, with and without an explicit size");
    test_windowed_defaults_to_the_render_size();
    test_windowed_takes_an_explicit_size();
    test_windowed_is_clamped_to_the_monitor();

    ut_section("the bounds and the refusals");
    test_the_size_bounds();
    test_the_refusals();

    ut_section("the style words");
    test_the_style_words();

    return ut_summary("window_mode");
}
