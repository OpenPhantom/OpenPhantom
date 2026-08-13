/* overlay_model.c: the half of the overlay that can be checked without the game.
 *
 * What it decides is which rows are on screen, and every way of getting that wrong is quiet. Fold
 * a group the wrong way and the panel looks empty; match the search too eagerly and every cheat is
 * always listed; match it too strictly and typing the name of a cheat hides it. None of those
 * crash, none of them log, and all of them are only visible to somebody who already knows what the
 * list should have said.
 *
 * The two cheat sources are deliberately not stubbed. Neither has resolved anything in a test
 * process, so the game's own tab is empty and this project's tab holds its two rows with no site
 * behind them, which is exactly the state a player sees on an unsupported executable. That is
 * worth pinning down: it is the case where the panel must still open and still be usable.
 */
#include "unittest.h"

#include "cheats_openphantom.h"
#include "overlay_model.h"

#include <string.h>

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
    ut_check(overlay_model_row_count() == 1u,
             "every group starts folded, so only the heading shows");
    ut_check(first_row_is_group(), "and that one row is the heading");
    ut_check(overlay_model_search()[0] == '\0', "with nothing typed");

    ut_section("folding");
    overlay_model_reset();
    overlay_model_set_tab(OVERLAY_TAB_OPENPHANTOM);
    overlay_model_rebuild();
    ut_check(overlay_model_row_count() == 1u, "the second tab starts folded too");
    overlay_model_toggle_group((uint32_t)OVERLAY_TAB_OPENPHANTOM);
    overlay_model_rebuild();
    ut_check(overlay_model_row_count() == 1u + (uint32_t)CHEATS_OWN_COUNT,
             "unfolding shows the heading and both of this project's cheats");
    ut_check(overlay_model_row(1, &row) && row.kind == OVERLAY_ROW_CHEAT,
             "the row under the heading is a cheat");
    ut_check(!row.available,
             "and with no engine behind it the row reports itself unavailable rather than ticking");
    ut_check(!overlay_model_activate(1),
             "switching an unavailable cheat is refused instead of quietly doing nothing");

    ut_section("a group folds back exactly as it was");
    overlay_model_toggle_group((uint32_t)OVERLAY_TAB_OPENPHANTOM);
    overlay_model_rebuild();
    ut_check(overlay_model_row_count() == 1u, "folding it again leaves the heading alone");

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
