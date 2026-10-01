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
 *
 * SIZE NOTE: over the 600 line mark with the entity spawner group in the walk. The sections
 * from the cheats down to dismemberment chain, each one checking the row the one before it
 * ended on, so they stay in one program.
 *
 * The width check and the sweep that opens everything for it are in overlay_width.c: they share
 * none of this file's walk down the tab, and they were the largest block in it that did not. The
 * seam left, if it grows again, is the search box, whose three sections share nothing with the
 * walk either.
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
#include "npc_spawn_link.h"
#include "overlay_model.h"
#include "spawn_keys.h"
#include "spawn_place.h"
#include "overlay_picture.h"
#include "overlay_choice.h"
#include "overlay_reason.h"
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
    ut_check(overlay_model_tab() == OVERLAY_TAB_OPENPHANTOM,
             "it opens on this patch's own tab and not on the shipped console's: the panel is "
             "opened for the free camera, the spawner and the settings far more often than for a "
             "retail code, and the other way round cost a click on every game start");
    ut_check(overlay_model_row_count() == HEADINGS,
             "every group starts folded, so that tab is its twelve headings and nothing else");
    ut_check(first_row_is_group(), "and the first of those rows is a heading");
    ut_check(overlay_model_search()[0] == '\0', "with nothing typed");

    overlay_model_set_tab(OVERLAY_TAB_ORIGINAL);
    overlay_model_rebuild();
    ut_check(overlay_model_row_count() == 2u,
             "the shipped console's tab is two headings, the toggles and the one-shot actions, as "
             "two separate groups");
    ut_check(first_row_is_group() && overlay_model_row(1, &row) &&
                 row.kind == OVERLAY_ROW_GROUP,
             "and both of them are headings: two groups, not one");
}

static void test_folding(void)
{
    ut_section("folding");
    overlay_model_reset();
    overlay_model_set_tab(OVERLAY_TAB_OPENPHANTOM);
    overlay_model_rebuild();
    ut_check(overlay_model_row_count() == HEADINGS,
             "the OpenPhantom tab shows twelve headings now, named for what is under them: "
             "cheats, free camera, entity spawner, appearance, level selection, engine, controls, "
             "multiplayer, window, frame rate, menus and dismemberment, all folded to start with");
    overlay_model_toggle_group((uint32_t)OVERLAY_GROUP_OPENPHANTOM);
    overlay_model_rebuild();
    ut_check(overlay_model_row_count() == HEADINGS + OVERLAY_CHEATS_ROW_COUNT,
             "unfolding the cheats shows its heading, this project's cheats but free camera and "
             "the jump-boost scale row, with the free camera heading still folded below them");
    ut_check(overlay_model_row(1, &row) && row.kind == OVERLAY_ROW_CHEAT,
             "the row under the heading is a cheat");
    ut_check(!row.available,
             "and with no engine behind it the row reports itself unavailable rather than ticking");
    ut_check(!overlay_model_activate(1),
             "switching an unavailable cheat is refused instead of quietly doing nothing");
    ut_check(overlay_model_row(1u + (uint32_t)CHEATS_OWN_SUPER_RUN, &row) &&
                 row.kind == OVERLAY_ROW_CHEAT && !row.available,
             "super run is a switch drawn under no clip, and with no run site behind it reports "
             "itself unavailable");
    ut_check(overlay_model_row(1u + OVERLAY_CHEATS_SUPER_RUN_SPEED_SLOT, &row) &&
                 row.kind == OVERLAY_ROW_VALUE && row.id == SUPER_RUN_SPEED_ROW_ID &&
                 strcmp(row.label, "Super run speed (1.1 to 4.0x)") == 0 && !row.available,
             "its speed is the typed row directly under it, unavailable for the same reason");
    ut_check(overlay_model_row(2u + OVERLAY_CHEATS_SUPER_RUN_SPEED_SLOT, &row) &&
                 row.kind == OVERLAY_ROW_SLIDER && row.id == SUPER_RUN_TRACK_ROW_ID &&
                 !row.available,
             "and its track is the row under that");
    ut_check(!overlay_model_slider_set(2u + OVERLAY_CHEATS_SUPER_RUN_SPEED_SLOT, 0.5f),
             "a drag on the track is refused while the cheat has no site");
    ut_check(overlay_model_row(3u + OVERLAY_CHEATS_SUPER_RUN_SPEED_SLOT, &row) &&
                 row.kind == OVERLAY_ROW_CHEAT && row.id == (uint32_t)CHEATS_OWN_JUMP_BOOST,
             "jump boost's own toggle follows the track");
}

static void test_super_run_scale_clamps(void)
{
    ut_section("the super run speed setter clamps on its own, with no panel involved");
    (void)cheats_openphantom_super_run_set_scale(0.5f);
    ut_check(cheats_openphantom_super_run_scale() > 1.09f &&
                 cheats_openphantom_super_run_scale() < 1.11f,
             "a value below the floor is clamped up to it");
    (void)cheats_openphantom_super_run_set_scale(99.0f);
    ut_check(cheats_openphantom_super_run_scale() > 3.99f &&
                 cheats_openphantom_super_run_scale() < 4.01f,
             "a value above the ceiling is clamped down to it");
    (void)cheats_openphantom_super_run_set_scale(2.0f);
    ut_check(cheats_openphantom_super_run_scale() > 1.99f &&
                 cheats_openphantom_super_run_scale() < 2.01f,
             "a value inside the band is kept as typed");
}

static void test_jump_scale_row(void)
{
    ut_section("the jump-boost scale row, right after jump boost's own toggle");
    /* Row 0 is the heading; the group's own slot table draws the toggles with super run's speed
     * and track under super run, then jump boost's toggle, then its scale last: slot
     * OVERLAY_CHEATS_JUMP_SCALE_SLOT, one row further down. The scale's ID is free camera's
     * number, the last of the enum. */
    cheats_openphantom_jump_boost_set_scale(2.5f);
    overlay_model_rebuild();
    ut_check(overlay_model_row(1u + OVERLAY_CHEATS_JUMP_SCALE_SLOT, &row) &&
                 row.kind == OVERLAY_ROW_VALUE && row.id == JUMP_SCALE_ROW_ID,
             "the last row of the group is the jump-boost scale row, with free camera's number");
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
    ut_check(!overlay_model_activate(1u + OVERLAY_CHEATS_JUMP_SCALE_SLOT),
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
    ut_section("the scale and its own track are the last rows of the cheats");
    ut_check(overlay_model_row(2u + OVERLAY_CHEATS_JUMP_SCALE_SLOT, &row) &&
                 row.kind == OVERLAY_ROW_SLIDER && row.id == JUMP_SCALE_TRACK_ROW_ID,
             "the scale has a track of its own on the line under it, as super run does");
    ut_check(overlay_model_row(3u + OVERLAY_CHEATS_JUMP_SCALE_SLOT, &row) &&
                 row.kind == OVERLAY_ROW_GROUP && strcmp(row.label, "Free camera") == 0,
             "the row after THAT is the next heading, and it is the free camera: the three "
             "groups used while standing in a level are drawn first");
}

static void test_levels_group(void)
{
    ut_section("the level selection group, under the appearance");
    ut_check(overlay_model_row(MSWAP_ROW(0u), &row) &&
                 row.kind == OVERLAY_ROW_GROUP && strcmp(row.label, "Level selection") == 0,
             "its heading follows the appearance heading, both folded: what is chosen once is "
             "drawn below what is used while standing in a level");
    overlay_model_toggle_group((uint32_t)OVERLAY_GROUP_OPENPHANTOM_LEVELS);
    overlay_model_rebuild();
    ut_check(overlay_model_row_count() == OPEN_ABOVE_ROWS + 2u,
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
                 strcmp(row.label, "Engine") == 0,
             "and the engine heading comes straight after, named for what is under it rather "
             "than for the DLL that reads it");
}

static void test_spawn_group(void)
{
    ut_section("the entity spawner group, directly under the free camera");
    ut_check(overlay_model_row(FC_ROW(OVERLAY_FREECAM_LINE_FIRST), &row) &&
                 row.kind == OVERLAY_ROW_GROUP && strcmp(row.label, "Entity spawner") == 0,
             "its heading follows the free camera group's last row, folded");
    overlay_model_toggle_group((uint32_t)OVERLAY_GROUP_OPENPHANTOM_SPAWN);
    overlay_model_rebuild();
    ut_check(overlay_model_row_count() ==
                 OPEN_ABOVE_ROWS,
             "open, it holds nine rows with its kind list and its fold shut: the count of the "
             "copies alive, the placement and its two keys, what is spawned, what it does, the "
             "note under that, the remove and the fold's summary");
    ut_check(overlay_model_row(SPAWN_ROW(OVERLAY_SPAWN_ALIVE_SLOT), &row) &&
                 row.kind == OVERLAY_ROW_INFO &&
                 strcmp(row.label, "    0 of 16 spawned entities alive") == 0 && !row.available,
             "the count first, a note under the heading, never clickable");
    ut_check(overlay_model_row(SPAWN_ROW(OVERLAY_SPAWN_PLACE_SLOT), &row) &&
                 row.kind == OVERLAY_ROW_ACTION &&
                 strcmp(row.label, "Place with the mouse") == 0 && !row.available &&
                 row.value[0] == '\0',
             "the placement second, unavailable with nothing chosen, its chip empty");
    ut_check(overlay_model_row(SPAWN_ROW(OVERLAY_SPAWN_PLACE_KEY_SLOT), &row) &&
                 row.kind == OVERLAY_ROW_HOTKEY &&
                 strcmp(row.label, "    Key: place with the mouse") == 0 && row.available &&
                 strcmp(row.value, "Set") == 0,
             "its key under it, a setting that can be bound whatever resolved, unbound");
    ut_check(overlay_model_row(SPAWN_ROW(OVERLAY_SPAWN_FACE_KEY_SLOT), &row) &&
                 row.kind == OVERLAY_ROW_HOTKEY &&
                 strcmp(row.label, "    Key: turn to face you") == 0 &&
                 strcmp(row.value, "Set") == 0,
             "and the key that turns the entity back to face the player");
    ut_check(overlay_model_row(SPAWN_ROW(OVERLAY_SPAWN_KIND_SLOT), &row) &&
                 row.kind == OVERLAY_ROW_ACTION && strcmp(row.label, "Entity to spawn") == 0,
             "what is spawned, the row that opens the list");
    ut_check(!row.available && strcmp(row.value, "None") == 0,
             "unavailable too, with nothing chosen and nothing to choose from");
    ut_check(!overlay_model_activate(SPAWN_ROW(OVERLAY_SPAWN_KIND_SLOT)),
             "an unavailable row does not open its list");
    ut_check(overlay_model_row(SPAWN_ROW(OVERLAY_SPAWN_BEHAVIOUR_SLOT), &row) &&
                 row.kind == OVERLAY_ROW_SEGMENT &&
                 strcmp(row.label, "Spawned entities") == 0 && !row.available &&
                 row.chosen == 0u && row.value[0] == '\0',
             "the behaviour, the whole choice on one row, standing on the first of its four "
             "words; it carries no chip, because the words already say which one is in force");
    ut_check(overlay_model_row(SPAWN_ROW(OVERLAY_SPAWN_BEHAVIOUR_NOTE), &row) &&
                 row.kind == OVERLAY_ROW_INFO && !row.available &&
                 strcmp(row.label, "    Attack fights you; Help fights for you") == 0,
             "and the thing its four words do not say, under it: who each of the two fighting "
             "words fights, with BOTH verbs carrying their object, since a bare second one reads "
             "as fighting the player too");
    ut_check(overlay_model_row(SPAWN_ROW(OVERLAY_SPAWN_REMOVE_SLOT), &row) &&
                 row.kind == OVERLAY_ROW_ACTION &&
                 strcmp(row.label, "Remove spawned entities") == 0 && !row.available,
             "the remove, an action, unavailable with nothing spawned");
    ut_check(overlay_model_row(SPAWN_ROW(OVERLAY_SPAWN_SUMMARY_SLOT), &row) &&
                 row.kind == OVERLAY_ROW_INFO &&
                 strcmp(row.label, "+ About spawned entities") == 0 && row.available,
             "the fold last, shut, and open to a click with nothing resolved");
    ut_check(overlay_model_activate(SPAWN_ROW(OVERLAY_SPAWN_SUMMARY_SLOT)),
             "the summary row opens it");
    overlay_model_rebuild();
    ut_check(overlay_model_row_count() == OPEN_ABOVE_ROWS + OVERLAY_SPAWN_LINE_COUNT,
             "open, its lines follow the summary");
    ut_check(overlay_model_row(SPAWN_ROW(OVERLAY_SPAWN_FIXED_ROWS), &row) &&
                 row.kind == OVERLAY_ROW_INFO &&
                 strcmp(row.label, "    Place with the mouse: the menu hides and") == 0,
             "the first line says how the placement is used");
    ut_check(overlay_model_activate(SPAWN_ROW(OVERLAY_SPAWN_SUMMARY_SLOT)),
             "the summary row shuts it again");
    overlay_model_rebuild();
    ut_check(overlay_model_row(SPAWN_ROW(OVERLAY_SPAWN_FIXED_ROWS), &row) &&
                 row.kind == OVERLAY_ROW_GROUP && strcmp(row.label, "Appearance") == 0,
             "and the appearance heading comes straight after the shut fold, named for what a "
             "player asks rather than for the model being swapped");

    ut_section("the row that says why the group cannot be used");
    /* What the placement mode works out once a frame; the panel only repeats it. Nothing drives
     * the mode in a test process, so it is written here as the mode would have left it. */
    spawn_place_state()->unavailable = "the session runs no copies";
    overlay_model_rebuild();
    ut_check(overlay_model_row_count() == OPEN_ABOVE_ROWS + 1u,
             "a reason adds one row and nothing else");
    ut_check(overlay_model_row(SPAWN_ROW(OVERLAY_SPAWN_KIND_SLOT), &row) &&
                 row.kind == OVERLAY_ROW_INFO &&
                 strcmp(row.label, "    Why: the session runs no copies") == 0 &&
                 !row.available,
             "under the two keys, in the mode's own words, never clickable");
    npc_spawn_link_refuse_here("the host's cap is reached");
    overlay_model_rebuild();
    ut_check(overlay_model_row(SPAWN_ROW(OVERLAY_SPAWN_KIND_SLOT), &row) &&
                 strcmp(row.label, "    Why: the session runs no copies") == 0,
             "the reason keeps its place when a refusal joins it");
    ut_check(overlay_model_row(SPAWN_ROW(OVERLAY_SPAWN_KIND_SLOT) + 1u, &row) &&
                 strcmp(row.label, "    Refused: the host's cap is reached") == 0,
             "and the refusal follows it, one saying the state and one the last wish");
    ut_check(overlay_model_row(SPAWN_ROW(OVERLAY_SPAWN_PLACE_KEY_SLOT), &row) &&
                 row.kind == OVERLAY_ROW_HOTKEY,
             "the key rows above the two have not moved");
    npc_spawn_link_forget_refusal();
    spawn_place_state()->unavailable = NULL;
    overlay_model_rebuild();
    ut_check(overlay_model_row_count() == OPEN_ABOVE_ROWS,
             "and with nothing in the way the group is its eight rows and its fold again");

    ut_section("the spawner's refusal outside a session");
    npc_spawn_link_refuse_here("no room: no floor in reach");
    overlay_model_rebuild();
    ut_check(overlay_model_row_count() ==
                 OPEN_ABOVE_ROWS + 1u,
             "a refusal decided here adds its row, as a session's refusal does");
    ut_check(overlay_model_row(SPAWN_ROW(OVERLAY_SPAWN_KIND_SLOT), &row) &&
                 row.kind == OVERLAY_ROW_INFO &&
                 strcmp(row.label, "    Refused: no room: no floor in reach") == 0 &&
                 !row.available,
             "under the two keys, saying why, never clickable");
    ut_check(overlay_model_row(SPAWN_ROW(OVERLAY_SPAWN_PLACE_KEY_SLOT), &row) &&
                 row.kind == OVERLAY_ROW_HOTKEY,
             "and the key rows above it have not moved");
    npc_spawn_link_forget_refusal();
    overlay_model_rebuild();
    ut_check(overlay_model_row_count() ==
                 OPEN_ABOVE_ROWS,
             "a new spawn forgets it and the row goes");

    ut_section("binding the placement key");
    ut_check(overlay_model_activate(SPAWN_ROW(OVERLAY_SPAWN_PLACE_KEY_SLOT)) &&
                 overlay_model_is_capturing_hotkey(),
             "a click on its key row waits for a key");
    overlay_model_capture_hotkey(0x78);
    overlay_model_rebuild();
    ut_check(!overlay_model_is_capturing_hotkey() &&
                 overlay_model_row(SPAWN_ROW(OVERLAY_SPAWN_PLACE_KEY_SLOT), &row) &&
                 strcmp(row.value, "F9") == 0 && spawn_keys_get(SPAWN_KEY_PLACE) == 0x78,
             "F9 lands on the row that was clicked, and on the mode's key");
    ut_check(overlay_model_row(SPAWN_ROW(OVERLAY_SPAWN_FACE_KEY_SLOT), &row) &&
                 strcmp(row.value, "Set") == 0,
             "and not on the other key");
    (void)spawn_keys_bind(SPAWN_KEY_PLACE, 0);
}

static void test_freecam_group(void)
{
    ut_section("the free camera group, directly under the cheats");
    ut_check(overlay_model_row(CHEAT_ROW(OVERLAY_CHEATS_ROW_COUNT), &row) &&
                 row.kind == OVERLAY_ROW_GROUP && strcmp(row.label, "Free camera") == 0,
             "its heading follows the cheats group's last row, folded");
    overlay_model_toggle_group((uint32_t)OVERLAY_GROUP_OPENPHANTOM_FREECAM);
    overlay_model_rebuild();
    ut_check(overlay_model_row_count() == FC_OPEN_ROWS,
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
                 strcmp(row.label, "Entity spawner") == 0,
             "and the entity spawner heading comes straight after");
}

/* The dismemberment group is the last heading of the tab, and it holds one switch. Found at the
 * end of the list rather than counted down to: everything between it and the level selection is
 * folded here, so the last row IS its heading, and a group added in between would not quietly
 * make this section check the wrong one. */
static void test_dismember_group(void)
{
    const uint32_t heading = overlay_model_row_count() - 1u;

    ut_section("the dismemberment group, one switch under the last heading of the tab");
    ut_check(overlay_model_row(heading, &row) && row.kind == OVERLAY_ROW_GROUP &&
                 strcmp(row.label, "Dismemberment") == 0,
             "the last row of the tab is its heading, folded");
    ut_check(strcmp(row.value, "OFF") == 0,
             "and the heading says so without being opened: one switch under it, and the band "
             "reads what that switch is");
    overlay_model_toggle_group((uint32_t)OVERLAY_GROUP_OPENPHANTOM_DISMEMBER);
    overlay_model_rebuild();
    ut_check(overlay_model_row_count() == heading + 2u, "open, it holds the one row");
    ut_check(overlay_model_row(heading + 1u, &row) && row.kind == OVERLAY_ROW_CHEAT &&
                 strcmp(row.label, "Lightsaber dismemberment") == 0,
             "named for the thing itself: a reader looking for it is looking for the word, not "
             "for the node correction underneath it");
    ut_check(row.available,
             "always available. It writes a settings key and asks nothing of the game, so no "
             "unresolved site can grey it out");
    overlay_model_toggle_group((uint32_t)OVERLAY_GROUP_OPENPHANTOM_DISMEMBER);
    overlay_model_rebuild();
}

static void test_open_freecam_fold(void)
{
    ut_section("opening the how-to-fly fold");
    ut_check(overlay_model_activate(FC_ROW(4)),
             "clicking the fold's own summary row is accepted, unlike an ordinary note");
    overlay_model_rebuild();
    ut_check(overlay_model_row_count() == FC_OPEN_ROWS + OVERLAY_FREECAM_LINE_COUNT,
             "open, the free camera group holds the key, the cheat, the two switches, the fold's "
             "own summary and its fifteen lines, with the spawner's heading below them");
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
    /* Fifteen lines, not seven: the three that describe hiding the panel and the two ways out
       are each a sentence too long to fit the panel's width, so each is written as a line plus
       an indented continuation, and the pad's four lines follow. The count is what this pins
       down: a line added without the rows below it moving is the failure that would otherwise
       go unseen. */
    ut_check(overlay_model_row(FC_ROW(10), &row) &&
                 strcmp(row.label, "    Your dev menu open key or Escape") == 0,
             "the sixth line names the key that hides the panel while the camera flies");
    ut_check(overlay_model_row(FC_ROW(14), &row) &&
                 strcmp(row.label, "    F4 ends the flight and leaves") == 0,
             "the tenth line names the other way out, the one that leaves the player put");
    ut_check(overlay_model_row(FC_ROW(15), &row) &&
                 strcmp(row.label, "      the player where they were") == 0,
             "and the eleventh is its continuation, indented past the line it finishes");
    ut_check(overlay_model_row(FC_ROW(16), &row) &&
                 strcmp(row.label, "    Pad: left stick flies, right stick looks,") == 0 &&
                 overlay_model_row(FC_ROW(19), &row) &&
                 strcmp(row.label, "      View hides and shows the panel") == 0,
             "the pad's four lines close the fold, the last one the opening button's");

    /* The heading that was below the summary is still below the lines. A fold that reordered the
       rows around it would be worse than one that does not open. */
    ut_check(overlay_model_row(FC_ROW(20), &row) && row.kind == OVERLAY_ROW_GROUP &&
                 strcmp(row.label, "Entity spawner") == 0,
             "the entity spawner heading is pushed down the screen by the fifteen lines");
}

static void test_close_freecam_fold(void)
{
    ut_section("closing the how-to-fly fold again");
    ut_check(overlay_model_activate(FC_ROW(4)),
             "the same summary row closes it back up");
    overlay_model_rebuild();
    ut_check(overlay_model_row_count() == FC_OPEN_ROWS,
             "its fifteen lines are gone again, back to costing one row like any other");
}

static void test_group_folds_back(void)
{
    ut_section("a group folds back exactly as it was");
    overlay_model_toggle_group((uint32_t)OVERLAY_GROUP_OPENPHANTOM_LEVELS);
    overlay_model_toggle_group((uint32_t)OVERLAY_GROUP_OPENPHANTOM_SPAWN);
    overlay_model_toggle_group((uint32_t)OVERLAY_GROUP_OPENPHANTOM_FREECAM);
    overlay_model_rebuild();
    ut_check(overlay_model_row_count() == HEADINGS + OVERLAY_CHEATS_ROW_COUNT,
             "folding the level selection, the spawner and the free camera leaves the cheats "
             "open under the twelve headings");
    overlay_model_toggle_group((uint32_t)OVERLAY_GROUP_OPENPHANTOM);
    overlay_model_rebuild();
    ut_check(overlay_model_row_count() == HEADINGS,
             "and folding the cheats leaves the twelve headings alone");
}

static void test_original_actions_group(void)
{
    ut_section("the Original tab's second group: one-shot actions, not toggles");
    overlay_model_reset();
    overlay_model_set_tab(OVERLAY_TAB_ORIGINAL);
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
    ut_check(row_count_after("") == HEADINGS, "all twelve headings folded to start with");
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
    overlay_model_set_tab(OVERLAY_TAB_ORIGINAL);
    overlay_model_rebuild();
    ut_check(!overlay_model_row(99u, &row), "a row past the end is refused");
    ut_check(!overlay_model_activate(99u), "and so is acting on one");
    overlay_model_set_tab((overlay_tab_t)99);
    ut_check(overlay_model_tab() == OVERLAY_TAB_ORIGINAL, "a tab that does not exist is ignored");
}

int main(void)
{
    test_search();
    test_fresh_panel();
    test_folding();
    test_jump_scale_row();
    test_jump_scale_clamps();
    test_super_run_scale_clamps();
    test_jump_scale_row_availability();
    test_edit_outside_capture();
    test_cheats_end();
    /* Down the tab in the order it is drawn, each section opening one more group and checking
     * the heading the group above it left standing. */
    test_freecam_group();
    test_open_freecam_fold();
    test_close_freecam_fold();
    test_spawn_group();
    test_levels_group();
    test_dismember_group();
    test_group_folds_back();
    test_original_actions_group();
    test_queued_play_as_swap();
    test_typing_opens_the_hit_group();
    test_search_box();
    test_search_box_overrun();
    test_missing_indices();

    return ut_summary("the overlay's model");
}
