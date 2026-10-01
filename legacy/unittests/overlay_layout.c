/* overlay_layout.c: where the panel's bands sit, and what the refusal band costs the rows.
 *
 * The footer's arithmetic is checked from unittests/overlay_keys.c, because that is where the
 * keys that move through the rows are, and the width question from unittests/overlay_legend.c;
 * this program holds the file's own rule, so a band drawn and never subtracted turns it red.
 *
 * What is under test is one rule in three parts that have to agree. The refusal band is ZERO high
 * while nothing stands in it, so a panel with nothing to say is exactly the panel that was there
 * before it existed. When something does stand, it appears at the BOTTOM, above the footer, and
 * that is the whole of why it is bearable: a refusal appears when somebody has just clicked at
 * something and is about to click again, and a band under the search box would have moved every
 * row down by 0.93 of a row between those two clicks with nothing under the pointer to say so.
 * Down there `rows_top` does not move, so no row a hand is already over changes; what the band
 * takes is the LAST row that fits, and the hit test refuses that row because it counts against
 * `visible_rows`. And a selection sitting on the row that no longer fits is pulled back into the
 * window by the build itself, since nothing else would until the next arrow key.
 *
 * The model is real here rather than stubbed, because overlay_layout.c asks it how far the list
 * is scrolled and how many rows there are, and a stub of that would only prove the stub.
 */
#include "unittest.h"

#include "overlay_draw.h"
#include "overlay_layout.h"
#include "overlay_look.h"
#include "overlay_model.h"
#include "overlay_notice.h"

#include "common/text.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

/* A 1280 by 720 display at the font's own size, with the tab words the panel measures. */
#define TEXT_H  16.0f
#define SCREEN_W 1280.0f
#define SCREEN_H 720.0f

static void build(void)
{
    const float tabs[OVERLAY_LAYOUT_TABS] = { 64.0f, 96.0f };

    overlay_layout_build(TEXT_H, 320.0f, overlay_model_row_count(), tabs, SCREEN_W, SCREEN_H);
}

/* A row of the panel, in screen coordinates: the middle of the row at position `i` on screen. */
static float row_middle(const layout_t *lay, uint32_t i)
{
    return lay->top + lay->rows_top + ((float)i + 0.5f) * lay->row_h;
}

/* More rows than the display can hold, so the band has a row to take and the list has an end to
 * fall off. Every group of the tab is open, which is how a player meets the longest list. */
static void a_tab_longer_than_the_screen(void)
{
    uint32_t group;

    overlay_model_reset();
    overlay_model_set_tab(OVERLAY_TAB_OPENPHANTOM);
    for (group = 0; group < (uint32_t)OVERLAY_GROUP_COUNT; ++group) {
        overlay_model_toggle_group(group);
    }
    overlay_model_rebuild();
}

static void test_an_empty_band_costs_nothing(void)
{
    layout_t shut;
    char     note[200];

    ut_section("with nothing to say the band costs no height at all");
    overlay_notice_forget();
    a_tab_longer_than_the_screen();
    build();
    shut = *overlay_layout();

    text_format(note, sizeof note, "the band is %.4f high with nothing in it",
                (double)shut.notice_h);
    note[sizeof note - 1] = '\0';
    ut_check(shut.notice_h == 0.0f, note);
    ut_check(shut.rows_top == shut.search_top + shut.search_h,
             "the rows begin where the search box ends: a panel with nothing to say lays out as "
             "if the band did not exist");
    ut_check(shut.notice_top == shut.foot_top,
             "and the band, being nothing at all, sits exactly where the footer begins");
}

static void test_a_standing_band_takes_the_last_row(void)
{
    layout_t shut;
    layout_t said;
    char     note[200];

    ut_section("with something to say it costs one band, and it costs it at the BOTTOM");
    overlay_notice_forget();
    a_tab_longer_than_the_screen();
    build();
    shut = *overlay_layout();

    overlay_notice_say("Refused: 240 is not a value that row takes");
    build();
    said = *overlay_layout();

    ut_check(said.notice_h > 0.0f, "the band has a height now");
    ut_check(said.notice_h == said.foot_h && said.notice_h == said.title_h,
             "and it is the band height the title and the footer have: a band holds one sentence "
             "the way they hold one word");
    text_format(note, sizeof note, "the rows began at %.4f and begin at %.4f",
                (double)shut.rows_top,
                (double)said.rows_top);
    note[sizeof note - 1] = '\0';
    ut_check(said.rows_top == shut.rows_top, note);
    ut_check(said.notice_top + said.notice_h == said.foot_top,
             "the band ends where the footer begins: it is the band directly above it, and the "
             "two abut the way every band in this panel does");
    ut_check(said.notice_top > said.rows_top, "and it begins below the rows, not above them");
    ut_check(said.search_top == shut.search_top && said.search_h == shut.search_h &&
                 said.title_h == shut.title_h && said.tab_h == shut.tab_h,
             "nothing above the rows moved either");
    ut_check(said.top == shut.top,
             "the panel still stands where it is anchored, which it does only because the band "
             "was counted against the rows rather than added to the height");
    ut_check(said.visible_rows == shut.visible_rows - 1u,
             "and it took exactly one row off the END of the list");
    ut_check(said.top + said.height <= SCREEN_H, "the whole of it is still on the display");
}

/* The half that is not the drawing. The pointer test divides from `rows_top` by `row_h` and
 * refuses anything past `visible_rows`, so it follows the band by construction; this is the check
 * that says so. Both directions matter and they are not one claim: the rows a hand is already
 * over must NOT move, and the row the band took must stop being a target. */
static void test_the_hit_test_follows(void)
{
    layout_t shut;
    float    first_row_y;
    float    last_row_y;

    ut_section("and the pointer follows it");
    overlay_notice_forget();
    a_tab_longer_than_the_screen();
    build();
    shut = *overlay_layout();
    first_row_y = row_middle(&shut, 0u);
    last_row_y  = row_middle(&shut, shut.visible_rows - 1u);

    ut_check(overlay_draw_row_at(shut.left + shut.width * 0.5f, first_row_y) == 0,
             "a point in the middle of the top row is the top row");

    overlay_notice_say("Refused: 240 is not a value that row takes");
    build();
    {
        const layout_t *said = overlay_layout();
        const float     x = said->left + said->width * 0.5f;

        ut_check(overlay_draw_row_at(x, first_row_y) == 0,
                 "the point that was the top row is STILL the top row: no row under a hand moved");
        ut_check(overlay_draw_row_at(x, last_row_y) < 0,
                 "and the point that was the last row is nothing at all, because the band is "
                 "drawn over it now");
        ut_check(overlay_draw_row_at(x, row_middle(said, said->visible_rows - 1u)) ==
                     (int32_t)said->visible_rows - 1,
                 "while the last row that still fits is still a target");
    }
}

/* The selection was on the row the band took. Nothing else would have moved it until the next
 * arrow key, so the panel would have drawn no current row at all for as long as the band stood. */
static void test_the_selection_is_pulled_back(void)
{
    uint32_t visible;

    ut_section("a selection on the row the band takes is pulled back into the window");
    overlay_notice_forget();
    a_tab_longer_than_the_screen();
    build();
    visible = overlay_layout()->visible_rows;
    ut_check(visible > 1u && overlay_model_row_count() > visible,
             "the list is longer than the window, so the window has an end to fall off");

    overlay_model_scroll_by(-(int32_t)overlay_model_row_count());
    overlay_model_set_selected((int32_t)visible - 1);
    ut_check(overlay_model_scroll(visible) == 0u &&
                 overlay_model_selected() == (int32_t)visible - 1,
             "and the selection is on the last row of it, with the list at the top");

    overlay_notice_say("Refused: 240 is not a value that row takes");
    build();
    {
        const uint32_t now = overlay_layout()->visible_rows;
        const uint32_t first = overlay_model_scroll(now);

        ut_check(now == visible - 1u, "the band takes the row the selection was on");
        ut_check(first == 1u,
                 "so the list has scrolled by exactly one, which is how far the selection had "
                 "left the window by");
        ut_check((uint32_t)overlay_model_selected() >= first &&
                     (uint32_t)overlay_model_selected() < first + now,
                 "and the selection is on screen again, on the row it was always on");
    }

    /* The row comes back when the band goes, and the list is left where the pull put it: the
     * scroll is the player's, and nothing here is undone behind them. */
    overlay_notice_forget();
    build();
    ut_check(overlay_layout()->visible_rows == visible, "and the row comes back when it goes");
    ut_check(overlay_model_scroll(visible) == 1u, "with the list left where it was put");
    overlay_model_set_selected(-1);
}

/* The band on the worst screen this game runs on, where a band costs the most, and on one too
 * short for anything: nothing may go negative and a row must remain. */
static void test_the_shortest_screens(void)
{
    const float tabs[OVERLAY_LAYOUT_TABS] = { 64.0f, 96.0f };
    const layout_t *lay;

    ut_section("the band on the screens where a band costs the most");
    overlay_notice_say("Refused: 240 is not a value that row takes");

    overlay_layout_build(12.0f, 300.0f, 200u, tabs, 640.0f, 480.0f);
    lay = overlay_layout();
    ut_check(lay->visible_rows >= 1u,
             "on a 640 by 480 screen there is still at least one row above the band");
    ut_check(lay->top == 2.0f * lay->text_h && lay->top + lay->height <= 480.0f,
             "and the panel still fits there, anchored");

    overlay_layout_build(16.0f, 320.0f, 200u, tabs, 1280.0f, 120.0f);
    lay = overlay_layout();
    ut_check(lay->visible_rows == 1u, "a screen shorter than the chrome still shows one row");
    ut_check(lay->notice_h > 0.0f && lay->notice_top > lay->rows_top && lay->height > 0.0f,
             "and no band of it goes to nothing or below");

    overlay_notice_forget();
}


/* ============================================================================================
 * The palette, which has no drawing to test it
 *
 * overlay_draw.c is linked into no test program, so a colour is only ever seen on a screen
 * nobody here has. What CAN be checked is the two claims the palette makes about itself, and
 * both are arithmetic:
 *
 *   ROLES. Four of them, and a value belongs to exactly one. Amber says where you are, green
 *   says a switch is on, the warn colour says refused, and everything else is grey. The failure
 *   this guards is the one the panel actually had: the RUN chip wearing the same colour as the
 *   selection frame, so eight rows claimed to be the current row at once. Put the accent back
 *   on any chip and the checks below go red.
 *
 *   CONTRAST. The body is the one translucent rectangle, ten percent transmission, so the
 *   surface every other colour sits on is computable: about 12/14/20 over a dark interior and
 *   37/39/45 over a white sky. The worst case is the sky, and the head of overlay_look.h says
 *   the text holds above ten to one there. That is a number, so it is checked rather than
 *   believed.
 * ========================================================================================== */
#define ALPHA_OF(c) (((c) >> 24) & 0xFFu)
#define RED_OF(c)   (((c) >> 16) & 0xFFu)
#define GREEN_OF(c) (((c) >> 8) & 0xFFu)
#define BLUE_OF(c)  ((c) & 0xFFu)

/* One channel of sRGB, taken back to light. The panel is drawn in sRGB and contrast is a
 * property of light, so the curve has to come off first; comparing the bytes directly is the
 * mistake that makes a mid grey look like it has half the brightness of white. */
static double to_light(double byte)
{
    const double c = byte / 255.0;

    return (c <= 0.04045) ? (c / 12.92) : pow((c + 0.055) / 1.055, 2.4);
}

static double luminance(double r, double g, double b)
{
    return 0.2126 * to_light(r) + 0.7152 * to_light(g) + 0.0722 * to_light(b);
}

static double opaque_luminance(uint32_t colour)
{
    return luminance((double)RED_OF(colour), (double)GREEN_OF(colour), (double)BLUE_OF(colour));
}

/* What the panel body actually becomes with a scene of `behind` behind it, per channel. */
static double body_luminance(double behind)
{
    const double a = (double)ALPHA_OF(C_PANEL_BODY) / 255.0;

    return luminance(a * (double)RED_OF(C_PANEL_BODY) + (1.0 - a) * behind,
                     a * (double)GREEN_OF(C_PANEL_BODY) + (1.0 - a) * behind,
                     a * (double)BLUE_OF(C_PANEL_BODY) + (1.0 - a) * behind);
}

static double ratio(double la, double lb)
{
    return (la > lb) ? ((la + 0.05) / (lb + 0.05)) : ((lb + 0.05) / (la + 0.05));
}

static double on_body(uint32_t ink, double behind)
{
    return ratio(opaque_luminance(ink), body_luminance(behind));
}

static double on_fill(uint32_t ink, uint32_t fill)
{
    return ratio(opaque_luminance(ink), opaque_luminance(fill));
}

/* Four roles, and a value belongs to exactly one of them. */
static void test_no_colour_carries_two_meanings(void)
{
    /* Every value that is NOT the accent, so that the accent appearing in any of them is a
     * second meaning for the one colour that says where you are. The RUN and Set chip is in
     * here on purpose: that is the pair that was blue and competed with the selection. */
    static const uint32_t not_the_accent[] = {
        C_CHIP_ACTION, C_CHIP_ACTION_TEXT, C_CHIP_ON, C_CHIP_ON_TEXT, C_CHIP_OFF,
        C_CHIP_OFF_TEXT, C_STATE_NA, C_ROW_TEXT, C_ROW_TEXT_DIM, C_GROUP_TEXT, C_GROUP_BAND,
        C_GROUP_HOT, C_ROW_HOT, C_LEADER, C_LEADER_HOT, C_RULE, C_BORDER, C_BAND_TITLE,
        C_TAB_ON_FILL, C_TAB_ON_TEXT, C_TAB_OFF_TEXT, C_FIELD_FILL, C_FIELD_BORDER,
        C_PLACEHOLDER, C_TYPED, C_WARN
    };
    /* Green means one thing, a switch that is on, and it is spent on the ON chip and on the
     * word a folded heading carries. Nothing else may be it. */
    static const uint32_t not_the_green[] = {
        C_ACCENT, C_ACCENT_DIM, C_ACCENT_TEXT, C_CHIP_ACTION, C_CHIP_ACTION_TEXT, C_CHIP_OFF,
        C_CHIP_OFF_TEXT, C_STATE_NA, C_ROW_TEXT, C_ROW_TEXT_DIM, C_LEADER, C_LEADER_HOT,
        C_RULE, C_BORDER, C_WARN
    };
    /* Everything that is opaque, which is everything: the body is the one exception and it is
     * checked on its own below. */
    static const uint32_t opaque[] = {
        C_ACCENT, C_ACCENT_DIM, C_ACCENT_TEXT, C_CHIP_ON, C_CHIP_ON_TEXT, C_CHIP_OFF,
        C_CHIP_OFF_TEXT, C_CHIP_ACTION, C_CHIP_ACTION_TEXT, C_STATE_NA, C_ROW_TEXT,
        C_ROW_TEXT_DIM, C_GROUP_TEXT, C_GROUP_BAND, C_GROUP_HOT, C_ROW_HOT, C_LEADER,
        C_LEADER_HOT, C_RULE, C_BORDER, C_BAND_TITLE, C_TAB_ON_FILL, C_TAB_ON_TEXT,
        C_TAB_OFF_TEXT, C_FIELD_FILL, C_FIELD_BORDER, C_PLACEHOLDER, C_TYPED, C_WARN,
        C_POINTER
    };
    size_t i;

    ut_section("four roles, and no colour carries two of them");

    for (i = 0; i < sizeof not_the_accent / sizeof not_the_accent[0]; ++i) {
        ut_checkf(not_the_accent[i] != C_ACCENT,
                  "entry %u is not the accent: only the selection, the heading bar, the fold"
                  " mark, a track and a chosen word may wear it", (unsigned)i);
    }
    for (i = 0; i < sizeof not_the_green / sizeof not_the_green[0]; ++i) {
        ut_checkf(not_the_green[i] != C_CHIP_ON,
                  "entry %u is not the on green: green says a switch is on and nothing else",
                  (unsigned)i);
    }
    ut_check(C_ACCENT != C_WARN, "where you are and refused are not the same value");
    ut_check(C_CHIP_ON != C_WARN, "on and refused are not the same value");
    ut_check(C_CHIP_ACTION_TEXT == C_ROW_TEXT,
             "RUN and Set are a grey chip with the panel's own text on it, not a colour");
    ut_check(C_CHIP_ACTION == C_CHIP_OFF,
             "and the chip behind them is the fill an OFF chip already uses");

    ut_section("exactly one rectangle is translucent");
    ut_check(ALPHA_OF(C_PANEL_BODY) != 0xFFu, "the body is the translucent one");
    for (i = 0; i < sizeof opaque / sizeof opaque[0]; ++i) {
        ut_checkf(ALPHA_OF(opaque[i]) == 0xFFu,
                  "entry %u is opaque, so its contrast does not depend on the scene",
                  (unsigned)i);
    }
}

/* The numbers the head of overlay_look.h writes down. A white sky behind the body is the worst
 * case and the only one that decides anything. */
static void test_the_contrasts_the_palette_claims(void)
{
    const double sky = 255.0;
    const double dark = 0.0;

    ut_section("the contrasts the palette claims, worst case first");

    ut_check(on_body(C_ROW_TEXT, sky) >= 10.0,
             "a row's name holds above ten to one against a white sky behind the body");
    ut_check(on_body(C_ROW_TEXT, dark) >= 10.0,
             "and against a dark interior as well");

    /* The accent measures 9.4 over the sky rather than above ten, and that is allowed here
     * because it is never a run of body text: it is a one pixel frame, a bar, a four pixel mark
     * and a track. Nine is the floor, so a darker amber cannot be slipped in unnoticed. */
    ut_check(on_body(C_ACCENT, sky) >= 9.0,
             "the accent holds above nine to one against a white sky behind the body");
    ut_check(on_body(C_ACCENT, dark) >= 10.0, "and above ten against a dark interior");

    ut_check(on_fill(C_ACCENT_TEXT, C_ACCENT) >= 10.0,
             "what is written on the accent holds above ten to one");
    ut_check(on_fill(C_CHIP_ON_TEXT, C_CHIP_ON) >= 4.5,
             "what is written on the on green clears the ordinary text floor");
    ut_check(on_fill(C_CHIP_ACTION_TEXT, C_CHIP_ACTION) >= 7.0,
             "the word in a RUN chip is the brightest thing in the panel against its own fill");
    ut_check(on_fill(C_CHIP_OFF_TEXT, C_CHIP_OFF) >= 4.5,
             "and the word in an OFF chip clears the same floor");
    ut_check(on_body(C_ROW_TEXT_DIM, sky) >= 4.5,
             "a note is dimmed but still readable over a white sky");

    /* The border is the one value asked to be seen against the scene itself rather than
     * against the body, on both sides of it. */
    ut_check(ratio(opaque_luminance(C_BORDER), luminance(255.0, 255.0, 255.0)) >= 3.0 &&
             ratio(opaque_luminance(C_BORDER), luminance(0.0, 0.0, 0.0)) >= 3.0,
             "the border is visible against a white sky and against a black interior");
}
int main(void)
{
    test_no_colour_carries_two_meanings();
    test_the_contrasts_the_palette_claims();
    test_an_empty_band_costs_nothing();
    test_a_standing_band_takes_the_last_row();
    test_the_hit_test_follows();
    test_the_selection_is_pulled_back();
    test_the_shortest_screens();
    return ut_summary("the panel's palette, its bands, and what the refusal band costs");
}
