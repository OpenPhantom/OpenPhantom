/* pointer_cage.c: the arithmetic behind the drawn menu cursor's clamp, without a display.
 *
 * The whole feature is four immediates, and only one thing in it is a calculation: what the
 * clamp should be for a given canvas. Two properties matter and neither is obvious from the
 * expression:
 *
 *   1. at 640x480 the computed clamp must equal the constants the engine SHIPS with. That is what
 *      makes this safe to default on, on the mode the engine was written for, the patch is the
 *      identity.
 *   2. a mode too small to hold the cursor must be refused rather than turned into a clamp with
 *      zero or negative extent. The size accessor reports zeroes before a mode is configured and
 *      leaves the globals negative on shutdown, so this is a real input, not a hypothetical one.
 *
 * Nothing here touches the game. The opcode checks, the read-back of the two origin operands and
 * the one write step are not testable without a live image and are NOT covered by this file.
 * Neither is the resize: that the clamp is rewritten absolutely from the new canvas, not adjusted
 * from the old one, is a property of the write into the resolved site, and the arithmetic below
 * has no state to accumulate, so calling it twice proves nothing about the write.
 */
#include "unittest.h"

#include "pointer_cage.h"


/* The two clamps in the retail block, as immediates: 0x25F and 0x1BF. */
#define SHIPPED_CLAMP_WIDTH  0x25F
#define SHIPPED_CLAMP_HEIGHT 0x1BF

static void check_extent(int width, int height, int expect_ok,
                         int expect_clamp_width, int expect_clamp_height, const char *what)
{
    int clamp_width  = -12345;
    int clamp_height = -12345;
    int ok = pointer_cage_extent(width, height, POINTER_CAGE_SHIPPED_CURSOR, &clamp_width,
                                 &clamp_height) ? 1 : 0;

    if (ok != expect_ok) {
        ut_checkf(0, "%s (accepted=%d, expected %d)", what, ok, expect_ok);
        return;
    }
    if (!ok) {
        /* A refusal must leave the outputs alone, or a caller that ignores the return value
         * writes a garbage clamp into the engine. */
        ut_checkf(clamp_width == -12345 && clamp_height == -12345,
                  "%s (a refusal must not write to its outputs)", what);
        return;
    }
    ut_checkf(clamp_width == expect_clamp_width && clamp_height == expect_clamp_height,
              "%s (got %dx%d, expected %dx%d)",
              what, clamp_width, clamp_height, expect_clamp_width, expect_clamp_height);
}

/* The property that makes this safe to default on. */
static void test_identity_at_the_shipped_mode(void)
{
    int clamp_width = 0;
    int clamp_height = 0;

    ut_check(pointer_cage_extent(640, 480, POINTER_CAGE_SHIPPED_CURSOR, &clamp_width,
                                 &clamp_height),
          "the mode the engine was written for is accepted");
    ut_check(clamp_width == SHIPPED_CLAMP_WIDTH,
          "at 640x480 the computed horizontal clamp IS the engine's own 0x25F");
    ut_check(clamp_height == SHIPPED_CLAMP_HEIGHT,
          "at 640x480 the computed vertical clamp IS the engine's own 0x1BF");
}

static void test_common_modes(void)
{
    check_extent(1920, 1080, 1, 1920 - POINTER_CAGE_MARGIN, 1080 - POINTER_CAGE_MARGIN,
                 "1920x1080 gives the whole screen less the cursor quad");
    check_extent(1280, 1024, 1, 1280 - POINTER_CAGE_MARGIN, 1024 - POINTER_CAGE_MARGIN,
                 "1280x1024, the one non-4:3 mode the engine accepts by name");
    check_extent(800, 600, 1, 800 - POINTER_CAGE_MARGIN, 600 - POINTER_CAGE_MARGIN,
                 "800x600");
    check_extent(3840, 2160, 1, 3840 - POINTER_CAGE_MARGIN, 2160 - POINTER_CAGE_MARGIN,
                 "3840x2160 does not overflow or saturate");
}

/* The cage must always be somewhere the cursor can actually be. */
static void test_refusals(void)
{
    check_extent(0, 0, 0, 0, 0, "the zeroes reported before a mode is configured are refused");
    check_extent(-1, -1, 0, 0, 0, "the negative sizes left on shutdown are refused");
    check_extent(640, 0, 0, 0, 0, "a valid width with a zero height is still refused");
    check_extent(0, 480, 0, 0, 0, "a zero width with a valid height is still refused");
    check_extent(POINTER_CAGE_MARGIN, POINTER_CAGE_MARGIN, 0, 0, 0,
                 "a mode exactly as small as the margin is refused, the clamp would be zero");
}

/* One pixel either side of the boundary, because that is where an off-by-one lives. */
static void test_the_boundary(void)
{
    check_extent(POINTER_CAGE_MARGIN + 1, POINTER_CAGE_MARGIN + 1, 1, 1, 1,
                 "the smallest mode that can hold a cursor gives a clamp of exactly 1");
    check_extent(POINTER_CAGE_MARGIN, POINTER_CAGE_MARGIN + 1, 0, 0, 0,
                 "one pixel narrower is refused");
    check_extent(POINTER_CAGE_MARGIN + 1, POINTER_CAGE_MARGIN, 0, 0, 0,
                 "one pixel shorter is refused");
}

/* The quad the cursor is drawn as grows with the menu canvas: menu_scale_refit.c writes
 * round(32 * ratio_y) into swpic_drawCursor, and the cursor's position is the quad's top left
 * corner. So the cage has to stop the quad's own size short of the far edges, or the quad hangs
 * past the canvas onto pixels nothing repaints. With the shipped margin of 33 a 64 pixel cursor
 * reached 31 pixels past a 1280x960 canvas. */
static void test_a_scaled_cursor_stays_on_its_canvas(void)
{
    static const struct {
        int width;
        int height;
        int cursor;
    } CASES[] = {
        { 640,  480,  32 },   /* MenuScale 1: the engine as shipped */
        { 1280, 960,  64 },   /* MenuScale 2 */
        { 1440, 1080, 72 },   /* 2.25, and the automatic ratio at 1440x1080 */
        { 1920, 1440, 96 },   /* MenuScale 3 */
        { 1920, 1080, 72 },   /* the automatic ratio at 1920x1080: 3 across, 2.25 down */
        { 3840, 2160, 127 },  /* the largest quad a signed byte immediate holds */
    };
    size_t index;

    for (index = 0; index < sizeof CASES / sizeof CASES[0]; ++index) {
        int clamp_width  = 0;
        int clamp_height = 0;
        int ok = pointer_cage_extent(CASES[index].width, CASES[index].height,
                                     CASES[index].cursor, &clamp_width, &clamp_height) ? 1 : 0;

        ut_checkf(ok && clamp_width + CASES[index].cursor <= CASES[index].width - 1 &&
                      clamp_height + CASES[index].cursor <= CASES[index].height - 1,
                  "a %d pixel cursor at the far corner of a %dx%d canvas stays on it: the cage "
                  "is %dx%d, so the quad ends at %d,%d",
                  CASES[index].cursor, CASES[index].width, CASES[index].height, clamp_width,
                  clamp_height, clamp_width + CASES[index].cursor,
                  clamp_height + CASES[index].cursor);
    }
}

int main(void)
{
    test_identity_at_the_shipped_mode();
    test_common_modes();
    test_refusals();
    test_the_boundary();
    test_a_scaled_cursor_stays_on_its_canvas();

    return ut_summary("pointer_cage");
}
