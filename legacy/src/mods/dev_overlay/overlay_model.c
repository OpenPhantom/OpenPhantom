/* overlay_model.c: the panel's state and the list of rows that follows from it.
 *
 * There is one group per tab today and the structure carries more, on purpose: the diagnostics and
 * the developer tools that will hang off this panel are groups beside the cheats, not a second
 * panel, and a shape that already folds and searches them costs nothing now.
 *
 * SIZE NOTE. This file is over the six hundred line mark. It was already over before the draw
 * distance row was added to it, and adding that row is what turned an inherited overage into one
 * worth writing down rather than passing on again.
 *
 * What is long is the row numbering and the reasoning attached to it. The OpenPhantom group's ids
 * are not a plain list: the jump-boost scale is inserted after its own toggle, the teleport key and
 * free camera swap places, a fold adds six more ids only while it is open, and each of those has a
 * static assert and a paragraph explaining what it is pinned to and what breaks if the enum behind
 * it is reordered. Deleting that reasoning to get under a limit would leave arithmetic nobody can
 * check, which the guidance here explicitly refuses.
 *
 * A SEAM was taken, and it was not the one this note used to name. Splitting the OpenPhantom tab
 * into Cheats and Utilities moved the seven settings rows into overlay_utilities.c, which is a
 * better cut than the editing state machine this note previously proposed: those rows share none
 * of this file's navigation, search, folding or typing state, so what moved is a whole
 * responsibility and what stayed is the part that has to know which row is being typed into.
 *
 * THE SEAM, if it grows again, is that editing state machine after all: the typed value and hotkey
 * capture reached through overlay_model_value_*() and overlay_model_capture_hotkey(). Both groups
 * now go through it, so it has two callers rather than one, which is the shape of something that
 * wants to be its own file.
 */
#include "overlay_model.h"

#include "overlay_key_name.h"
#include "overlay_utilities.h"

#include "cheats_openphantom.h"
#include "cheats_original.h"
#include "cheats_original_actions.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* One entry per group. Which tab a group belongs to is fixed by GROUP_TAB below, not stored here:
 * a group cannot change tabs at runtime, so there is nothing to keep in sync by getting it wrong. */
typedef struct group_state {
    const char *title;
    bool        expanded;
} group_state_t;

static const overlay_tab_t GROUP_TAB[OVERLAY_GROUP_COUNT] = {
    OVERLAY_TAB_ORIGINAL,      /* OVERLAY_GROUP_ORIGINAL_TOGGLES */
    OVERLAY_TAB_ORIGINAL,      /* OVERLAY_GROUP_ORIGINAL_ACTIONS */
    OVERLAY_TAB_OPENPHANTOM,   /* OVERLAY_GROUP_OPENPHANTOM      */
    OVERLAY_TAB_OPENPHANTOM    /* OVERLAY_GROUP_OPENPHANTOM_UTILITIES */
};

typedef struct overlay_model_state {
    overlay_tab_t tab;
    char          search[OVERLAY_SEARCH_MAX];
    group_state_t groups[OVERLAY_GROUP_COUNT];
    overlay_row_t rows[OVERLAY_ROWS_MAX];
    uint32_t      row_count;
    bool          capturing_hotkey;   /* a hotkey row is waiting for a keypress */
    uint32_t      capturing_hotkey_row;   /* which one; two rows here bind a key */
    bool          freecam_info_expanded;   /* the "how to fly" row is showing its lines */
    bool          freecam_was_on;     /* last-seen CHEATS_OWN_FREECAM state, to catch the edge */
    bool          editing_value;           /* a value row is waiting for typed digits */
    uint32_t      editing_value_row;       /* which one, an id in the OpenPhantom group */
    char          value_edit_buf[7];  /* what has been typed so far; six usable characters plus
                                             * the terminator, sized so "<buf>_" (the display's own
                                             * cursor, see source_row() below) still fits inside the
                                             * row's own value[8] with room for ITS terminator too */
} overlay_model_state_t;

static overlay_model_state_t model;

/* The teleport-key row takes over free camera's own numeric slot in this group's id space, and
 * free camera itself moves one slot later (FREECAM_ROW_ID) - so walking ids in order puts the
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
 * later than before to make room, which is transparent to all three - none of them are numbered by
 * anything other than these macros. Same guard as above, one enum slot earlier: this only lines up
 * because jump boost sits directly before free camera with nothing else between them. */
_Static_assert((uint32_t)CHEATS_OWN_JUMP_BOOST + 1u == (uint32_t)CHEATS_OWN_FREECAM,
               "jump boost must sit directly before free camera in cheats_own_id_t for "
               "JUMP_SCALE_ROW_ID below to still land right after its own toggle row");
#define JUMP_SCALE_ROW_ID ((uint32_t)CHEATS_OWN_JUMP_BOOST + 1u)
#define HOTKEY_ROW_ID  (JUMP_SCALE_ROW_ID + 1u)
#define FREECAM_ROW_ID (HOTKEY_ROW_ID + 1u)

/* One past free camera's own row: a fold, the same shape as a group's own expand/collapse but
 * scoped to one row rather than a whole section - clicking it toggles model.freecam_info_expanded,
 * and while that is true, FREECAM_INFO_LINE_COUNT more INFO rows are drawn directly beneath it,
 * one per line of FREECAM_INFO_LINES. They carry ids from FREECAM_LINE_FIRST_ID rather than ids
 * following this one; see openphantom_row_id() for why the two differ. Reusing
 * OVERLAY_ROW_INFO's existing rendering entirely - full width, no chip - rather than adding a
 * second kind: the fold marker and the indent are both just characters in the label text (see
 * source_row() below), so nothing in overlay_draw.c has to change to draw this. The reason for
 * folding it at all rather than showing the lines outright: they do not fit un-wrapped on one
 * line, and several more rows permanently in a five-cheat group is disproportionate to what the
 * group otherwise costs on screen - collapsed, this reads as one more row exactly the size of any
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
static const char *const FREECAM_INFO_LINES[FREECAM_INFO_LINE_COUNT] = {
    "Needs a teleport key set first",
    "WASD to move",
    "Mouse to look",
    "E / Q for up and down",
    "Scroll wheel changes speed",
    "Your teleport key ends the flight",
    "  and brings the player here",
    "F4 ends the flight and leaves",
    "  the player where they were"
};

/* "Skip to next level" - one slot after the free-camera info fold's own SUMMARY row (INFO_ROW_ID)
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
_Static_assert(FREECAM_LINE_FIRST_ID + FREECAM_INFO_LINE_COUNT <= UTILITIES_FIRST_ID,
               "the cheats group has grown into the utilities group's id space; raise "
               "UTILITIES_FIRST_ID");

/* Short enough for value[8]. Letters and digits already match their own virtual-key codes; function
 * keys and the handful of others worth naming get their own case; anything else prints as hex
 * rather than silently showing nothing, since a key that was bound has to be identifiable if
 * something else on the system also happens to be using it. */
/* ============================================================================================ */

static char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

bool overlay_model_matches(const char *label, const char *needle)
{
    size_t i;
    size_t j;

    if (needle == NULL || needle[0] == '\0') {
        return true;
    }
    if (label == NULL) {
        return false;
    }

    /* The inner loop cannot walk past the end of `label`: `needle[j]` is never the terminator
     * inside it, so the first character of `label` that is one compares unequal and breaks. */
    for (i = 0; label[i] != '\0'; ++i) {
        for (j = 0; needle[j] != '\0'; ++j) {
            if (lower(label[i + j]) != lower(needle[j])) {
                break;
            }
        }
        if (needle[j] == '\0') {
            return true;
        }
    }
    return false;
}

static void copy_label(char *out, const char *text)
{
    size_t i;

    if (text == NULL) {
        out[0] = '\0';
        return;
    }
    for (i = 0; i + 1u < OVERLAY_LABEL_MAX && text[i] != '\0'; ++i) {
        out[i] = text[i];
    }
    out[i] = '\0';
}

/* ============================================================================================ */

void overlay_model_reset(void)
{
    uint32_t i;

    model.tab = OVERLAY_TAB_ORIGINAL;
    model.search[0] = '\0';
    model.row_count = 0;
    model.capturing_hotkey = false;   /* leaving the panel open mid-capture must not strand it */
    model.freecam_info_expanded = false;   /* folds closed on every open, same as the groups do */
    model.freecam_was_on = false;   /* re-synced against the real state on the very next rebuild */
    model.editing_value = false;   /* same reasoning as capturing_hotkey just above */
    model.value_edit_buf[0] = '\0';

    model.groups[OVERLAY_GROUP_ORIGINAL_TOGGLES].title = "Original cheats";
    model.groups[OVERLAY_GROUP_ORIGINAL_ACTIONS].title = "Original cheats (one-time effects)";
    /* Split by what a row does rather than by what reads it, following the retail half of this
     * panel, which already separates its own toggles from its one-time effects. The tab began as
     * five cheats with a settings row appended and settings kept arriving, until a reader had to
     * scroll past invincibility to reach the draw distance. */
    model.groups[OVERLAY_GROUP_OPENPHANTOM].title = "Cheats";
    model.groups[OVERLAY_GROUP_OPENPHANTOM_UTILITIES].title = "Utilities";
    for (i = 0; i < (uint32_t)OVERLAY_GROUP_COUNT; ++i) {
        model.groups[i].expanded = false;      /* everything starts folded, as asked */
    }
}

overlay_tab_t overlay_model_tab(void)
{
    return model.tab;
}

/* The first drawn row, as a REQUEST rather than a fact: the reader below is what makes it true
 * against a row count that changes under it. */
static int32_t scroll_want = 0;

void overlay_model_scroll_by(int32_t rows)
{
    scroll_want += rows;
    if (scroll_want < 0) {
        scroll_want = 0;
    }
}

static void scroll_home(void)
{
    scroll_want = 0;
}

uint32_t overlay_model_scroll(uint32_t visible)
{
    const uint32_t count = overlay_model_row_count();
    const uint32_t most  = (count > visible) ? (count - visible) : 0u;

    if (scroll_want < 0) {
        scroll_want = 0;
    }
    if ((uint32_t)scroll_want > most) {
        scroll_want = (int32_t)most;      /* the list shrank, or it was never that long */
    }
    return (uint32_t)scroll_want;
}

void overlay_model_set_tab(overlay_tab_t tab)
{
    if ((unsigned)tab < (unsigned)OVERLAY_TAB_COUNT) {
        model.tab = tab;
        scroll_home();
    }
}

const char *overlay_model_search(void)
{
    return model.search;
}

void overlay_model_set_search(const char *text)
{
    size_t i;

    scroll_home();

    if (text == NULL) {
        model.search[0] = '\0';
        return;
    }
    for (i = 0; i + 1u < OVERLAY_SEARCH_MAX && text[i] != '\0'; ++i) {
        model.search[i] = text[i];
    }
    model.search[i] = '\0';
}

void overlay_model_search_append(char letter)
{
    size_t length = strlen(model.search);

    scroll_home();
    if (length + 1u >= OVERLAY_SEARCH_MAX) {
        return;
    }
    if (letter < 0x20 || letter > 0x7E) {
        return;                          /* only what the box can actually show */
    }
    model.search[length] = letter;
    model.search[length + 1u] = '\0';
}

void overlay_model_search_backspace(void)
{
    size_t length = strlen(model.search);

    scroll_home();

    if (length > 0u) {
        model.search[length - 1u] = '\0';
    }
}

void overlay_model_toggle_group(uint32_t group)
{
    if (group < (uint32_t)OVERLAY_GROUP_COUNT) {
        model.groups[group].expanded = !model.groups[group].expanded;
    }
}

/* ============================================================================================ */

static void append_row(const overlay_row_t *row)
{
    if (model.row_count < OVERLAY_ROWS_MAX) {
        model.rows[model.row_count++] = *row;
    }
}

/* How many rows a group's source holds, and what one of them looks like. The three sources differ
 * in everything except this shape, so the rest of the file does not care which group is open. */
static uint32_t source_count(overlay_group_t group)
{
    switch (group) {
    case OVERLAY_GROUP_ORIGINAL_TOGGLES:
        return cheats_original_count();
    case OVERLAY_GROUP_ORIGINAL_ACTIONS:
        return (uint32_t)CHEATS_ACTION_COUNT;
    case OVERLAY_GROUP_OPENPHANTOM_UTILITIES:
        return OVERLAY_UTILITIES_ROW_COUNT;
    case OVERLAY_GROUP_OPENPHANTOM:
    default:
        /* +4, one for each row this group holds that is not one of its own cheats: the jump-boost
         * scale row, inserted right after jump boost's own toggle row; the free-camera teleport key
         * row, which takes over free camera's own (now shifted) numeric slot; the "how to fly"
         * fold, one past where free camera itself now sits; and "Skip to next level", one past
         * that. See JUMP_SCALE_ROW_ID/HOTKEY_ROW_ID/FREECAM_ROW_ID/INFO_ROW_ID/END_LEVEL_ROW_ID
         * and their own handling in source_row() below. The fold's own lines add
         * FREECAM_INFO_LINE_COUNT more only while it is open, the same shape a group's own child
         * count already uses in append_group() below.
         *
         * This number and the row ids above move together. Adding a row and forgetting this
         * leaves the new row built but never drawn, which is what happened when one was added. */
        return (uint32_t)CHEATS_OWN_COUNT + 4u +
               (model.freecam_info_expanded ? FREECAM_INFO_LINE_COUNT : 0u);
    }
}

/* Slot is the position a row occupies on screen within this group; the answer is the id that
 * belongs there. The two are the same number until the free camera fold opens, because that is
 * the only thing in this group that changes how many rows there are. When it is open, its lines
 * are drawn where they read, directly under the summary, while keeping the ids at the end of the
 * space where they can grow and shrink without moving anything else. The fixed rows below the
 * summary keep their ids and simply sit further down the screen.
 *
 * Everything downstream compares against the fixed constants above and never against a slot, so
 * activation, hotkey capture and value editing are all unaffected by the fold being open. */
static uint32_t openphantom_row_id(uint32_t slot)
{
    if (!model.freecam_info_expanded || slot <= INFO_ROW_ID) {
        return slot;                 /* shut, or at or above the summary: the orders agree */
    }
    if (slot <= INFO_ROW_ID + FREECAM_INFO_LINE_COUNT) {
        return FREECAM_LINE_FIRST_ID + (slot - INFO_ROW_ID - 1u);      /* one of the lines */
    }
    return slot - FREECAM_INFO_LINE_COUNT;   /* a fixed row, pushed down the screen by them */
}

static void source_row(overlay_group_t group, uint32_t id, overlay_row_t *out)
{
    out->group = (uint32_t)group;
    out->id = id;
    out->expanded = false;
    out->pending = false;      /* only the actions group's own play-as rows ever set this */
    out->value[0] = '\0';      /* only graphics detail, among all the actions, ever sets this */

    switch (group) {
    case OVERLAY_GROUP_ORIGINAL_TOGGLES:
        out->kind = OVERLAY_ROW_CHEAT;
        copy_label(out->label, cheats_original_name(id));
        out->on = cheats_original_is_on(id);
        out->available = true;         /* a row only exists here once its table resolved */
        return;
    case OVERLAY_GROUP_ORIGINAL_ACTIONS:
        out->kind = OVERLAY_ROW_ACTION;
        copy_label(out->label, cheats_original_actions_name((cheats_action_id_t)id));
        out->on = false;               /* meaningless for an action; never read by the drawer */
        out->available = cheats_original_actions_is_available((cheats_action_id_t)id);
        out->pending = cheats_original_actions_is_pending((cheats_action_id_t)id);
        /* Read live on every rebuild, not cached from the press that set it: the retail console can
         * also cycle this, and a chip showing a level nobody is at any more would be lying about
         * the one thing this row exists to show. */
        if ((cheats_action_id_t)id == CHEATS_ACTION_GRAPHICS_DETAIL) {
            int32_t level = cheats_original_actions_graphics_level();

            if (level > 0) {
                _snprintf(out->value, sizeof out->value, "%d", (int)level);
                out->value[sizeof out->value - 1] = '\0';
            }
        }
        return;
    case OVERLAY_GROUP_OPENPHANTOM_UTILITIES: {
        /* A slot here IS its position in the list, with none of the arithmetic the cheats group
         * below needs: nothing in this group is positioned relative to a cheat enum and nothing in
         * it folds. The id carries the base so it cannot be confused with a cheats-group id; see
         * UTILITIES_FIRST_ID. */
        const char *editing = NULL;
        bool        capturing = false;

        out->id = UTILITIES_FIRST_ID + id;
        if (model.editing_value && model.editing_value_row == out->id) {
            editing = model.value_edit_buf;
        }
        if (model.capturing_hotkey && model.capturing_hotkey_row == out->id) {
            capturing = true;
        }
        overlay_utilities_row(id, editing, capturing, out);
        return;
    }
    case OVERLAY_GROUP_OPENPHANTOM:
    default:
        /* Arriving as a screen position and continuing as an id. Reassigned rather than kept in
           a second local so that no comparison below can accidentally reach the slot instead. */
        id = openphantom_row_id(id);
        out->id = id;

        if (id == JUMP_SCALE_ROW_ID) {
            out->kind = OVERLAY_ROW_VALUE;
            copy_label(out->label, "Jump boost scale");
            out->on = false;        /* meaningless for a value row; never read by the drawer */
            out->available = cheats_openphantom_is_available(CHEATS_OWN_JUMP_BOOST);
            /* Read live either way, same reasoning as the hotkey row just below: mid-edit shows
             * exactly what has been typed with a trailing cursor, otherwise the value this cheat
             * would multiply by right now if switched on, formatted the same "1.30x" way its own
             * chip is meant to be typed back in. */
            if (model.editing_value && model.editing_value_row == JUMP_SCALE_ROW_ID) {
                _snprintf(out->value, sizeof out->value, "%s_", model.value_edit_buf);
            } else {
                _snprintf(out->value, sizeof out->value, "%.2fx",
                         (double)cheats_openphantom_jump_boost_scale());
            }
            out->value[sizeof out->value - 1] = '\0';
            return;
        }
        if (id == HOTKEY_ROW_ID) {
            out->kind = OVERLAY_ROW_HOTKEY;
            copy_label(out->label, "Free camera teleport key");
            out->on = false;        /* meaningless for a hotkey row; never read by the drawer */
            out->available = cheats_openphantom_is_available(CHEATS_OWN_FREECAM);
            /* Always populated, never left for the drawer's own ACTION/CHEAT fallback word to
             * guess at - "RUN" and "OFF" are both wrong for a key binding. */
            if (model.capturing_hotkey) {
                _snprintf(out->value, sizeof out->value, "...");
            } else {
                int32_t vk = cheats_openphantom_freecam_hotkey();
                if (vk != 0) {
                    overlay_key_name(vk, out->value, sizeof out->value);
                } else {
                    _snprintf(out->value, sizeof out->value, "Set");
                }
            }
            out->value[sizeof out->value - 1] = '\0';
            return;
        }
        if (id == FREECAM_ROW_ID) {
            /* Free camera itself, one slot after its own teleport-key row now rather than at its
             * plain cheats_own_id_t position - see HOTKEY_ROW_ID/FREECAM_ROW_ID above. Named by
             * CHEATS_OWN_FREECAM explicitly rather than casting id the way the generic fallback
             * below does for the other three cheats: id here is FREECAM_ROW_ID, one past the real
             * enum's range, and casting that would ask cheats_openphantom.c about an id it never
             * offered a name for. */
            out->kind = OVERLAY_ROW_CHEAT;
            copy_label(out->label, cheats_openphantom_name(CHEATS_OWN_FREECAM));
            out->on = cheats_openphantom_is_on(CHEATS_OWN_FREECAM);
            out->available = cheats_openphantom_is_available(CHEATS_OWN_FREECAM);
            /* Free camera specifically also needs a teleport key bound before it can be switched
             * ON - cheats_openphantom_toggle() enforces this too, so this is display honesty
             * rather than the only gate: a row that looked clickable but silently refused every
             * click would be worse than one that shows why. Once it IS on, availability no longer
             * depends on this: the row is unreachable anyway with the mouse claimed, and the
             * hotkey is how it actually turns back off. */
            if (!out->on && cheats_openphantom_freecam_hotkey() == 0) {
                out->available = false;
            }
            return;
        }
        if (id == INFO_ROW_ID) {
            out->kind = OVERLAY_ROW_INFO;
            copy_label(out->label,
                      model.freecam_info_expanded ? "- How free camera flies"
                                                   : "+ How free camera flies");
            out->expanded = model.freecam_info_expanded;
            out->on = false;
            out->available = true;
            return;
        }
        if (id == END_LEVEL_ROW_ID) {
            out->kind = OVERLAY_ROW_ACTION;
            /* It HAS been played since this row was added, so the label no longer says it
               has not. It stays marked as a debug tool, which is still true: it is a jump to the
               next level with none of the bookkeeping the game itself does on the way out of
               one. */
            copy_label(out->label, "Skip to next level (debug)");
            out->on = false;         /* meaningless for an action; never read by the drawer */
            out->available = cheats_openphantom_end_level_is_available();
            return;
        }
        if (model.freecam_info_expanded && id >= FREECAM_LINE_FIRST_ID &&
            id < FREECAM_LINE_FIRST_ID + FREECAM_INFO_LINE_COUNT) {
            char line[OVERLAY_LABEL_MAX];

            out->kind = OVERLAY_ROW_INFO;
            _snprintf(line, sizeof line, "    %s",
                      FREECAM_INFO_LINES[id - FREECAM_LINE_FIRST_ID]);
            line[sizeof line - 1] = '\0';
            copy_label(out->label, line);
            out->on = false;
            out->available = true;   /* a nested line, not a gate; never clicked either way */
            return;
        }
        /* Everything left is one of the other cheats (ammunition, health, no fog, invincible NPCs,
         * one-shot NPCs, giant player, tiny player, jump boost's own toggle), whose ids still line
         * up 1:1 with cheats_own_id_t - only free camera's own slot was repurposed above, and jump
         * boost's toggle keeps its own plain id even though the row right after it does not. */
        out->kind = OVERLAY_ROW_CHEAT;
        copy_label(out->label, cheats_openphantom_name((cheats_own_id_t)id));
        out->on = cheats_openphantom_is_on((cheats_own_id_t)id);
        out->available = cheats_openphantom_is_available((cheats_own_id_t)id);
        return;
    }
}

/* One group's own heading plus, if it is expanded, its matching children. Pulled out of
 * overlay_model_rebuild() because the current tab can hold more than one of these now, and each
 * is otherwise identical: build its heading, count its own hits, decide its own fold. */
static void append_group(overlay_group_t group)
{
    const uint32_t count = source_count(group);
    const bool     searching = model.search[0] != '\0';
    overlay_row_t  row;
    uint32_t       matches = 0;
    uint32_t       i;

    for (i = 0; i < count; ++i) {
        source_row(group, i, &row);
        if (overlay_model_matches(row.label, model.search)) {
            ++matches;
        }
    }

    /* A search opens the group that has hits, without disturbing the fold the user chose: clearing
     * the box puts it back exactly as it was. */
    row.kind = OVERLAY_ROW_GROUP;
    copy_label(row.label, model.groups[group].title);
    row.expanded = model.groups[group].expanded || (searching && matches > 0u);
    row.on = false;
    row.available = true;
    row.pending = false;    /* the loop above may have left these set from the last child scanned */
    row.value[0] = '\0';
    row.group = (uint32_t)group;
    row.id = (uint32_t)group;
    append_row(&row);

    if (!row.expanded) {
        return;
    }

    for (i = 0; i < count; ++i) {
        overlay_row_t leaf;

        source_row(group, i, &leaf);
        if (!overlay_model_matches(leaf.label, model.search)) {
            continue;
        }
        append_row(&leaf);
    }
}

void overlay_model_rebuild(void)
{
    uint32_t g;
    bool     freecam_on = cheats_openphantom_is_on(CHEATS_OWN_FREECAM);

    if (freecam_on != model.freecam_was_on) {
        /* Free camera just changed state, either by the panel's own toggle or (more often) its
         * hotkey, which flips this straight inside cheats_openphantom.c without ever going through
         * overlay_model_activate(). Catching it here, on every rebuild, is what sees the hotkey
         * path too. The mouse is fully claimed for as long as free camera flies, so this is also
         * the only way the fold could open at all without a click reaching it - and forcing it
         * shut again the instant free camera turns off is what keeps an old reading list from
         * lingering once there is nothing left it is explaining. A manual click in between still
         * wins over this: it only fires again on the NEXT genuine on/off flip, not every frame. */
        model.freecam_info_expanded = freecam_on;
        model.freecam_was_on = freecam_on;
    }

    model.row_count = 0;
    for (g = 0; g < (uint32_t)OVERLAY_GROUP_COUNT; ++g) {
        if (GROUP_TAB[g] == model.tab) {
            append_group((overlay_group_t)g);
        }
    }
}

uint32_t overlay_model_row_count(void)
{
    return model.row_count;
}

bool overlay_model_row(uint32_t index, overlay_row_t *out)
{
    if (index >= model.row_count || out == NULL) {
        return false;
    }
    *out = model.rows[index];
    return true;
}

bool overlay_model_activate(uint32_t index)
{
    overlay_row_t row;

    if (index >= model.row_count) {
        return false;
    }
    row = model.rows[index];

    if (row.kind == OVERLAY_ROW_GROUP) {
        overlay_model_toggle_group(row.group);
        return true;
    }
    if (!row.available) {
        return false;
    }
    if (row.kind == OVERLAY_ROW_INFO) {
        /* Only the fold's own summary row (id == INFO_ROW_ID) is interactive; the lines it
         * reveals when open are notes, not controls, the same as an ordinary INFO row always
         * was - they just never had anything to do. */
        if (row.id == INFO_ROW_ID) {
            model.freecam_info_expanded = !model.freecam_info_expanded;
            return true;
        }
        return false;
    }
    if (row.kind == OVERLAY_ROW_HOTKEY) {
        /* Starts a capture; does not bind anything itself. overlay_input.c routes the next
         * key-down to overlay_model_capture_hotkey() while this is true, rather than that key
         * reaching its usual handling. The row is remembered because there is more than one that
         * binds a key, and the capture has to land on the one that was clicked. */
        model.capturing_hotkey = true;
        model.capturing_hotkey_row = row.id;
        return true;
    }
    if (row.kind == OVERLAY_ROW_VALUE) {
        /* Starts a fresh typed value, discarding anything left over from a previous edit that was
         * never committed - the same "click it again to redo it" shape the hotkey row above has.
         * Does not touch the stored value itself; only overlay_model_value_commit() does. */
        model.editing_value = true;
        model.editing_value_row = row.id;
        model.value_edit_buf[0] = '\0';
        return true;
    }
    if (row.kind == OVERLAY_ROW_ACTION && row.group == (uint32_t)OVERLAY_GROUP_OPENPHANTOM) {
        /* The only OpenPhantom row that is an action rather than a toggle - checked here, before
         * the switch below, for the same reason HOTKEY/VALUE are: cheats_openphantom_toggle()
         * would otherwise be asked for an id it was never given a name or an on/off for. */
        return cheats_openphantom_end_level_invoke();
    }
    switch ((overlay_group_t)row.group) {
    case OVERLAY_GROUP_OPENPHANTOM_UTILITIES:
        /* Every switch in that group, answered by the group itself. The two edit kinds above have
         * already been taken, so what reaches here is a plain toggle. */
        return overlay_utilities_toggle(row.id - UTILITIES_FIRST_ID);
    case OVERLAY_GROUP_ORIGINAL_TOGGLES:
        (void)cheats_original_toggle(row.id);
        return true;
    case OVERLAY_GROUP_ORIGINAL_ACTIONS:
        return cheats_original_actions_invoke((cheats_action_id_t)row.id);
    case OVERLAY_GROUP_OPENPHANTOM:
    default:
        /* row.id is FREECAM_ROW_ID for free camera's own row now, not CHEATS_OWN_FREECAM - one
         * past the real enum's range, because the hotkey row took over free camera's old slot
         * (see HOTKEY_ROW_ID/FREECAM_ROW_ID above). Casting that straight through would ask
         * cheats_openphantom_toggle() for an id its own bounds check refuses, so the click would
         * silently do nothing - map it back to the real id here instead. Every other id in this
         * group still lines up 1:1 with cheats_own_id_t, so only this one case needs remapping. */
        (void)cheats_openphantom_toggle(
            row.id == FREECAM_ROW_ID ? CHEATS_OWN_FREECAM : (cheats_own_id_t)row.id);
        return true;
    }
}

bool overlay_model_is_capturing_hotkey(void)
{
    return model.capturing_hotkey;
}

void overlay_model_capture_hotkey(int32_t virtual_key)
{
    uint32_t row;

    if (!model.capturing_hotkey) {
        return;
    }
    row = model.capturing_hotkey_row;
    model.capturing_hotkey = false;
    if (row >= UTILITIES_FIRST_ID) {
        /* A refusal leaves the binding alone and the row shows the key it still has, which is the
         * same shape the value rows use for text that is not a number. The refused keys are the
         * ones that would leave the panel unopenable or unusable; see open_key_row.c. */
        (void)overlay_utilities_bind(row - UTILITIES_FIRST_ID, virtual_key);
        return;
    }
    cheats_openphantom_freecam_set_hotkey(virtual_key);
}

bool overlay_model_is_editing_value(void)
{
    return model.editing_value;
}

void overlay_model_value_append(char digit)
{
    size_t length;

    if (!model.editing_value) {
        return;
    }
    /* Only what a positive decimal number can contain, and only one point - anything else is
     * refused outright rather than accepted and left to fail atof() later, the same "do not accept
     * what cannot mean anything" reasoning overlay_model_search_append() above applies to its own,
     * much wider, set of allowed characters. */
    if (digit != '.' && (digit < '0' || digit > '9')) {
        return;
    }
    if (digit == '.' && strchr(model.value_edit_buf, '.') != NULL) {
        return;
    }
    length = strlen(model.value_edit_buf);
    if (length + 1u >= sizeof model.value_edit_buf) {
        return;
    }
    model.value_edit_buf[length] = digit;
    model.value_edit_buf[length + 1u] = '\0';
}

void overlay_model_value_backspace(void)
{
    size_t length;

    if (!model.editing_value) {
        return;
    }
    length = strlen(model.value_edit_buf);
    if (length > 0u) {
        model.value_edit_buf[length - 1u] = '\0';
    }
}

void overlay_model_value_commit(void)
{
    uint32_t row;

    if (!model.editing_value) {
        return;
    }
    row = model.editing_value_row;
    model.editing_value = false;
    if (model.value_edit_buf[0] == 0) {
        return;      /* nothing was typed, leave whatever value was already set alone */
    }

    if (row >= UTILITIES_FIRST_ID) {
        /* Every typed row in that group, parsed and written by the group itself. A refusal leaves
         * the setting alone, which is what the row then shows. */
        (void)overlay_utilities_commit(row - UTILITIES_FIRST_ID, model.value_edit_buf);
        return;
    }

    /* The jump-boost scale, with the same reasoning about a refused parse: atof() answers 0 for
     * text that fails entirely, a lone full stop for instance, which this treats the same as a
     * typed 0 or a negative rather than letting the setter's own clamp turn either into
     * JUMP_BOOST_SCALE_MIN. */
    {
        float parsed = (float)atof(model.value_edit_buf);

        if (parsed > 0.0f) {
            cheats_openphantom_jump_boost_set_scale(parsed);
        }
    }
}

void overlay_model_value_cancel(void)
{
    model.editing_value = false;
}
