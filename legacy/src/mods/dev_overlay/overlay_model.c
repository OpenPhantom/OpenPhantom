/* overlay_model.c: the panel's state and the list of rows that follows from it.
 *
 * There is one group per tab today and the structure carries more, on purpose: the diagnostics and
 * the developer tools that will hang off this panel are groups beside the cheats, not a second
 * panel, and a shape that already folds and searches them costs nothing now.
 */
#include "overlay_model.h"

#include "cheats_openphantom.h"
#include "cheats_original.h"
#include "cheats_original_actions.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
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
    OVERLAY_TAB_OPENPHANTOM    /* OVERLAY_GROUP_OPENPHANTOM      */
};

typedef struct overlay_model_state {
    overlay_tab_t tab;
    char          search[OVERLAY_SEARCH_MAX];
    group_state_t groups[OVERLAY_GROUP_COUNT];
    overlay_row_t rows[OVERLAY_ROWS_MAX];
    uint32_t      row_count;
    bool          capturing_hotkey;   /* the free-camera exit hotkey row is waiting for a keypress */
    bool          freecam_info_expanded;   /* the "how to fly" row is showing its lines */
    bool          freecam_was_on;     /* last-seen CHEATS_OWN_FREECAM state, to catch the edge */
} overlay_model_state_t;

static overlay_model_state_t model;

/* The exit-hotkey row takes over free camera's own numeric slot in this group's id space, and
 * free camera itself moves one slot later (FREECAM_ROW_ID) - so walking ids in order puts the
 * hotkey row directly BEFORE the cheat it gates rather than after it, the same order a player
 * reads the panel in. Read top to bottom, that tells the whole story on its own: set an exit key,
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
#define HOTKEY_ROW_ID  ((uint32_t)CHEATS_OWN_FREECAM)
#define FREECAM_ROW_ID ((uint32_t)CHEATS_OWN_FREECAM + 1u)

/* One past free camera's own row: a fold, the same shape as a group's own expand/collapse but
 * scoped to one row rather than a whole section - clicking it toggles model.freecam_info_expanded,
 * and while that is true, FREECAM_INFO_LINE_COUNT more INFO rows follow it (ids INFO_ROW_ID+1
 * through INFO_ROW_ID+FREECAM_INFO_LINE_COUNT), one per line of FREECAM_INFO_LINES. Reusing
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
#define FREECAM_INFO_LINE_COUNT 6u
static const char *const FREECAM_INFO_LINES[FREECAM_INFO_LINE_COUNT] = {
    "Needs an exit key set first",
    "WASD to move",
    "Mouse to look",
    "E / Q for up and down",
    "Scroll wheel changes speed",
    "Press your exit key to exit free camera"
};

/* Short enough for value[8]. Letters and digits already match their own virtual-key codes; function
 * keys and the handful of others worth naming get their own case; anything else prints as hex
 * rather than silently showing nothing, since a key that was bound has to be identifiable if
 * something else on the system also happens to be using it. */
static void key_name(int32_t vk, char *out, size_t out_size)
{
    if (vk >= 'A' && vk <= 'Z') {
        _snprintf(out, out_size, "%c", (char)vk);
    } else if (vk >= '0' && vk <= '9') {
        _snprintf(out, out_size, "%c", (char)vk);
    } else if (vk >= VK_F1 && vk <= VK_F12) {
        _snprintf(out, out_size, "F%d", (int)(vk - VK_F1 + 1));
    } else {
        switch (vk) {
        case VK_SPACE:   _snprintf(out, out_size, "Space");  break;
        case VK_TAB:     _snprintf(out, out_size, "Tab");    break;
        case VK_RETURN:  _snprintf(out, out_size, "Enter");  break;
        case VK_ESCAPE:  _snprintf(out, out_size, "Esc");    break;
        case VK_CONTROL: _snprintf(out, out_size, "Ctrl");   break;
        case VK_SHIFT:   _snprintf(out, out_size, "Shift");  break;
        case VK_MENU:    _snprintf(out, out_size, "Alt");    break;
        default:         _snprintf(out, out_size, "%02X", (unsigned)vk); break;
        }
    }
    out[out_size - 1] = '\0';
}

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

    model.groups[OVERLAY_GROUP_ORIGINAL_TOGGLES].title = "Original cheats";
    model.groups[OVERLAY_GROUP_ORIGINAL_ACTIONS].title = "Original cheats (one-time effects)";
    model.groups[OVERLAY_GROUP_OPENPHANTOM].title = "OpenPhantom cheats";
    for (i = 0; i < (uint32_t)OVERLAY_GROUP_COUNT; ++i) {
        model.groups[i].expanded = false;      /* everything starts folded, as asked */
    }
}

overlay_tab_t overlay_model_tab(void)
{
    return model.tab;
}

void overlay_model_set_tab(overlay_tab_t tab)
{
    if ((unsigned)tab < (unsigned)OVERLAY_TAB_COUNT) {
        model.tab = tab;
    }
}

const char *overlay_model_search(void)
{
    return model.search;
}

void overlay_model_set_search(const char *text)
{
    size_t i;

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
    case OVERLAY_GROUP_OPENPHANTOM:
    default:
        /* +2: the free-camera exit hotkey row, which takes over free camera's own numeric slot,
         * and the "how to fly" fold, one past where free camera itself now sits - see
         * HOTKEY_ROW_ID/FREECAM_ROW_ID/INFO_ROW_ID and their own handling in source_row() below.
         * The total id count is unchanged either way: repurposing one existing slot and adding one
         * new one nets the same +2 as before. The fold's own lines add FREECAM_INFO_LINE_COUNT
         * more only while it is open, the same shape a group's own child count already uses in
         * append_group() below. */
        return (uint32_t)CHEATS_OWN_COUNT + 2u +
               (model.freecam_info_expanded ? FREECAM_INFO_LINE_COUNT : 0u);
    }
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
    case OVERLAY_GROUP_OPENPHANTOM:
    default:
        if (id == HOTKEY_ROW_ID) {
            out->kind = OVERLAY_ROW_HOTKEY;
            copy_label(out->label, "Free camera exit key");
            out->on = false;        /* meaningless for a hotkey row; never read by the drawer */
            out->available = cheats_openphantom_is_available(CHEATS_OWN_FREECAM);
            /* Always populated, never left for the drawer's own ACTION/CHEAT fallback word to
             * guess at - "RUN" and "OFF" are both wrong for a key binding. */
            if (model.capturing_hotkey) {
                _snprintf(out->value, sizeof out->value, "...");
            } else {
                int32_t vk = cheats_openphantom_freecam_hotkey();
                if (vk != 0) {
                    key_name(vk, out->value, sizeof out->value);
                } else {
                    _snprintf(out->value, sizeof out->value, "Set");
                }
            }
            out->value[sizeof out->value - 1] = '\0';
            return;
        }
        if (id == FREECAM_ROW_ID) {
            /* Free camera itself, one slot after its own exit-hotkey row now rather than at its
             * plain cheats_own_id_t position - see HOTKEY_ROW_ID/FREECAM_ROW_ID above. Named by
             * CHEATS_OWN_FREECAM explicitly rather than casting id the way the generic fallback
             * below does for the other three cheats: id here is FREECAM_ROW_ID, one past the real
             * enum's range, and casting that would ask cheats_openphantom.c about an id it never
             * offered a name for. */
            out->kind = OVERLAY_ROW_CHEAT;
            copy_label(out->label, cheats_openphantom_name(CHEATS_OWN_FREECAM));
            out->on = cheats_openphantom_is_on(CHEATS_OWN_FREECAM);
            out->available = cheats_openphantom_is_available(CHEATS_OWN_FREECAM);
            /* Free camera specifically also needs an exit hotkey bound before it can be switched
             * ON - cheats_openphantom_toggle() enforces this too, so this is display honesty
             * rather than the only gate: a row that looked clickable but silently refused every
             * click would be worse than one that shows why. Once it IS on, availability no longer
             * depends on this - the row is unreachable anyway with the mouse claimed, and the exit
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
        if (model.freecam_info_expanded && id > INFO_ROW_ID &&
            id <= INFO_ROW_ID + FREECAM_INFO_LINE_COUNT) {
            char line[OVERLAY_LABEL_MAX];

            out->kind = OVERLAY_ROW_INFO;
            _snprintf(line, sizeof line, "    %s", FREECAM_INFO_LINES[id - INFO_ROW_ID - 1u]);
            line[sizeof line - 1] = '\0';
            copy_label(out->label, line);
            out->on = false;
            out->available = true;   /* a nested line, not a gate; never clicked either way */
            return;
        }
        /* Everything left is one of the other five cheats (ammunition, health, no fog, invincible
         * NPCs, one-shot NPCs), whose ids still line up 1:1 with cheats_own_id_t - only free
         * camera's own slot was repurposed above. */
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
        /* Free camera just changed state - either the panel's own toggle, or (more often) its exit
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
         * reaching its usual handling. */
        model.capturing_hotkey = true;
        return true;
    }
    switch ((overlay_group_t)row.group) {
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
    if (!model.capturing_hotkey) {
        return;
    }
    model.capturing_hotkey = false;
    cheats_openphantom_freecam_set_hotkey(virtual_key);
}
