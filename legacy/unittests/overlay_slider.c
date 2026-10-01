/* overlay_slider.c: the one way a dragged track reaches the settings file, and the one throttle
 * on it.
 *
 * What would be silent if it were wrong. The file is around ninety kilobytes and every write
 * rewrites it, so a write path that misses the throttle costs five megabytes a second of file
 * traffic for one dragged handle, and the stutter that causes gets blamed on the setting rather
 * than on the changing of it. It is silent because nothing about the picture looks different: the
 * handle follows the hand either way. Every writer goes through this one path with one copy of
 * the two numbers, the sideways keys and the Default button included, rather than writing a
 * track of their own.
 *
 * The two of them are a DISCRETE change, not a motion, so they neither keep the hold between
 * calls nor wait the throttle out. Both of those are decisions with a cost, and the checks at
 * the bottom are what say which way they went: a hold kept between presses would refuse the
 * pointer its own drag, and a throttle with no key release to flush on would drop the last
 * press of a burst, which is a value somebody set.
 *
 * The clock is handed in, the way spawn_place.c takes its click, because the interval is the one
 * thing in a throttle that cannot be driven from outside otherwise. It only ever goes FORWARD
 * here, as GetTickCount does: the difference is unsigned, so a clock that went back would wrap
 * and every move would look overdue. The model is stubbed: it is the far side of the seam, and
 * what this file is about is which of the hand's moves reach it.
 */
#include "unittest.h"

#include "overlay_slider.h"

#include "overlay_model.h"
#include "overlay_notice.h"
#include "overlay_number.h"

#include "common/text.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* The two intervals, written out rather than included from the module: a test that imports the
 * constant it is checking proves only that the constant equals itself. */
#define ORDINARY_MS 250u
#define FULL_MS      33u

/* --- the model, as far as this file needs it ------------------------------------------------- */

static uint32_t writes;          /* how many reached the file */
static uint32_t last_index;
static float    last_value;
static bool     refuse;          /* the write the model turns down */
static uint32_t rebuilds;
static bool     full_rate_row;   /* whether track 1 asks for the full rate */

bool overlay_model_slider_set(uint32_t index, float fraction)
{
    if (refuse) {
        return false;
    }
    ++writes;
    last_index = index;
    last_value = fraction;
    return true;
}

bool overlay_model_slider_wants_full_rate(uint32_t index)
{
    return full_rate_row && index == 1u;
}

void overlay_model_rebuild(void)
{
    ++rebuilds;
}

/* The far side of the seam again, for the two questions a discrete change asks that a drag
 * does not: what the row's numbers are, and where its handle stands with no hand on it.
 * Tracks 1 and 3 run 1.0 to 2.5 in hundredths with a standard of 1.0, which is the draw
 * distance row as overlay_picture.c writes it; every other track answers nothing, so a press
 * on one has somewhere to go wrong.
 *
 * TWO of them answer, and not one, because the rule about a press on a DIFFERENT track from
 * the one a hand has can only be checked against a second track that exists. With one, the
 * press was turned down here, at the numbers, and the check below read as though it had been
 * turned down by the hold. */
static float row_fraction = 0.0f;

bool overlay_model_slider_limits(uint32_t index, overlay_number_t *out)
{
    if ((index != 1u && index != 3u) || out == NULL) {
        return false;
    }
    out->minimum      = 1.0f;
    out->maximum      = 2.5f;
    out->step         = 0.01f;
    out->coarse       = 0.10f;
    out->standard     = 1.0f;
    out->has_standard = true;
    return true;
}

bool overlay_model_row(uint32_t index, overlay_row_t *out)
{
    if ((index != 1u && index != 3u) || out == NULL) {
        return false;
    }
    out->fraction = row_fraction;
    return true;
}

/* The number the row above a handle reads. Written as the fraction itself so that a check can
 * say which fraction reached it; what the real one does with it is overlay_kit.c's, and
 * unittests/overlay_groups.c is what drives that. */
bool overlay_model_slider_value(uint32_t index, float fraction, char *out, size_t size)
{
    if (index != 1u || out == NULL || size < 8u) {
        return false;
    }
    text_format(out, size, "%.3f", (double)fraction);
    return true;
}

/* --- the clock, which only goes forward ------------------------------------------------------- */

static uint32_t clock_ms = 100000u;

static uint32_t after(uint32_t ms)
{
    clock_ms += ms;
    return clock_ms;
}

static void nothing_written(void)
{
    writes   = 0;
    rebuilds = 0;
    refuse   = false;
}

/* Both hands let go, so a section starts with no track held whatever the one before it did. */
static void let_everything_go(void)
{
    overlay_slider_let_go(OVERLAY_SLIDER_POINTER);
    overlay_slider_let_go(OVERLAY_SLIDER_TRIGGER);
    nothing_written();
}

/* --- grabbing, moving, letting go -------------------------------------------------------------
 * A click on a track writes at once; a move waits the interval out; letting go writes whatever the
 * hand settled on. */
static void the_shape_of_a_drag(void)
{
    ut_section("a drag from the click to the release");

    let_everything_go();
    overlay_slider_grab(OVERLAY_SLIDER_POINTER, 3, 0.25f, after(1000u));
    ut_check(writes == 1u && last_index == 3u, "a click on a track writes where it was clicked, "
             "at once: the handle jumped, so the file gets the jump");
    ut_near(last_value, 0.25, 0.0001, "and it writes the fraction it was clicked at");
    ut_check(overlay_slider_row(OVERLAY_SLIDER_POINTER) == 3,
             "the pointer is holding that track");

    overlay_slider_move(OVERLAY_SLIDER_POINTER, 0.50f, after(ORDINARY_MS - 1u));
    ut_check(writes == 1u, "a move one millisecond inside the interval writes nothing");
    {
        float where = 0.0f;

        ut_check(overlay_slider_held(NULL, &where), "but the track is still held");
        ut_near(where, 0.50, 0.0001,
                "and the handle is where the hand is: the drawing follows every frame while the "
                "file waits");
    }

    overlay_slider_move(OVERLAY_SLIDER_POINTER, 0.60f, after(1u));
    ut_check(writes == 2u, "a move on the interval writes");
    ut_near(last_value, 0.60, 0.0001, "and writes where the hand is now, not where it was");

    overlay_slider_move(OVERLAY_SLIDER_POINTER, 0.75f, after(1u));
    ut_check(writes == 2u, "the interval starts again from the write, not from the grab");

    overlay_slider_let_go(OVERLAY_SLIDER_POINTER);
    ut_check(writes == 3u, "letting go writes what the hand settled on, throttle or not");
    ut_near(last_value, 0.75, 0.0001, "so nothing the hand settled on is lost");
    ut_check(overlay_slider_row(OVERLAY_SLIDER_POINTER) == -1 && !overlay_slider_held(NULL, NULL),
             "and the track is let go");

    let_everything_go();
    overlay_slider_grab(OVERLAY_SLIDER_POINTER, 3, 0.25f, after(1000u));
    nothing_written();
    overlay_slider_let_go(OVERLAY_SLIDER_POINTER);
    ut_check(writes == 0u, "letting go of a track the file already has writes nothing");
}

/* --- the throttle -----------------------------------------------------------------------------
 * The numbers are in one place, so both hands and both ways in are held to the same two. The stamp
 * is the time of the last write and not of the last pickup, so a hand landing on a track nobody
 * has touched for a while writes on its first move rather than waiting for nothing. */
static void the_two_rates(void)
{
    ut_section("the two write intervals, and who gets which");

    full_rate_row = false;
    let_everything_go();
    overlay_slider_grab(OVERLAY_SLIDER_POINTER, 1, 0.0f, after(1000u));
    nothing_written();
    overlay_slider_move(OVERLAY_SLIDER_POINTER, 0.1f, after(FULL_MS));
    ut_check(writes == 0u, "an ordinary track does not write at the full rate's interval");
    overlay_slider_move(OVERLAY_SLIDER_POINTER, 0.2f, after(ORDINARY_MS - FULL_MS));
    ut_check(writes == 1u, "it writes four times a second");

    full_rate_row = true;
    let_everything_go();
    overlay_slider_grab(OVERLAY_SLIDER_POINTER, 1, 0.0f, after(1000u));
    nothing_written();
    overlay_slider_move(OVERLAY_SLIDER_POINTER, 0.1f, after(FULL_MS - 1u));
    ut_check(writes == 0u, "the field of view's track waits its own interval out too");
    overlay_slider_move(OVERLAY_SLIDER_POINTER, 0.2f, after(1u));
    ut_check(writes == 1u, "and then writes thirty times a second, because its whole effect is "
             "the picture zooming under the hand");

    /* The pad. It takes hold without writing, so its first move through is what stamps the
     * clock. */
    full_rate_row = false;
    let_everything_go();
    overlay_slider_take(OVERLAY_SLIDER_TRIGGER, 1, 0.0f);
    overlay_slider_move(OVERLAY_SLIDER_TRIGGER, 0.05f, after(ORDINARY_MS));
    ut_check(writes == 1u, "the first move after a pickup writes: nothing was waiting on it");
    overlay_slider_move(OVERLAY_SLIDER_TRIGGER, 0.1f, after(ORDINARY_MS - 1u));
    ut_check(writes == 1u, "the triggers wait the same interval the pointer does");
    overlay_slider_move(OVERLAY_SLIDER_TRIGGER, 0.2f, after(1u));
    ut_check(writes == 2u, "and write on it");

    full_rate_row = true;
    let_everything_go();
    overlay_slider_take(OVERLAY_SLIDER_TRIGGER, 1, 0.0f);
    overlay_slider_move(OVERLAY_SLIDER_TRIGGER, 0.05f, after(ORDINARY_MS));
    nothing_written();
    overlay_slider_move(OVERLAY_SLIDER_TRIGGER, 0.1f, after(FULL_MS - 1u));
    ut_check(writes == 0u, "and on the field of view they wait the same shorter one");
    overlay_slider_move(OVERLAY_SLIDER_TRIGGER, 0.2f, after(1u));
    ut_check(writes == 1u, "and write on that");
    full_rate_row = false;
    let_everything_go();
}

/* --- taking hold without writing --------------------------------------------------------------
 * The pad's triggers pick up whatever track the pointer is over, every frame. Noticing a track is
 * not changing it. */
static void taking_hold_writes_nothing(void)
{
    ut_section("taking hold of a track is not moving it");

    let_everything_go();
    overlay_slider_take(OVERLAY_SLIDER_TRIGGER, 2, 0.4f);
    ut_check(writes == 0u && overlay_slider_row(OVERLAY_SLIDER_TRIGGER) == 2,
             "the triggers take the track under the pointer without writing anything");

    overlay_slider_let_go(OVERLAY_SLIDER_TRIGGER);
    ut_check(writes == 0u, "and letting go of it again writes nothing either");

    overlay_slider_take(OVERLAY_SLIDER_TRIGGER, -1, 0.4f);
    ut_check(overlay_slider_row(OVERLAY_SLIDER_TRIGGER) == -1,
             "and with nothing under the pointer they hold nothing");
}

/* --- one hold, and a hand commands only its own -----------------------------------------------
 * The case this rule exists for: a mouse drag whose hand has wandered off the row it grabbed, with
 * a pad plugged in. The triggers read whatever row is under the pointer, every frame. */
static void a_hand_commands_only_its_own_hold(void)
{
    ut_section("two hands, one hold");

    let_everything_go();
    overlay_slider_grab(OVERLAY_SLIDER_POINTER, 4, 0.5f, after(1000u));
    overlay_slider_move(OVERLAY_SLIDER_POINTER, 0.55f, after(1u));
    nothing_written();

    overlay_slider_take(OVERLAY_SLIDER_TRIGGER, 7, 0.9f);
    ut_check(overlay_slider_row(OVERLAY_SLIDER_POINTER) == 4 &&
                 overlay_slider_row(OVERLAY_SLIDER_TRIGGER) == -1,
             "the triggers do not take a track the pointer is dragging");

    overlay_slider_let_go(OVERLAY_SLIDER_TRIGGER);
    ut_check(overlay_slider_row(OVERLAY_SLIDER_POINTER) == 4 && writes == 0u,
             "and letting go of a hold they do not have ends nobody's drag: a pointer that "
             "wandered off its row keeps driving the handle it grabbed");

    overlay_slider_move(OVERLAY_SLIDER_TRIGGER, 0.1f, after(ORDINARY_MS * 10u));
    ut_check(writes == 0u, "nor does a hand that holds nothing write");
    {
        float where = 0.0f;

        (void)overlay_slider_held(NULL, &where);
        ut_near(where, 0.55, 0.0001, "and it does not move the handle either");
    }

    overlay_slider_let_go(OVERLAY_SLIDER_POINTER);
    ut_check(writes == 1u && overlay_slider_row(OVERLAY_SLIDER_POINTER) == -1,
             "the hand that has it is the one that can let it go");

    overlay_slider_take(OVERLAY_SLIDER_TRIGGER, 7, 0.9f);
    ut_check(overlay_slider_row(OVERLAY_SLIDER_TRIGGER) == 7,
             "and then the other hand can take a track");
    let_everything_go();
}

/* --- a hand that does nothing holds nothing ---------------------------------------------------
 * pad_panel.c drives the triggers every frame a pad is present and takes the track under the
 * pointer whether or not the triggers are pushed in at all. If the rule above guarded that hold,
 * the track would be the TRIGGER hand's, and the pointer's click, the sideways keys and Default
 * would all be refused with nothing moving and nothing saying why: a panel whose sliders do not
 * work, as a field run with a pad plugged in showed.
 *
 * So the rule guards a hold that is DRIVING the track, meaning one that has been grabbed or moved,
 * and a hold that has only NOTICED the track gives way. And a hand that leaves the row lets go,
 * rather than keeping a track nobody is on.
 */
static void a_hand_that_does_nothing_holds_nothing(void)
{
    ut_section("an idle hold gives way, and a hand that leaves the row lets go");

    full_rate_row = false;
    let_everything_go();
    overlay_slider_take(OVERLAY_SLIDER_TRIGGER, 1, 0.4f);
    ut_check(overlay_slider_row(OVERLAY_SLIDER_TRIGGER) == 1,
             "the triggers notice the track under the pointer");

    overlay_slider_grab(OVERLAY_SLIDER_POINTER, 1, 0.8f, after(1000u));
    ut_check(overlay_slider_row(OVERLAY_SLIDER_POINTER) == 1,
             "and a click on that same track takes it from them: noticing a track is not "
             "driving it, and a hand that is not driving one does not own it");
    ut_check(writes == 1u, "so the click reaches the file, which is the whole of what a click "
             "on a track does");
    ut_near(last_value, 0.80, 0.0001, "at the fraction it was clicked at");
    ut_check(overlay_slider_row(OVERLAY_SLIDER_TRIGGER) == -1,
             "and the hand that was only noticing it has it no longer");

    /* The other direction, which is the case the rule was written for and which stands. */
    let_everything_go();
    overlay_slider_grab(OVERLAY_SLIDER_POINTER, 4, 0.5f, after(1000u));
    overlay_slider_move(OVERLAY_SLIDER_POINTER, 0.55f, after(ORDINARY_MS));
    overlay_slider_take(OVERLAY_SLIDER_TRIGGER, 4, 0.9f);
    ut_check(overlay_slider_row(OVERLAY_SLIDER_POINTER) == 4 &&
                 overlay_slider_row(OVERLAY_SLIDER_TRIGGER) == -1,
             "a track a hand is DRIVING is not taken from it by a hand that has only "
             "noticed it");

    ut_section("a hand that leaves the row lets go");

    let_everything_go();
    overlay_slider_take(OVERLAY_SLIDER_TRIGGER, 2, 0.4f);
    overlay_slider_take(OVERLAY_SLIDER_TRIGGER, -1, 0.0f);
    ut_check(overlay_slider_row(OVERLAY_SLIDER_TRIGGER) == -1 &&
                 !overlay_slider_held(NULL, NULL),
             "the pointer off every track lets the track go, rather than leaving it held by "
             "a hand that is on nothing");
    ut_check(writes == 0u, "and nothing was written by letting go of a track nobody moved");

    ut_section("a press and a Default against an idle hold");

    let_everything_go();
    row_fraction = 0.0f;
    overlay_slider_take(OVERLAY_SLIDER_TRIGGER, 1, 0.5f);
    ut_check(overlay_slider_nudge(3, 1, false) && writes == 1u,
             "a sideways press on another track goes through while the only hold on the "
             "panel is one nobody is driving");
    ut_check(last_index == 3u, "and it lands on the track that was pressed");

    let_everything_go();
    row_fraction = 0.5f;
    overlay_slider_take(OVERLAY_SLIDER_TRIGGER, 1, 0.5f);
    nothing_written();
    ut_check(overlay_slider_to_standard(3) && writes == 1u,
             "and so does Default, which is the same press with the standard for a step");
    let_everything_go();
    row_fraction = 0.0f;
}

/* --- what the drawing is told ----------------------------------------------------------------- */
static void the_handle_the_drawing_sees(void)
{
    int32_t index = -1;
    float   fraction = 0.0f;

    ut_section("what the drawing reads off the hold");

    let_everything_go();
    ut_check(!overlay_slider_held(&index, &fraction),
             "with nothing held the drawing is told so, and draws from the file");

    overlay_slider_take(OVERLAY_SLIDER_TRIGGER, 6, 0.3f);
    ut_check(overlay_slider_held(&index, &fraction) && index == 6,
             "a track the TRIGGERS hold is a track the drawing follows, the same as the pointer's");
    ut_near(fraction, 0.30, 0.0001, "at the fraction the hand has it");

    overlay_slider_move(OVERLAY_SLIDER_TRIGGER, 2.0f, after(1u));
    (void)overlay_slider_held(NULL, &fraction);
    ut_near(fraction, 1.00, 0.0001, "a hand past the end of the track puts the handle on the end");

    overlay_slider_move(OVERLAY_SLIDER_TRIGGER, -1.0f, after(1u));
    (void)overlay_slider_held(NULL, &fraction);
    ut_near(fraction, 0.00, 0.0001, "and past the other end, on the other end");
    let_everything_go();
}

/* --- a refused write ------------------------------------------------------------------------- */
static void a_write_the_model_turns_down(void)
{
    ut_section("a write the model refuses");

    let_everything_go();
    overlay_slider_grab(OVERLAY_SLIDER_POINTER, 5, 0.0f, after(1000u));
    nothing_written();
    overlay_notice_forget();
    refuse = true;
    overlay_slider_move(OVERLAY_SLIDER_POINTER, 0.5f, after(ORDINARY_MS));
    ut_check(rebuilds == 0u,
             "a refused write builds no picture again: nothing changed, so there is nothing to "
             "show");
    /* The handle stays where it was, which is the same picture a write that landed on the value
     * the row already had leaves. The band is the only difference between the two, and every one
     * of the seven hands on a track in this panel comes through the write that says it. */
    ut_check(overlay_notice_text() != NULL,
             "and it SAYS so: a settings file that cannot be written loses the setting, and "
             "nothing else on the row would have told anybody");
    overlay_slider_move(OVERLAY_SLIDER_POINTER, 0.6f, after(1u));
    ut_check(writes == 0u,
             "and a refusal costs the interval and not the frame: the next try waits its turn");
    refuse = false;
    overlay_notice_forget();
    overlay_slider_move(OVERLAY_SLIDER_POINTER, 0.6f, after(ORDINARY_MS));
    ut_check(writes == 1u && rebuilds == 1u, "the one that lands builds the picture again");
    ut_check(overlay_notice_text() == NULL, "and says nothing, because nothing was turned down");
    let_everything_go();
}

/* --- one press, and the Default button --------------------------------------------------
 * A discrete change, not a motion. Both of these go through the same write the drag does, so
 * any further path into the settings file would show up here as a write this file never counted.
 */
static void one_press_and_the_default(void)
{
    ut_section("one press of a sideways key");

    let_everything_go();
    row_fraction = 0.0f;       /* the row sits at 1.00, its low end */
    ut_check(overlay_slider_nudge(1, 1, false) && writes == 1u,
             "a press writes at once, through the same write a drag uses");
    ut_near(last_value, 0.006667, 0.0002,
            "one hundredth up a scale that runs over one and a half");
    ut_check(overlay_slider_row(OVERLAY_SLIDER_KEYS) == -1 &&
                 !overlay_slider_held(NULL, NULL),
             "and it lets the track go inside the call: a hold kept between presses would "
             "refuse the pointer its own drag");

    nothing_written();
    ut_check(overlay_slider_nudge(1, 1, true) && writes == 1u,
             "the modifier writes too");
    ut_near(last_value, 0.066667, 0.0002, "and it is the row's own larger press, a tenth");

    nothing_written();
    ut_check(overlay_slider_nudge(1, 1, false) && writes == 1u,
             "a second press in a row writes again rather than waiting an interval out: "
             "every press is a value somebody asked for, and no key release would flush "
             "the last of them");

    nothing_written();
    row_fraction = 1.0f;
    ut_check(overlay_slider_nudge(1, 1, false) && writes == 1u,
             "a press at the top end still writes");
    ut_near(last_value, 1.0, 0.0001, "and leaves the value where it was");

    nothing_written();
    ut_check(!overlay_slider_nudge(2, 1, false) && writes == 0u,
             "a track whose row answers no numbers is not stepped");
    ut_check(!overlay_slider_nudge(-1, 1, false) && writes == 0u, "nor is no track at all");

    ut_section("Default");
    nothing_written();
    row_fraction = 0.5f;
    ut_check(overlay_slider_to_standard(1) && writes == 1u, "Default writes, once");
    ut_near(last_value, 0.0, 0.0001,
            "and puts the handle where the row's standard is, which on this one is 1.0");
    nothing_written();
    ut_check(!overlay_slider_to_standard(2) && writes == 0u,
             "a track whose row named no standard has no Default to press");

    ut_section("a press while a hand is on the track");
    /* The pad's triggers hold the track under the pointer while they are pushed in, so "a
     * hand is on it" is an ordinary case and not the exception. */
    let_everything_go();
    row_fraction = 0.0f;
    overlay_slider_take(OVERLAY_SLIDER_TRIGGER, 1, 0.5f);
    nothing_written();
    ut_check(overlay_slider_nudge(1, 1, false) && writes == 1u,
             "the press moves the track the hand has, rather than being refused by it");
    {
        float where = 0.0f;

        ut_check(overlay_slider_held(NULL, &where) &&
                     overlay_slider_row(OVERLAY_SLIDER_TRIGGER) == 1,
                 "the hand still has it afterwards");
        ut_near(where, 0.506667, 0.0002,
                "and the handle has moved with it, stepping from where the HAND had it and "
                "not from what the file last got");
    }

    /* The hand made to DRIVE the track first: the drag a press must not end is one that is
     * moving, not one that is merely resting on the row. */
    overlay_slider_move(OVERLAY_SLIDER_TRIGGER, 0.6f, after(ORDINARY_MS * 10u));
    nothing_written();
    ut_check(!overlay_slider_nudge(3, 1, false) && writes == 0u,
             "a press on a DIFFERENT track while a hand is DRIVING one is refused, so a "
             "burst of presses cannot end somebody's drag");
    let_everything_go();
    row_fraction = 0.0f;
}

/* --- the number the two rows show ------------------------------------------------------
 * A track drives TWO rows: the number above it and the track row itself, which carries the
 * number at the end of its own line. While a hand is on the track both have to read the
 * hand's fraction, and the way this goes wrong is one of the two being left out, which looks
 * like nothing at all until somebody drags a handle and the two numbers disagree.
 *
 * What is checked here is the ROUTING: which row is handed which fraction. What the panel
 * then makes of that fraction is the model's, and unittests/overlay_groups.c drives it
 * against the real rows. */
static void the_number_the_two_rows_show(void)
{
    overlay_row_t above;
    overlay_row_t on_the_track;
    overlay_row_t elsewhere;

    ut_section("the number on the two rows a held track drives");
    let_everything_go();
    memset(&above, 0, sizeof above);
    memset(&on_the_track, 0, sizeof on_the_track);
    memset(&elsewhere, 0, sizeof elsewhere);
    above.kind        = OVERLAY_ROW_VALUE;
    on_the_track.kind = OVERLAY_ROW_SLIDER;
    elsewhere.kind    = OVERLAY_ROW_VALUE;
    text_format(above.value, sizeof above.value, "file");
    text_format(on_the_track.value, sizeof on_the_track.value, "file");
    text_format(elsewhere.value, sizeof elsewhere.value, "file");

    overlay_slider_number_on(0u, &above);
    overlay_slider_number_on(1u, &on_the_track);
    ut_check(strcmp(above.value, "file") == 0 && strcmp(on_the_track.value, "file") == 0,
             "with no hand on the track, both rows keep the number the file put on them");

    overlay_slider_take(OVERLAY_SLIDER_TRIGGER, 1, 0.25f);
    overlay_slider_number_on(0u, &above);
    overlay_slider_number_on(1u, &on_the_track);
    ut_check(strcmp(above.value, "0.250") == 0,
             "the row above a held track reads the fraction the HAND has");
    ut_check(strcmp(on_the_track.value, "0.250") == 0,
             "and so does the track row, which is the one a reader is looking at while "
             "they drag it");
    ut_check(strcmp(above.value, on_the_track.value) == 0, "so the two cannot disagree");

    overlay_slider_number_on(5u, &elsewhere);
    ut_check(strcmp(elsewhere.value, "file") == 0,
             "a row the hold is not about keeps its own number");
    elsewhere.kind = OVERLAY_ROW_CHEAT;
    overlay_slider_number_on(1u, &elsewhere);
    ut_check(strcmp(elsewhere.value, "file") == 0,
             "and so does a row that is neither a number nor a track");
    let_everything_go();
}

int main(void)
{
    the_shape_of_a_drag();
    the_two_rates();
    taking_hold_writes_nothing();
    a_hand_commands_only_its_own_hold();
    a_hand_that_does_nothing_holds_nothing();
    the_handle_the_drawing_sees();
    a_write_the_model_turns_down();
    one_press_and_the_default();
    the_number_the_two_rows_show();
    return ut_summary("the one write path into a slider");
}
