/* overlay_model.c: the half of the overlay that can be checked without the game.
 *
 * What it decides is which rows are on screen, and every way of getting that wrong is quiet. Fold
 * a group the wrong way and the panel looks empty; match the search too eagerly and every cheat is
 * always listed; match it too strictly and typing the name of a cheat hides it. None of those
 * crash, none of them log, and all of them are only visible to somebody who already knows what the
 * list should have said.
 *
 * The three cheat sources are deliberately not stubbed. None has resolved anything in a test
 * process, so the game's own toggles and one-shot actions are both empty and this project's tab
 * holds its two rows with no site behind them, which is exactly the state a player sees on an
 * unsupported executable. That is worth pinning down: it is the case where the panel must still
 * open and still be usable.
 */
#include "unittest.h"

#include "cheats_openphantom.h"
#include "cheats_original_actions.h"
#include "overlay_model.h"

#include <string.h>

/* The drawing and input halves, stubbed, and only these two.
 *
 * The sources under test reach across to the other half of the overlay in two places: the size row
 * asks the renderer how big the screen is, and the level skip and the free camera both ask the
 * input half to close the panel. Linking the real overlay_draw.c and overlay_input.c to satisfy
 * those would drag Direct3D and a window procedure into a process that has neither, which is a
 * much larger dependency than the model test wants for two calls it does not exercise.
 *
 * The screen stub answers "no screen", which is the honest answer here and the one the size row is
 * already written to survive: no device means no measurement, so automatic sizing falls back to its
 * own default rather than reading a number out of an uninitialised renderer. */
bool overlay_draw_screen(float *out_width, float *out_height)
{
    (void)out_width;
    (void)out_height;
    return false;
}

void overlay_input_close(void)
{
}

/* The first row is always the group heading, so a folded tab has exactly one row. */
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

int main(void)
{
    overlay_row_t row;

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

    ut_section("folding");
    overlay_model_reset();
    overlay_model_set_tab(OVERLAY_TAB_OPENPHANTOM);
    overlay_model_rebuild();
    ut_check(overlay_model_row_count() == 1u,
             "the OpenPhantom tab holds one group, and it starts folded too");
    overlay_model_toggle_group((uint32_t)OVERLAY_GROUP_OPENPHANTOM);
    overlay_model_rebuild();
    ut_check(overlay_model_row_count() == 1u + (uint32_t)CHEATS_OWN_COUNT + 6u,
             "unfolding shows the heading, this project's cheats, the jump-boost scale row, the "
             "free-camera exit hotkey row, the fly-controls note, the skip-to-next-level action "
             "and the draw distance and dev menu size rows appended after them");
    ut_check(overlay_model_row(1, &row) && row.kind == OVERLAY_ROW_CHEAT,
             "the row under the heading is a cheat");
    ut_check(!row.available,
             "and with no engine behind it the row reports itself unavailable rather than ticking");
    ut_check(!overlay_model_activate(1),
             "switching an unavailable cheat is refused instead of quietly doing nothing");

    ut_section("the jump-boost scale row, right after jump boost's own toggle");
    /* Row 0 is the heading, rows 1..7 are the seven cheats ahead of jump boost in the enum, row 8
     * is jump boost's own toggle (id 7), and the scale row takes over free camera's OLD slot - id
     * CHEATS_OWN_COUNT-1, row CHEATS_OWN_COUNT - one level further out than the hotkey row used to
     * sit before this row was inserted ahead of it. */
    cheats_openphantom_jump_boost_set_scale(2.5f);
    overlay_model_rebuild();
    ut_check(overlay_model_row((uint32_t)CHEATS_OWN_COUNT, &row) &&
                 row.kind == OVERLAY_ROW_VALUE,
             "the row at free camera's old slot is now the jump-boost scale row");
    ut_check(strcmp(row.label, "Jump boost scale") == 0, "named for what it edits");
    ut_check(!row.available,
             "unavailable too - it follows jump boost's own site, which resolved nothing here");
    ut_check(strcmp(row.value, "2.50x") == 0,
             "shows the current scale even though the cheat itself never armed - the number is "
             "real regardless of whether anything is hooked to multiply by it yet");

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

    ut_section("the edit functions are harmless no-ops outside of a capture");
    /* Reachable directly without a resolved site - unlike overlay_model_activate() above, none of
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

    ut_section("the free-camera exit hotkey row, now two slots after jump boost's toggle");
    ut_check(overlay_model_row((uint32_t)CHEATS_OWN_COUNT + 1u, &row) &&
                 row.kind == OVERLAY_ROW_HOTKEY,
             "one slot further out than the scale row above it");
    ut_check(!row.available,
             "unavailable too - it follows free camera's own site, which resolved nothing here");
    ut_check(strcmp(row.value, "Set") == 0,
             "unbound shows as an instruction to set one, not a blank chip or a stray ON/OFF");
    ut_check(!overlay_model_activate((uint32_t)CHEATS_OWN_COUNT + 1u),
             "starting a capture on an unavailable row is refused the same as any other cheat");
    ut_check(!overlay_model_is_capturing_hotkey(),
             "and refusing it must not have left a capture armed with nothing behind it");

    ut_section("free camera's own row, one after its exit hotkey");
    ut_check(overlay_model_row((uint32_t)CHEATS_OWN_COUNT + 2u, &row) &&
                 row.kind == OVERLAY_ROW_CHEAT,
             "free camera itself now sits one row after the hotkey that gates it");
    ut_check(!row.available,
             "and still unavailable with no exit hotkey bound, same as before the reorder");

    ut_section("the fly-controls note, one past free camera's own row");
    ut_check(overlay_model_row((uint32_t)CHEATS_OWN_COUNT + 3u, &row) &&
                 row.kind == OVERLAY_ROW_INFO,
             "the last row in the group is the how-to-fly fold");
    ut_check(row.available, "always available - it is a note, not gated behind any site");
    ut_check(strcmp(row.label, "+ How free camera flies") == 0,
             "closed by default, marked with a plus the same way a group would be");

    ut_section("skip to next level, the one action row in this group");
    ut_check(overlay_model_row((uint32_t)CHEATS_OWN_COUNT + 4u, &row) &&
                 row.kind == OVERLAY_ROW_ACTION,
             "an action rather than a toggle, and the last row the group holds while folded shut");
    ut_check(!row.available,
             "unavailable here, since the cell it writes is resolved by the original cheat table "
             "and nothing resolved in this test");

    ut_section("the draw distance row, appended after the action");
    ut_check(overlay_model_row((uint32_t)CHEATS_OWN_COUNT + 5u, &row) &&
                 row.kind == OVERLAY_ROW_VALUE,
             "a typed value row, the second one this group holds, sitting last while folded shut");
    ut_check(strcmp(row.label, "Draw distance (1.0 to 2.5)") == 0,
             "named for what it edits, and carrying the accepted range so a player learns it "
             "from the row rather than from having a number refused");
    ut_check(row.available,
             "available with nothing resolved, unlike every other row here: it edits a setting "
             "file rather than the running game, so it works with no level loaded and even with "
             "view_distance_fix not installed at all");
    ut_check(row.value[0] != '\0',
             "and it always shows a number, read from the ini rather than from the game");

    ut_section("the dev menu size row, last of the group's fixed rows");
    ut_check(overlay_model_row((uint32_t)CHEATS_OWN_COUNT + 6u, &row) &&
                 row.kind == OVERLAY_ROW_VALUE,
             "a typed value row, the third this group holds, and the last row of all while the "
             "fold is shut, since the fold's own lines are the only rows that ever join the group");
    ut_check(strcmp(row.label, "Dev menu size (0.33 to 4.0)") == 0,
             "named for what it edits and carrying its range, the same as the row above it");
    ut_check(row.available,
             "available with nothing resolved, for a stronger reason than the draw distance row "
             "above: it edits how this panel is drawn and reaches into no engine site at all, so "
             "there is nothing here that could fail to resolve");
    ut_check(row.value[0] != '\0',
             "and it always shows a number, the scale the panel is currently drawn at");

    ut_section("opening the how-to-fly fold");
    ut_check(overlay_model_activate((uint32_t)CHEATS_OWN_COUNT + 3u),
             "clicking the fold's own summary row is accepted, unlike an ordinary note");
    overlay_model_rebuild();
    ut_check(overlay_model_row_count() ==
                 1u + (uint32_t)CHEATS_OWN_COUNT + 6u + 9u,
             "open, the heading, the cheats, the scale row, the hotkey row, free camera's own row, "
             "the fold's own summary and its nine lines, the skip-to-next-level action, the draw "
             "distance row and the dev menu size row are all on screen");
    ut_check(overlay_model_row((uint32_t)CHEATS_OWN_COUNT + 3u, &row) &&
                 strcmp(row.label, "- How free camera flies") == 0,
             "the summary itself now reads open, marked with a minus");
    /* Directly under the summary that revealed them, which is the only place a reader looks for
       them. Their ids are the tail of the group's id space, since that is the part allowed to
       change size, so the row they are drawn at and the id they carry are deliberately not the
       same number. This checks the drawn order, which is the half a player sees. */
    ut_check(overlay_model_row((uint32_t)CHEATS_OWN_COUNT + 4u, &row) &&
                 row.kind == OVERLAY_ROW_INFO &&
                 strcmp(row.label, "    Needs a teleport key set first") == 0,
             "the first line sits immediately below the summary, not at the end of the group");
    ut_check(!overlay_model_activate((uint32_t)CHEATS_OWN_COUNT + 4u),
             "but a line itself does nothing when clicked - only the summary is interactive");
    /* Nine lines, not six: the two that describe the two ways out are each a sentence too long to
       fit the panel's width, so each is written as a line plus an indented continuation rather
       than being allowed to run off the edge. The count is what this pins down - a line added
       without the rows below it moving is the failure that would otherwise go unseen. */
    ut_check(overlay_model_row((uint32_t)CHEATS_OWN_COUNT + 11u, &row) &&
                 strcmp(row.label, "    F4 ends the flight and leaves") == 0,
             "the eighth line names the other way out, the one that leaves the player put");
    ut_check(overlay_model_row((uint32_t)CHEATS_OWN_COUNT + 12u, &row) &&
                 strcmp(row.label, "      the player where they were") == 0,
             "and the ninth is its continuation, indented past the line it finishes");

    /* The rows that were below the summary are still below the lines, in the order they had. A
       fold that reorders the rows around it would be worse than one that does not open. */
    ut_check(overlay_model_row((uint32_t)CHEATS_OWN_COUNT + 13u, &row) &&
                 row.kind == OVERLAY_ROW_ACTION,
             "the skip-to-next-level action is pushed down the screen by the nine lines");
    ut_check(overlay_model_row((uint32_t)CHEATS_OWN_COUNT + 14u, &row) &&
                 strcmp(row.label, "Draw distance (1.0 to 2.5)") == 0,
             "and the draw distance row after it, still in the order it was drawn in");
    ut_check(overlay_model_row((uint32_t)CHEATS_OWN_COUNT + 15u, &row) &&
                 strcmp(row.label, "Dev menu size (0.33 to 4.0)") == 0,
             "and the dev menu size row last, which is the whole group accounted for");

    ut_section("closing the how-to-fly fold again");
    ut_check(overlay_model_activate((uint32_t)CHEATS_OWN_COUNT + 3u),
             "the same summary row closes it back up");
    overlay_model_rebuild();
    ut_check(overlay_model_row_count() == 1u + (uint32_t)CHEATS_OWN_COUNT + 6u,
             "its nine lines are gone again, back to costing one row like any other cheat");

    ut_section("a group folds back exactly as it was");
    overlay_model_toggle_group((uint32_t)OVERLAY_GROUP_OPENPHANTOM);
    overlay_model_rebuild();
    ut_check(overlay_model_row_count() == 1u, "folding it again leaves the heading alone");

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

    ut_section("a queued play-as swap, before anything has resolved");
    ut_check(!cheats_original_actions_is_pending(CHEATS_ACTION_PLAY_OBI),
             "nothing is pending on an executable nothing resolved against - character 0 (Obi-Wan) "
             "must not read as queued just because it shares its index with an unresolved struct's "
             "own zero-initialised default");
    ut_check(cheats_original_actions_pending_label() == NULL,
             "and there is no label for a swap that was never queued");
    ut_check(overlay_model_row(5, &row) && row.id == (uint32_t)CHEATS_ACTION_PLAY_OBI,
             "row 5 under the actions heading is Play as Obi-Wan, by index");
    ut_check(!row.pending, "and it does not show as queued either");

    ut_section("typing opens the group that has hits, and clearing puts it back");
    overlay_model_reset();
    overlay_model_set_tab(OVERLAY_TAB_OPENPHANTOM);
    ut_check(row_count_after("") == 1, "folded to start with");
    ut_check(row_count_after("zzzz") == 1,
             "a search nothing matches leaves the group folded rather than opening it empty");
    ut_check(row_count_after("") == 1,
             "and clearing the box restores the fold you chose, not the one the search "
             "forced");

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

    ut_section("indices that do not exist");
    overlay_model_reset();
    overlay_model_rebuild();
    ut_check(!overlay_model_row(99u, &row), "a row past the end is refused");
    ut_check(!overlay_model_activate(99u), "and so is acting on one");
    overlay_model_set_tab((overlay_tab_t)99);
    ut_check(overlay_model_tab() == OVERLAY_TAB_ORIGINAL, "a tab that does not exist is ignored");

    return ut_summary("the overlay's model");
}
