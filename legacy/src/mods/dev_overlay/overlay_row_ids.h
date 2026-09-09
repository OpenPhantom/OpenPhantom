/* overlay_row_ids.h: which id each row in the panel has, and how many rows there can be.
 *
 * Lifted out of overlay_model.c, which was at the size limit and whose own SIZE NOTE already said
 * that this was the part making it long. Nothing here decides anything: it is the numbering, the
 * reasoning behind the numbering, and the asserts that fail the build when a row added to one group
 * walks into another group's space. The code that reads these stayed where it was.
 *
 * The one thing that did not come across is FREECAM_INFO_LINES, the fold's text, because that is a
 * definition rather than a declaration and belongs in exactly one translation unit.
 */
#ifndef DEV_OVERLAY_OVERLAY_ROW_IDS_H
#define DEV_OVERLAY_OVERLAY_ROW_IDS_H

#include "overlay_model.h"
#include "overlay_utilities.h"
#include "overlay_window.h"

#include "cheats_openphantom.h"

#include <stdint.h>

/* The teleport-key row takes over free camera's own numeric slot in this group's id space, and
 * free camera itself moves one slot later (FREECAM_ROW_ID), so walking ids in order puts the
 * hotkey row directly BEFORE the cheat it gates rather than after it, the same order a player
 * reads the panel in. Read top to bottom, that tells the whole story on its own: set a teleport key,
 * then the toggle right below it stops reading unavailable. Neither is a cheats_own_id_t, and
 * deliberately outside that enum's range instead of extending it: both are rows this panel adds,
 * not cheats cheats_openphantom.c itself offers a name or an on/off for.
 *
 * The remapping only works because CHEATS_OWN_FREECAM is the LAST id before CHEATS_OWN_COUNT; the
 * assert below fails the build the day that stops being true, rather than silently reordering
 * something else instead. */
_Static_assert((uint32_t)CHEATS_OWN_FREECAM == (uint32_t)CHEATS_OWN_COUNT - 1u,
               "free camera must stay the last cheat in cheats_own_id_t for HOTKEY_ROW_ID/"
               "FREECAM_ROW_ID below to still put the hotkey row right before it");

/* The jump-boost scale row takes the same approach one slot earlier: inserted directly after jump
 * boost's own toggle row rather than appended at the end, so a player reads "Jump boost: ON" and
 * the number it is currently multiplying by in the very next row, not somewhere else in the list.
 * Everything from here down (the hotkey row, free camera itself, the info fold) shifts one slot
 * later than before to make room, which is transparent to all three; none of them are numbered by
 * anything other than these macros. Same guard as above, one enum slot earlier: this only lines up
 * because jump boost sits directly before free camera with nothing else between them. */
_Static_assert((uint32_t)CHEATS_OWN_JUMP_BOOST + 1u == (uint32_t)CHEATS_OWN_FREECAM,
               "jump boost must sit directly before free camera in cheats_own_id_t for "
               "JUMP_SCALE_ROW_ID below to still land right after its own toggle row");
#define JUMP_SCALE_ROW_ID ((uint32_t)CHEATS_OWN_JUMP_BOOST + 1u)
#define HOTKEY_ROW_ID  (JUMP_SCALE_ROW_ID + 1u)
#define FREECAM_ROW_ID (HOTKEY_ROW_ID + 1u)

/* One past free camera's own row: a fold, the same shape as a group's own expand/collapse but
 * scoped to one row rather than a whole section. Clicking it toggles model.freecam_info_expanded,
 * and while that is true, FREECAM_INFO_LINE_COUNT more INFO rows are drawn directly beneath it,
 * one per line of FREECAM_INFO_LINES. They carry ids from FREECAM_LINE_FIRST_ID rather than ids
 * following this one; see openphantom_row_id() for why the two differ. Reusing
 * OVERLAY_ROW_INFO's existing rendering entirely, full width and no chip, rather than adding a
 * second kind: the fold marker and the indent are both just characters in the label text (see
 * source_row() below), so nothing in overlay_draw.c has to change to draw this. The reason for
 * folding it at all rather than showing the lines outright: they do not fit un-wrapped on one
 * line, and several more rows permanently in the cheats group is disproportionate to what the
 * group otherwise costs on screen; collapsed, this reads as one more row exactly the size of any
 * other cheat. The first line restates the hotkey-row ordering above in plain words, for a player
 * who opens this before noticing the row order says the same thing on its own; the last does the
 * same for the way back out, since once free camera is on, this fold is the only place left that
 * still says which key does that. */
#define INFO_ROW_ID (FREECAM_ROW_ID + 1u)
/* Nine lines, not six, because two of them describe the two ways out and each is a sentence wider
 * than the panel. A line that does not fit is drawn clipped, running off the edge of the box with
 * no ellipsis and no wrap, so the reader loses the end of exactly the sentence that tells them how
 * to get out. Each is written as a line plus a continuation indented two spaces instead. */
#define FREECAM_INFO_LINE_COUNT 9u

/* The rows the OpenPhantom group holds that are not one of its own cheats: the jump-boost scale,
 * the free-camera teleport key, the "how to fly" fold and "Skip to next level". Named rather than
 * written as a 4 in source_count() because the ceiling below counts it too. */
#define OPENPHANTOM_EXTRA_ROWS 4u

/* The three groups on the OpenPhantom tab, every one of them open, the fold open and a full size
 * list. This is the number OVERLAY_ROWS_MAX has to cover, and the Original tab is far smaller. */
enum {
    OPENPHANTOM_TAB_ROWS_MAX = 3u + (uint32_t)CHEATS_OWN_COUNT + OPENPHANTOM_EXTRA_ROWS +
                               FREECAM_INFO_LINE_COUNT + OVERLAY_UTILITIES_ROW_COUNT +
                               OVERLAY_WINDOW_ROWS_MAX
};

_Static_assert(OVERLAY_ROWS_MAX >= OPENPHANTOM_TAB_ROWS_MAX,
               "the row array is smaller than the rows the OpenPhantom tab can build");

/* "Skip to next level", one slot after the free-camera info fold's own SUMMARY row (INFO_ROW_ID)
 * but before its child lines, which is what keeps this row's own id fixed regardless of whether
 * that fold happens to be open: the child lines are only sometimes present in the count, so
 * anything placed after them would move every time the fold opens or closes. Nothing about this
 * row depends on free camera at all; it only needs a slot that will not move. */
#define END_LEVEL_ROW_ID (INFO_ROW_ID + 1u)

/* The fold's own lines, past every fixed row above. Their ids are the tail of this id space
 * because the tail is the only part of it that may change size, but the tail is NOT where they
 * belong on screen: a reader looks for them directly beneath the summary that revealed them, not
 * at the bottom of the group. openphantom_row_id() below is where those two orders are put back
 * together, and it is the only code that needs to know they ever differed. */
#define FREECAM_LINE_FIRST_ID (END_LEVEL_ROW_ID + 1u)

/* The utilities group's own ids, numbered from a base clear of every id the cheats group above can
 * produce. Two pieces of panel state remember a row by its id alone, the typed value and the
 * hotkey capture, and both groups hold a row of each kind, so overlapping numbers would let a
 * commit land on a row in the other group. The assert is what keeps the two spaces apart as rows
 * are added to either. */
#define UTILITIES_FIRST_ID 64u

/* Above the Utilities block rather than immediately after it, so that adding a row there needs
 * no arithmetic here. The assert below is what keeps the two from meeting. */
#define WINDOW_FIRST_ID 128u
_Static_assert(UTILITIES_FIRST_ID + OVERLAY_UTILITIES_ROW_COUNT <= WINDOW_FIRST_ID,
               "the Utilities rows have grown into the Window group's ids: raise "
               "WINDOW_FIRST_ID");
_Static_assert(FREECAM_LINE_FIRST_ID + FREECAM_INFO_LINE_COUNT <= UTILITIES_FIRST_ID,
               "the cheats group has grown into the utilities group's id space; raise "
               "UTILITIES_FIRST_ID");

#endif /* DEV_OVERLAY_OVERLAY_ROW_IDS_H */
