/* cursor_anchor.c: the coordinate arithmetic of the pointer anchor, without the game.
 *
 * There is exactly one piece of pure logic in this feature and it is the piece the defect turns
 * on: a window message packs the pointer's CLIENT position into one dword as two SIGNED 16-bit
 * values, and the engine reads them with `movsx`.
 *
 * The sign matters here more than anywhere else in this DLL. Before the repair the pointer was
 * warped to a screen point outside the client area, so the coordinates coming back were large and
 * positive (client x = 2240 on a window at screen -1920). After it, the pointer sits in the middle
 * of the client area and every push past the left or top edge arrives NEGATIVE. Reading those as
 * unsigned would turn "one pixel left of the edge" into 65535 and the anchor would chase its own
 * tail, so the two functions below are checked at every boundary that exists.
 */
#include "unittest.h"

#include "cursor_anchor.h"

#include <stdint.h>

static void check_word(uint32_t packed, int expected, const char *what)
{
    int actual = cursor_anchor_signed_word(packed);

    ut_checkf(actual == expected, "%s (got %d, expected %d)", what, actual, expected);
}

static uint32_t pack(int x, int y)
{
    return ((uint32_t)y & 0xFFFFu) << 16 | ((uint32_t)x & 0xFFFFu);
}

static void test_sign_extension(void)
{
    check_word(0x0000u,        0, "zero");
    check_word(0x0001u,        1, "one");
    check_word(0x0140u,      320, "the engine's centre x");
    check_word(0x00F0u,      240, "the engine's centre y");
    check_word(0x7FFFu,    32767, "the largest positive 16-bit value");
    check_word(0x8000u,   -32768, "the sign boundary, one past the largest positive");
    check_word(0xFFFFu,       -1, "minus one");
    check_word(0xFF9Cu,     -100, "a hundred pixels left of the client edge");

    /* The high half must never leak into the low one: the engine masks with 0xFFFF before its
     * movsx, and so does this. */
    check_word(0xABCD0140u,  320, "the high half is ignored");
    check_word(0xFFFF0001u,    1, "a negative high half does not make the low half negative");
}

static void test_centre_predicate(void)
{
    ut_check(cursor_anchor_point_is_centre(pack(320, 240)),
          "(320,240) is the centre");
    ut_check(!cursor_anchor_point_is_centre(pack(321, 240)),
          "one pixel right of the centre is not the centre");
    ut_check(!cursor_anchor_point_is_centre(pack(320, 241)),
          "one pixel below the centre is not the centre");
    ut_check(!cursor_anchor_point_is_centre(pack(0, 0)),
          "the client origin is not the centre");

    /* The two coordinates that the un-repaired warp produced on the reported setup: the window is
     * on a monitor at screen -1920,0, so a pointer warped to screen (320,240) reports client
     * x = 2240. That must NOT read as centred; it is what made the early-out unreachable. */
    ut_check(!cursor_anchor_point_is_centre(pack(2240, 240)),
          "client x 2240 (screen 320 on a window at -1920) is not the centre");

    /* And the mirror image, a monitor to the LEFT of the primary would give a negative one. */
    ut_check(!cursor_anchor_point_is_centre(pack(-1600, 240)),
          "a negative client x is not the centre");

    /* The one case a naive unsigned compare would get wrong: 320 - 65536 packs to the same low
     * word as 320 does only if the sign is dropped, and here it must not be. */
    ut_check(!cursor_anchor_point_is_centre(pack(320, -240)),
          "a negative y with the right x is not the centre");
}

static void test_the_whole_transit(void)
{
    /* Every coordinate a 16-bit client field can carry, in both halves, must round-trip through
     * the packing and the sign extension. This is the guarantee the anchor needs: whatever the
     * message says, we read exactly what the engine's movsx reads. */
    int value;
    int mismatches = 0;

    for (value = -32768; value <= 32767; ++value) {
        if (cursor_anchor_signed_word(pack(value, 0)) != value ||
            cursor_anchor_signed_word(pack(0, value) >> 16) != value) {
            ++mismatches;
        }
    }
    ut_check(mismatches == 0, "all 65536 client coordinates survive packing in both halves");
}

int main(void)
{
    test_sign_extension();
    test_centre_predicate();
    test_the_whole_transit();

    return ut_summary("cursor_anchor");
}
