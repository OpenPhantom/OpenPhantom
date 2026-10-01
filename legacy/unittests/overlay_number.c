/* overlay_number.c: a number row's arithmetic, held at absolute values.
 *
 * Five floats in and an answer out, which is the whole reason this file exists apart from the
 * groups: a test that drives a real row can only ask whether the two paths through it agree, and
 * two paths agreeing is what a mutation that wrongs both of them survives. The numbers below are
 * written out by hand (a quarter of the way up a track from 1.0 to 2.5 is 1.375 and not 2.125),
 * so a swapped pair of ends is red here even when everything downstream is consistently wrong.
 *
 * The three rows it drives are the three shapes the panel has: a band that starts above zero
 * (the draw distance), one that starts at a fraction (the subtitle size) and one whose ends come
 * out of a settings file and can therefore be equal (the field of view).
 */
#include "unittest.h"

#include <stddef.h>

#include "overlay_number.h"

#include <stdbool.h>
#include <stdint.h>

/* The draw distance's own numbers, and the two it is worth having a second row for. */
static const overlay_number_t DRAW_DISTANCE = { 1.0f, 2.5f, 0.01f, 0.10f, 1.0f, true };
static const overlay_number_t SUBTITLES     = { 0.50f, 3.00f, 0.01f, 0.10f, 1.00f, true };
static const overlay_number_t NO_STANDARD   = { 0.0f, 10.0f, 1.0f, 5.0f, 0.0f, false };

static bool near(float a, float b)
{
    const float d = a - b;

    return (d < 0.0005f) && (d > -0.0005f);
}

static void test_a_fraction_and_a_value(void)
{
    float f = -1.0f;
    float v = -1.0f;

    ut_section("a fraction of a track, and a value along it");
    ut_check(overlay_number_value_at(&DRAW_DISTANCE, 0.0f, &v) && near(v, 1.0f),
             "the left end of the track is the row's minimum");
    ut_check(overlay_number_value_at(&DRAW_DISTANCE, 1.0f, &v) && near(v, 2.5f),
             "and the right end is its maximum");
    ut_check(overlay_number_value_at(&DRAW_DISTANCE, 0.25f, &v) && near(v, 1.375f),
             "a quarter of the way up is a quarter of the way up, not three quarters");
    ut_check(overlay_number_value_at(&SUBTITLES, 0.20f, &v) && near(v, 1.00f),
             "and on a band that starts at a half, a fifth of the track is 1.00");

    ut_check(overlay_number_fraction_of(&DRAW_DISTANCE, 1.375f, &f) && near(f, 0.25f),
             "back the other way, the same quarter");
    ut_check(overlay_number_fraction_of(&DRAW_DISTANCE, 1.0f, &f) && near(f, 0.0f),
             "the minimum sits at the left end");
    ut_check(overlay_number_fraction_of(&DRAW_DISTANCE, 2.5f, &f) && near(f, 1.0f),
             "and the maximum at the right one");

    /* A value off the scale is left honest on the row above, but the HANDLE belongs on the
     * track: a fraction past the end draws it hanging off one. */
    ut_check(overlay_number_fraction_of(&DRAW_DISTANCE, 4.0f, &f) && near(f, 1.0f),
             "a value past the end puts the handle at the end and not past it");
    ut_check(overlay_number_fraction_of(&DRAW_DISTANCE, -3.0f, &f) && near(f, 0.0f),
             "and one below the start puts it at the start");
    ut_check(overlay_number_value_at(&DRAW_DISTANCE, 2.0f, &v) && near(v, 2.5f),
             "a fraction past the end of the track reads as its end");
}

/* A row whose ends come out of a settings file has somebody who can set them equal, and the
 * whole of this file divides by the difference. */
static void test_a_track_with_no_length(void)
{
    const overlay_number_t flat = { 90.0f, 90.0f, 1.0f, 5.0f, 90.0f, true };
    const overlay_number_t back_to_front = { 120.0f, 60.0f, 1.0f, 5.0f, 90.0f, true };
    float                  out = 7.0f;

    ut_section("a track whose two ends are the same");
    ut_check(!overlay_number_usable(&flat), "two equal ends are not a track");
    ut_check(!overlay_number_usable(&back_to_front), "nor are two in the wrong order");
    ut_check(!overlay_number_usable(NULL), "and neither is nothing at all");
    ut_check(!overlay_number_value_at(&flat, 0.5f, &out) && out == 7.0f,
             "no value comes off it, and nothing is written through the caller's float");
    ut_check(!overlay_number_fraction_of(&flat, 90.0f, &out) && out == 7.0f,
             "no fraction either");
    ut_check(!overlay_number_stepped(&flat, 0.5f, 1, false, &out) && out == 7.0f,
             "and no press moves anything");
    ut_check(!overlay_number_standard(&flat, &out) && out == 7.0f, "nor does Default");
}

static void test_one_press(void)
{
    float f = 0.0f;
    float v = 0.0f;

    ut_section("one press of a sideways key, and one with the modifier");
    /* From the left end: one press is one step up the VALUE, which on this row is a hundredth
     * of a scale that runs over one and a half. */
    ut_check(overlay_number_stepped(&DRAW_DISTANCE, 0.0f, 1, false, &f) &&
                 overlay_number_value_at(&DRAW_DISTANCE, f, &v) && near(v, 1.01f),
             "one press up from 1.00 is 1.01");
    ut_check(overlay_number_stepped(&DRAW_DISTANCE, 0.0f, 1, true, &f) &&
                 overlay_number_value_at(&DRAW_DISTANCE, f, &v) && near(v, 1.10f),
             "and with the modifier held it is 1.10, the row's own larger press");

    ut_check(overlay_number_fraction_of(&DRAW_DISTANCE, 2.00f, &f) &&
                 overlay_number_stepped(&DRAW_DISTANCE, f, -1, false, &f) &&
                 overlay_number_value_at(&DRAW_DISTANCE, f, &v) && near(v, 1.99f),
             "a press the other way takes one step off");
    ut_check(overlay_number_fraction_of(&DRAW_DISTANCE, 2.00f, &f) &&
                 overlay_number_stepped(&DRAW_DISTANCE, f, -1, true, &f) &&
                 overlay_number_value_at(&DRAW_DISTANCE, f, &v) && near(v, 1.90f),
             "and with the modifier, ten of them");

    ut_section("a press at either end of the track");
    ut_check(overlay_number_stepped(&DRAW_DISTANCE, 1.0f, 1, false, &f) && near(f, 1.0f),
             "at the top end a press up leaves the value where it is");
    ut_check(overlay_number_stepped(&DRAW_DISTANCE, 0.0f, -1, false, &f) && near(f, 0.0f),
             "and at the bottom end a press down does");
    /* Clamped as a VALUE and not as a fraction: a coarse press one step short of the end has to
     * land ON the end and not at a fraction that reads back as a value past it. */
    ut_check(overlay_number_fraction_of(&DRAW_DISTANCE, 2.45f, &f) &&
                 overlay_number_stepped(&DRAW_DISTANCE, f, 1, true, &f) &&
                 overlay_number_value_at(&DRAW_DISTANCE, f, &v) && near(v, 2.5f),
             "a large press that would overshoot the end stops exactly on it");

    ut_section("a row with no press size");
    {
        const overlay_number_t no_step = { 0.0f, 1.0f, 0.0f, 0.0f, 0.5f, true };

        f = 3.0f;
        ut_check(!overlay_number_stepped(&no_step, 0.5f, 1, false, &f) && f == 3.0f,
                 "nothing is stepped, and the caller's float is left alone");
        ut_check(!overlay_number_stepped(&DRAW_DISTANCE, 0.5f, 0, false, &f) && f == 3.0f,
                 "and a press of neither direction moves nothing");
    }
}

/* Default is what the row had with nothing set, and on two of the three rows in the picture group
 * that is NOT an end of the track. */
static void test_default(void)
{
    float f = 0.0f;
    float v = 0.0f;

    ut_section("where Default puts the handle");
    ut_check(overlay_number_standard(&SUBTITLES, &f) &&
                 overlay_number_value_at(&SUBTITLES, f, &v) && near(v, 1.00f),
             "the subtitle size goes back to 1.00, which is a fifth up its own track and neither "
             "of its ends");
    ut_check(!near(f, 0.0f) && !near(f, 1.0f),
             "so the handle lands in the middle of the track and not on an end of it");
    ut_check(overlay_number_standard(&DRAW_DISTANCE, &f) &&
                 overlay_number_value_at(&DRAW_DISTANCE, f, &v) && near(v, 1.0f),
             "the draw distance goes back to 1.0, which is what an untouched installation reads");

    /* The field of view, as overlay_picture.c states it: its ends are the track and its standard
     * is the base variable_fov published, which is nothing like either end. */
    {
        const overlay_number_t fov = { 60.0f, 120.0f, 1.0f, 5.0f, 75.0f, true };

        ut_check(overlay_number_standard(&fov, &f) &&
                     overlay_number_value_at(&fov, f, &v) && near(v, 75.0f),
                 "the field of view goes back to the width with no offset, not to the low end of "
                 "its track");
        ut_check(!near(v, 60.0f), "which is the whole of the difference, and 60 is the low end");
    }

    f = 9.0f;
    ut_check(!overlay_number_standard(&NO_STANDARD, &f) && f == 9.0f,
             "a row that has no standard gets no Default, rather than one that puts it at zero");
}

/* What the end of a track row gives up as the panel narrows. A track that cannot be grabbed is
 * worse than a button that is not there. */
static void test_what_fits(void)
{
    ut_section("the end of a track row, as the room runs out");
    ut_check(overlay_number_fit(100.0f, 20.0f, 10.0f, 20.0f, 2.0f, true) ==
                 OVERLAY_NUMBER_FIT_BOTH,
             "with room to spare, the number and the Default");
    ut_check(overlay_number_fit(40.0f, 20.0f, 10.0f, 20.0f, 2.0f, true) ==
                 OVERLAY_NUMBER_FIT_VALUE,
             "the Default goes first, because the track has to stay long enough to grab");
    ut_check(overlay_number_fit(25.0f, 20.0f, 10.0f, 20.0f, 2.0f, true) ==
                 OVERLAY_NUMBER_FIT_NONE,
             "and then the number, rather than the track shrinking under its least length");
    ut_check(overlay_number_fit(100.0f, 20.0f, 10.0f, 20.0f, 2.0f, false) ==
                 OVERLAY_NUMBER_FIT_VALUE,
             "a row with no standard never shows a Default however much room there is");
    /* Exactly at the boundary: 20 of track, a gap, 10 of number, a gap, 20 of Default is 54. */
    ut_check(overlay_number_fit(54.0f, 20.0f, 10.0f, 20.0f, 2.0f, true) ==
                 OVERLAY_NUMBER_FIT_BOTH,
             "the last width that holds both holds both");
    ut_check(overlay_number_fit(53.9f, 20.0f, 10.0f, 20.0f, 2.0f, true) ==
                 OVERLAY_NUMBER_FIT_VALUE,
             "and one below it holds the number alone");
}

/* How far outside its ends a track can still be taken hold of. A track from 200 to 300 at the
 * font's own height, with the number drawn half a height past the end, which is the panel's own
 * TRACK_GAP: the number therefore starts at 308 and the button after it.
 *
 * The forward end is what this is for. A grab writes AT ONCE and through no throttle, so a press
 * that takes the track past its end sets the row to its maximum on the spot; with a text height
 * of slack at both ends the first eight pixels of the number did exactly that, and the row's
 * value was gone before anybody knew they had missed. */
static void test_how_far_a_grab_reaches(void)
{
    ut_section("how far outside the track a press still takes hold of it");
    ut_check(overlay_number_grabs(250.0f, 200.0f, 300.0f, 16.0f, 8.0f),
             "a press on the track takes it");
    ut_check(overlay_number_grabs(200.0f, 200.0f, 300.0f, 16.0f, 8.0f) &&
                 overlay_number_grabs(300.0f, 200.0f, 300.0f, 16.0f, 8.0f),
             "and so does one on either end exactly");

    ut_check(overlay_number_grabs(185.0f, 200.0f, 300.0f, 16.0f, 8.0f),
             "a text height behind it takes it too: the handle is drawn centred on the end, so "
             "half of it is back there, and nothing else is drawn on that part of the row");
    ut_check(!overlay_number_grabs(183.0f, 200.0f, 300.0f, 16.0f, 8.0f),
             "further back than that is the row's indent, where nothing happens");

    ut_check(overlay_number_grabs(307.0f, 200.0f, 300.0f, 16.0f, 8.0f),
             "the gap between the track and the number still takes it, for the same half handle");
    ut_check(!overlay_number_grabs(309.0f, 200.0f, 300.0f, 16.0f, 8.0f),
             "but the number does NOT: with a text height of slack at this end too, a press "
             "there would take the track and put the row at its maximum, unthrottled");
    ut_check(!overlay_number_grabs(315.0f, 200.0f, 300.0f, 16.0f, 8.0f),
             "and neither does the rest of it, as far as a text height of slack would reach");
}

/* The columns on the right of a track row, at absolute numbers.
 *
 * A panel at the font's own height: H is 16, the track begins 36 in, the inner right margin is
 * at 300, the shortest track worth grabbing is 6 H, the gap is half an H, the number column is
 * seven cells of 8 and the Default chip is 72 wide. Everything below is worked out by hand from
 * those and written down as the number it comes to, because a check that recomputes the formula
 * it is checking agrees with itself and proves nothing.
 *
 * What this exists to catch. The painter used to measure THIS ROW'S number and put the track's
 * end, the number and Default where that measurement left them, so three rows reading 1.21,
 * 0.05 and 240 had three different track ends and three different Default positions. The number
 * column is a width of its own now and the row's own number is not an argument here at all.
 * Hand a row's number width in again and the first check below goes red. */
static void test_the_columns_on_the_right_of_a_track(void)
{
    const float x0 = 36.0f;
    const float least = 96.0f;      /* 6 H */
    const float gap = 8.0f;         /* 0.5 H */
    const float number_w = 56.0f;   /* seven cells */
    const float default_w = 72.0f;
    overlay_number_columns_t col;

    ut_section("the number stands in a column of its own, and the track ends where it ends");

    /* 264 of room, and 96 + 8 + 56 + 8 + 72 is 240, so both the number and Default fit. */
    ut_check(overlay_number_columns(x0, 300.0f, least, number_w, default_w, gap, true, &col),
             "a panel with room for all three answers");
    ut_check(col.fit == OVERLAY_NUMBER_FIT_BOTH, "and it answers with both");
    ut_near(col.default_x1, 300.0f, 0.001f, "Default ends on the margin");
    ut_near(col.default_x0, 228.0f, 0.001f, "and begins 72 short of it, its own width");
    ut_near(col.number_x1, 220.0f, 0.001f, "the number column ends a gap short of Default");
    ut_near(col.track_x1, 156.0f, 0.001f, "and the track ends a column and a gap short of that");
    ut_near(col.number_x1 - number_w, col.track_x1 + gap, 0.001f,
            "the column exactly fills what is between them: no row's own number is in this sum");

    /* Every number a row can show is right aligned against number_x1, and the widest of them
     * still clears the track by the gap. Four widths, from a three cell 240 to a full column. */
    {
        const float widths[] = { 24.0f, 32.0f, 40.0f, 56.0f };
        size_t      i;

        for (i = 0; i < sizeof widths / sizeof widths[0]; ++i) {
            ut_checkf(col.number_x1 - widths[i] >= col.track_x1 + gap - 0.001f,
                      "a number %u wide sits inside the column and clears the track",
                      (unsigned)widths[i]);
        }
    }

    /* A row with no standard has no Default, and then the column ends on the margin itself. */
    ut_check(overlay_number_columns(x0, 300.0f, least, number_w, default_w, gap, false, &col),
             "a row with nothing to put back still answers");
    ut_check(col.fit == OVERLAY_NUMBER_FIT_VALUE, "with the number alone");
    ut_near(col.number_x1, 300.0f, 0.001f, "the column ends on the margin");
    ut_near(col.track_x1, 236.0f, 0.001f, "and the track ends a column and a gap short of it");
    ut_near(col.default_x0, col.default_x1, 0.001f,
            "and the Default box has no width at all, which is how the hit test is told");

    /* 144 of room: not the 240 both want, not the 160 the number alone wants. The track keeps
     * it, because a track that cannot be grabbed is worse than a number that is not shown. */
    ut_check(overlay_number_columns(x0, 180.0f, least, number_w, default_w, gap, true, &col),
             "a narrow panel still answers");
    ut_check(col.fit == OVERLAY_NUMBER_FIT_NONE, "with the track alone");
    ut_near(col.track_x1, 180.0f, 0.001f, "which then runs the whole way to the margin");
    ut_near(col.default_x0, col.default_x1, 0.001f, "and there is no Default box to press");

    /* Exactly the 160 the number alone needs, and not a unit more. */
    ut_check(overlay_number_columns(x0, 196.0f, least, number_w, default_w, gap, true, &col) &&
             col.fit == OVERLAY_NUMBER_FIT_VALUE,
             "room for the number to the unit is room for the number");
    ut_near(col.track_x1, 132.0f, 0.001f, "and the track is left the 96 it asked for");

    ut_check(!overlay_number_columns(36.0f, 36.0f, least, number_w, default_w, gap, true, &col),
             "a row with no width at all draws nothing rather than drawing it backwards");
    ut_check(!overlay_number_columns(x0, 300.0f, least, number_w, default_w, gap, true, NULL),
             "and it refuses to answer into nowhere");
}
int main(void)
{
    test_a_fraction_and_a_value();
    test_a_track_with_no_length();
    test_one_press();
    test_default();
    test_what_fits();
    test_how_far_a_grab_reaches();
    test_the_columns_on_the_right_of_a_track();
    return ut_summary("a number row's arithmetic and the end of its track");
}
