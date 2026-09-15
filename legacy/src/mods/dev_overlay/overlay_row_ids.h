/* overlay_row_ids.h: which id each row in the panel has, and how many rows there can be.
 *
 * Lifted out of overlay_model.c, which was at the size limit and whose own SIZE NOTE already said
 * that this was the part making it long. Nothing here decides anything: it is the numbering, the
 * reasoning behind the numbering, and the asserts that fail the build when a row added to one group
 * walks into another group's space. The code that reads these stayed where it was.
 *
 * Two pieces of panel state remember a row by its id alone, the typed value and the hotkey
 * capture, and several groups hold a row of each kind, so no two groups may share a number. Every
 * settings group is numbered from a base of its own, clear of every id the group before it can
 * produce, and the assert at each base is what keeps them apart as rows are added. The drawn order
 * is the group enum's, not the order of the bases: a group takes the next free block when it
 * arrives, and renumbering the ones before it would buy nothing.
 */
#ifndef DEV_OVERLAY_OVERLAY_ROW_IDS_H
#define DEV_OVERLAY_OVERLAY_ROW_IDS_H

#include "overlay_controls.h"
#include "overlay_dismember.h"
#include "overlay_fog.h"
#include "overlay_framerate.h"
#include "overlay_levels.h"
#include "overlay_menu_extras.h"
#include "overlay_freecam.h"
#include "overlay_model.h"
#include "overlay_picture.h"
#include "overlay_utilities.h"
#include "overlay_window.h"

#include "cheats_openphantom.h"

#include <stdint.h>

/* The cheats group's ids. A toggle's id is its cheats_own_id_t; the three rows that are not
 * toggles carry ids past that enum. Free camera has a group of its own, and it is the LAST id of
 * the enum, so its number is free and the jump-boost scale row takes it; super run's speed row
 * and its slider track take the two numbers after the enum. The group's DRAWN order is a slot
 * table in overlay_cheats.c, which puts each typed row directly under the toggle it belongs to:
 * a player reads "Super run: ON" and the speed on the next line, "Jump boost: ON" and its scale
 * on the next. The asserts fail the build the day the enum's tail changes shape instead of
 * silently renumbering something else. */
_Static_assert((uint32_t)CHEATS_OWN_FREECAM == (uint32_t)CHEATS_OWN_COUNT - 1u,
               "free camera must stay the last cheat in cheats_own_id_t: the cheats group draws "
               "every id below it and the free camera group draws it instead");
_Static_assert((uint32_t)CHEATS_OWN_JUMP_BOOST + 1u == (uint32_t)CHEATS_OWN_FREECAM,
               "jump boost must sit directly before free camera in cheats_own_id_t for "
               "JUMP_SCALE_ROW_ID to take free camera's number");
#define JUMP_SCALE_ROW_ID       ((uint32_t)CHEATS_OWN_JUMP_BOOST + 1u)
#define SUPER_RUN_SPEED_ROW_ID  ((uint32_t)CHEATS_OWN_COUNT)
#define SUPER_RUN_TRACK_ROW_ID  ((uint32_t)CHEATS_OWN_COUNT + 1u)

/* The rows the cheats group draws: every cheat but free camera, super run's speed and its
 * track, and the jump boost scale. The level skip was the tail of this group until the Level
 * selection group took it. The two slots named are the ones the tests are written against. */
#define OVERLAY_CHEATS_ROW_COUNT            12u
#define OVERLAY_CHEATS_SUPER_RUN_SPEED_SLOT 8u
#define OVERLAY_CHEATS_JUMP_SCALE_SLOT      11u
_Static_assert(SUPER_RUN_TRACK_ROW_ID + 1u == OVERLAY_CHEATS_ROW_COUNT,
               "the cheats group's highest id and its row count disagree");

/* The utilities group's own ids, numbered from a base clear of every id the cheats group above
 * can produce. */
#define UTILITIES_FIRST_ID 64u
_Static_assert(OVERLAY_CHEATS_ROW_COUNT <= UTILITIES_FIRST_ID,
               "the cheats group has grown into the utilities group's id space; raise "
               "UTILITIES_FIRST_ID");

/* Above the Utilities block rather than immediately after it, so that adding a row there needs
 * no arithmetic here. The assert below is what keeps the two from meeting. */
#define WINDOW_FIRST_ID 128u
_Static_assert(UTILITIES_FIRST_ID + OVERLAY_UTILITIES_ROW_COUNT <= WINDOW_FIRST_ID,
               "the Utilities rows have grown into the Window group's ids: raise "
               "WINDOW_FIRST_ID");

/* The frame rate group, above the Window block for the same reason. */
#define FRAMERATE_FIRST_ID 192u
_Static_assert(WINDOW_FIRST_ID + OVERLAY_WINDOW_ROWS_MAX <= FRAMERATE_FIRST_ID,
               "the Window rows have grown into the Frame rate group's ids: raise "
               "FRAMERATE_FIRST_ID");

/* The control scheme's group, Enhanced input. */
#define CONTROLS_FIRST_ID 224u
_Static_assert(FRAMERATE_FIRST_ID + OVERLAY_FRAMERATE_ROW_COUNT <= CONTROLS_FIRST_ID,
               "the Frame rate rows have grown into the Enhanced input group's ids: raise "
               "CONTROLS_FIRST_ID");

/* The picture's group, Enhanced resolution. */
#define PICTURE_FIRST_ID 256u
_Static_assert(CONTROLS_FIRST_ID + OVERLAY_CONTROLS_ROWS_MAX <= PICTURE_FIRST_ID,
               "the Enhanced input rows have grown into the Enhanced resolution group's ids: "
               "raise PICTURE_FIRST_ID");

/* The fog's group. */
#define FOG_FIRST_ID 288u
_Static_assert(PICTURE_FIRST_ID + OVERLAY_PICTURE_ROW_COUNT <= FOG_FIRST_ID,
               "the Enhanced resolution rows have grown into the Fog group's ids: raise "
               "FOG_FIRST_ID");

/* The free camera's group: the teleport key, the cheat itself, the two switches and the "how to
 * fly" fold with its lines. Its ids are its slots, because the fold's lines are the last rows of
 * the group and so the tail of its id space is also the tail of the screen; nothing there needs
 * remapping. */
#define FREECAM_FIRST_ID 320u
_Static_assert(FOG_FIRST_ID + OVERLAY_FOG_ROW_COUNT <= FREECAM_FIRST_ID,
               "the Fog rows have grown into the Free camera group's ids: raise "
               "FREECAM_FIRST_ID");

/* The dismemberment group, one switch. */
#define DISMEMBER_FIRST_ID 352u
_Static_assert(FREECAM_FIRST_ID + OVERLAY_FREECAM_ROWS_MAX <= DISMEMBER_FIRST_ID,
               "the Free camera rows have grown into the Dismemberment group's ids: raise "
               "DISMEMBER_FIRST_ID");

/* The game's own screens' group, one switch. */
#define MENU_EXTRAS_FIRST_ID 384u
_Static_assert(DISMEMBER_FIRST_ID + OVERLAY_DISMEMBER_ROW_COUNT <= MENU_EXTRAS_FIRST_ID,
               "the Dismemberment rows have grown into the In game options extras group's ids: "
               "raise MENU_EXTRAS_FIRST_ID");

/* The level selection group: the skip, the start level and its list. Its ids are its slots,
 * the list's entries being the last rows of the group. */
#define LEVELS_FIRST_ID 416u
_Static_assert(MENU_EXTRAS_FIRST_ID + OVERLAY_MENU_EXTRAS_ROWS_MAX <= LEVELS_FIRST_ID,
               "the In game options extras rows have grown into the Level selection group's "
               "ids: raise LEVELS_FIRST_ID");

/* The eleven groups on the OpenPhantom tab, every one of them open, the folds open and a full
 * size list: eleven headings and every row each group can draw. This is the number
 * OVERLAY_ROWS_MAX has to cover, and the Original tab is far smaller. It once counted three
 * groups and left the frame rate group out, seven rows short of what overlay_model_rebuild()
 * builds; the array still held them, so nothing was lost, but the assert was guarding a smaller
 * number than the real one. */
enum {
    OPENPHANTOM_TAB_ROWS_MAX = 11u + OVERLAY_CHEATS_ROW_COUNT + OVERLAY_LEVELS_ROWS_MAX +
                               OVERLAY_FREECAM_ROWS_MAX +
                               OVERLAY_DISMEMBER_ROW_COUNT + OVERLAY_MENU_EXTRAS_ROWS_MAX +
                               OVERLAY_UTILITIES_ROW_COUNT + OVERLAY_PICTURE_ROW_COUNT +
                               OVERLAY_FOG_ROW_COUNT + OVERLAY_CONTROLS_ROWS_MAX +
                               OVERLAY_WINDOW_ROWS_MAX + OVERLAY_FRAMERATE_ROW_COUNT
};

_Static_assert(OVERLAY_ROWS_MAX >= OPENPHANTOM_TAB_ROWS_MAX,
               "the row array is smaller than the rows the OpenPhantom tab can build");

#endif /* DEV_OVERLAY_OVERLAY_ROW_IDS_H */
