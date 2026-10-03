/* overlay_model.c: the panel's state and the list of rows that follows from it.
 *
 * The Original tab shows two headings and the OpenPhantom tab twelve, and the structure carries
 * more, on purpose: the diagnostics and the developer tools that will hang off this panel are
 * groups beside the cheats, not a second panel, and a shape that already folds and searches them
 * costs nothing now.
 *
 * A heading and a source of rows are not the same thing here, and which headings there are, in
 * what order, and which sources stand under each is overlay_headings.c's. This file asks it once
 * per rebuild and keeps what the player changes: which heading is folded, what is searched and
 * which row the keyboard is on.
 *
 * SIZE NOTE. This file is over the six hundred line mark. It was already over before the draw
 * distance row was added to it, and adding that row is what turned an inherited overage into one
 * worth writing down rather than passing on again.
 *
 * What was long is the row numbering and the reasoning attached to it. The OpenPhantom group's ids
 * are not a plain list: the jump-boost scale is inserted after its own toggle, the teleport key and
 * free camera swap places, a fold adds nine more ids only while it is open, and each of those has
 * a static assert and a paragraph explaining what it is pinned to and what breaks if the enum
 * behind it is reordered. Deleting that reasoning to get under a limit would leave arithmetic
 * nobody can check, which the guidance here explicitly refuses.
 *
 * So it moved instead, to overlay_row_ids.h, when a bounds fix to the row array pushed this file
 * past the hard limit. That header is the numbering, its reasoning and its asserts, plus the one
 * number that says how many rows the widest tab can build; nothing in it decides anything, and
 * every function that reads it stayed here. The fold's own text is the exception, because it is a
 * definition rather than a declaration and belongs in one translation unit.
 *
 * An earlier seam was taken as well, and it was not the one this note used to name. Splitting the
 * OpenPhantom tab into Cheats and Utilities moved the settings rows into overlay_utilities.c,
 * which is a better cut than the editing state machine this note previously proposed: those rows
 * share none of this file's navigation, search, folding or typing state, so what moved is a whole
 * responsibility and what stayed is the part that has to know which row is being typed into.
 *
 * A third seam was taken when a sixth group put this file within fifteen lines of the hard limit:
 * the cheats group's own rows, the one group whose rows this file still described itself, went
 * to overlay_cheats.c the way every settings group already had, with the model handing in the
 * state a row needs (which fold is open, what is being typed, whether a key is being captured)
 * instead of the rows reading it here.
 *
 * The fourth and fifth seams were taken together when the panel learned to remember where it was
 * left, to say why a row cannot be used and to be driven from a keyboard, and the three of those
 * put this file 250 lines past the hard limit. The first was the one this note had named for
 * years and never taken: the editing state machine, the typed value and the key capture, which
 * every group goes through and none of this file's navigation touches, now overlay_edit.c. The
 * second was the dispatch that asks each group how many rows it has and what one of them looks
 * like, about 170 lines of switch that held no state at all, now overlay_row_source.c.
 *
 * The sixth seam was the group structure, taken when a twelfth heading was due and this file stood
 * within fifteen lines of the hard limit: the three tables that say which tab a source belongs to,
 * which heading it is drawn under and in what order, with the walk over them and the titles, now
 * overlay_headings.c. They were read once per rebuild and decided nothing else.
 *
 * The seam, if it grows again, is the word on a folded heading: the summary gathered while the rows
 * are counted and the rule that turns it into "2 on", a name or a reason. It reads the rows handed
 * to it and nothing of this file's state.
 */
#include "overlay_model.h"

#include "overlay_choice.h"
#include "overlay_edit.h"
#include "overlay_headings.h"
#include "overlay_notice.h"
#include "overlay_row_source.h"
#include "overlay_reason.h"
#include "overlay_row_fill.h"
#include "overlay_slider.h"
#include "session_lock.h"

#include "common/logging.h"
#include "common/text.h"

#include "overlay_cheats.h"
#include "overlay_controls.h"
#include "overlay_dismember.h"
#include "overlay_modelswap.h"
#include "overlay_fog.h"
#include "overlay_framerate.h"
#include "overlay_freecam.h"
#include "overlay_levels.h"
#include "overlay_menu_extras.h"
#include "overlay_multiplayer.h"
#include "overlay_picture.h"
#include "overlay_row_ids.h"
#include "overlay_spawn.h"
#include "overlay_utilities.h"
#include "overlay_window.h"

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

/* One entry per group, and all a group keeps is whether the player folded it. Its tab, its place
 * in the drawn order and its title are overlay_headings.c's: a group cannot change any of those at
 * runtime, so there is nothing to keep in sync by storing them here. */
typedef struct group_state {
    bool expanded;
} group_state_t;

typedef struct overlay_model_state {
    overlay_tab_t tab;
    char          search[OVERLAY_SEARCH_MAX];
    group_state_t groups[OVERLAY_GROUP_COUNT];
    overlay_row_t rows[OVERLAY_ROWS_MAX];
    uint32_t      row_count;
    uint32_t      heading_count;    /* how many of them are bands, for the footer's tally */
    int32_t       selected;           /* the row the keyboard is on, or -1 for none */
    /* What is being typed into and which key row is waiting is overlay_edit.c's, along with the
     * two functions that finish either of them. None of it is navigation, every group goes
     * through it, and this file was over its limit. */
} overlay_model_state_t;

static overlay_model_state_t model;

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

void overlay_model_forget_edits(void)
{
    overlay_edit_forget();
    /* The refusal band as well, here rather than beside the close in overlay_input.c: there are
     * four ways to close the panel and this is the only call all four make. It sat at one of
     * them, and a sentence about a keypress from the last visit stood over the rows on the next
     * opening. */
    overlay_notice_forget();
}

void overlay_model_reset(void)
{
    uint32_t i;

    /* This patch's own tab, not the shipped console's. The panel is opened for the free
     * camera, the spawner and the settings far more often than for a retail code, and the
     * wrong first tab cost one click every time the game started. */
    model.tab = OVERLAY_TAB_OPENPHANTOM;
    model.search[0] = '\0';
    model.row_count = 0;
    model.selected = -1;
    overlay_model_forget_edits();
    overlay_freecam_reset();             /* the "how to fly" fold, closed like the groups */
    overlay_levels_reset();              /* and the level list */
    overlay_spawn_reset();               /* and the spawner's list of the level's actors */
    overlay_menu_extras_reset();         /* and the "what this adds" fold, the same */
    overlay_controls_reset();            /* and the "what these do" fold */
    overlay_window_reset();              /* and the window group's size list, same reason */

    for (i = 0; i < (uint32_t)OVERLAY_GROUP_COUNT; ++i) {
        model.groups[i].expanded = false;      /* everything starts folded, as asked */
    }
}

overlay_tab_t overlay_model_tab(void)
{
    return model.tab;
}

/* The first drawn row, as a REQUEST rather than a fact: the reader below reconciles it with a
 * row count that changes under it. */
static int32_t scroll_want = 0;

void overlay_model_scroll_by(int32_t rows)
{
    scroll_want += rows;
    if (scroll_want < 0) {
        scroll_want = 0;
    }
}

/* Back to the top, which is what every change that replaces the list asks for: a tab change and
 * every keystroke in the search box. The keyboard's selection goes with it, because an index into
 * a list that has just been replaced names a row nobody chose. */
static void scroll_home(void)
{
    scroll_want = 0;
    model.selected = -1;
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

/* The selection, read and written the same way the scroll is: stored as a request and reconciled
 * with the row count where it is read, because the count moves under it with every keystroke in
 * the search box and every fold. */
int32_t overlay_model_selected(void)
{
    const uint32_t count = overlay_model_row_count();

    if (model.selected < 0 || count == 0u) {
        return -1;
    }
    if ((uint32_t)model.selected >= count) {
        return (int32_t)(count - 1u);
    }
    return model.selected;
}

void overlay_model_set_selected(int32_t index)
{
    const uint32_t count = overlay_model_row_count();

    if (index < 0 || count == 0u) {
        model.selected = -1;
        return;
    }
    model.selected = ((uint32_t)index >= count) ? (int32_t)(count - 1u) : index;
}

void overlay_model_move_selection(int32_t rows, uint32_t from)
{
    const int32_t at = overlay_model_selected();
    int32_t       want;

    /* From nothing the first press lands on the row the player is looking at rather than at the
     * top of a list they may have scrolled a long way down. */
    if (at < 0) {
        overlay_model_set_selected((int32_t)from);
        return;
    }
    /* Held at the top rather than cleared. A negative index CLEARS the selection, which is what a
     * hand on the mouse asks for, and an arrow held down at the first row is not asking for that:
     * it would take the current row away and the next press would put it back somewhere else. */
    want = at + rows;
    overlay_model_set_selected((want < 0) ? 0 : want);
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
        /* A source drawn under another heading has no fold of its own; folding it means folding
         * the heading its rows are under, which is the only thing on screen that could have been
         * clicked. */
        const overlay_group_t heading = overlay_headings_drawn_under((overlay_group_t)group);

        model.groups[heading].expanded = !model.groups[heading].expanded;
    }
}

/* ============================================================================================ */

static void append_row(const overlay_row_t *row)
{
    static bool reported_full;

    if (model.row_count < OVERLAY_ROWS_MAX) {
        if (row->kind == OVERLAY_ROW_GROUP) {
            ++model.heading_count;   /* counted where a band is put in, and nowhere else */
        }
        model.rows[model.row_count++] = *row;
        return;
    }
    /* The assert above is what should stop this ever happening, but it can only count the parts
     * that are constants. A source whose length the display decides could still outrun the array,
     * and a row that is silently dropped looks to the player like a feature that was never added.
     * Once a session is enough to say so. */
    if (!reported_full) {
        reported_full = true;
        log_warning("the panel built more than %u rows and the rest were dropped, starting with "
                    "\"%s\". Nothing below it can be reached until the array is raised.",
                    (unsigned)OVERLAY_ROWS_MAX, row->label);
    }
}

/* What a heading knows about the rows under it, gathered while they are counted for the search so
 * that they are built once rather than twice. */
typedef struct group_summary {
    uint32_t matches;    /* rows the search found, which is what decides the fold */
    uint32_t cheats;     /* rows that are switches */
    uint32_t on;         /* and how many of those are on right now */
    uint32_t actable;    /* rows a player can act on at all */
    uint32_t taken;      /* and how many of those a session has taken away */
    uint32_t reason;     /* the reason the first taken row gave */
    uint32_t picked;     /* rows that are the chosen entry of a choice */
    char     pick[16];   /* and the last of their names, sized like a row's own value */
} group_summary_t;

static void summarise(group_summary_t *sum, const overlay_row_t *row, bool taken)
{
    if (row->kind == OVERLAY_ROW_CHEAT) {
        ++sum->cheats;
        if (row->on) {
            ++sum->on;
        }
    }
    /* A choice has no switch to count, so what it contributes is the entry it settled on. */
    if (overlay_choice_chosen_name(row, sum->pick, sizeof sum->pick)) {
        ++sum->picked;
    }
    if (!overlay_row_kind_is_acted_on(row->kind)) {
        return;
    }
    ++sum->actable;
    if (taken) {
        if (sum->taken == 0u) {
            sum->reason = row->reason;
        }
        ++sum->taken;
    }
}

/* The word on a folded heading, so a list of twelve bands answers what is on without any of them
 * being opened. A folded group used to look the same whether nothing or six cheats under it were
 * on, and the field for this has been on the row since it was written.
 *
 * It is worked out from the rows themselves rather than asked of each group, so a group that
 * grows a switch is counted without being changed for it and no group can answer differently from
 * what it draws. The price is that the word is a count and not a name.
 *
 * SWITCHES are counted and choices are not: the count would be the same however a choice stood,
 * and "1 on" beside a heading says something is switched on under it. That left three headings
 * saying nothing at all the day their switches became lists, so a heading with NO switches and
 * exactly one chosen entry under it carries that entry's name instead. Two chosen entries cannot
 * be one word and none has no word, so both of those read empty.
 *
 * Answers whether the word says something under the heading is ON, which the drawing colours by.
 * It used to work that out by spelling the finished text back apart, letter by letter ("O" then
 * "N", or a leading digit), which is a second predicate for a state this function already knows
 * and which breaks the first time a band says anything else. */
static bool heading_chip(char *out, size_t size, const group_summary_t *sum)
{
    out[0] = '\0';
    if (sum->taken > 0u && sum->taken == sum->actable) {
        text_format(out, size, "%s", overlay_reason_word(sum->reason));
        return false;       /* a reason, not a state: the whole group is out of reach */
    }
    if (sum->cheats == 1u) {
        text_format(out, size, "%s", sum->on > 0u ? "ON" : "OFF");
        return sum->on > 0u;
    }
    if (sum->on > 0u) {
        text_format(out, size, "%u on", sum->on);
        return true;
    }
    /* Not lit: an entry that was chosen is not a switch that is on. */
    if (sum->cheats == 0u && sum->picked == 1u) {
        text_format(out, size, "%s", sum->pick);
        return false;
    }
    return false;
}

/* A note under the row above it. An info line is this panel's own way of attaching one, so this
 * needs no new row kind and no group has to count it: it is appended past the source's own rows
 * and nothing indexes it. */
static void append_note(const char *text, overlay_group_t group, uint32_t id)
{
    overlay_row_t note;

    overlay_row_defaults(&note);
    note.kind = OVERLAY_ROW_INFO;
    copy_label(note.label, text);
    note.group = (uint32_t)group;
    note.id = id;
    append_row(&note);
}

/* Why a row is not available, written out once per heading for each reason whose chip word cannot
 * carry it on its own. Answers the reasons already written, so the second row with the same
 * reason adds nothing. The session is not decided here: the lock owns those words and writes them
 * itself, below. */
static uint32_t say_why(const overlay_row_t *leaf, overlay_group_t body, uint32_t said)
{
    const char *sentence;
    uint32_t    bit;

    if (leaf->available || leaf->reason >= (uint32_t)OVERLAY_REASON_COUNT) {
        return said;
    }
    bit = 1u << leaf->reason;
    if ((said & bit) != 0u) {
        return said;
    }
    sentence = overlay_reason_sentence(leaf->reason);
    if (sentence == NULL) {
        return said;
    }
    append_note(sentence, body, leaf->id);
    return said | bit;
}

/* One heading, its own rows and the rows of anything drawn under it, and the notes that say why
 * a row cannot be used. Pulled out of overlay_model_rebuild() because a tab holds several of
 * these and each is otherwise identical: build the heading, count its own hits, decide its own
 * fold. */
static void append_group(overlay_group_t heading)
{
    overlay_group_t bodies[OVERLAY_GROUP_COUNT];
    uint32_t        counts[OVERLAY_GROUP_COUNT];
    const uint32_t  body_count =
        overlay_headings_bodies(heading, bodies, (uint32_t)OVERLAY_GROUP_COUNT);
    const bool      searching = model.search[0] != '\0';
    group_summary_t sum;
    overlay_row_t   row;
    uint32_t        said = 0;          /* the reasons already written under this heading */
    bool            said_session = false;
    uint32_t        b;
    uint32_t        i;

    memset(&sum, 0, sizeof sum);
    for (b = 0; b < body_count; ++b) {
        /* Once per rebuild: a source may count a level's actors or a display's modes to answer,
         * and the loops below both read the number rather than asking again. */
        counts[b] = overlay_row_source_count(bodies[b]);
        for (i = 0; i < counts[b]; ++i) {
            bool taken;

            overlay_row_source_fill(bodies[b], i, &row);
            if (overlay_model_matches(row.label, model.search)) {
                ++sum.matches;
            }
            taken = session_lock_take((uint32_t)bodies[b], i, &row);
            summarise(&sum, &row, taken);
        }
    }

    /* A search opens the group that has hits, without disturbing the fold the user chose: clearing
     * the box puts it back exactly as it was. */
    row.kind = OVERLAY_ROW_GROUP;
    copy_label(row.label, overlay_headings_title(heading));
    row.expanded = model.groups[heading].expanded || (searching && sum.matches > 0u);
    row.available = true;
    row.pending = false;    /* the loop above may have left these set from the last child scanned */
    row.fraction = 0.0f;
    row.chosen = 0u;
    row.host_value = false;
    row.reason = (uint32_t)OVERLAY_REASON_NONE;
    /* A heading's `on` is what its own word claims, so the drawing colours by a state rather than
     * by reading the text back. */
    row.on = heading_chip(row.value, sizeof row.value, &sum);
    row.group = (uint32_t)heading;
    row.id = (uint32_t)heading;
    append_row(&row);

    if (!row.expanded) {
        return;
    }

    for (b = 0; b < body_count; ++b) {
        for (i = 0; i < counts[b]; ++i) {
            overlay_row_t leaf;
            bool          taken;

            overlay_row_source_fill(bodies[b], i, &leaf);
            if (!overlay_model_matches(leaf.label, model.search)) {
                continue;
            }
            /* A row a running session cannot survive is greyed here rather than in each group's
             * own source: the reason is one sentence for all of them and belongs in one place
             * (session_lock.c), and a group that grows a row gets the same treatment without
             * being changed for it. */
            taken = session_lock_take((uint32_t)bodies[b], i, &leaf);
            append_row(&leaf);
            said = say_why(&leaf, bodies[b], said);
            /* And the lock's own sentence, under the FIRST row it took rather than at the end of
             * the group. At the end it stood sixteen rows below the row it explained, which is
             * further than anybody reads for it. */
            if (taken && !said_session && session_lock_holds_group((uint32_t)bodies[b])) {
                const char *word = session_lock_word((uint32_t)bodies[b]);

                said_session = true;
                /* NULL for a group that says it itself; see session_lock.h. */
                if (word != NULL) {
                    append_note(word, bodies[b], counts[b]);
                }
            }
        }
    }
}

void overlay_model_rebuild(void)
{
    uint32_t g;

    overlay_freecam_sync();   /* the fold follows the camera's own on/off, hotkey path included */
    session_lock_refresh();   /* one reading of the multiplayer's note for this whole picture */
    model.row_count = 0;
    model.heading_count = 0;
    /* Headings in the order they are drawn, and only the headings: a source with no heading of
     * its own is built by the one it is drawn under. */
    for (g = 0; g < overlay_headings_count(); ++g) {
        const overlay_group_t heading = overlay_headings_at(g);

        if (overlay_headings_tab(heading) == model.tab) {
            append_group(heading);
        }
    }
}

uint32_t overlay_model_row_count(void)
{
    return model.row_count;
}

uint32_t overlay_model_heading_count(void)
{
    return model.heading_count;
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
        /* Only a fold's own summary row is interactive; the lines it reveals when open are
         * notes, not controls, the same as an ordinary INFO row always was, and the group
         * answers false for those itself. Four groups carry a fold. */
        if (row.group == (uint32_t)OVERLAY_GROUP_OPENPHANTOM_FREECAM) {
            return overlay_freecam_toggle(row.id - FREECAM_FIRST_ID);
        }
        if (row.group == (uint32_t)OVERLAY_GROUP_OPENPHANTOM_MENU_EXTRAS) {
            return overlay_menu_extras_toggle(row.id - MENU_EXTRAS_FIRST_ID);
        }
        if (row.group == (uint32_t)OVERLAY_GROUP_OPENPHANTOM_CONTROLS) {
            return overlay_controls_toggle(row.id - CONTROLS_FIRST_ID);
        }
        if (row.group == (uint32_t)OVERLAY_GROUP_OPENPHANTOM_SPAWN) {
            return overlay_spawn_toggle(row.id - SPAWN_FIRST_ID);
        }
        return false;
    }
    if (row.kind == OVERLAY_ROW_HOTKEY) {
        overlay_edit_start_capture(row.id);
        return true;
    }
    if (row.kind == OVERLAY_ROW_VALUE) {
        overlay_edit_start_value(row.id);
        /* The row being typed into is the current row, whichever way the edit was
         * started. A click starts one and clears the selection the pointer took in the
         * same breath, so without this the sideways keys had nothing to act on in exactly
         * the state a player reaches by clicking the number they mean to change. */
        overlay_model_set_selected((int32_t)index);
        return true;
    }
    if (row.kind == OVERLAY_ROW_SLIDER) {
        /* The Default drawn at the end of the track. A track is dragged and not pressed,
         * so this is the only thing a press on that row can mean, and Return needs no rule
         * of its own for it: it acts on the row it is on, as everywhere else here. A track
         * whose row named no standard answers false and stays where it is.
         *
         * This is the one call out of this file into overlay_slider.c, which calls back
         * into it. It is here rather than in the two callers, Return and the click,
         * because acting on a row is this function, and the same rule written twice beside
         * it is the pair that comes apart. */
        return overlay_slider_to_standard((int32_t)index);
    }
    switch ((overlay_group_t)row.group) {
    case OVERLAY_GROUP_OPENPHANTOM_UTILITIES:
        /* Every switch in that group, answered by the group itself. The two edit kinds above have
         * already been taken, so what reaches here is a plain toggle. */
        return overlay_utilities_toggle(row.id - UTILITIES_FIRST_ID);
    case OVERLAY_GROUP_OPENPHANTOM_PICTURE:
        return overlay_picture_toggle(row.id - PICTURE_FIRST_ID);
    case OVERLAY_GROUP_OPENPHANTOM_FOG:
        return overlay_fog_toggle(row.id - FOG_FIRST_ID);
    case OVERLAY_GROUP_OPENPHANTOM_FREECAM:
        return overlay_freecam_toggle(row.id - FREECAM_FIRST_ID);
    case OVERLAY_GROUP_OPENPHANTOM_DISMEMBER:
        return overlay_dismember_toggle(row.id - DISMEMBER_FIRST_ID);
    case OVERLAY_GROUP_OPENPHANTOM_MODELSWAP:
        return overlay_modelswap_toggle(row.id - MODELSWAP_FIRST_ID);
    case OVERLAY_GROUP_OPENPHANTOM_LEVELS:
        return overlay_levels_toggle(row.id - LEVELS_FIRST_ID);
    case OVERLAY_GROUP_OPENPHANTOM_SPAWN:
        return overlay_spawn_toggle(row.id - SPAWN_FIRST_ID);
    case OVERLAY_GROUP_OPENPHANTOM_MENU_EXTRAS:
        return overlay_menu_extras_toggle(row.id - MENU_EXTRAS_FIRST_ID);
    case OVERLAY_GROUP_OPENPHANTOM_CONTROLS:
        return overlay_controls_toggle(row.id - CONTROLS_FIRST_ID);
    case OVERLAY_GROUP_OPENPHANTOM_WINDOW:
        return overlay_window_toggle(row.id - WINDOW_FIRST_ID);
    case OVERLAY_GROUP_OPENPHANTOM_FRAMERATE:
        return overlay_framerate_toggle(row.id - FRAMERATE_FIRST_ID);
    case OVERLAY_GROUP_OPENPHANTOM_MULTIPLAYER:
        /* The two buttons; the key row was taken above, where its capture starts. */
        return overlay_multiplayer_toggle(row.id - MULTIPLAYER_FIRST_ID);
    case OVERLAY_GROUP_ORIGINAL_TOGGLES:
        (void)cheats_original_toggle(row.id);
        return true;
    case OVERLAY_GROUP_ORIGINAL_ACTIONS:
        return cheats_original_actions_invoke((cheats_action_id_t)row.id);
    case OVERLAY_GROUP_OPENPHANTOM:
    default:
        /* A cheat; the typed row was taken above. */
        return overlay_cheats_toggle(row.id);
    }
}

/* The one test in front of every question about a track: it is a track, and it can be used. It
 * was written out four times, once per question, and the fourth was the one that would have
 * forgotten the availability half. Which group owns the track is overlay_row_source.c's, the way
 * every other per-group difference in this panel is. */
static bool a_usable_track(uint32_t index, overlay_row_t *out)
{
    return overlay_model_row(index, out) && out->kind == OVERLAY_ROW_SLIDER && out->available;
}

bool overlay_model_slider_set(uint32_t index, float fraction)
{
    overlay_row_t row;

    return a_usable_track(index, &row) && overlay_row_source_slider_set(&row, fraction);
}

bool overlay_model_slider_value(uint32_t index, float fraction, char *out, size_t size)
{
    overlay_row_t row;

    return out != NULL && size != 0u && a_usable_track(index, &row) &&
           overlay_row_source_slider_value(&row, fraction, out, size);
}

bool overlay_model_slider_limits(uint32_t index, overlay_number_t *out)
{
    overlay_row_t row;

    return out != NULL && a_usable_track(index, &row) &&
           overlay_row_source_slider_limits(&row, out);
}

bool overlay_model_slider_wants_full_rate(uint32_t index)
{
    overlay_row_t row;

    /* Not through a_usable_track(): this one asked nothing about availability before and asking
     * now would change which rate an unavailable row's drag writes at, which is a question no
     * unavailable row ever gets to. */
    if (!overlay_model_row(index, &row) || row.kind != OVERLAY_ROW_SLIDER ||
        row.group != (uint32_t)OVERLAY_GROUP_OPENPHANTOM_PICTURE) {
        return false;              /* the field of view is the one, and it is in that group */
    }
    return overlay_picture_slider_wants_full_rate(row.id - PICTURE_FIRST_ID);
}
