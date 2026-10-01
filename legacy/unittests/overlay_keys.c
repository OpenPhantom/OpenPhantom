/* overlay_keys.c: what the panel keeps when it closes, the row the keyboard is on, and the keys
 * that move it.
 *
 * The keys are driven through overlay_keys.c itself and not only through overlay_model.c, so left
 * and right, Home, End and Return are checked, and with them the line that keeps every one of those
 * off a panel that is open but not drawn. That line is the whole of the difference between a Return
 * that acts on the row a player is looking at and a Return that runs whatever an invisible
 * selection is sitting on while the free camera flies.
 *
 * Two rules that are pure. The panel keeps where the player was when it closes, so looking at one
 * setting in the game and coming back costs no steps, and it still ends what was half done,
 * because a key capture that survived would swallow the next key pressed in the game. And the
 * keyboard has a row of its own, so a machine with neither a mouse nor a pad can still change
 * things in this panel.
 *
 * Both are checked against the real model, with the real groups behind it, because a stub of the
 * row list would only prove the stub: what is interesting is exactly that the row count moves
 * under the selection as groups fold and the search filters, which is the case a fixed list of
 * rows cannot produce.
 */
#include "unittest.h"

#include "overlay_choice.h"
#include "overlay_keys.h"
#include "overlay_layout.h"
#include "overlay_model.h"
#include "overlay_notice.h"
#include "overlay_number.h"
#include "overlay_slider.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* The keys as the window procedure sees them, spelled the way overlay_keys.c spells them. */
#define KEY_RETURN    0x0D
#define KEY_END       0x23
#define KEY_HOME      0x24
#define KEY_LEFT      0x25
#define KEY_UP        0x26
#define KEY_RIGHT     0x27
#define KEY_DOWN      0x28
#define KEY_LETTER_A  0x41

/* Shared by the sections below, which run in order and hand state on to each other. */
static overlay_row_t row;

/* A layout the navigation can move through. Nothing here draws, but overlay_keys.c reads
 * `visible_rows` to keep the selection on screen and to work out what a page is, and before the
 * first build that number is zero. The numbers are a 1280 by 720 display at the font's own size. */
static void a_screen(void)
{
    const float tabs[OVERLAY_LAYOUT_TABS] = { 64.0f, 96.0f };

    overlay_layout_build(16.0f, 320.0f, overlay_model_row_count(), tabs, 1280.0f, 720.0f);
}

/* The first row on screen of a kind, or -1. Used by the two sections below so that neither has to
 * count to a row whose position is not what it is checking. */
static int32_t first_row_of_kind(overlay_row_kind_t kind, bool must_be_available)
{
    const uint32_t count = overlay_model_row_count();
    uint32_t       i;

    for (i = 0; i < count; ++i) {
        overlay_row_t candidate;

        if (overlay_model_row(i, &candidate) && candidate.kind == kind &&
            (!must_be_available || candidate.available)) {
            return (int32_t)i;
        }
    }
    return -1;
}

/* What closing the panel keeps and what it ends.
 *
 * It keeps the tab, the search box and the open groups, so looking at one setting in the game and
 * coming back costs no steps. What has to end is what is HALF DONE, because a key capture that
 * survived would swallow the next key pressed in the game and a half typed number would come back
 * with digits in it from a minute ago. */
static void test_the_panel_remembers(void)
{
    int32_t at;

    ut_section("closing the panel keeps where the player was and ends what was half done");
    overlay_model_reset();
    overlay_model_set_tab(OVERLAY_TAB_OPENPHANTOM);
    overlay_model_toggle_group((uint32_t)OVERLAY_GROUP_OPENPHANTOM_UTILITIES);
    overlay_model_set_search("size");
    overlay_model_rebuild();

    at = first_row_of_kind(OVERLAY_ROW_VALUE, true);
    ut_check(at >= 0 && overlay_model_activate((uint32_t)at) && overlay_model_is_editing_value(),
             "a number is being typed into");
    overlay_model_value_append('2');
    overlay_model_scroll_by(2);
    /* And a refusal standing in the band, which is the third thing that is half done. It used to
     * be cleared beside the close in overlay_input.c, where only one of the four ways of closing
     * the panel reached it, so the band came back on the next opening saying something about a
     * keystroke from the visit before and costing the rows a band of height. */
    overlay_notice_say("Refused: 9999 is not a value that row takes");
    ut_check(overlay_notice_text() != NULL, "and a refusal is standing in the band");

    /* Exactly what the panel does when it closes. */
    overlay_model_forget_edits();

    ut_check(!overlay_model_is_editing_value(),
             "closing ends the typing rather than leaving a field armed at a row nobody can see");
    ut_check(!overlay_model_is_capturing_hotkey(), "and no key capture is left waiting either");
    ut_check(overlay_notice_text() == NULL,
             "and the band is empty, whichever key or button did the closing");
    ut_check(overlay_model_tab() == OVERLAY_TAB_OPENPHANTOM, "the tab is where the player left it");
    ut_check(strcmp(overlay_model_search(), "size") == 0, "so is what they typed into the search");
    overlay_model_rebuild();
    ut_check(first_row_of_kind(OVERLAY_ROW_VALUE, true) >= 0,
             "and the group they opened is still open, with its rows on screen");
    ut_check(overlay_model_scroll(1u) == 2u, "and the list is still scrolled where they left it");

    ut_check(overlay_model_activate((uint32_t)first_row_of_kind(OVERLAY_ROW_VALUE, true)) &&
                 overlay_model_is_editing_value(),
             "starting a new edit works, and it starts empty");
    overlay_model_value_cancel();
    overlay_model_set_search("");
}

/* The keyboard's own row, stepped through like the pad's, so a machine with neither a mouse nor a
 * pad can still change things in this panel. */
static void test_keyboard_selection(void)
{
    uint32_t count;

    ut_section("the row the keyboard is on");
    overlay_model_reset();
    overlay_model_set_tab(OVERLAY_TAB_OPENPHANTOM);
    overlay_model_rebuild();
    count = overlay_model_row_count();

    ut_check(overlay_model_selected() < 0, "nothing is selected until a key moves it");
    overlay_model_move_selection(1, 0u);
    ut_check(overlay_model_selected() == 0,
             "the first press lands on the row the player is looking at rather than at the top of "
             "a list they may have scrolled a long way down");
    overlay_model_move_selection(1, 0u);
    ut_check(overlay_model_selected() == 1, "and the next moves one row on");
    overlay_model_move_selection(-5, 0u);
    ut_check(overlay_model_selected() == 0, "it cannot be pushed off the top");
    overlay_model_move_selection(500, 0u);
    ut_check(overlay_model_selected() == (int32_t)count - 1, "nor off the bottom");

    ut_check(overlay_model_activate((uint32_t)overlay_model_selected()),
             "Return acts on it: the last heading folds open like any other");
    overlay_model_rebuild();
    ut_check(overlay_model_row_count() > count, "and the rows under it are on screen");

    /* The list shrinking under it is the case the scroll already had to survive, and for the same
       reason: the row count moves with every fold and every keystroke in the search box. */
    overlay_model_set_selected((int32_t)overlay_model_row_count() - 1);
    overlay_model_activate(0u);          /* folds the first group, which shortens the list */
    overlay_model_rebuild();
    ut_check(overlay_model_selected() >= 0 &&
                 overlay_model_selected() < (int32_t)overlay_model_row_count(),
             "a selection past the end of a list that shrank reads as its last row, never past it");

    overlay_model_set_selected(-1);
    ut_check(overlay_model_selected() < 0,
             "and it clears, which is what a hand on the mouse does to it");
    overlay_model_move_selection(1, 0u);
    overlay_model_set_tab(OVERLAY_TAB_ORIGINAL);
    ut_check(overlay_model_selected() < 0,
             "a tab change drops it too: an index into a list that has been replaced names a row "
             "nobody chose");
}

/* The keys themselves, through overlay_keys_navigate().
 *
 * The panel is drawn for all of this; test_an_invisible_panel_takes_no_keys() takes it away. */
static void test_the_keys(void)
{
    int32_t at;

    ut_section("the keys that move the selection");
    overlay_model_reset();
    overlay_model_set_tab(OVERLAY_TAB_OPENPHANTOM);
    overlay_model_rebuild();
    a_screen();
    ut_check(overlay_layout()->visible_rows > 2u, "the panel has room for more than two rows");

    ut_check(overlay_keys_navigate(KEY_DOWN, true, false) && overlay_model_selected() == 0,
             "the first Down lands on the top row on screen");
    ut_check(overlay_keys_navigate(KEY_DOWN, true, false) && overlay_model_selected() == 1,
             "and the next moves one row on");
    ut_check(overlay_keys_navigate(KEY_UP, true, false) && overlay_model_selected() == 0,
             "Up moves back");
    ut_check(overlay_keys_navigate(KEY_END, true, false) &&
                 overlay_model_selected() == (int32_t)overlay_model_row_count() - 1,
             "End goes to the last row");
    ut_check(overlay_keys_navigate(KEY_HOME, true, false) && overlay_model_selected() == 0,
             "and Home back to the first");
    ut_check(!overlay_keys_navigate(KEY_LETTER_A, true, false),
             "a key that is none of them is handed back rather than swallowed, so typing into the "
             "search box is undisturbed");

    ut_section("Return and the sideways keys");
    ut_check(overlay_model_row(0u, &row) && row.kind == OVERLAY_ROW_GROUP && !row.expanded,
             "the first row is a folded heading");
    ut_check(overlay_keys_navigate(KEY_RETURN, true, false) && overlay_model_row(0u, &row) &&
                 row.expanded,
             "Return on it folds it open");
    ut_check(overlay_keys_navigate(KEY_LEFT, true, false) && overlay_model_row(0u, &row) &&
                 !row.expanded,
             "Left on an open heading shuts it again rather than changing the tab");
    ut_check(overlay_keys_navigate(KEY_RIGHT, true, false) && overlay_model_row(0u, &row) &&
                 row.expanded,
             "and Right opens it, which is what makes a tree out of a list");

    /* On anything that is not a heading, sideways is the tab, because a row has nothing to open. */
    at = first_row_of_kind(OVERLAY_ROW_CHEAT, false);
    ut_check(at > 0, "there is a row under the open heading");
    overlay_model_set_selected(at);
    ut_check(overlay_keys_navigate(KEY_LEFT, true, false) &&
                 overlay_model_tab() == OVERLAY_TAB_ORIGINAL,
             "Left on an ordinary row changes the tab");
    ut_check(overlay_keys_navigate(KEY_RIGHT, true, false) &&
                 overlay_model_tab() == OVERLAY_TAB_OPENPHANTOM,
             "and Right changes it back");
    ut_check(!overlay_keys_navigate(KEY_RIGHT, true, false) ||
                 overlay_model_tab() == OVERLAY_TAB_OPENPHANTOM,
             "past the last tab it stops rather than wrapping");
}

/* Left and Right on a row of WORDS.
 *
 * What this can and cannot hold, because the difference matters more than the checks. The one row
 * of words the panel has belongs to the entity spawner, and that group is unavailable in a test
 * process: there is no level and no player, so npc_spawner_is_available() answers false and every
 * row of the group reads unavailable. A row of words that cannot be used has no words to walk, so
 * what is pinned below is the FALL-THROUGH: the key reaches the tab, and the choice under it does
 * not move. That catches the arm being widened to swallow the key on a locked row, and it catches
 * the choice being written through a row nobody can act on.
 *
 * It does NOT catch the arm being deleted, because a deleted arm falls through to the tab as well.
 * Catching that needs a row of words that can be used, which needs a spawner that answers:
 * unittests/overlay_spawner.c stands one in for npc_spawner.c and walks the words there. The
 * words themselves, the step to the neighbour and the -1 at each end are checked in
 * unittests/overlay_choice.c against the real row. */
static void test_sideways_on_a_row_of_words(void)
{
    int32_t  at;
    uint32_t was;

    ut_section("Left and Right on a row of words");
    overlay_model_reset();
    overlay_model_set_tab(OVERLAY_TAB_OPENPHANTOM);
    overlay_model_toggle_group((uint32_t)OVERLAY_GROUP_OPENPHANTOM_SPAWN);
    overlay_model_rebuild();
    a_screen();

    at = first_row_of_kind(OVERLAY_ROW_SEGMENT, false);
    ut_check(at >= 0 && overlay_model_row((uint32_t)at, &row) && !row.available,
             "the spawner's row of words is on screen, and it cannot be used here");
    ut_check(!overlay_choice_is_strip(&row),
             "so it is not drawn as a strip either: the paint, the panel's width and this rule "
             "read that off one answer");
    was = row.chosen;

    overlay_model_set_selected(at);
    ut_check(overlay_keys_navigate(KEY_LEFT, true, false) &&
                 overlay_model_tab() == OVERLAY_TAB_ORIGINAL,
             "Left on it changes the tab, as it does on every other row that cannot be acted on");
    overlay_model_set_tab(OVERLAY_TAB_OPENPHANTOM);
    overlay_model_rebuild();
    ut_check(overlay_model_row((uint32_t)at, &row) && row.chosen == was,
             "and the word it stands on has not moved: a choice is not written through a row "
             "nobody can act on");
    overlay_model_reset();
    overlay_model_set_tab(OVERLAY_TAB_OPENPHANTOM);
    overlay_model_rebuild();
}

/* The first usable number row on screen, and whether it has a track under it. Found by
   SHAPE and not through the search box: a track carries no label, so a search matches the
   number above it and hides the very row half of this section is about. */
static int32_t a_number_row(bool with_a_track)
{
    const uint32_t count = overlay_model_row_count();
    uint32_t       i;

    for (i = 0; i < count; ++i) {
        overlay_row_t here;
        overlay_row_t below;
        bool          has_track;

        if (!overlay_model_row(i, &here) || here.kind != OVERLAY_ROW_VALUE ||
            !here.available) {
            continue;
        }
        has_track = overlay_model_row(i + 1u, &below) &&
                    below.kind == OVERLAY_ROW_SLIDER && below.available;
        if (has_track == with_a_track) {
            return (int32_t)i;
        }
    }
    return -1;
}

/* The number on the row above the track at `track`, read back out of the panel. */
static float number_at(int32_t track)
{
    overlay_row_t on_it;

    return (track > 0 && overlay_model_row((uint32_t)track, &on_it)) ? on_it.fraction : -1.0f;
}

/* The third arm of the sideways rule: on a number row and on the track under it, Left and Right
 * change the VALUE and do not change the tab.
 *
 * Driven through overlay_keys_navigate() and not through the rule underneath it, because
 * the way this went wrong before was a rule written in one place and a key that never
 * reached it. The picture group's draw distance is the row: it writes a settings file the
 * test process can write, so a press here really does move a number.
 *
 * The tab is what the check is about. A number row that fell through to the tab would look
 * like a working panel until somebody pressed Left on a slider and found themselves on the
 * other tab. */
static void test_sideways_on_a_number(void)
{
    int32_t value_row;
    int32_t track;
    float   was;

    ut_section("Left and Right on a number row change the number, not the tab");
    overlay_model_reset();
    overlay_model_set_tab(OVERLAY_TAB_OPENPHANTOM);
    overlay_model_toggle_group((uint32_t)OVERLAY_GROUP_OPENPHANTOM_PICTURE);
    overlay_model_rebuild();
    a_screen();

    value_row = a_number_row(true);
    track = value_row + 1;
    ut_check(value_row >= 0,
             "the picture group is open, and its draw distance has a track on the line "
             "under it");

    overlay_model_set_selected(value_row);
    was = number_at(track);
    ut_check(overlay_keys_navigate(KEY_RIGHT, true, false) &&
                 overlay_model_tab() == OVERLAY_TAB_OPENPHANTOM,
             "Right on the number is taken, and the tab does not move");
    ut_check(number_at(track) > was, "the handle has gone up");

    was = number_at(track);
    ut_check(overlay_keys_navigate(KEY_LEFT, true, false) &&
                 overlay_model_tab() == OVERLAY_TAB_OPENPHANTOM,
             "and Left is taken as well");
    ut_check(number_at(track) < was, "and takes it down again");

    /* The modifier moves further. It is the one thing about the press that nothing else in
     * the panel reads, so a coarse step wired to the fine one would show nowhere else. */
    {
        float fine;
        float coarse;

        was = number_at(track);
        (void)overlay_keys_navigate(KEY_RIGHT, true, false);
        fine = number_at(track) - was;
        was = number_at(track);
        (void)overlay_keys_navigate(KEY_RIGHT, true, true);
        coarse = number_at(track) - was;
        ut_check(coarse > fine * 2.0f,
                 "a press with the modifier held moves further than one without it");
    }

    /* The track itself, which is the other row the rule names. */
    overlay_model_set_selected(track);
    was = number_at(track);
    ut_check(overlay_keys_navigate(KEY_LEFT, true, false) &&
                 overlay_model_tab() == OVERLAY_TAB_OPENPHANTOM,
             "Left on the TRACK is taken too, and still does not change the tab");
    ut_check(number_at(track) < was, "and moves the same number");

    /* Both ends. A row that fell through to the tab at the end of its own track would walk
     * off the panel the moment somebody held a direction down. */
    {
        int32_t i;

        for (i = 0; i < 40; ++i) {
            (void)overlay_keys_navigate(KEY_LEFT, true, true);
        }
        ut_check(overlay_model_tab() == OVERLAY_TAB_OPENPHANTOM,
                 "holding Left to the bottom end of the track leaves the tab where it was");
        ut_check(number_at(track) == 0.0f, "and the handle stops at the end");
        for (i = 0; i < 60; ++i) {
            (void)overlay_keys_navigate(KEY_RIGHT, true, true);
        }
        ut_check(overlay_model_tab() == OVERLAY_TAB_OPENPHANTOM &&
                     number_at(track) == 1.0f,
                 "and the same at the top end");
    }

    ut_section("a press goes through the one write path, not around it");
    /* The pad's triggers hold whatever track the pointer is over, every frame, so this is
     * the ordinary state and not a corner of one. A key that wrote to the model directly
     * would move the number and leave the HANDLE where the hand had it: the drawing would
     * show a handle at one value and, an inch away, the number of another. */
    {
        float where = 0.0f;

        overlay_slider_take(OVERLAY_SLIDER_TRIGGER, track, 0.5f);
        overlay_model_set_selected(value_row);
        (void)overlay_keys_navigate(KEY_RIGHT, true, false);
        ut_check(overlay_slider_held(NULL, &where) && where > 0.5f,
                 "the press moved the handle the hand is holding, which it can only do by "
                 "going through the one hold every other hand writes through");
        overlay_slider_let_go(OVERLAY_SLIDER_TRIGGER);
        overlay_model_rebuild();
    }

    ut_section("Return on a track is its Default");
    overlay_model_set_selected(track);
    /* The draw distance's standard is 1.0, which is the bottom of its own track, so the
     * handle has to come all the way back from the top end it was just driven to. */
    ut_check(overlay_keys_navigate(KEY_RETURN, true, false) && number_at(track) == 0.0f,
             "Return on the track puts the row back to its standard");

    ut_section("the sideways keys reach a row that is being typed into");
    /* The state a player reaches by clicking a number: the edit is running and the panel is
     * in that row's mode. overlay_input.c is what ends the typing when a sideways key
     * arrives; what is checked here is the half that is testable without a window, which is
     * that the row is still the current one and the rule still acts on it. */
    overlay_model_set_selected(value_row);
    ut_check(overlay_model_activate((uint32_t)value_row) && overlay_model_is_editing_value(),
             "the number is being typed into");
    ut_check(overlay_model_selected() == value_row,
             "and the row being typed into is the current row, which is what the sideways "
             "keys act on");
    /* Off the bottom end first, since the Default above put it there and a press against an end
       moves nothing. */
    (void)overlay_keys_navigate(KEY_RIGHT, true, true);
    was = number_at(track);
    /* LEFT, because Right at the last tab stops anyway and would pass whatever this did: the
       claim is that the key never reaches the tab, so it has to be the one that would move it. */
    ut_check(overlay_keys_navigate(KEY_LEFT, true, false) &&
                 overlay_model_tab() == OVERLAY_TAB_OPENPHANTOM,
             "a sideways key mid-edit is still the row's own and not the tab's");
    ut_check(number_at(track) < was, "and it moves the number");
    overlay_model_value_cancel();

    ut_section("a row with no track of its own");
    /* The dev menu size is one: its ends depend on the picture it is drawn in and a pull on
     * it would move the track being pulled, so it stays a number that is typed. */
    overlay_model_reset();
    overlay_model_set_tab(OVERLAY_TAB_OPENPHANTOM);
    overlay_model_toggle_group((uint32_t)OVERLAY_GROUP_OPENPHANTOM_UTILITIES);
    overlay_model_rebuild();
    a_screen();
    value_row = a_number_row(false);
    ut_check(value_row >= 0, "there is a number with nothing under it");
    overlay_model_set_selected(value_row);
    ut_check(overlay_keys_navigate(KEY_LEFT, true, false) &&
                 overlay_model_tab() == OVERLAY_TAB_ORIGINAL,
             "Left on it changes the tab, as it does on any row with nothing to set");

    overlay_model_reset();
    overlay_model_set_tab(OVERLAY_TAB_OPENPHANTOM);
    overlay_model_rebuild();
}

/* Open and not drawn: the free camera in flight and the placement mode. Both hand every key they
 * do not use straight on, so without this the arrows would move a selection nobody can see, left
 * and right would change the tab behind the picture, and Return would ACT on that selection. The
 * keys are handed back rather than swallowed, which leaves them exactly where they were before
 * any of this existed. */
static void test_an_invisible_panel_takes_no_keys(void)
{
    int32_t was;

    ut_section("a panel that is open and not drawn takes none of them");
    overlay_model_reset();
    overlay_model_set_tab(OVERLAY_TAB_OPENPHANTOM);
    overlay_model_rebuild();
    a_screen();
    overlay_model_set_selected(0);
    was = overlay_model_selected();

    ut_check(!overlay_keys_navigate(KEY_DOWN, false, false) && overlay_model_selected() == was,
             "Down moves nothing");
    ut_check(!overlay_keys_navigate(KEY_END, false, false) && overlay_model_selected() == was,
             "End jumps nowhere");
    ut_check(!overlay_keys_navigate(KEY_RIGHT, false, false) &&
                 overlay_model_tab() == OVERLAY_TAB_OPENPHANTOM,
             "Right leaves the tab alone");
    ut_check(overlay_model_row(0u, &row) && !row.expanded,
             "the heading under the selection is shut");
    ut_check(!overlay_keys_navigate(KEY_RETURN, false, false) && overlay_model_row(0u, &row) &&
                 !row.expanded,
             "and Return does not act on a row nobody can see");
}

/* The bands the keys move through, and the one that was added under them.
 *
 * The footer is a band like the title, so its height comes off the rows in overlay_layout.c and
 * not where it is drawn. Everything below the last row that fits is reached by scrolling, and the
 * hit test counts rows from `rows_top` by `row_h`; a band taken off anywhere else leaves the last
 * row drawn where nothing can be clicked, and both halves look right on their own. */
static void test_the_bands(void)
{
    const float tabs[OVERLAY_LAYOUT_TABS] = { 64.0f, 96.0f };
    const layout_t *lay;

    ut_section("the panel's bands, and the footer under the list");
    overlay_layout_build(16.0f, 320.0f, 200u, tabs, 1280.0f, 720.0f);
    lay = overlay_layout();
    ut_check(lay->foot_h > 0.0f, "there is a footer");
    /* The footer is the same band height as the title, which is what frames the list between
     * two bands of one size. The two come from two different constants, so this is a claim
     * and not a restatement.
     *
     * What stood here were two identities: `foot_top + foot_h == height` and
     * `rows_top + visible_rows * row_h <= foot_top`. overlay_layout.c writes foot_top as
     * `height - foot_h` and height as the rows plus the bottom pad plus foot_h, so both were
     * asking that file to agree with itself and neither could ever be red. Under the mutation
     * that drew the footer without taking it off the rows, both stayed green; what went red
     * was the anchor check below, which compares against the display. */
    ut_check(lay->foot_h == lay->title_h,
             "and it is the same height as the title band, so the list is framed by two bands "
             "of one size");
    /* The panel is anchored two text heights down from the top, and it is only pushed up from
     * there when it does not fit. So "it still stands where it was anchored" is the check that
     * catches a band drawn but not taken off the rows: the row count would be one too many, the
     * panel one band too tall, and the whole of it would slide up the screen. Asking instead
     * whether the panel is INSIDE the display proves nothing, because that push is exactly what
     * keeps it inside. */
    ut_check(lay->top == 2.0f * lay->text_h,
             "the panel still stands where it is anchored, which it does only because the rows "
             "were counted with the footer already taken off");
    ut_check(lay->top + lay->height <= 720.0f, "and the whole of it is on the display");

    /* One more row would not fit. That is the claim `visible_rows` makes, and it is the one a
     * footer drawn but not subtracted would break. */
    ut_check(lay->rows_top + (float)(lay->visible_rows + 1u) * lay->row_h +
                 0.5f * lay->text_h + lay->foot_h + 2.0f * lay->text_h > 720.0f,
             "it is as many rows as fit and not one more, counting the footer as the band it is");

    /* 640 by 480 with H at the authored twelve pixels: the shortest display this game runs on,
     * and the one where a band costs the most. It is not the smallest H the panel can be asked
     * for: DevMenuSize runs from 0.33 up to a ceiling that follows the screen's height, and on
     * 480 lines that ceiling is 1.0 (screen_ceiling() in dev_menu_size_row.c). The WIDTH
     * question on this screen is in unittests/overlay_legend.c. */
    overlay_layout_build(12.0f, 300.0f, 200u, tabs, 640.0f, 480.0f);
    lay = overlay_layout();
    ut_check(lay->visible_rows >= 1u, "on a 640 by 480 screen there is still at least one row");
    ut_check(lay->top == 2.0f * lay->text_h && lay->top + lay->height <= 480.0f,
             "and it still fits there, anchored, on the shortest display this game runs on");

    /* A screen too short for even one row: the panel keeps one anyway, because a panel with no
     * rows at all is a worse answer than a cramped one, and nothing may go negative. */
    overlay_layout_build(16.0f, 320.0f, 200u, tabs, 1280.0f, 120.0f);
    lay = overlay_layout();
    ut_check(lay->visible_rows == 1u, "a screen shorter than the chrome still shows one row");
    ut_check(lay->foot_h > 0.0f && lay->foot_top > 0.0f && lay->height > 0.0f,
             "and no band of it goes to nothing or below");
}

int main(void)
{
    test_the_panel_remembers();
    test_keyboard_selection();
    test_the_keys();
    test_sideways_on_a_number();
    test_sideways_on_a_row_of_words();
    test_an_invisible_panel_takes_no_keys();
    test_the_bands();
    return ut_summary("the panel's memory, the keyboard's row, the keys and the bands");
}
