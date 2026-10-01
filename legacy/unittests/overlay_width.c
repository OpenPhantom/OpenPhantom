/* overlay_width.c: every label and every chip fits the room the panel leaves for it.
 *
 * It shares nothing with unittests/overlay_model.c: that one walks down the tab, each section
 * checking the row the section before it ended on, while this one opens everything on both tabs
 * and measures what comes out.
 *
 * The sweep is also what the tally is checked against, so the tally came with it: both read the
 * list that open_everything() leaves behind and neither reads anything else.
 *
 * What went wrong here once, because it is the reason for the first check in the walk below. The
 * rebuild that fills the model was dropped from the section at the bottom of this file, so
 * open_everything() read an empty list, found no heading to press, and the measurement ran over
 * NO rows: 463 checks became 185 and every one of them passed. A loop whose bound comes out of
 * the thing under test says nothing at all when that bound is zero, and it says it in green.
 */
#include "unittest.h"

#include "common/host_settings_note.h"
#include "common/session_note.h"
#include "common/text.h"

#include "overlay_choice.h"
#include "overlay_model.h"
#include "overlay_reason.h"
#include "overlay_rows.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* The tally's own row, read back out of the model. Named for the section that uses it: the walk
 * below declares one per row and a plain `row` here would shadow it. */
static overlay_row_t tally_row;

/* Every label and every chip has to fit, and this is the check that keeps it true.
 *
 * The panel is one bitmap font at one size, so a label too long for the room is drawn clipped with
 * an ellipsis in place of its end, and without this check only a screenshot would show it. The chip
 * has the same problem one level down: it is a fixed buffer, so a value too long is truncated with
 * no ellipsis at all, and "auto 1.33x" reached a player as "auto 1.".
 *
 * The budgets below are characters, not pixels, because a test process has no font. They are the
 * room the layout leaves at its widest, converted at the font's own worst case, so a label that
 * passes here fits on screen and a label that fails here would have been clipped. Raising a budget
 * is a decision about the panel's width, in overlay_layout.c, not about this check.
 */
/* A row costs its label AND its chip, side by side, so the budget is on the two together: a
 * long label beside a short chip fits where the same label beside "auto 1.00x" does not.
 * Checking them separately was the first version of this and it passed a label that was still
 * being clipped, because two budgets that each pass can still exceed the room once added.
 *
 * The number is WIDTH_MAX minus FURNITURE from overlay_layout.c, converted at this font's
 * widest glyph and rounded down. The chip keeps a cap of its own, which is the buffer it
 * lives in rather than the room on screen. */
#define ROW_BUDGET  48u
#define CHIP_BUDGET 15u

static void check_every_row_fits(const char *what)
{
    uint32_t count = overlay_model_row_count();
    char     floor_note[160];
    uint32_t i;

    /* A floor under the walk, and it is the point of this function rather than a formality. The
     * section below once lost the rebuild that fills the model, so this ran over ZERO rows and
     * stayed green while it measured nothing; the whole check went from 463 to 185 and reported
     * nothing wrong. A loop whose bound comes out of the thing under test needs to say that the
     * bound was not nothing. */
    text_format(floor_note, sizeof floor_note,
                "%s: the model built %u rows, and a walk over none of them measures nothing",
                what, (unsigned)count);
    floor_note[sizeof floor_note - 1] = '\0';
    ut_check(count > 0u, floor_note);

    for (i = 0; i < count; ++i) {
        overlay_row_t row;
        char          note[160];

        if (!overlay_model_row(i, &row)) {
            continue;
        }
        /* The chip the panel DRAWS, which for a row that is not available is its reason rather
         * than whatever value the row happens to carry. Measuring the value there passed a row
         * whose word was three characters while the drawing put nine beside the same name. The
         * one exception is a taken row on a client that carries the host's value, whose chip is
         * that value, as overlay_chip_word() has it.
         *
         * A note draws NO chip and runs the full width of the panel, which is what
         * overlay_chip_widest() already says by skipping it, so it is measured by its label
         * alone. It used to be measured against a reason word it never shows, and the spawner's
         * notes were therefore held to seven characters less than the room they have. */
        const char *chip = (row.kind == OVERLAY_ROW_INFO) ? ""
                         : (row.available || (row.host_value && row.value[0] != 0)) ? row.value
                         : overlay_reason_word(row.reason);
        /* And what a row of SEGMENTS costs: its name and the words standing where a chip would
         * be. It leaves `value` empty on purpose, four words not fitting sixteen characters, so
         * measuring `value` counted the widest row of its group as its name alone. 0 elsewhere. */
        const uint32_t strip = overlay_choice_row_width(&row);
        const uint32_t cost = (strip > 0u) ? strip
                                           : (uint32_t)(strlen(row.label) + strlen(chip));

        text_format(note, sizeof note, "%s: row %u \"%s\" plus what stands beside it is %u "
                    "characters, and the panel has room for %u", what, (unsigned)i, row.label,
                    (unsigned)cost, (unsigned)ROW_BUDGET);
        note[sizeof note - 1] = '\0';
        ut_check(cost <= ROW_BUDGET, note);

        /* And that the words were counted at all: a number bigger than the name and the chip
         * together is what says so. Fitting alone would pass a row whose words nobody measured. */
        if (row.kind == OVERLAY_ROW_SEGMENT) {
            text_format(note, sizeof note, "%s: row %u \"%s\" is measured at %u characters, and "
                        "its name and chip alone are %u: the words beside it were not counted",
                        what, (unsigned)i, row.label, (unsigned)cost,
                        (unsigned)(strlen(row.label) + strlen(chip)));
            note[sizeof note - 1] = '\0';
            ut_check(cost > strlen(row.label) + strlen(chip), note);
        }

        text_format(note, sizeof note, "%s: row %u chip \"%s\" is %u characters, and the buffer "
                    "holds %u", what, (unsigned)i, chip, (unsigned)strlen(chip),
                    (unsigned)CHIP_BUDGET);
        note[sizeof note - 1] = '\0';
        ut_check(strlen(chip) <= CHIP_BUDGET, note);
    }
}

/* Everything on the open tab opened: every folded heading and every fold inside one.
 *
 * Found by what they look like rather than counted to. The list used to be nine headings by name
 * and four folds by index, and both halves went wrong the same way: a group left out of the list
 * is a group whose rows are never checked, which is how a 54-character note reached a screenshot
 * past a 48-character budget, and an index into a list that has grown opens whatever is at that
 * number now. Neither can happen to a sweep that reads the list it is given.
 *
 * It runs until nothing is left to open, because opening a fold adds rows below it and opening a
 * heading reveals folds that were not in the list a moment ago. */
static void open_everything(void)
{
    bool opened = true;

    while (opened) {
        const uint32_t count = overlay_model_row_count();
        uint32_t       i;

        opened = false;
        for (i = 0; i < count; ++i) {
            overlay_row_t candidate;

            if (!overlay_model_row(i, &candidate) || candidate.expanded) {
                continue;
            }
            /* A heading, or a fold's own summary row, which this panel marks with a plus. */
            if (candidate.kind != OVERLAY_ROW_GROUP &&
                !(candidate.kind == OVERLAY_ROW_INFO && candidate.label[0] == '+')) {
                continue;
            }
            if (overlay_model_activate(i)) {
                overlay_model_rebuild();
                opened = true;
                break;
            }
        }
    }
}

/* The footer tells a player how many bands and how many rows the open tab has. The number has to
 * be counted off what was BUILT rather than worked out from the group tables: two sources are
 * drawn under another group's heading, so a count of the sources on the tab is fourteen where a
 * player sees twelve. */
static void test_the_tally(void)
{
    uint32_t bands;
    uint32_t i;

    ut_section("the tally the footer shows, against a walk over the rows");
    overlay_model_reset();
    overlay_model_set_tab(OVERLAY_TAB_OPENPHANTOM);
    overlay_model_rebuild();

    bands = 0;
    for (i = 0; i < overlay_model_row_count(); ++i) {
        if (overlay_model_row(i, &tally_row) && tally_row.kind == OVERLAY_ROW_GROUP) {
            ++bands;
        }
    }
    ut_check(overlay_model_heading_count() == bands,
             "the tally counts the bands that were built, not the sources that could have built "
             "them: two of those are drawn under another group's heading");
    ut_check(bands == HEADINGS, "and on this tab that is twelve");

    open_everything();
    bands = 0;
    for (i = 0; i < overlay_model_row_count(); ++i) {
        if (overlay_model_row(i, &tally_row) && tally_row.kind == OVERLAY_ROW_GROUP) {
            ++bands;
        }
    }
    ut_check(overlay_model_heading_count() == bands,
             "and it still agrees with a walk once every group is open");

    overlay_model_set_tab(OVERLAY_TAB_ORIGINAL);
    overlay_model_rebuild();
    ut_check(overlay_model_heading_count() == 2u, "the other tab has two");
    ut_check(overlay_model_row_count() == 2u, "and folded it is those two rows");
}

static void test_labels_and_chips_fit(void)
{
    ut_section("every label and every chip fits the room the panel leaves for it");
    /* Both tabs, everything unfolded, so the sweep sees every row this panel can put on screen
       rather than whichever ones happened to be open.

       The rebuild is not decoration. overlay_model_reset() sets the row count to zero and builds
       nothing, so without one open_everything() reads an empty list, finds no heading to press,
       and this section walked NO rows: every check in it passed by never running. */
    overlay_model_reset();
    overlay_model_set_tab(OVERLAY_TAB_ORIGINAL);
    overlay_model_rebuild();
    open_everything();
    check_every_row_fits("the Original tab");

    overlay_model_set_tab(OVERLAY_TAB_OPENPHANTOM);
    overlay_model_rebuild();
    open_everything();
    check_every_row_fits("the OpenPhantom tab, every group and every fold open");
}

/* On a client of a running session four taken rows carry the host's value in their chips, the
 * widest words those rows ever show: "host 2.50x" beside the draw distance's own name. The sweep
 * above runs with no session and never meets them, so this runs it again on a client, with the
 * widest value the host may name for each of the four. */
static void test_the_host_values_fit(void)
{
    session_note_t        note;
    host_settings_t       host;
    host_settings_taken_t taken;
    overlay_row_t         row;
    uint32_t              carried = 0;
    uint32_t              i;

    ut_section("the host's values fit beside the rows they stand on");
    memset(&host, 0, sizeof host);
    host.running = true;
    host.present = (uint16_t)((1u << HOST_SETTING_COUNT) - 1u);
    host.values[HOST_SETTING_VIEW_RANGE_SCALE]   = 2.5f;
    host.values[HOST_SETTING_FOG_BAND_SCALE]     = 0.25f;
    host.values[HOST_SETTING_AUTHORED_FOG_BAND]  = 1.0f;
    host.values[HOST_SETTING_DISMEMBERMENT_MODE] = 0.0f;
    memset(&note, 0, sizeof note);
    note.running = true;
    ut_check(host_settings_publish(&host) && session_note_publish(&note),
             "a session with this machine a client is published, with the host's values");

    overlay_model_reset();
    overlay_model_set_tab(OVERLAY_TAB_OPENPHANTOM);
    overlay_model_rebuild();
    open_everything();
    check_every_row_fits("the OpenPhantom tab on a client, every group and every fold open");
    for (i = 0; i < overlay_model_row_count(); ++i) {
        if (overlay_model_row(i, &row) && row.host_value) {
            ++carried;
        }
    }
    ut_checkf(carried == 4u, "and the sweep met the four rows that carry them (%u)",
              (unsigned)carried);

    memset(&host, 0, sizeof host);
    memset(&taken, 0, sizeof taken);
    note.running = false;
    ut_check(host_settings_publish(&host) &&
                 host_settings_publish_taken("view_distance_fix", &taken) &&
                 session_note_publish(&note),
             "and both are withdrawn again");
}

int main(void)
{
    test_the_tally();
    test_labels_and_chips_fit();
    test_the_host_values_fit();

    return ut_summary("every row of the panel fits the panel");
}
