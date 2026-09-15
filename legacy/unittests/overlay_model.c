/* overlay_model.c: the half of the overlay that can be checked without the game.
 *
 * What it decides is which rows are on screen, and every way of getting that wrong is quiet. Fold
 * a group the wrong way and the panel looks empty; match the search too eagerly and every cheat is
 * always listed; match it too strictly and typing the name of a cheat hides it. None of those
 * crash, none of them log, and all of them are only visible to somebody who already knows what the
 * list should have said.
 *
 * The cheat sources are deliberately not stubbed. None has resolved anything in a test process,
 * so the game's own toggles and one-shot actions are both empty and this project's tab holds
 * its rows with no site behind them, the state a player sees on an unsupported executable. That
 * is worth pinning down: it is the case where the panel must still open and still be usable.

 *
 * Two programs share the sources, the stubs (overlay_stubs.c) and the row positions
 * (overlay_rows.h): this one holds the navigation, the search, the cheats, the free camera and
 * the folds, and overlay_groups.c walks the settings groups by position. The split is by
 * subject, not by tab, so the checks that compare a row against its neighbours still find them
 * in the same file; it was one file until the tenth group put it over the size limit.
 */
#include "unittest.h"

#include "common/text.h"

#include "cheats_openphantom.h"
#include "cheats_original_actions.h"
#include "overlay_controls.h"
#include "overlay_dismember.h"
#include "overlay_fog.h"
#include "freecam_world.h"
#include "freeze_anim_row.h"
#include "overlay_freecam.h"
#include "overlay_levels.h"
#include "overlay_menu_extras.h"
#include "overlay_model.h"
#include "overlay_picture.h"
#include "overlay_row_ids.h"
#include "overlay_rows.h"
#include "overlay_utilities.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Every group starts folded and shows only its heading, so a folded tab has one row per group,
 * which is two on both tabs. */
static int row_count_after(const char *search)
{
    overlay_model_set_search(search);
    overlay_model_rebuild();
    return (int)overlay_model_row_count();
}

static int first_row_is_group(void)
{
    overlay_row_t row;

    return overlay_model_row(0, &row) && row.kind == OVERLAY_ROW_GROUP;
}

/* Every label and every chip has to fit, and this is the check that keeps it true.
 *
 * The panel is one bitmap font at one size, so a label too long for the room is drawn clipped with
 * an ellipsis in place of its end. Four rows were shipped that way and it was found in a
 * screenshot rather than by anything here, which is the wrong order. The chip has the same problem
 * one level down: it is a fixed buffer, so a value too long is truncated with no ellipsis at all,
 * and "auto 1.33x" reached a player as "auto 1.".
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
    uint32_t i;

    for (i = 0; i < count; ++i) {
        overlay_row_t row;
        char          note[160];

        if (!overlay_model_row(i, &row)) {
            continue;
        }
        text_format(note, sizeof note, "%s: row %u \"%s\" plus its chip \"%s\" is %u "
                    "characters, and the panel has room for %u", what, (unsigned)i, row.label,
                    row.value, (unsigned)(strlen(row.label) + strlen(row.value)),
                    (unsigned)ROW_BUDGET);
        note[sizeof note - 1] = '\0';
        ut_check(strlen(row.label) + strlen(row.value) <= ROW_BUDGET, note);

        text_format(note, sizeof note, "%s: row %u chip \"%s\" is %u characters, and the buffer "
                    "holds %u", what, (unsigned)i, row.value, (unsigned)strlen(row.value),
                    (unsigned)CHIP_BUDGET);
        note[sizeof note - 1] = '\0';
        ut_check(strlen(row.value) <= CHIP_BUDGET, note);
    }
}

/* Shared by the sections below, which run in order and hand state on to each other the
 * way one main used to. */
static overlay_row_t row;

static void test_search(void)
{
    ut_section("the search, which is pure and is where the quiet mistakes live");
    ut_check(overlay_model_matches("turntables", ""), "an empty search matches everything");
    ut_check(overlay_model_matches("turntables", NULL), "no search at all matches everything");
    ut_check(overlay_model_matches("turntables", "turn"), "a prefix matches");
    ut_check(overlay_model_matches("turntables", "tables"), "a suffix matches");
    ut_check(overlay_model_matches("turntables", "ntab"), "the middle matches");
    ut_check(overlay_model_matches("turntables", "TURN"), "the search ignores case");
    ut_check(overlay_model_matches("TurnTables", "turntables"), "so does the label");
    ut_check(!overlay_model_matches("turntables", "turntablesx"),
             "a search longer than the label does not match");
    ut_check(!overlay_model_matches("turntables", "zz"), "an absent substring does not match");
    ut_check(!overlay_model_matches("", "a"), "an empty label matches nothing but an empty search");
    ut_check(overlay_model_matches("", ""), "an empty label still matches an empty search");
    ut_check(!overlay_model_matches(NULL, "a"),
             "a missing label is refused rather than crashed on");
}

static void test_fresh_panel(void)
{
    ut_section("what a freshly opened panel shows");
    overlay_model_reset();
    overlay_model_rebuild();
    ut_check(overlay_model_tab() == OVERLAY_TAB_ORIGINAL, "it opens on the game's own cheats");
    ut_check(overlay_model_row_count() == 2u,
             "every group starts folded, so only the two Original headings show: the toggles and "
             "the one-shot actions, as two separate groups");
    ut_check(first_row_is_group(), "and the first of those rows is a heading");
    ut_check(overlay_model_row(1, &row) && row.kind == OVERLAY_ROW_GROUP,
             "so is the second: two groups, not one, on the Original tab");
    ut_check(overlay_model_search()[0] == '\0', "with nothing typed");
}

static void test_folding(void)
{
    ut_section("folding");
    overlay_model_reset();
    overlay_model_set_tab(OVERLAY_TAB_OPENPHANTOM);
    overlay_model_rebuild();
    ut_check(overlay_model_row_count() == HEADINGS,
             "the OpenPhantom tab holds eleven groups now, cheats, level selection, free camera, "
             "dismemberment, Cheatmenu options, in game options extras, enhanced resolution, fog, "
             "enhanced input, window and frame rate, and all of them start folded");
    overlay_model_toggle_group((uint32_t)OVERLAY_GROUP_OPENPHANTOM);
    overlay_model_rebuild();
    ut_check(overlay_model_row_count() == HEADINGS + OVERLAY_CHEATS_ROW_COUNT,
             "unfolding the cheats shows its heading, this project's cheats but free camera and "
             "the jump-boost scale row, with the level selection heading still folded below them");
    ut_check(overlay_model_row(1, &row) && row.kind == OVERLAY_ROW_CHEAT,
             "the row under the heading is a cheat");
    ut_check(!row.available,
             "and with no engine behind it the row reports itself unavailable rather than ticking");
    ut_check(!overlay_model_activate(1),
             "switching an unavailable cheat is refused instead of quietly doing nothing");
}

static void test_jump_scale_row(void)
{
    ut_section("the jump-boost scale row, right after jump boost's own toggle");
    /* Row 0 is the heading, rows 1..7 are the seven cheats ahead of jump boost in the enum, row 8
     * is jump boost's own toggle (id 7), and the scale row takes over free camera's OLD slot: id
     * CHEATS_OWN_COUNT-1, row CHEATS_OWN_COUNT, one level further out than the hotkey row used to
     * sit before this row was inserted ahead of it. */
    cheats_openphantom_jump_boost_set_scale(2.5f);
    overlay_model_rebuild();
    ut_check(overlay_model_row((uint32_t)CHEATS_OWN_COUNT, &row) &&
                 row.kind == OVERLAY_ROW_VALUE,
             "the row at free camera's old slot is now the jump-boost scale row");
    ut_check(strcmp(row.label, "Jump boost scale") == 0, "named for what it edits");
    ut_check(!row.available,
             "unavailable too, because it follows jump boost's own site, which resolved nothing "
             "here");
    ut_check(strcmp(row.value, "2.50x") == 0,
             "shows the current scale even though the cheat itself never armed; the number is "
             "real regardless of whether anything is hooked to multiply by it yet");
}

static void test_jump_scale_clamps(void)
{
    ut_section("the scale getter and setter clamp on their own, with no panel involved");
    cheats_openphantom_jump_boost_set_scale(0.1f);
    ut_check(cheats_openphantom_jump_boost_scale() > 0.49f &&
                 cheats_openphantom_jump_boost_scale() < 0.51f,
             "a value below the floor is clamped up to it rather than accepted as typed");
    cheats_openphantom_jump_boost_set_scale(99.0f);
    ut_check(cheats_openphantom_jump_boost_scale() > 4.99f &&
                 cheats_openphantom_jump_boost_scale() < 5.01f,
             "a value above the ceiling is clamped down to it the same way");
    cheats_openphantom_jump_boost_set_scale(1.3f);   /* restored for the sections below */
}

static void test_jump_scale_row_availability(void)
{
    ut_section("the scale row is gated behind availability the same as the hotkey row");
    /* Exactly the shape overlay_model_capture_hotkey()'s own row already has, and for the same
     * reason: a row that looked clickable but could never mean anything (nothing is hooked up to
     * read the number this would produce) would be worse than one that shows why it cannot be
     * touched yet, same as this file's own header comment already argues for the hotkey row. */
    ut_check(!overlay_model_is_editing_value(), "nothing is being edited yet");
    ut_check(!overlay_model_activate((uint32_t)CHEATS_OWN_COUNT),
             "starting an edit on an unavailable row is refused the same as any other cheat");
    ut_check(!overlay_model_is_editing_value(),
             "and refusing it must not have left an edit armed with nothing behind it");
}

static void test_edit_outside_capture(void)
{
    ut_section("the edit functions are harmless no-ops outside of a capture");
    /* Reachable directly without a resolved site. Unlike overlay_model_activate() above, none of
     * these four check availability, only whether a capture is actually running, so this is the
     * same "safe when called out of order" property overlay_model_search_backspace() already has
     * on an empty box. */
    cheats_openphantom_jump_boost_set_scale(1.3f);
    overlay_model_value_append('9');
    overlay_model_value_backspace();
    overlay_model_value_commit();
    overlay_model_value_cancel();
    ut_check(!overlay_model_is_editing_value(), "still nothing being edited");
    ut_check(cheats_openphantom_jump_boost_scale() > 1.29f &&
                 cheats_openphantom_jump_boost_scale() < 1.31f,
             "and the stored scale never moved, since none of the four had a capture to act on");
}

static void test_cheats_end(void)
{
    ut_section("the scale is the last row of the cheats");
    ut_check(overlay_model_row((uint32_t)CHEATS_OWN_COUNT + 1u, &row) &&
                 row.kind == OVERLAY_ROW_GROUP,
             "the row after the scale is the next group's heading: the level skip that used to "
             "sit here is the Level selection group's now");
}

static void test_levels_group(void)
{
    ut_section("the level selection group, directly under the cheats");
    ut_check(overlay_model_row(1u + OVERLAY_CHEATS_ROW_COUNT, &row) &&
                 row.kind == OVERLAY_ROW_GROUP && strcmp(row.label, "Level selection") == 0,
             "its heading follows the cheats group's last row, folded");
    overlay_model_toggle_group((uint32_t)OVERLAY_GROUP_OPENPHANTOM_LEVELS);
    overlay_model_rebuild();
    ut_check(overlay_model_row_count() == HEADINGS + OVERLAY_CHEATS_ROW_COUNT + 2u,
             "open, it holds two rows with its list shut: the skip and the start level");
    ut_check(overlay_model_row(LVL_ROW(0), &row) && row.kind == OVERLAY_ROW_ACTION &&
                 strcmp(row.label, "Skip to next level (debug)") == 0,
             "the skip first, an action, as it was at the tail of the cheats");
    ut_check(!row.available, "unavailable here, its site unresolved");
    ut_check(overlay_model_row(LVL_ROW(1), &row) && row.kind == OVERLAY_ROW_ACTION &&
                 strcmp(row.label, "New game starts at") == 0,
             "the start level second, the row that opens the list");
    ut_check(!row.available, "unavailable too, with the campaign sites unresolved");
    ut_check(strcmp(row.value, "Off") == 0, "and off as shipped: a new game starts where it did");
    ut_check(!overlay_model_activate(LVL_ROW(1)),
             "an unavailable row does not open its list");
    ut_check(overlay_model_row(LVL_ROW(2), &row) && row.kind == OVERLAY_ROW_GROUP &&
                 strcmp(row.label, "Free camera") == 0,
             "and the free camera heading comes straight after");
}

static void test_freecam_group(void)
{
    ut_section("the free camera group, directly under the level selection");
    ut_check(overlay_model_row(LVL_ROW(OVERLAY_LEVELS_ENTRY_FIRST), &row) &&
                 row.kind == OVERLAY_ROW_GROUP && strcmp(row.label, "Free camera") == 0,
             "its heading follows the level selection group's last row, folded");
    overlay_model_toggle_group((uint32_t)OVERLAY_GROUP_OPENPHANTOM_FREECAM);
    overlay_model_rebuild();
    ut_check(overlay_model_row_count() == HEADINGS + OVERLAY_CHEATS_ROW_COUNT + 2u + 5u,
             "open, it holds five rows with its fold shut: the key, the cheat, the two switches "
             "and the fold");

    ut_check(overlay_model_row(FC_ROW(0), &row) && row.kind == OVERLAY_ROW_HOTKEY,
             "the teleport key first, so the rows read as the steps they are: set a key, then "
             "the toggle below it stops reading unavailable");
    ut_check(!row.available,
             "unavailable here, because it follows free camera's own site, which resolved nothing");
    ut_check(strcmp(row.value, "Set") == 0,
             "unbound shows as an instruction to set one, not a blank chip or a stray ON/OFF");
    ut_check(!overlay_model_activate(FC_ROW(0)),
             "starting a capture on an unavailable row is refused the same as any other cheat");
    ut_check(!overlay_model_is_capturing_hotkey(),
             "and refusing it must not have left a capture armed with nothing behind it");

    ut_check(overlay_model_row(FC_ROW(1), &row) && row.kind == OVERLAY_ROW_CHEAT,
             "free camera itself sits one row after the key that gates it");
    ut_check(!row.available,
             "and is unavailable with no teleport key bound, whatever its site did");

    ut_check(overlay_model_row(FC_ROW(2), &row) && row.kind == OVERLAY_ROW_CHEAT &&
                 strcmp(row.label, "Animations freeze while paused") == 0,
             "the animation switch under the cheat: the pause's own look");
    ut_check(!row.on, "off as shipped: a pause keeps the look it had, the idles moving");
    ut_check(!row.available,
             "unavailable here, where the draw latch it writes resolved nothing");

    ut_check(overlay_model_row(FC_ROW(3), &row) && row.kind == OVERLAY_ROW_CHEAT &&
                 strcmp(row.label, "World runs while flying") == 0,
             "the world switch next, a setting for the next flight");
    ut_check(!row.on, "off as shipped: a flight holds the world still unless asked otherwise");
    ut_check(!row.available,
             "unavailable here with free camera's own site unresolved, as the cheat is");

    /* One or the other. Neither row is available in this process, its site unresolved, so the
       model refuses the click before the rule runs; the rule itself is one line each way in
       overlay_freecam_toggle(). What is pinned here is the shipped state: the freeze on and
       the world held, never both on. */
    ut_check(!freeze_anim_row_get() && !freecam_world_runs(),
             "as shipped both are off: the world is held and the animations run on the spot");

    ut_check(overlay_model_row(FC_ROW(4), &row) && row.kind == OVERLAY_ROW_INFO,
             "the how-to-fly fold last");
    ut_check(row.available, "always available, because it is a note and not gated behind any "
                            "site");
    ut_check(strcmp(row.label, "+ How free camera flies") == 0,
             "closed by default, marked with a plus the same way a group would be");
    ut_check(overlay_model_row(FC_ROW(5), &row) && row.kind == OVERLAY_ROW_GROUP &&
                 strcmp(row.label, "Dismemberment") == 0,
             "and the dismemberment heading comes straight after");
}

static void test_dismember_group(void)
{
    ut_section("the dismemberment group, one switch under its own heading");
    overlay_model_toggle_group((uint32_t)OVERLAY_GROUP_OPENPHANTOM_DISMEMBER);
    overlay_model_rebuild();
    ut_check(overlay_model_row_count() == HEADINGS + OVERLAY_CHEATS_ROW_COUNT + 2u + 5u + 1u,
             "open, it holds the one row");
    ut_check(overlay_model_row(DIS_ROW(0), &row) && row.kind == OVERLAY_ROW_CHEAT &&
                 strcmp(row.label, "Lightsaber dismemberment") == 0,
             "named for the thing itself: a reader looking for it is looking for the word, not "
             "for the node correction underneath it");
    ut_check(row.available,
             "always available. It writes a settings key and asks nothing of the game, so no "
             "unresolved site can grey it out");
    ut_check(overlay_model_row(DIS_ROW(1), &row) && row.kind == OVERLAY_ROW_GROUP &&
                 strcmp(row.label, "Cheatmenu options") == 0,
             "and the Cheatmenu options heading comes straight after");
    /* Folded back, so the fold sections below count the free camera group's rows against the
       headings alone; the settings groups under this one are walked by overlay_groups.c. */
    overlay_model_toggle_group((uint32_t)OVERLAY_GROUP_OPENPHANTOM_DISMEMBER);
    overlay_model_rebuild();
}

static void test_open_freecam_fold(void)
{
    ut_section("opening the how-to-fly fold");
    ut_check(overlay_model_activate(FC_ROW(4)),
             "clicking the fold's own summary row is accepted, unlike an ordinary note");
    overlay_model_rebuild();
    ut_check(overlay_model_row_count() ==
                 HEADINGS + OVERLAY_CHEATS_ROW_COUNT + 2u + 5u + OVERLAY_FREECAM_LINE_COUNT,
             "open, the free camera group holds the key, the cheat, the two switches, the fold's "
             "own summary and its eleven lines, with the utilities heading below them");
    ut_check(overlay_model_row(FC_ROW(4), &row) &&
                 strcmp(row.label, "- How free camera flies") == 0,
             "the summary itself now reads open, marked with a minus");
    /* Directly under the summary that revealed them, which is the only place a reader looks for
       them; they are the last rows of the group, so a slot there is its id. */
    ut_check(overlay_model_row(FC_ROW(5), &row) &&
                 row.kind == OVERLAY_ROW_INFO &&
                 strcmp(row.label, "    Needs a teleport key set first") == 0,
             "the first line sits immediately below the summary");
    ut_check(!overlay_model_activate(FC_ROW(5)),
             "but a line itself does nothing when clicked; only the summary is interactive");
    /* Eleven lines, not seven: the three that describe hiding the panel and the two ways out are
       each a sentence too long to fit the panel's width, so each is written as a line plus an
       indented continuation instead of being allowed to run off the edge. The count is what this
       pins down: a line added without the rows below it moving is the failure that would
       otherwise go unseen. */
    ut_check(overlay_model_row(FC_ROW(10), &row) &&
                 strcmp(row.label, "    Your Cheatmenu open key or Escape") == 0,
             "the sixth line names the key that hides the panel while the camera flies");
    ut_check(overlay_model_row(FC_ROW(14), &row) &&
                 strcmp(row.label, "    F4 ends the flight and leaves") == 0,
             "the tenth line names the other way out, the one that leaves the player put");
    ut_check(overlay_model_row(FC_ROW(15), &row) &&
                 strcmp(row.label, "      the player where they were") == 0,
             "and the eleventh is its continuation, indented past the line it finishes");

    /* The heading that was below the summary is still below the lines. A fold that reordered the
       rows around it would be worse than one that does not open. */
    ut_check(overlay_model_row(FC_ROW(16), &row) && row.kind == OVERLAY_ROW_GROUP &&
                 strcmp(row.label, "Dismemberment") == 0,
             "the dismemberment heading is pushed down the screen by the eleven lines");
}

static void test_close_freecam_fold(void)
{
    ut_section("closing the how-to-fly fold again");
    ut_check(overlay_model_activate(FC_ROW(4)),
             "the same summary row closes it back up");
    overlay_model_rebuild();
    ut_check(overlay_model_row_count() == HEADINGS + OVERLAY_CHEATS_ROW_COUNT + 2u + 5u,
             "its eleven lines are gone again, back to costing one row like any other");
    overlay_model_toggle_group((uint32_t)OVERLAY_GROUP_OPENPHANTOM_FREECAM);
    overlay_model_toggle_group((uint32_t)OVERLAY_GROUP_OPENPHANTOM_LEVELS);
    overlay_model_rebuild();
    ut_check(overlay_model_row_count() == HEADINGS + OVERLAY_CHEATS_ROW_COUNT,
             "and the group folds back to its heading like any other");
}

static void test_group_folds_back(void)
{
    ut_section("a group folds back exactly as it was");
    overlay_model_toggle_group((uint32_t)OVERLAY_GROUP_OPENPHANTOM);
    overlay_model_rebuild();
    ut_check(overlay_model_row_count() == HEADINGS,
             "folding it again leaves the ten headings alone");
}

static void test_original_actions_group(void)
{
    ut_section("the Original tab's second group: one-shot actions, not toggles");
    overlay_model_reset();
    overlay_model_toggle_group((uint32_t)OVERLAY_GROUP_ORIGINAL_ACTIONS);
    overlay_model_rebuild();
    ut_check(overlay_model_row_count() == 2u + (uint32_t)CHEATS_ACTION_COUNT,
             "both Original headings plus every one-shot action, the toggle group left folded");
    ut_check(overlay_model_row(2, &row) && row.kind == OVERLAY_ROW_ACTION,
             "a row under the actions heading is an action, not a cheat");
    ut_check(!row.available,
             "and with no engine behind it, unavailable rather than offered and inert");
    ut_check(!overlay_model_activate(2),
             "running an unavailable action is refused instead of quietly doing nothing");
}

static void test_queued_play_as_swap(void)
{
    ut_section("a queued play-as swap, before anything has resolved");
    ut_check(!cheats_original_actions_is_pending(CHEATS_ACTION_PLAY_OBI),
             "nothing is pending on an executable nothing resolved against: character 0 (Obi-Wan) "
             "must not read as queued just because it shares its index with an unresolved struct's "
             "own zero-initialised default");
    ut_check(cheats_original_actions_pending_label() == NULL,
             "and there is no label for a swap that was never queued");
    ut_check(overlay_model_row(5, &row) && row.id == (uint32_t)CHEATS_ACTION_PLAY_OBI,
             "row 5 under the actions heading is Play as Obi-Wan, by index");
    ut_check(!row.pending, "and it does not show as queued either");
}

static void test_typing_opens_the_hit_group(void)
{
    ut_section("typing opens the group that has hits, and clearing puts it back");
    overlay_model_reset();
    overlay_model_set_tab(OVERLAY_TAB_OPENPHANTOM);
    ut_check(row_count_after("") == HEADINGS, "all ten groups folded to start with");
    ut_check(row_count_after("zzzz") == HEADINGS,
             "a search nothing matches leaves them folded rather than opening any of them empty");
    ut_check(row_count_after("") == HEADINGS,
             "and clearing the box restores the folds you chose, not the ones the search "
             "forced");
}

static void test_search_box(void)
{
    ut_section("the search box itself");
    overlay_model_reset();
    overlay_model_search_append('a');
    overlay_model_search_append('B');
    ut_check(strcmp(overlay_model_search(), "aB") == 0, "characters are appended as typed");
    overlay_model_search_backspace();
    ut_check(strcmp(overlay_model_search(), "a") == 0, "backspace removes the last one");
    overlay_model_search_backspace();
    overlay_model_search_backspace();
    ut_check(overlay_model_search()[0] == '\0', "backspace on an empty box is harmless");
    overlay_model_search_append('\n');
    overlay_model_search_append((char)0x7F);
    ut_check(overlay_model_search()[0] == '\0',
             "a control character is refused, because the box could not show it");
}

static void test_search_box_overrun(void)
{
    ut_section("the box cannot be overrun");
    {
        int i;

        overlay_model_reset();
        for (i = 0; i < (int)OVERLAY_SEARCH_MAX * 2; ++i) {
            overlay_model_search_append('x');
        }
        ut_check(strlen(overlay_model_search()) == OVERLAY_SEARCH_MAX - 1u,
                 "typing past the end fills it and stops, leaving room for the terminator");
    }
}

static void test_missing_indices(void)
{
    ut_section("indices that do not exist");
    overlay_model_reset();
    overlay_model_rebuild();
    ut_check(!overlay_model_row(99u, &row), "a row past the end is refused");
    ut_check(!overlay_model_activate(99u), "and so is acting on one");
    overlay_model_set_tab((overlay_tab_t)99);
    ut_check(overlay_model_tab() == OVERLAY_TAB_ORIGINAL, "a tab that does not exist is ignored");
}

static void test_labels_and_chips_fit(void)
{
    ut_section("every label and every chip fits the room the panel leaves for it");
    /* Both tabs, both groups of each, everything unfolded, so the sweep sees every row this panel
       can put on screen rather than whichever ones happened to be open. */
    overlay_model_reset();
    overlay_model_set_tab(OVERLAY_TAB_ORIGINAL);
    overlay_model_toggle_group((uint32_t)OVERLAY_GROUP_ORIGINAL_TOGGLES);
    overlay_model_toggle_group((uint32_t)OVERLAY_GROUP_ORIGINAL_ACTIONS);
    overlay_model_rebuild();
    check_every_row_fits("the Original tab");

    overlay_model_set_tab(OVERLAY_TAB_OPENPHANTOM);
    overlay_model_toggle_group((uint32_t)OVERLAY_GROUP_OPENPHANTOM);
    overlay_model_toggle_group((uint32_t)OVERLAY_GROUP_OPENPHANTOM_UTILITIES);
    /* The window group is opened here too, and leaving it out is how a 54-character note reached a
     * screenshot past a 48-character budget: this walks the rows that are on screen, so a group
     * that is folded is a group that is not checked. Every group this tab has belongs in this
     * list, and the next one added does too. */
    overlay_model_toggle_group((uint32_t)OVERLAY_GROUP_OPENPHANTOM_FREECAM);
    overlay_model_toggle_group((uint32_t)OVERLAY_GROUP_OPENPHANTOM_DISMEMBER);
    overlay_model_toggle_group((uint32_t)OVERLAY_GROUP_OPENPHANTOM_MENU_EXTRAS);
    overlay_model_toggle_group((uint32_t)OVERLAY_GROUP_OPENPHANTOM_PICTURE);
    overlay_model_toggle_group((uint32_t)OVERLAY_GROUP_OPENPHANTOM_FOG);
    overlay_model_toggle_group((uint32_t)OVERLAY_GROUP_OPENPHANTOM_CONTROLS);
    overlay_model_toggle_group((uint32_t)OVERLAY_GROUP_OPENPHANTOM_WINDOW);
    overlay_model_rebuild();
    check_every_row_fits("the OpenPhantom tab");

    /* And with every fold open: their lines are the longest text in the panel and the only rows
       that are sometimes absent. */
    (void)overlay_model_activate(FC_ROW(4));
    overlay_model_rebuild();
    check_every_row_fits("the OpenPhantom tab with the fly fold open");
    (void)overlay_model_activate(MENU_ROW(1) + OVERLAY_FREECAM_LINE_COUNT);
    overlay_model_rebuild();
    check_every_row_fits("the OpenPhantom tab with two folds open");
    (void)overlay_model_activate(CTRL_ROW(7) + OVERLAY_FREECAM_LINE_COUNT +
                                 OVERLAY_MENU_EXTRAS_LINE_COUNT);
    overlay_model_rebuild();
    check_every_row_fits("the OpenPhantom tab with every fold open");
}

int main(void)
{
    test_search();
    test_fresh_panel();
    test_folding();
    test_jump_scale_row();
    test_jump_scale_clamps();
    test_jump_scale_row_availability();
    test_edit_outside_capture();
    test_cheats_end();
    test_levels_group();
    test_freecam_group();
    test_dismember_group();
    test_open_freecam_fold();
    test_close_freecam_fold();
    test_group_folds_back();
    test_original_actions_group();
    test_queued_play_as_swap();
    test_typing_opens_the_hit_group();
    test_search_box();
    test_search_box_overrun();
    test_missing_indices();
    test_labels_and_chips_fit();

    return ut_summary("the overlay's model");
}
