/* overlay_groups.c: the settings groups of the OpenPhantom tab, walked by position.
 *
 * The other half of the model test. overlay_model.c holds the navigation, the search, the cheats,
 * the free camera and the folds; this program opens the groups above the settings the way a
 * player would and then checks every settings row in the order it is drawn, its caption, its kind
 * and whether it is offered, against the row beside it. Each check is written as a claim in
 * English because this is the only place the panel's intended order is stated at all.
 *
 * SIZE NOTE. Over six hundred lines, and what is long is the list itself: this program is one
 * claim per row of the OpenPhantom tab, in the order they are drawn, and shortening it means
 * leaving rows unstated. The seam, when it grows again, is the three sections that are about
 * a TRACK rather than about the order of the rows: the value under the handle, the two rows
 * one track drives, and where each Default comes from. They share only the model.
 *
 * Nothing is stubbed beyond overlay_stubs.c, and none of the cheat sources has resolved anything
 * in a test process, so every row here is in the state a player sees on an unsupported
 * executable: the settings rows all offered, since they edit a file, and the one that needs a
 * published number, the field of view, greyed.
 */
#include "unittest.h"

#include "cheats_openphantom.h"
#include "cheats_original.h"
#include "overlay_cheats.h"
#include "overlay_controls.h"
#include "overlay_dismember.h"
#include "overlay_fog.h"
#include "overlay_freecam.h"
#include "overlay_menu_extras.h"
#include "overlay_model.h"
#include "overlay_multiplayer.h"
#include "overlay_notice.h"
#include "overlay_picture.h"
#include "overlay_reason.h"
#include "overlay_rows.h"
#include "overlay_slider.h"
#include "overlay_stubs.h"
#include "overlay_utilities.h"
#include "session_lock.h"
#include "fog_band_row.h"
#include "fov_row.h"
#include "overlay_row_ids.h"
#include "sensitivity_row.h"
#include "strict_range_row.h"
#include "subtitle_size_row.h"
#include "view_range_row.h"

#include "common/ini.h"
#include "common/player_help_note.h"

#include "common/text.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* Shared by the sections below, which run in order and hand state on to each other. */
static overlay_row_t row;

/* The groups above the picture, opened so that every position in overlay_rows.h holds. The
 * appearance group between them stays folded: what it offers is whatever the installation has, so
 * a count through it would be a count of somebody's files. */
static void open_the_groups_above(void)
{
    ut_section("the groups above the picture, opened so the positions below hold");
    overlay_model_reset();
    overlay_model_set_tab(OVERLAY_TAB_OPENPHANTOM);
    overlay_model_toggle_group((uint32_t)OVERLAY_GROUP_OPENPHANTOM);
    overlay_model_toggle_group((uint32_t)OVERLAY_GROUP_OPENPHANTOM_FREECAM);
    overlay_model_toggle_group((uint32_t)OVERLAY_GROUP_OPENPHANTOM_SPAWN);
    overlay_model_toggle_group((uint32_t)OVERLAY_GROUP_OPENPHANTOM_LEVELS);
    overlay_model_rebuild();
    ut_check(overlay_model_row_count() == OPEN_ABOVE_ROWS + OVERLAY_LEVELS_ENTRY_FIRST,
             "the cheats, the free camera and the entity spawner with their lists and folds shut, "
             "and the level selection with its list shut, are on screen, with every heading "
             "below them folded");
}

static void test_utilities_group(void)
{
    ut_section("the menus group: this panel\'s own rows and the game\'s own screens together");
    /* Two sources under one heading, because they answer one question: where the settings of
       this patch turn up. The multiplayer, window and frame rate groups between the controls and
       this one stay folded, so the heading sits four rows past the controls\' last. */
    overlay_model_toggle_group((uint32_t)OVERLAY_GROUP_OPENPHANTOM_UTILITIES);
    overlay_model_rebuild();
    ut_check(overlay_model_row_count() ==
                 OPEN_ABOVE_ROWS + OVERLAY_LEVELS_ENTRY_FIRST +
                 OVERLAY_PICTURE_ROW_COUNT + OVERLAY_FOG_ROW_COUNT +
                 OVERLAY_CONTROLS_FIXED_ROWS +
                 OVERLAY_UTILITIES_ROW_COUNT + OVERLAY_MENU_EXTRAS_LINE_FIRST,
             "open, it holds this panel\'s two rows and the two the game\'s own screens add");
    ut_check(overlay_model_row(UTIL_ROW(0u) - 1u, &row) && row.kind == OVERLAY_ROW_GROUP &&
                 strcmp(row.label, "Menus") == 0,
             "named for what the rows under it answer: where the settings of this patch appear");

    ut_check(overlay_model_row(UTIL_ROW(0), &row) && row.kind == OVERLAY_ROW_VALUE &&
                 strcmp(row.label, "Dev menu size (0.33 to 4.0)") == 0,
             "the dev menu size first, the one typed value here. It is named for the panel's "
             "own title band; the ini key it writes stays DevMenuSize, because a renamed key "
             "drops every size a player has already set");
    ut_check(row.available,
             "available with nothing resolved, unlike every row in the cheats group: every row "
             "here edits a setting file rather than the running game, so they work with no "
             "level loaded and even with the DLL that reads them gone");

    ut_check(overlay_model_row(UTIL_ROW(1), &row) && row.kind == OVERLAY_ROW_HOTKEY,
             "then the key binding, a capture rather than a value");
    ut_check(strcmp(row.label, "Key that opens this menu") == 0,
             "named for what it binds, in the words a player would use for it");
    ut_check(row.id == UTILITIES_FIRST_ID + 1u,
             "and its id is the second of the group\'s own block of ids");
}

/* The two buttons under the chat's key, with no session, which is how every row here is seen in
 * this program. Both are there and both are greyed, with the one word that says a session is
 * what is missing and its sentence once, under the first of them. What a press does in a session
 * is unittests/overlay_session.c's. */
static void test_the_two_buttons_without_a_session(void)
{
    player_help_ask_t ask;
    const uint32_t    closes = overlay_stubs_closes;
    const bool        filed = player_help_ask_read(&ask);
    const uint32_t    serial = filed ? ask.serial : 0u;

    ut_check(overlay_model_row(MP_ROW(OVERLAY_MULTIPLAYER_REPAIR), &row) &&
                 row.kind == OVERLAY_ROW_ACTION && strcmp(row.label, "Repair lock") == 0 &&
                 row.id == MULTIPLAYER_FIRST_ID + (uint32_t)OVERLAY_MULTIPLAYER_REPAIR,
             "under the key, a button: Repair lock");
    ut_check(!row.available && row.reason == (uint32_t)OVERLAY_REASON_NEEDS_SESSION &&
                 strcmp(overlay_reason_word(row.reason), "MP only") == 0,
             "greyed with no session, and its chip says a session is what it needs");
    ut_check(overlay_model_row(MP_ROW(OVERLAY_MULTIPLAYER_REPAIR) + 1u, &row) &&
                 row.kind == OVERLAY_ROW_INFO &&
                 strcmp(row.label, "Only in a multiplayer session") == 0,
             "the sentence behind that word stands under the first row that carries it");
    ut_check(overlay_model_row(MP_ROW(OVERLAY_MULTIPLAYER_TELEPORT) + 1u, &row) &&
                 row.kind == OVERLAY_ROW_ACTION && strcmp(row.label, "Teleport to host") == 0 &&
                 row.id == MULTIPLAYER_FIRST_ID + (uint32_t)OVERLAY_MULTIPLAYER_TELEPORT,
             "then the second button, Teleport to host, one row down for that sentence");
    ut_check(!row.available && row.reason == (uint32_t)OVERLAY_REASON_NEEDS_SESSION,
             "greyed for the same reason, and the sentence is not written a second time");
    ut_check(!overlay_model_activate(MP_ROW(OVERLAY_MULTIPLAYER_REPAIR)) &&
                 !overlay_model_activate(MP_ROW(OVERLAY_MULTIPLAYER_TELEPORT) + 1u),
             "neither can be pressed");
    ut_check(overlay_stubs_closes == closes && player_help_ask_read(&ask) == filed &&
                 (!filed || ask.serial == serial),
             "and a press that was refused neither closes the panel nor files an ask");
}

/* The chat's key, under a heading of its own, because a folded heading that says neither chat nor
 * key is not where a player looks for it.
 *
 * Bound through the model and not through the row's own module, because the path is what can
 * fail: the row is numbered above the appearance group, and the capture hands a key to a group by
 * the highest base below the row's id. A row with no arm of its own lands in the appearance
 * group's, which binds nothing, and the key is dropped with no sentence in the band and nothing in
 * the file. Runs with the controls open and every heading below them folded. */
static void test_multiplayer_group(void)
{
    char before[16];
    char saved[16];
    bool had_key;

    ut_section("the multiplayer group, directly under the controls: the chat\'s key, two buttons");
    ut_check(overlay_model_row(MP_ROW(0u) - 1u, &row) && row.kind == OVERLAY_ROW_GROUP &&
                 strcmp(row.label, "Multiplayer") == 0,
             "its heading follows the controls\' last row, folded: its first row is a key, and a "
             "player looking for a key looks beside the controls");
    ut_check(row.value[0] == '\0',
             "and folded it says nothing, since there is no switch under it and no choice");
    overlay_model_toggle_group((uint32_t)OVERLAY_GROUP_OPENPHANTOM_MULTIPLAYER);
    overlay_model_rebuild();
    ut_check(overlay_multiplayer_row_count() == (uint32_t)OVERLAY_MULTIPLAYER_LAST &&
                 OVERLAY_MULTIPLAYER_ROW_COUNT == (uint32_t)OVERLAY_MULTIPLAYER_LAST + 1u,
             "the group draws three rows, the key and the two buttons, while no button has been "
             "pressed, and budgets its ids for a fourth, the line that says what a press came to");
    ut_check(overlay_model_row_count() ==
                 OPEN_ABOVE_ROWS + OVERLAY_LEVELS_ENTRY_FIRST + OVERLAY_PICTURE_ROW_COUNT +
                 OVERLAY_FOG_ROW_COUNT + OVERLAY_CONTROLS_FIXED_ROWS +
                 (uint32_t)OVERLAY_MULTIPLAYER_LAST + 1u,
             "open, it holds those three and one sentence that says why the buttons are greyed");
    ut_check(overlay_model_row(MP_ROW(OVERLAY_MULTIPLAYER_CHAT_KEY), &row) &&
                 row.kind == OVERLAY_ROW_HOTKEY &&
                 strcmp(row.label, "Key that opens the chat") == 0,
             "the chat\'s key first, under the words the search finds it by");
    ut_check(row.available && row.id == MULTIPLAYER_FIRST_ID && MULTIPLAYER_FIRST_ID == 1344u &&
                 (uint32_t)OVERLAY_MULTIPLAYER_CHAT_KEY == 0u,
             "offered with no multiplayer loaded, since it writes a file. It is still slot 0 and "
             "its id is still 1344, the group\'s base: the buttons came in under it");
    ut_check(strcmp(row.value, "T") == 0,
             "and with no ChatKey in the file it shows T, the key the multiplayer falls back to");
    test_the_two_buttons_without_a_session();
    ut_check(overlay_model_row(MP_ROW(OVERLAY_MULTIPLAYER_LAST) + 1u, &row) &&
                 row.kind == OVERLAY_ROW_GROUP && strcmp(row.label, "Window") == 0,
             "and the window\'s heading comes straight after, with no line about a last press");

    had_key = ini_read_string("multiplayer", "ChatKey", "", before, sizeof before);
    overlay_notice_forget();
    ut_check(overlay_model_activate(MP_ROW(0)) && overlay_model_is_capturing_hotkey(),
             "pressing the row waits for a key");
    overlay_model_rebuild();
    ut_check(overlay_model_row(MP_ROW(0), &row) && strcmp(row.value, "...") == 0,
             "and the row says so while it waits");
    overlay_model_capture_hotkey('U');
    overlay_model_rebuild();
    ut_check(!overlay_model_is_capturing_hotkey(), "the next key ends the wait");
    ut_check(ini_read_string("multiplayer", "ChatKey", "", saved, sizeof saved) &&
                 strcmp(saved, "U") == 0,
             "and reaches the file as its name: a row numbered above the appearance group has an "
             "arm of its own in the capture, and the key is not dropped");
    ut_check(overlay_model_row(MP_ROW(0), &row) && strcmp(row.value, "U") == 0,
             "the row shows the key it now has");
    ut_check(overlay_notice_text() != NULL && strncmp(overlay_notice_text(), "Saved", 5) == 0 &&
                 overlay_notice_confirms(),
             "and the band says it was saved, as a confirmation rather than a refusal, so it is "
             "not drawn in the colour a refusal is");

    /* The file as the run found it, so a second run starts from the same row. */
    (void)ini_write_string("multiplayer", "ChatKey", had_key ? before : NULL);
    overlay_notice_forget();
    overlay_model_toggle_group((uint32_t)OVERLAY_GROUP_OPENPHANTOM_MULTIPLAYER);
    overlay_model_rebuild();
    ut_check(overlay_model_row(MP_ROW(0), &row) && row.kind == OVERLAY_ROW_GROUP &&
                 strcmp(row.label, "Window") == 0,
             "folded again, so the positions of the groups below it hold");
}

static void test_menu_extras_group(void)
{
    ut_section("the game\'s own screens, which continue the menus rather than starting a group");
    ut_check(overlay_model_row(MENU_ROW(0), &row) && row.kind != OVERLAY_ROW_GROUP,
             "no heading stands between this panel\'s last row and the first of theirs");
    ut_check(overlay_model_row(MENU_ROW(0), &row) && row.kind == OVERLAY_ROW_CHEAT &&
                 strcmp(row.label, "Show extra menu options (restart the game)") == 0,
             "the switch that puts this project\'s widgets onto the game\'s own screens, which "
             "ships off so those screens look as they did in 1999");
    ut_check(!row.on,
             "and it reads off with no settings file, matching both of the keys it writes");
    ut_check(overlay_model_row(MENU_ROW(1), &row) && row.kind == OVERLAY_ROW_INFO &&
                 strcmp(row.label, "+ What this adds") == 0,
             "then a fold that says what the switch adds, closed by default and marked with a "
             "plus the same way the free camera\'s is");
    ut_check(overlay_model_row(MENU_ROW(2), &row) && row.kind == OVERLAY_ROW_GROUP &&
                 strcmp(row.label, "Dismemberment") == 0,
             "and the last heading of the tab comes straight after");

    ut_check(overlay_model_activate(MENU_ROW(1)), "clicking the fold\'s summary is accepted");
    overlay_model_rebuild();
    ut_check(overlay_model_row(MENU_ROW(1), &row) &&
                 strcmp(row.label, "- What this adds") == 0,
             "and it reads open, marked with a minus");
    ut_check(overlay_model_row(MENU_ROW(2), &row) && row.kind == OVERLAY_ROW_INFO &&
                 strcmp(row.label, "    Adds this patch's settings to the") == 0,
             "the first line sits immediately below the summary");
    ut_check(!overlay_model_activate(MENU_ROW(2)),
             "but a line itself does nothing when clicked; only the summary is interactive");
    ut_check(overlay_model_row(MENU_ROW(2u + OVERLAY_MENU_EXTRAS_LINE_COUNT), &row) &&
                 row.kind == OVERLAY_ROW_GROUP &&
                 strcmp(row.label, "Dismemberment") == 0,
             "and the last heading is pushed down the screen by the lines");
    ut_check(overlay_model_activate(MENU_ROW(1)), "the same summary row closes it back up");
    overlay_model_rebuild();
    ut_check(overlay_model_row(MENU_ROW(2), &row) && row.kind == OVERLAY_ROW_GROUP,
             "and the lines are gone again");
}

/* One of the picture group's rows, by the slot name overlay_picture.h gives it.
 *
 * The order of this group is written down three times: the table in overlay_picture.c, the
 * enum in overlay_picture.h, and the four slots session_lock.c takes because they write
 * `[view_distance_fix]`. The _Static_assert beside the enum compares LENGTHS, which two orders
 * shuffled against each other pass as easily as two that agree. session_lock.c addresses
 * its four by name, and the walk below is what says each of those names is the row it claims:
 * a row swapped with its neighbour fails here with the label in the message. */
#define PIC(slot) PIC_ROW(OVERLAY_PICTURE_##slot)

static void test_picture_group(void)
{
    ut_section("the engine group, under the level selection, the picture group inside");
    ut_check(overlay_model_row(LVL_ROW(OVERLAY_LEVELS_ENTRY_FIRST), &row) &&
                 row.kind == OVERLAY_ROW_GROUP && strcmp(row.label, "Engine") == 0,
             "its heading follows the level selection group's last row, folded, and it says "
             "Engine, the player's word for how the engine draws the world; it is still no DLL's "
             "name, since three DLLs read the rows under it");
    overlay_model_toggle_group((uint32_t)OVERLAY_GROUP_OPENPHANTOM_PICTURE);
    overlay_model_rebuild();
    ut_check(overlay_model_row_count() ==
                 OPEN_ABOVE_ROWS + OVERLAY_LEVELS_ENTRY_FIRST +
                 OVERLAY_PICTURE_ROW_COUNT + OVERLAY_FOG_ROW_COUNT,
             "open, it holds its own rows AND the fog's, which has no heading of its own: the "
             "fog row that follows the draw distance names a number in this group");

    ut_check(overlay_model_row(PIC(VIEW_RANGE), &row) && row.kind == OVERLAY_ROW_VALUE,
             "the draw distance comes first, a typed value");
    ut_check(strcmp(row.label, "Draw distance (1.0 to 2.5)") == 0,
             "named for what it edits, and carrying the accepted range so a player learns it "
             "from the row rather than from having a number refused");
    ut_check(row.available && row.value[0] != 0,
             "available with nothing resolved and always showing a number, since it edits a "
             "setting file and never the running game");

    ut_check(overlay_model_row(PIC(VIEW_RANGE_TRACK), &row) && row.kind == OVERLAY_ROW_SLIDER,
             "a slider track directly under the draw distance, so the handle never covers the "
             "number it sets");

    ut_check(overlay_model_row(PIC(VIEW_RANGE_LIVE), &row) && row.kind == OVERLAY_ROW_INFO,
             "a note under the draw distance, not a control: it reports what the game is actually "
             "running, which the governor and the cell watchdog can both lower without saying so");
    ut_check(!overlay_model_activate(PIC(VIEW_RANGE_LIVE)),
             "and it cannot be clicked into, which is the whole point of it being a note");

    ut_check(overlay_model_row(PIC(AUTO_RANGE), &row) && row.kind == OVERLAY_ROW_CHEAT,
             "then the automation switch, directly under the setting it governs");
    ut_check(strcmp(row.label, "Draw distance follows the frame rate") == 0,
             "named for what it does to the row above rather than for the machinery behind it");
    ut_check(row.available, "available for the same reason as the row above it");

    ut_check(overlay_model_row(PIC(STRICT_RANGE), &row) && row.kind == OVERLAY_ROW_CHEAT,
             "then the strict switch, beside the automation switch it supersedes rather than "
             "somewhere else in the list");
    ut_check(strcmp(row.label, "Keep the draw distance (costs frame rate)") == 0,
             "named for the trade rather than the machinery, and for the cost a reader meets in "
             "ordinary play: the watchdog only acts above 1.00x");
    ut_check(row.available, "available for the same reason as the row above it");

    ut_check(overlay_model_row(PIC(FOV), &row) && row.kind == OVERLAY_ROW_VALUE,
             "then the field of view, a typed value like the one above it");
    ut_check(strncmp(row.label, "Field of view (", 15) == 0,
             "carrying the range variable_fov\'s own slider offers, read from the file rather than "
             "assumed, so widening that slider widens this row with it");
    ut_check(!row.available && row.value[0] == 0,
             "and it is the ONE row here that can be unavailable: it needs a width in degrees that "
             "only variable_fov can publish, and with that DLL absent there is nothing to show");

    ut_check(overlay_model_row(PIC(FOV_TRACK), &row) && row.kind == OVERLAY_ROW_SLIDER,
             "and its TRACK is a row of its own directly under it, rather than squeezed into the "
             "gap beside the number: a line costs one row and buys a target several times longer "
             "that cannot be mistaken for a rule struck through the name");
    ut_check(row.label[0] == 0,
             "with no text of its own, because it belongs to the row above rather than saying "
             "anything a reader has not just read");
    ut_check(!row.available,
             "and it is unavailable exactly when the row it drives is, so a handle is never "
             "offered for a value that cannot be shown");

    ut_check(overlay_model_row(PIC(SUBTITLE_SIZE), &row) && row.kind == OVERLAY_ROW_VALUE &&
                 strcmp(row.label, "Subtitle size (0.50 to 3.0)") == 0,
             "the subtitle size last, named for what it changes rather than for the key it "
             "writes: the one row here that is not the 3-D picture, and enhanced_resolution\'s "
             "own key");
    ut_check(overlay_model_row(PIC(SUBTITLE_SIZE_TRACK), &row) && row.kind == OVERLAY_ROW_SLIDER,
             "with a track of its own beneath it: this one moves text somewhere else on the "
             "screen, which is a slider\'s job");
}


static void test_fog_group(void)
{
    ut_section("the fog rows, which continue the picture rather than starting a group");
    ut_check(overlay_model_row(FOG_ROW(0), &row) && row.kind != OVERLAY_ROW_GROUP,
             "no heading stands between the last picture row and the first fog row");

    ut_check(overlay_model_row(FOG_ROW(0), &row) && row.kind == OVERLAY_ROW_CHEAT,
             "no fog heads the group, moved out of the cheats long ago so every fog control a "
             "player might look for sits together");

    ut_check(overlay_model_row(FOG_ROW(1), &row) && row.kind == OVERLAY_ROW_VALUE,
             "then the fog thickness, a typed value since how near the fog sits is a number");
    ut_check(strcmp(row.label, "Fog thickness (0.25 to 1.0)") == 0,
             "named for what a player would call it, carrying its range like the value above");

    ut_check(overlay_model_row(FOG_ROW(2), &row) && row.kind == OVERLAY_ROW_SLIDER,
             "and the fog thickness gets its own track the same way");

    ut_check(overlay_model_row(FOG_ROW(3), &row) && row.kind == OVERLAY_ROW_CHEAT &&
                 strcmp(row.label, "Fog follows the draw distance") == 0,
             "then whether the band follows the draw distance, which decides what the thickness "
             "above is a share of rather than whether it applies at all");
    ut_check(row.available, "always available: it edits a setting file, like every row here");

}

static void test_controls_group(void)
{
    ut_section("the controls group, the control scheme under the picture");
    ut_check(overlay_model_row(FOG_ROW(OVERLAY_FOG_ROW_COUNT), &row) &&
                 row.kind == OVERLAY_ROW_GROUP && strcmp(row.label, "Controls") == 0,
             "its heading follows the last fog row, folded, and is named for the scheme rather "
             "than for the DLL that reads it");
    overlay_model_toggle_group((uint32_t)OVERLAY_GROUP_OPENPHANTOM_CONTROLS);
    overlay_model_rebuild();
    ut_check(overlay_model_row_count() ==
                 OPEN_ABOVE_ROWS + OVERLAY_LEVELS_ENTRY_FIRST +
                 OVERLAY_PICTURE_ROW_COUNT + OVERLAY_FOG_ROW_COUNT + OVERLAY_CONTROLS_FIXED_ROWS,
             "open, its eight rows sit between the picture and the multiplayer group\'s heading");

    ut_check(overlay_model_row(CTRL_ROW(0), &row) && row.kind == OVERLAY_ROW_CHEAT &&
                 strcmp(row.label, "Enhanced controller mode (the four below)") == 0,
             "the pad\'s one-click switch first, above the four rows it sets, since their names "
             "give no hint that a pad wants all of them");
    ut_check(!row.on, "and it reads off with no settings file, as all four of its rows do");
    ut_check(row.available, "and it is always available, since it only writes the file");

    ut_check(overlay_model_row(CTRL_ROW(1), &row) && row.kind == OVERLAY_ROW_CHEAT &&
                 strcmp(row.label, "Free look") == 0,
             "then free look, under the name the game\'s own controls screen gave it, so a reader "
             "who has seen that screen recognises this row");
    ut_check(row.available, "always available: it edits a settings file");

    ut_check(overlay_model_row(CTRL_ROW(2), &row) && row.kind == OVERLAY_ROW_CHEAT &&
                 strcmp(row.label, "Strafe") == 0,
             "then sideways walking, under the game\'s own name for it as well, not a "
             "description this panel invented");

    ut_check(overlay_model_row(CTRL_ROW(3), &row) && row.kind == OVERLAY_ROW_CHEAT,
             "the camera follow sits directly under the strafe row it depends on");
    ut_check(!row.available,
             "and is unavailable with strafe off, because the walk never leaves the heading "
             "then, so there would be nothing for the camera to follow");

    ut_check(overlay_model_row(CTRL_ROW(4), &row) && row.kind == OVERLAY_ROW_CHEAT,
             "steering a jump sits beside the camera follow, since both are built on free look");
    ut_check(!row.available,
             "and is unavailable with strafe off too, because the angle it steers by is built "
             "from the sideways input and there would be nothing to aim with");

    ut_check(overlay_model_row(CTRL_ROW(5), &row) && row.kind == OVERLAY_ROW_VALUE &&
                 strcmp(row.label, "Mouse speed") == 0,
             "then the mouse speed, which is here because the game\'s own controls screen no "
             "longer offers it and mouse look still ships on, and which carries that screen\'s "
             "name too");
    ut_check(overlay_model_row(CTRL_ROW(6), &row) && row.kind == OVERLAY_ROW_SLIDER &&
                 row.available,
             "with a track of its own beneath it, and unlike the field of view it is always "
             "available: both of its ends are fixed, so nothing has to be published first");

    ut_check(overlay_model_row(CTRL_ROW(7), &row) && row.kind == OVERLAY_ROW_INFO &&
                 strcmp(row.label, "+ What these do") == 0,
             "then a fold that says what each row does, closed by default and marked with a "
             "plus the same way the free camera\'s is");
    ut_check(overlay_model_row(CTRL_ROW(8), &row) && row.kind == OVERLAY_ROW_GROUP &&
                 strcmp(row.label, "Multiplayer") == 0,
             "and the multiplayer group\'s heading comes straight after, so the group took "
             "nothing from below it");

    ut_check(overlay_model_activate(CTRL_ROW(7)), "clicking the fold\'s summary is accepted");
    overlay_model_rebuild();
    ut_check(overlay_model_row(CTRL_ROW(8), &row) && row.kind == OVERLAY_ROW_INFO &&
                 strcmp(row.label, "    Controller mode: the best modern feel") == 0,
             "the first line sits immediately below the summary and names the first row");
    ut_check(!overlay_model_activate(CTRL_ROW(8)),
             "but a line itself does nothing when clicked; only the summary is interactive");
    ut_check(overlay_model_row(CTRL_ROW(8u + OVERLAY_CONTROLS_LINE_COUNT), &row) &&
                 row.kind == OVERLAY_ROW_GROUP && strcmp(row.label, "Multiplayer") == 0,
             "and the multiplayer group\'s heading is pushed down the screen by the lines");
    ut_check(overlay_model_activate(CTRL_ROW(7)), "the same summary row closes it back up");
    overlay_model_rebuild();
    ut_check(overlay_model_row(CTRL_ROW(8), &row) && row.kind == OVERLAY_ROW_GROUP,
             "and the lines are gone again");
}

static void test_draw_distance_switches_exclude(void)
{
    ut_section("the two draw-distance switches cannot both be on");
    /* They contradict each other: strict mode declines the governor outright, so a frame-rate
       switch still reading ON would be describing something that is not happening. This writes the
       settings file, which is the only channel these rows have, and puts it back afterwards. */
    if (strict_range_row_set(true)) {
        overlay_model_rebuild();
        ut_check(overlay_model_row(PIC(AUTO_RANGE), &row) && !row.available,
                 "with strict on, the frame-rate switch is greyed rather than left reading ON "
                 "over a governor that is no longer acting");
        ut_check(!row.on, "and it reports off, which is the state the game is actually in");
        ut_check(!overlay_model_activate(PIC(AUTO_RANGE)),
                 "and it cannot be clicked, so the two can never both be on");

        ut_check(strict_range_row_set(false), "strict goes back off");
        overlay_model_rebuild();
        ut_check(overlay_model_row(PIC(AUTO_RANGE), &row) && row.available,
                 "and the frame-rate switch comes back, because strict never wrote its key: a "
                 "reader who turns strict on to look at something gets their governor back");
    } else {
        ut_check(false,
                 "the settings file could not be written, so the exclusion was never exercised");
    }
}

static void test_typed_rows_kept_apart(void)
{
    ut_section("the two groups keep their own typed rows apart");
    /* Both groups hold a value row and a key row, and the panel remembers which row is being
       edited by its id alone. Overlapping ids would land a commit on the wrong group's row, which
       is why the utilities are numbered from a base clear of the cheats group. */
    ut_check(overlay_model_row(PIC(VIEW_RANGE), &row),
             "the picture group\'s draw distance row exists");
    {
        uint32_t utility_id = row.id;

        ut_check(overlay_model_row(1u + OVERLAY_CHEATS_JUMP_SCALE_SLOT, &row) &&
                     row.kind == OVERLAY_ROW_VALUE,
                 "and so does the cheats group's own typed row, the jump-boost scale");
        ut_check(row.id != utility_id,
                 "and the two carry different ids, so a commit cannot land on the other group's "
                 "row");
    }

}

/* What a refused number SAYS. A row that drops a refused answer goes on showing the value it
 * already had, which is exactly what it shows when the write worked and the value happened to be
 * the same, so nothing anywhere tells a player their number was turned down. The one sentence
 * lives in overlay_edit.c, and this is the check that holds it there.
 *
 * A lone full stop is the refusal a player can actually reach. The field accepts digits and one
 * point and nothing else, so the text is always nearly a number; the parser refuses it for having
 * no digit in it, and the same row clamps anything that does. */
static void test_a_refused_number_says_so(void)
{
    float was;

    ut_section("a number the row cannot take says so");
    ut_check(overlay_model_row(PIC(VIEW_RANGE), &row) && row.available,
             "the draw distance is there");
    was = row.fraction;

    overlay_notice_forget();
    ut_check(overlay_model_activate(PIC(VIEW_RANGE)) && overlay_model_is_editing_value(),
             "and it is being typed into");
    overlay_model_value_append('.');
    overlay_model_value_commit();
    ut_check(overlay_notice_text() != NULL &&
                 strstr(overlay_notice_text(), "not a value that row takes") != NULL,
             "a full stop on its own is no number, and the band says it was turned down");
    ut_check(strstr(overlay_notice_text(), ".") != NULL,
             "and says what was typed, which is the half the row itself cannot show");

    overlay_notice_forget();
    ut_check(overlay_model_activate(PIC(VIEW_RANGE)) && overlay_model_is_editing_value(),
             "the same row again");
    overlay_model_value_append('1');
    overlay_model_value_commit();
    ut_check(overlay_notice_text() == NULL,
             "and a number the row does take says nothing at all: the band is for refusals and "
             "not for every keystroke");

    /* Back where the run found it, so nothing after this reads a draw distance this check set. */
    (void)overlay_model_slider_set(PIC(VIEW_RANGE_TRACK), was);
    overlay_model_rebuild();
}

/* Three groups build their rows out of a table now (overlay_kit.h), and a table's length is what
 * stops matching the number written beside it. That number is what the model asks for when it
 * walks the group, so a table that grew or shrank would either build rows nobody draws or draw
 * rows nobody built, and the slot numbers under it are what session_lock.c addresses. A NUMBER
 * entry counts TWO, its value and the track beneath it, which is the arithmetic worth pinning. */
static void test_the_tables_and_their_counts(void)
{
    ut_section("a group's table draws exactly as many rows as the group says it does");
    ut_check(overlay_picture_row_count() == OVERLAY_PICTURE_ROW_COUNT,
             "the picture's six entries draw its nine rows: three of them are numbers and each "
             "brings a track");
    ut_check(overlay_fog_row_count() == OVERLAY_FOG_ROW_COUNT,
             "the fog's three entries draw its four");
    ut_check(overlay_dismember_row_count() == OVERLAY_DISMEMBER_ROW_COUNT,
             "and dismemberment's one entry draws one");
}

/* One fraction, one number. While a track is held, the handle is drawn from the hand and the file
 * is written four times a second, so the number on the row above it has to come out of the same
 * fraction the handle is drawn at. The model's answer here is what the drawing puts there.
 *
 * What would be silent if it were wrong: the row would read a number its own handle is not at,
 * only while somebody drags it, and only by as much as a quarter of a second of hand movement. No
 * build and no log would say anything, and the file would end up holding whichever of the two the
 * hand let go on.
 *
 * Driven over the picture group's tracks by position, because position is what the drawing
 * addresses. It writes the settings file, which is the only channel these rows have, and puts
 * each track back where it found it. */
static void test_the_number_under_the_handle(void)
{
    static const uint32_t TRACKS[] = { PIC(VIEW_RANGE_TRACK), PIC(FOV_TRACK),
                                       PIC(SUBTITLE_SIZE_TRACK) };
    static const float    FRACTIONS[] = { 0.0f, 0.2f, 0.5f, 0.9f, 1.0f };
    uint32_t              driven = 0;
    size_t                t;
    size_t                f;

    ut_section("the number on a row and the handle under it come from one fraction");
    for (t = 0; t < sizeof TRACKS / sizeof TRACKS[0]; ++t) {
        float was;

        if (!overlay_model_row(TRACKS[t], &row) || row.kind != OVERLAY_ROW_SLIDER) {
            ut_checkf(0, "row %u is a track", (unsigned)TRACKS[t]);
            continue;
        }
        if (!row.available) {
            continue;   /* the field of view, when nobody published a width to put it in */
        }
        was = row.fraction;
        ++driven;
        for (f = 0; f < sizeof FRACTIONS / sizeof FRACTIONS[0]; ++f) {
            char from_the_hand[16];
            char from_the_file[16];

            if (!overlay_model_slider_value(TRACKS[t], FRACTIONS[f], from_the_hand,
                                            sizeof from_the_hand) ||
                !overlay_model_slider_set(TRACKS[t], FRACTIONS[f])) {
                ut_checkf(0, "track %u answers for its handle at %.2f and takes the drag",
                          (unsigned)TRACKS[t], (double)FRACTIONS[f]);
                continue;
            }
            /* The same fraction down the other path: written, built again, and read back off the
             * row above, which is where the number comes from once the hand lets go. */
            overlay_model_rebuild();
            if (!overlay_model_row(TRACKS[t] - 1u, &row) || row.kind != OVERLAY_ROW_VALUE) {
                ut_checkf(0, "the row above track %u is the value it drives",
                          (unsigned)TRACKS[t]);
                continue;
            }
            text_format(from_the_file, sizeof from_the_file, "%s", row.value);
            ut_checkf(strcmp(from_the_hand, from_the_file) == 0,
                      "track %u at %.2f: the hand says \"%s\" and the file says \"%s\"",
                      (unsigned)TRACKS[t], (double)FRACTIONS[f], from_the_hand, from_the_file);
        }
        (void)overlay_model_slider_set(TRACKS[t], was);
        overlay_model_rebuild();
    }
    ut_check(driven > 0u, "at least one track was there to drive");

    {
        char shown[16];

        ut_check(!overlay_model_slider_value(PIC(VIEW_RANGE_TRACK), 0.5f, NULL, 0u),
                 "nowhere to write the number is no answer");
        ut_check(!overlay_model_slider_value(PIC(VIEW_RANGE), 0.5f, shown, sizeof shown),
                 "and the value row above a track is not a track");
    }
}

/* The same number in the two places the panel shows it, over the real rows.
 *
 * The state that matters is mid-drag, with the handle held a long way from what the file
 * still has: a row reading the file there is a number that disagrees with the handle drawn
 * right beside it, and it disagrees for up to a quarter of a second at a time, which is
 * long enough to read and short enough never to be caught in a screenshot. */
static void test_the_two_rows_one_track_drives(void)
{
    const uint32_t track = PIC(VIEW_RANGE_TRACK);
    overlay_row_t  above;
    overlay_row_t  on_the_track;
    char           from_the_file[16];
    char           from_the_hand[16];

    ut_section("the two rows one track drives");
    (void)overlay_model_slider_set(track, 0.0f);
    overlay_model_rebuild();
    ut_check(overlay_model_row(track, &on_the_track) &&
                 on_the_track.kind == OVERLAY_ROW_SLIDER && on_the_track.available,
             "the draw distance's track is there");
    text_format(from_the_file, sizeof from_the_file, "%s", on_the_track.value);
    ut_check(from_the_file[0] != 0,
             "the track row carries the number as well as the row above it, so an eye on "
             "the handle does not travel a row and the width of the panel to read it");
    ut_check(overlay_model_row(track - 1u, &above) &&
                 strcmp(above.value, from_the_file) == 0,
             "and with no hand on it, that is the same number the row above shows: one "
             "reading of one file");

    /* A hand on the track, a long way from what the file has, and nothing written yet. */
    overlay_slider_take(OVERLAY_SLIDER_TRIGGER, (int32_t)track, 0.8f);
    ut_check(overlay_model_row(track - 1u, &above) &&
                 overlay_model_row(track, &on_the_track),
             "both rows are still on screen");
    overlay_slider_number_on(track - 1u, &above);
    overlay_slider_number_on(track, &on_the_track);
    ut_check(strcmp(above.value, on_the_track.value) == 0,
             "mid-drag the two read the same thing");
    ut_check(strcmp(on_the_track.value, from_the_file) != 0,
             "and it is NOT what the file still says, which is what the handle beside it "
             "would have disagreed with");
    ut_check(overlay_model_slider_value(track, 0.8f, from_the_hand, sizeof from_the_hand) &&
                 strcmp(on_the_track.value, from_the_hand) == 0,
             "it is the fraction the hand has, through the one inversion");

    overlay_slider_let_go(OVERLAY_SLIDER_TRIGGER);
    (void)overlay_model_slider_set(track, 0.0f);
    overlay_model_rebuild();
}

/* The retail codes a running session takes, against the eleven the game has.
 *
 * session_lock.c names its three as text, because a code's index belongs to the
 * executable. Nothing held those three against anything: unittests/session_lock.c has to
 * stand in for the executable at exactly the seam that would answer, so its own table is
 * the lock's three literals written a second time and the two agree by being copies.
 * This program links the lock and cheats_original.c together and is therefore the one
 * place the question can be asked. What it catches: a code misspelled in session_lock.c
 * matches nothing the running game ever passes, the row stays open through a session,
 * and no build and no other program says a word. */
static void test_the_locked_codes_are_real(void)
{
    uint32_t i;

    ut_section("every retail code a session takes is a code the game actually has");
    ut_check(session_lock_taken_toggle_count() > 0u &&
                 session_lock_taken_toggle_count() < cheats_original_known_count(),
             "the lock takes some of the console's codes and not all of them");

    for (i = 0; i < session_lock_taken_toggle_count(); ++i) {
        const char *taken = session_lock_taken_toggle(i);
        bool        known = false;
        uint32_t    k;
        char        note[128];

        for (k = 0; k < cheats_original_known_count(); ++k) {
            if (strcmp(taken, cheats_original_known_code(k)) == 0) {
                known = true;
                break;
            }
        }
        text_format(note, sizeof note,
                    "\"%s\" is one of the eleven codes cheats_original.c knows", taken);
        ut_check(known, note);
    }

    ut_check(session_lock_taken_toggle(session_lock_taken_toggle_count()) == NULL &&
                 cheats_original_known_code(cheats_original_known_count()) == NULL,
             "and both lists end where they say they do");
}

/* Where each track's Default comes from, over the real rows.
 *
 * This is the check the naming traps are about. The picture group has three numbers and
 * three different kinds of standard: one that is a real default in the middle of its band,
 * one that is only the value an untouched installation reads, and one that is not a constant
 * at all. Two constants in those rows' headers are named like defaults and are not:
 * FOV_ROW_MIN_DEFAULT and FOV_ROW_MAX_DEFAULT are the two ENDS of the field of view's track,
 * named for the defaults variable_fov's own slider uses for them. A Default wired to the
 * first would set the narrowest picture the row allows and call it the default, and nothing
 * in the build or in a log would say so.
 *
 * The field of view is driven with a base written into the file underneath it, because with
 * none published the row has no standard at all, which is the other half of the claim. */
static void test_where_a_default_comes_from(void)
{
    overlay_number_t limits;

    ut_section("where each track's Default comes from");
    ut_check(overlay_model_slider_limits(PIC(SUBTITLE_SIZE_TRACK), &limits) &&
                 limits.has_standard && limits.standard == SUBTITLE_SIZE_DEFAULT,
             "the subtitle size goes back to the size the text has at 640x480");
    ut_check(limits.standard > limits.minimum && limits.standard < limits.maximum,
             "which is a value in the middle of its band and neither end of the track");

    ut_check(overlay_model_slider_limits(PIC(VIEW_RANGE_TRACK), &limits) &&
                 limits.has_standard && limits.standard == VIEW_RANGE_MIN,
             "the draw distance goes back to what an untouched installation reads, which "
             "is the value its reader falls back to and not a default anybody chose");

    /* With nothing published, the field of view has no Default rather than a wrong one. */
    ut_check(ini_write_float("variable_fov", "BaseFov", 0.0f, 1),
             "the test can write the file the row reads");
    overlay_model_rebuild();
    ut_check(!overlay_model_slider_limits(PIC(FOV_TRACK), &limits),
             "with no base published the row cannot be used at all, so it has neither a Default "
             "nor a press: a row that cannot say what it is showing cannot be put back to "
             "anything");

    (void)ini_write_float("variable_fov", "ExtraDegrees", 0.0f, 1);
    ut_check(ini_write_float("variable_fov", "BaseFov", 75.0f, 1), "a base is published");
    overlay_model_rebuild();
    ut_check(overlay_model_slider_limits(PIC(FOV_TRACK), &limits) && limits.has_standard &&
                 limits.standard == 75.0f,
             "and then Default is the width with no offset, which is that base");
    ut_check(limits.minimum == FOV_ROW_MIN_DEFAULT && limits.standard != limits.minimum,
             "and NOT FOV_ROW_MIN_DEFAULT, which is sitting right there as the low end of the "
             "same track and is what the misleading name would have given it");

    /* Put the file back where the run found it, so nothing after this sees a field of view
     * that only this check published. */
    (void)ini_write_float("variable_fov", "BaseFov", 0.0f, 1);
    overlay_model_rebuild();

    /* The four that are named in their own headers. Nothing else holds them: the fog's, moved to
     * the low end of its track, would fail no other test, and that is a Default the panel draws,
     * that Return presses and that a player uses to get back out of a setting they broke. The
     * cheats' two are asked of the group by id rather than of the row by position: their rows
     * need a resolved cheat to be available at all, and nothing resolves in a test process, so
     * overlay_model_slider_limits() answers nothing for either. It is the same table the row
     * itself is filled from. */
    ut_check(overlay_model_slider_limits(FOG_ROW(2), &limits) && limits.has_standard &&
                 limits.standard == FOG_BAND_DEFAULT,
             "the fog thickness goes back to FOG_BAND_DEFAULT, which is the band the game ships "
             "with and happens to be the top of the track as well");
    ut_check(overlay_model_slider_limits(CTRL_ROW(6), &limits) && limits.has_standard &&
                 limits.standard == SENSITIVITY_DEFAULT,
             "the mouse speed goes back to what the game's own controls screen used to set");
    ut_check(limits.standard > limits.minimum && limits.standard < limits.maximum,
             "which is a value inside its band and neither end of the track");

    ut_check(overlay_cheats_slider_limits(JUMP_SCALE_TRACK_ROW_ID, &limits) &&
                 limits.has_standard && limits.standard == JUMP_BOOST_SCALE_DEFAULT,
             "the jump boost scale goes back to the height the cheat is installed with");
    ut_check(overlay_cheats_slider_limits(SUPER_RUN_TRACK_ROW_ID, &limits) &&
                 limits.has_standard && limits.standard == SUPER_RUN_SCALE_DEFAULT,
             "and super run to the speed its own settings key falls back to");
}

int main(void)
{
    test_the_tables_and_their_counts();
    test_the_locked_codes_are_real();
    open_the_groups_above();
    test_picture_group();
    test_fog_group();
    test_controls_group();
    test_multiplayer_group();
    test_utilities_group();
    test_menu_extras_group();
    test_draw_distance_switches_exclude();
    test_typed_rows_kept_apart();
    test_a_refused_number_says_so();
    test_the_number_under_the_handle();
    test_the_two_rows_one_track_drives();
    test_where_a_default_comes_from();
    return ut_summary("the overlay's settings groups");
}
