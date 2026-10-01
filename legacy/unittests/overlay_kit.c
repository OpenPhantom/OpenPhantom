/* overlay_kit.c: a group's rows written as a table, driven over a table of this file's own.
 *
 * The one thing here that can break a group without breaking the build is the count: a NUMBER
 * entry draws TWO rows, its value and the track under it, so an entry put in above another moves
 * that one's slot by two. session_lock.c addresses those slots by number, so a slot that moved
 * hands a player a setting the multiplayer's content check hashes, in the middle of a session,
 * with nothing on screen to say so. The rest of this program is the ordinary shape of a row.
 */
#include "unittest.h"

#include "overlay_kit.h"
#include "overlay_reason.h"

#include "common/text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The table's world: one switch, one action, one number and one note, with everything they read
 * and write kept here so that each check can put the world where it wants it. */
static struct {
    bool  on;
    bool  ran;
    float value;
    bool  offered;
    bool  writes_land;
    bool  host_decides;   /* this machine is a client whose host named the setting */
} world;

static bool get_on(void)          { return world.on; }
static bool set_on(bool on)
{
    if (!world.writes_land) {
        return false;
    }
    world.on = on;
    return true;
}

static bool run(void)
{
    world.ran = true;
    return true;
}

static bool offered(void)         { return world.offered; }
static uint32_t why(void)         { return (uint32_t)OVERLAY_REASON_NEEDS_ROW; }

static bool number(float *out)
{
    if (!world.offered) {
        return false;      /* the field of view's case: nobody published a width */
    }
    *out = world.value;
    return true;
}

static bool set_number(float value)
{
    if (!world.writes_land) {
        return false;
    }
    world.value = value;
    return true;
}

static void format(float value, char *out, size_t size)
{
    text_format(out, size, "%.2fx", (double)value);
}

static bool parse(const char *text, float *out)
{
    if (text == NULL || text[0] < '0' || text[0] > '9') {
        return false;
    }
    *out = (float)(text[0] - '0');
    return true;
}

/* What Default puts back on the row below. A function and not a number, so that a row
 * without one can say so by leaving it out; the table below has both kinds. */
static bool standard(float *out)
{
    *out = 2.0f;
    return true;
}

static void a_note(char *out, size_t size)
{
    text_format(out, size, "  in force: %.2fx", (double)world.value);
}

/* The host's word, as a group's hook writes it. It scribbles into the buffer and the state before
 * it refuses, which is what a hook may do, so the row keeping its own value then is the kit's
 * doing; and a number that reports a state has it ignored, because a number has no switch. */
static bool host_number(char *out, size_t size, bool *on)
{
    text_format(out, size, "scribble");
    *on = true;
    if (!world.host_decides) {
        return false;
    }
    text_format(out, size, "host 1.50x");
    return true;
}

/* The host has the switch off, whatever this machine's own is. */
static bool host_switch(char *out, size_t size, bool *on)
{
    *on = !world.on;
    if (!world.host_decides) {
        return false;
    }
    *on = false;
    text_format(out, size, "host OFF");
    return true;
}

static const overlay_kit_entry_t ROWS[] = {
    { .type = OVERLAY_KIT_TOGGLE, .label = "A switch", .get_on = get_on, .set_on = set_on,
      .host_word = host_switch },
    { .type       = OVERLAY_KIT_NUMBER,
      .label      = "A number (1 to 5)",
      .number     = number,
      .set_number = set_number,
      .format     = format,
      .parse      = parse,
      .minimum    = 1.0f,
      .maximum    = 5.0f,
      .step       = 0.01f,
      .coarse     = 0.10f,
      .standard   = standard,
      .full_rate  = true,
      .host_word  = host_number },
    { .type = OVERLAY_KIT_NOTE, .label_now = a_note },
    { .type = OVERLAY_KIT_ACTION, .label = "Do it", .run = run },
    { .type = OVERLAY_KIT_TOGGLE, .label = "A gated switch", .get_on = get_on, .set_on = set_on,
      .offered = offered, .reason = why }
};

#define ENTRIES ((uint32_t)(sizeof ROWS / sizeof ROWS[0]))

/* The drawn slots this table produces, which is what the checks below address. */
#define SLOT_SWITCH   0u
#define SLOT_NUMBER   1u
#define SLOT_TRACK    2u
#define SLOT_NOTE     3u
#define SLOT_ACTION   4u
#define SLOT_GATED    5u

static overlay_row_t row;

static void a_world(void)
{
    memset(&world, 0, sizeof world);
    world.value = 3.0f;
    world.offered = true;
    world.writes_land = true;
}

static void test_the_count(void)
{
    ut_section("a number entry is two rows and everything else is one");
    a_world();
    ut_check(overlay_kit_count(ROWS, ENTRIES) == 6u,
             "five entries draw six rows, because the number brings its track");
    ut_check(overlay_kit_count(ROWS, 1u) == 1u, "the switch alone is one");
    ut_check(overlay_kit_count(ROWS, 2u) == 3u, "and the switch with the number is three");
    ut_check(overlay_kit_count(NULL, 4u) == 0u, "no table draws nothing");
}

static void test_each_kind(void)
{
    ut_section("what each kind of entry puts on the row");
    a_world();

    overlay_kit_fill(ROWS, ENTRIES, SLOT_SWITCH, NULL, &row);
    ut_check(row.kind == OVERLAY_ROW_CHEAT && strcmp(row.label, "A switch") == 0 &&
                 row.available && !row.on,
             "a toggle is a cheat row, named, available and off");
    world.on = true;
    overlay_kit_fill(ROWS, ENTRIES, SLOT_SWITCH, NULL, &row);
    ut_check(row.on, "and it reads the world rather than remembering");

    overlay_kit_fill(ROWS, ENTRIES, SLOT_NUMBER, NULL, &row);
    ut_check(row.kind == OVERLAY_ROW_VALUE && strcmp(row.value, "3.00x") == 0,
             "a number is a value row, through the row's own formatter");
    overlay_kit_fill(ROWS, ENTRIES, SLOT_NUMBER, "2.7", &row);
    ut_check(strcmp(row.value, "2.7_") == 0,
             "and while it is being typed into it shows what has been typed, with the cursor");

    overlay_kit_fill(ROWS, ENTRIES, SLOT_TRACK, NULL, &row);
    ut_check(row.kind == OVERLAY_ROW_SLIDER && row.label[0] == '\0' &&
                 row.fraction > 0.49f && row.fraction < 0.51f,
             "the track under it carries the handle and no name of its own");

    overlay_kit_fill(ROWS, ENTRIES, SLOT_NOTE, NULL, &row);
    ut_check(row.kind == OVERLAY_ROW_INFO && strcmp(row.label, "  in force: 3.00x") == 0,
             "a note writes its own line, which is how a name carries a number");

    overlay_kit_fill(ROWS, ENTRIES, SLOT_ACTION, NULL, &row);
    ut_check(row.kind == OVERLAY_ROW_ACTION && strcmp(row.label, "Do it") == 0,
             "an action is an action row");

    overlay_kit_fill(ROWS, ENTRIES, 6u, NULL, &row);
    ut_check(row.label[0] == '\0' && !row.available,
             "a slot past the end is an empty unavailable row, so a caller's mistake reads as a "
             "blank line and never as another row's text");
}

static void test_when_a_row_cannot_be_used(void)
{
    ut_section("a row that is not offered, and why not");
    a_world();
    world.offered = true;
    overlay_kit_fill(ROWS, ENTRIES, SLOT_GATED, NULL, &row);
    ut_check(row.available && row.reason == (uint32_t)OVERLAY_REASON_NONE,
             "offered, and nothing to say about it");

    world.offered = false;
    overlay_kit_fill(ROWS, ENTRIES, SLOT_GATED, NULL, &row);
    ut_check(!row.available && row.reason == (uint32_t)OVERLAY_REASON_NEEDS_ROW,
             "not offered, and the row says which row decides it");
    ut_check(!overlay_kit_activate(ROWS, ENTRIES, SLOT_GATED),
             "and it refuses to be pressed, which is the second lock behind the model's own");

    /* The field of view's case: the number cannot be read, so the value and its track are both
     * greyed and neither says a reason, because nobody named one. */
    overlay_kit_fill(ROWS, ENTRIES, SLOT_NUMBER, NULL, &row);
    ut_check(!row.available && row.value[0] == '\0' &&
                 row.reason == (uint32_t)OVERLAY_REASON_NONE,
             "a number nobody published is greyed and empty");
    overlay_kit_fill(ROWS, ENTRIES, SLOT_TRACK, NULL, &row);
    ut_check(!row.available, "and so is its track");

    /* One answer, and every acting path reads it: a press, a drag and a typed commit all ask
     * whether the row is usable, so a caller that goes straight to the kit meets the same gate
     * the model's row.available is. */
    ut_check(!overlay_kit_usable(ROWS, ENTRIES, SLOT_NUMBER) &&
                 !overlay_kit_usable(ROWS, ENTRIES, SLOT_TRACK) &&
                 !overlay_kit_usable(ROWS, ENTRIES, SLOT_GATED),
             "a number nobody published and a row nobody offers are both unusable");
    ut_check(!overlay_kit_activate(ROWS, ENTRIES, SLOT_NUMBER) &&
                 !overlay_kit_commit(ROWS, ENTRIES, SLOT_NUMBER, "4") &&
                 !overlay_kit_slider(ROWS, ENTRIES, SLOT_TRACK, 0.5f) &&
                 world.value == 3.0f,
             "so it cannot be pressed, typed into or dragged, and nothing was written");

    world.offered = true;
    ut_check(overlay_kit_usable(ROWS, ENTRIES, SLOT_NUMBER) &&
                 overlay_kit_usable(ROWS, ENTRIES, SLOT_TRACK) &&
                 overlay_kit_usable(ROWS, ENTRIES, SLOT_SWITCH),
             "and with the number back the same three paths open again");
    ut_check(!overlay_kit_usable(ROWS, ENTRIES, 99u),
             "a slot past the end of the table is not usable either");
}

/* The numbers on a number entry, which nothing but overlay_kit_limits() reads.
 *
 * The sideways keys and Default go through it, and this is the only place `coarse` or the
 * standard is held against the row it belongs to. The limits are checked against each other rather
 * than written out a second time, because a copy of the table is not a check on it.
 *
 * The reach of this is the table in THIS file. A group's own table is static and no caller
 * outside that file can read it, so a wrong `coarse` in overlay_picture.c is still nothing
 * the build can see. unittests/overlay_groups.c is what reads those, through the model.
 *
 * The standard is a HOOK and not a number, which is the one thing here a reader is likely
 * to get wrong: a number could not tell a row with no Default from one whose Default is
 * zero, and the row that made this necessary is the field of view, whose standard is read
 * out of a settings file every time it is asked for. */
static void test_the_limits(void)
{
    overlay_number_t limits;

    ut_section("the numbers behind a number row");
    a_world();

    ut_check(overlay_kit_limits(ROWS, ENTRIES, SLOT_NUMBER, &limits),
             "a number row has limits");
    ut_check(limits.minimum == 1.0f && limits.maximum == 5.0f,
             "the two ends are the table's own where no ends() overrides them");
    ut_check(limits.step == 0.01f && limits.coarse == 0.10f && limits.standard == 2.0f &&
                 limits.has_standard,
             "and the step, the coarse step and the Default come back as they were written");

    /* What each has to be for the row it belongs to. A step of zero lands every drag on the
     * same value; a coarse step below the fine one moves less when more was asked for; a
     * Default outside the ends puts the row where it cannot be typed back to. */
    ut_check(limits.step > 0.0f, "the step moves the row");
    ut_check(limits.coarse >= limits.step &&
                 limits.coarse <= limits.maximum - limits.minimum,
             "the coarse step is at least the fine one and no wider than the row itself");
    ut_check(limits.standard >= limits.minimum && limits.standard <= limits.maximum,
             "and the Default is a value the row can hold");

    ut_check(overlay_kit_limits(ROWS, ENTRIES, SLOT_TRACK, &limits) &&
                 limits.coarse == 0.10f,
             "the track under the value answers with the same numbers");

    /* A row that names no standard has none. It is what stops a Default drawn on a row
     * nobody wrote one for from putting that row at zero, which on two of the panel's
     * tracks is below the low end and on the rest is a value nobody chose. */
    {
        static const overlay_kit_entry_t BARE[] = {
            { .type = OVERLAY_KIT_NUMBER, .label = "No standard", .number = number,
              .set_number = set_number, .format = format, .parse = parse,
              .minimum = 1.0f, .maximum = 5.0f, .step = 0.01f, .coarse = 0.10f }
        };

        ut_check(overlay_kit_limits(BARE, 1u, 0u, &limits) && !limits.has_standard,
                 "a number row with no standard written on it says so");
        ut_check(limits.step == 0.01f,
                 "and still answers everything else, so the keys work on it and only "
                 "Default does not");
    }
    ut_check(!overlay_kit_limits(ROWS, ENTRIES, SLOT_SWITCH, &limits) &&
                 !overlay_kit_limits(ROWS, ENTRIES, SLOT_ACTION, &limits) &&
                 !overlay_kit_limits(ROWS, ENTRIES, 99u, &limits),
             "a switch, an action and a slot past the end have none");
}

static void test_pressing_and_typing(void)
{
    ut_section("pressing, typing and dragging");
    a_world();

    ut_check(overlay_kit_activate(ROWS, ENTRIES, SLOT_SWITCH) && world.on,
             "pressing a switch asks for the other state");
    ut_check(overlay_kit_activate(ROWS, ENTRIES, SLOT_SWITCH) && !world.on, "and back again");
    ut_check(overlay_kit_activate(ROWS, ENTRIES, SLOT_ACTION) && world.ran, "an action runs");
    ut_check(!overlay_kit_activate(ROWS, ENTRIES, SLOT_NUMBER),
             "a number is typed into rather than pressed");
    ut_check(!overlay_kit_activate(ROWS, ENTRIES, SLOT_TRACK), "and a track is dragged");
    ut_check(!overlay_kit_activate(ROWS, ENTRIES, SLOT_NOTE), "a note is read and nothing else");

    world.writes_land = false;
    ut_check(!overlay_kit_activate(ROWS, ENTRIES, SLOT_SWITCH),
             "a write that did not land is reported, so the caller leaves the row where it was");
    world.writes_land = true;

    ut_check(overlay_kit_commit(ROWS, ENTRIES, SLOT_NUMBER, "4") && world.value > 3.99f &&
                 world.value < 4.01f,
             "typed text goes through the row's own parser");
    ut_check(!overlay_kit_commit(ROWS, ENTRIES, SLOT_NUMBER, "no"),
             "and text that is not a number is refused rather than clamped, so a typing mistake "
             "does not become an extreme");
    ut_check(!overlay_kit_commit(ROWS, ENTRIES, SLOT_NUMBER, ""), "nor does an empty field");
    ut_check(!overlay_kit_commit(ROWS, ENTRIES, SLOT_SWITCH, "4"),
             "a switch has nothing to commit");
}

static void test_the_track(void)
{
    ut_section("the track, its ends and its step");
    a_world();

    ut_check(overlay_kit_slider(ROWS, ENTRIES, SLOT_TRACK, 0.0f) && world.value == 1.0f,
             "fully left is the row's minimum, exactly, which is the end a coarser grid could "
             "not reach");
    ut_check(overlay_kit_slider(ROWS, ENTRIES, SLOT_TRACK, 1.0f) && world.value == 5.0f,
             "and fully right is its maximum");
    ut_check(overlay_kit_slider(ROWS, ENTRIES, SLOT_TRACK, 2.0f) && world.value == 5.0f,
             "a fraction past the end is clamped rather than written");
    ut_check(overlay_kit_slider(ROWS, ENTRIES, SLOT_TRACK, -1.0f) && world.value == 1.0f,
             "and so is one before it");

    ut_check(overlay_kit_slider(ROWS, ENTRIES, SLOT_TRACK, 0.5f), "half way lands");
    {
        char shown[16];

        format(world.value, shown, sizeof shown);
        ut_check(strcmp(shown, "3.00x") == 0,
                 "and on the row's own step, so the drag and the text beside it never disagree "
                 "about what was set");
    }

    ut_check(!overlay_kit_slider(ROWS, ENTRIES, SLOT_NUMBER, 0.5f),
             "the value row above it is not a track");
    ut_check(overlay_kit_full_rate(ROWS, ENTRIES, SLOT_TRACK),
             "this one asks to reach the file at the full rate");
    ut_check(!overlay_kit_full_rate(ROWS, ENTRIES, SLOT_NUMBER),
             "and the row above it is not a track, so it asks for nothing");
}

/* The inversion: what the row above the track reads with the handle at a fraction.
 *
 * What would be silent if it were wrong. While a track is held the handle comes from the hand and
 * the file is written four times a second, so the number beside it has to be worked out from the
 * same fraction the handle is drawn at. If that sum reads the track from the wrong end, or rounds
 * differently from the write, the number is simply a different number: nothing refuses, nothing
 * logs, and the row and its handle disagree only while somebody is dragging it. */
static void test_the_value_under_the_handle(void)
{
    char shown[16];

    ut_section("what a handle at a fraction is worth");
    a_world();

    ut_check(overlay_kit_value_at(ROWS, ENTRIES, SLOT_TRACK, 0.0f, shown, sizeof shown) &&
                 strcmp(shown, "1.00x") == 0,
             "fully left reads the row's minimum, the end of the track it is at");
    ut_check(overlay_kit_value_at(ROWS, ENTRIES, SLOT_TRACK, 1.0f, shown, sizeof shown) &&
                 strcmp(shown, "5.00x") == 0,
             "and fully right reads its maximum");
    ut_check(overlay_kit_value_at(ROWS, ENTRIES, SLOT_TRACK, 0.25f, shown, sizeof shown) &&
                 strcmp(shown, "2.00x") == 0,
             "a quarter along reads a quarter of the way up, not three quarters: the ends are not "
             "swapped");

    /* The same fraction, through both. The point of the inversion is that there is one sum. */
    {
        const float FRACTIONS[] = { 0.0f, 0.125f, 0.3f, 0.5f, 0.77f, 1.0f };
        size_t      i;

        for (i = 0; i < sizeof FRACTIONS / sizeof FRACTIONS[0]; ++i) {
            char written[16];

            ut_check(overlay_kit_slider(ROWS, ENTRIES, SLOT_TRACK, FRACTIONS[i]), "the drag lands");
            format(world.value, written, sizeof written);
            (void)overlay_kit_value_at(ROWS, ENTRIES, SLOT_TRACK, FRACTIONS[i], shown,
                                       sizeof shown);
            ut_checkf(strcmp(written, shown) == 0,
                      "at %.3f the number shown and the number written are the same: \"%s\" "
                      "against \"%s\"", (double)FRACTIONS[i], shown, written);
        }
    }

    /* And back again: a value read off the row places the handle, and that handle reads the value
     * back. */
    {
        overlay_row_t track;

        world.value = 4.0f;
        overlay_kit_fill(ROWS, ENTRIES, SLOT_TRACK, NULL, &track);
        ut_check(overlay_kit_value_at(ROWS, ENTRIES, SLOT_TRACK, track.fraction, shown,
                                      sizeof shown) && strcmp(shown, "4.00x") == 0,
                 "a value puts the handle somewhere, and that somewhere reads the value back");
    }

    ut_check(!overlay_kit_value_at(ROWS, ENTRIES, SLOT_NUMBER, 0.5f, shown, sizeof shown),
             "the value row above the track is not a track and answers nothing");
    ut_check(!overlay_kit_value_at(ROWS, ENTRIES, SLOT_TRACK, 0.5f, NULL, 0u),
             "and nowhere to write it is no answer either");
    ut_check(!overlay_kit_value_at(ROWS, ENTRIES, SLOT_GATED, 0.5f, shown, sizeof shown),
             "and a row that is no number has no handle to read a value under");
    /* Whether the row can be USED at all is not decided here: the fill greys a number nobody
     * publishes, and overlay_model_slider_value() refuses an unavailable track before it asks. */
}

/* A row whose setting the host of a running session decides carries the host's value in its chip,
 * and the kit is the one place that puts it there: the group's hook says whether, and what. */
static void test_the_host_decides(void)
{
    ut_section("the host's value on a row the host decides");
    a_world();
    overlay_kit_fill(ROWS, ENTRIES, SLOT_NUMBER, NULL, &row);
    ut_check(!row.host_value && strcmp(row.value, "3.00x") == 0,
             "with no host deciding, a number reads its own value, and a hook that scribbled "
             "before it said no has left nothing on the row");
    world.on = true;
    overlay_kit_fill(ROWS, ENTRIES, SLOT_SWITCH, NULL, &row);
    ut_check(!row.host_value && row.value[0] == '\0' && row.on,
             "and a switch carries no word at all, and its own state even though the hook "
             "scribbled another before it said no");

    world.host_decides = true;
    overlay_kit_fill(ROWS, ENTRIES, SLOT_NUMBER, NULL, &row);
    ut_check(row.host_value && strcmp(row.value, "host 1.50x") == 0,
             "with the host deciding, the number's chip is the host's value, marked as such");
    ut_check(!row.on, "and a state the number's hook wrote is not taken: a number has no switch");
    overlay_kit_fill(ROWS, ENTRIES, SLOT_TRACK, NULL, &row);
    ut_check(!row.host_value && strcmp(row.value, "3.00x") == 0,
             "and the track under it keeps its own number: it is not a chip, and it is hidden "
             "while the session holds the row");
    overlay_kit_fill(ROWS, ENTRIES, SLOT_SWITCH, NULL, &row);
    ut_check(row.host_value && strcmp(row.value, "host OFF") == 0,
             "a switch carries the host's word");
    ut_check(!row.on && world.on,
             "and the host's state, off over this machine's on, which is what a folded heading "
             "counts; this machine's own key is untouched");
    overlay_kit_fill(ROWS, ENTRIES, SLOT_ACTION, NULL, &row);
    ut_check(!row.host_value, "and a row with no hook is untouched");
}

int main(void)
{
    test_the_count();
    test_each_kind();
    test_when_a_row_cannot_be_used();
    test_the_limits();
    test_pressing_and_typing();
    test_the_track();
    test_the_value_under_the_handle();
    test_the_host_decides();
    return ut_summary("a group's rows as a table");
}
