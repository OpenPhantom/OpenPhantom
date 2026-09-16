/* overlay_groups.c: the settings groups of the OpenPhantom tab, walked by position.
 *
 * The other half of the model test. overlay_model.c holds the navigation, the search, the cheats,
 * the free camera and the folds; this program opens the groups above the settings the way a
 * player would and then checks every settings row in the order it is drawn, its caption, its kind
 * and whether it is offered, against the row beside it. Each check is written as a claim in
 * English because this is the only place the panel's intended order is stated at all.
 *
 * Nothing is stubbed beyond overlay_stubs.c, and none of the cheat sources has resolved anything
 * in a test process, so every row here is in the state a player sees on an unsupported
 * executable: the settings rows all offered, since they edit a file, and the one that needs a
 * published number, the field of view, greyed.
 */
#include "unittest.h"

#include "cheats_openphantom.h"
#include "overlay_controls.h"
#include "overlay_dismember.h"
#include "overlay_fog.h"
#include "overlay_freecam.h"
#include "overlay_menu_extras.h"
#include "overlay_model.h"
#include "overlay_picture.h"
#include "overlay_rows.h"
#include "overlay_utilities.h"
#include "strict_range_row.h"

#include <stdint.h>
#include <string.h>

/* Shared by the sections below, which run in order and hand state on to each other. */
static overlay_row_t row;

/* The groups above the settings, opened so that every position in overlay_rows.h holds. */
static void open_the_groups_above(void)
{
    ut_section("the groups above the settings, opened so the positions below hold");
    overlay_model_reset();
    overlay_model_set_tab(OVERLAY_TAB_OPENPHANTOM);
    overlay_model_toggle_group((uint32_t)OVERLAY_GROUP_OPENPHANTOM);
    overlay_model_toggle_group((uint32_t)OVERLAY_GROUP_OPENPHANTOM_LEVELS);
    overlay_model_toggle_group((uint32_t)OVERLAY_GROUP_OPENPHANTOM_SPAWN);
    overlay_model_toggle_group((uint32_t)OVERLAY_GROUP_OPENPHANTOM_FREECAM);
    overlay_model_toggle_group((uint32_t)OVERLAY_GROUP_OPENPHANTOM_DISMEMBER);
    overlay_model_rebuild();
    ut_check(overlay_model_row_count() == HEADINGS + OVERLAY_CHEATS_ROW_COUNT + 2u + 6u + 5u +
                 1u,
             "the cheats, the level selection and the NPC spawner with their lists and fold "
             "shut, the free camera with its fold shut and the dismemberment switch are on "
             "screen, with every settings heading folded below them");
}

static void test_utilities_group(void)
{
    ut_section("the Cheatmenu options group, the panel's own rows");
    /* A group of its own since the settings outgrew the cheats they were appended to, and a
       small one since everything else took a heading of its own. Indexed from its heading, which
       sits directly after the dismemberment group's row. */
    overlay_model_toggle_group((uint32_t)OVERLAY_GROUP_OPENPHANTOM_UTILITIES);
    overlay_model_rebuild();
    ut_check(overlay_model_row_count() ==
                 HEADINGS + OVERLAY_CHEATS_ROW_COUNT + 2u + 6u + 5u + 1u +
                     OVERLAY_UTILITIES_ROW_COUNT,
             "every heading, the cheats, free camera and dismemberment groups\' own rows, and "
             "both rows of this one under the fourth heading");
    ut_check(overlay_model_row(DIS_ROW(1), &row) && row.kind == OVERLAY_ROW_GROUP,
             "the fourth heading sits directly after the dismemberment group\'s row");
    ut_check(strcmp(row.label, "Cheatmenu options") == 0,
             "named for what the rows under it set: this panel, and nothing in the game");

    ut_check(overlay_model_row(UTIL_ROW(0), &row) && row.kind == OVERLAY_ROW_VALUE &&
                 strcmp(row.label, "Cheatmenu size (0.33 to 4.0)") == 0,
             "the Cheatmenu size first, the one typed value here");
    ut_check(row.available,
             "available with nothing resolved, unlike every row in the cheats group: every row "
             "here edits a setting file rather than the running game, so they work with no "
             "level loaded and even with the DLL that reads them gone");

    ut_check(overlay_model_row(UTIL_ROW(1), &row) && row.kind == OVERLAY_ROW_HOTKEY,
             "and the key binding last, a capture rather than a value");
    ut_check(strcmp(row.label, "Key that opens this menu") == 0,
             "named for what it binds, in the words a player would use for it");
}

static void test_menu_extras_group(void)
{
    ut_section("the in game options extras group, one switch for the game's own screens");
    ut_check(overlay_model_row(UTIL_ROW(OVERLAY_UTILITIES_ROW_COUNT), &row) &&
                 row.kind == OVERLAY_ROW_GROUP &&
                 strcmp(row.label, "In game options extras") == 0,
             "its heading follows the last Cheatmenu row, folded");
    overlay_model_toggle_group((uint32_t)OVERLAY_GROUP_OPENPHANTOM_MENU_EXTRAS);
    overlay_model_rebuild();
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
                 strcmp(row.label, "Enhanced resolution") == 0,
             "and the picture\'s heading comes straight after");

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
                 strcmp(row.label, "Enhanced resolution") == 0,
             "and the picture\'s heading is pushed down the screen by the lines");
    ut_check(overlay_model_activate(MENU_ROW(1)), "the same summary row closes it back up");
    overlay_model_rebuild();
    ut_check(overlay_model_row(MENU_ROW(2), &row) && row.kind == OVERLAY_ROW_GROUP,
             "and the lines are gone again");
}

static void test_picture_group(void)
{
    ut_section("the enhanced resolution group, the picture under the utilities");
    ut_check(overlay_model_row(MENU_ROW(OVERLAY_MENU_EXTRAS_LINE_FIRST), &row) &&
                 row.kind == OVERLAY_ROW_GROUP && strcmp(row.label, "Enhanced resolution") == 0,
             "its heading follows the in game options extras row, folded");
    overlay_model_toggle_group((uint32_t)OVERLAY_GROUP_OPENPHANTOM_PICTURE);
    overlay_model_rebuild();
    ut_check(overlay_model_row_count() ==
                 HEADINGS + OVERLAY_CHEATS_ROW_COUNT + 2u + 6u + 5u + 1u +
                 OVERLAY_UTILITIES_ROW_COUNT + 2u +
                 OVERLAY_PICTURE_ROW_COUNT,
             "open, its rows sit between the utilities and the fog heading");

    ut_check(overlay_model_row(PIC_ROW(0), &row) && row.kind == OVERLAY_ROW_VALUE,
             "the draw distance comes first, a typed value");
    ut_check(strcmp(row.label, "Draw distance (1.0 to 2.5)") == 0,
             "named for what it edits, and carrying the accepted range so a player learns it "
             "from the row rather than from having a number refused");
    ut_check(row.available && row.value[0] != 0,
             "available with nothing resolved and always showing a number, since it edits a "
             "setting file and never the running game");

    ut_check(overlay_model_row(PIC_ROW(1), &row) && row.kind == OVERLAY_ROW_SLIDER,
             "a slider track directly under the draw distance, so the handle never covers the "
             "number it sets");

    ut_check(overlay_model_row(PIC_ROW(2), &row) && row.kind == OVERLAY_ROW_INFO,
             "a note under the draw distance, not a control: it reports what the game is actually "
             "running, which the governor and the cell watchdog can both lower without saying so");
    ut_check(!overlay_model_activate(PIC_ROW(2)),
             "and it cannot be clicked into, which is the whole point of it being a note");

    ut_check(overlay_model_row(PIC_ROW(3), &row) && row.kind == OVERLAY_ROW_CHEAT,
             "then the automation switch, directly under the setting it governs");
    ut_check(strcmp(row.label, "Draw distance follows the frame rate") == 0,
             "named for what it does to the row above rather than for the machinery behind it");
    ut_check(row.available, "available for the same reason as the row above it");

    ut_check(overlay_model_row(PIC_ROW(4), &row) && row.kind == OVERLAY_ROW_CHEAT,
             "then the strict switch, beside the automation switch it supersedes rather than "
             "somewhere else in the list");
    ut_check(strcmp(row.label, "Keep the draw distance (costs frame rate)") == 0,
             "named for the trade rather than the machinery, and for the cost a reader meets in "
             "ordinary play: the watchdog only acts above 1.00x");
    ut_check(row.available, "available for the same reason as the row above it");

    ut_check(overlay_model_row(PIC_ROW(5), &row) && row.kind == OVERLAY_ROW_VALUE,
             "then the field of view, a typed value like the one above it");
    ut_check(strncmp(row.label, "Field of view (", 15) == 0,
             "carrying the range variable_fov\'s own slider offers, read from the file rather than "
             "assumed, so widening that slider widens this row with it");
    ut_check(!row.available && row.value[0] == 0,
             "and it is the ONE row here that can be unavailable: it needs a width in degrees that "
             "only variable_fov can publish, and with that DLL absent there is nothing to show");

    ut_check(overlay_model_row(PIC_ROW(6), &row) && row.kind == OVERLAY_ROW_SLIDER,
             "and its TRACK is a row of its own directly under it, rather than squeezed into the "
             "gap beside the number: a line costs one row and buys a target several times longer "
             "that cannot be mistaken for a rule struck through the name");
    ut_check(row.label[0] == 0,
             "with no text of its own, because it belongs to the row above rather than saying "
             "anything a reader has not just read");
    ut_check(!row.available,
             "and it is unavailable exactly when the row it drives is, so a handle is never "
             "offered for a value that cannot be shown");

    ut_check(overlay_model_row(PIC_ROW(7), &row) && row.kind == OVERLAY_ROW_VALUE &&
                 strcmp(row.label, "Subtitle size (0.50 to 3.0)") == 0,
             "the subtitle size last, named for what it changes rather than for the key it "
             "writes: the one row here that is not the 3-D picture, and enhanced_resolution\'s "
             "own key");
    ut_check(overlay_model_row(PIC_ROW(8), &row) && row.kind == OVERLAY_ROW_SLIDER,
             "with a track of its own beneath it: this one moves text somewhere else on the "
             "screen, which is a slider\'s job");
}


static void test_fog_group(void)
{
    ut_section("the fog group, under the picture");
    ut_check(overlay_model_row(PIC_ROW(OVERLAY_PICTURE_ROW_COUNT), &row) &&
                 row.kind == OVERLAY_ROW_GROUP && strcmp(row.label, "Fog") == 0,
             "its heading follows the last picture row, folded");
    overlay_model_toggle_group((uint32_t)OVERLAY_GROUP_OPENPHANTOM_FOG);
    overlay_model_rebuild();
    ut_check(overlay_model_row_count() ==
                 HEADINGS + OVERLAY_CHEATS_ROW_COUNT + 2u + 6u + 5u + 1u +
                 OVERLAY_UTILITIES_ROW_COUNT + 2u +
                 OVERLAY_PICTURE_ROW_COUNT + OVERLAY_FOG_ROW_COUNT,
             "open, its four rows sit between the picture and the enhanced input heading");

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
    ut_section("the enhanced input group, the control scheme under the fog");
    ut_check(overlay_model_row(FOG_ROW(OVERLAY_FOG_ROW_COUNT), &row) &&
                 row.kind == OVERLAY_ROW_GROUP && strcmp(row.label, "Enhanced input") == 0,
             "its heading follows the last fog row, folded");
    overlay_model_toggle_group((uint32_t)OVERLAY_GROUP_OPENPHANTOM_CONTROLS);
    overlay_model_rebuild();
    ut_check(overlay_model_row_count() ==
                 HEADINGS + OVERLAY_CHEATS_ROW_COUNT + 2u + 6u + 5u + 1u +
                 OVERLAY_UTILITIES_ROW_COUNT + 2u +
                 OVERLAY_PICTURE_ROW_COUNT + OVERLAY_FOG_ROW_COUNT + OVERLAY_CONTROLS_FIXED_ROWS,
             "open, its eight rows sit between the fog and the window group\'s heading");

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
                 strcmp(row.label, "Window mode") == 0,
             "and the window group\'s heading comes straight after, so the group took nothing "
             "from below it");

    ut_check(overlay_model_activate(CTRL_ROW(7)), "clicking the fold\'s summary is accepted");
    overlay_model_rebuild();
    ut_check(overlay_model_row(CTRL_ROW(8), &row) && row.kind == OVERLAY_ROW_INFO &&
                 strcmp(row.label, "    Controller mode: the best modern feel") == 0,
             "the first line sits immediately below the summary and names the first row");
    ut_check(!overlay_model_activate(CTRL_ROW(8)),
             "but a line itself does nothing when clicked; only the summary is interactive");
    ut_check(overlay_model_row(CTRL_ROW(8u + OVERLAY_CONTROLS_LINE_COUNT), &row) &&
                 row.kind == OVERLAY_ROW_GROUP && strcmp(row.label, "Window mode") == 0,
             "and the window group\'s heading is pushed down the screen by the lines");
    ut_check(overlay_model_activate(CTRL_ROW(7)), "the same summary row closes it back up");
    overlay_model_rebuild();
    ut_check(overlay_model_row(CTRL_ROW(8), &row) && row.kind == OVERLAY_ROW_GROUP,
             "and the lines are gone again");

    overlay_model_toggle_group((uint32_t)OVERLAY_GROUP_OPENPHANTOM_CONTROLS);
    overlay_model_rebuild();
}

static void test_draw_distance_switches_exclude(void)
{
    ut_section("the two draw-distance switches cannot both be on");
    /* They contradict each other: strict mode declines the governor outright, so a frame-rate
       switch still reading ON would be describing something that is not happening. This writes the
       settings file, which is the only channel these rows have, and puts it back afterwards. */
    if (strict_range_row_set(true)) {
        overlay_model_rebuild();
        ut_check(overlay_model_row(PIC_ROW(3), &row) && !row.available,
                 "with strict on, the frame-rate switch is greyed rather than left reading ON "
                 "over a governor that is no longer acting");
        ut_check(!row.on, "and it reports off, which is the state the game is actually in");
        ut_check(!overlay_model_activate(PIC_ROW(3)),
                 "and it cannot be clicked, so the two can never both be on");

        ut_check(strict_range_row_set(false), "strict goes back off");
        overlay_model_rebuild();
        ut_check(overlay_model_row(PIC_ROW(3), &row) && row.available,
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
    ut_check(overlay_model_row(PIC_ROW(0), &row), "the picture group\'s draw distance row exists");
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

int main(void)
{
    open_the_groups_above();
    test_utilities_group();
    test_menu_extras_group();
    test_picture_group();
    test_fog_group();
    test_controls_group();
    test_draw_distance_switches_exclude();
    test_typed_rows_kept_apart();
    return ut_summary("the overlay's settings groups");
}
