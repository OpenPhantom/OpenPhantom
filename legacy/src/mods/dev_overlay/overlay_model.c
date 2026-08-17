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
} overlay_model_state_t;

static overlay_model_state_t model;

/* The id one past the real cheats in OVERLAY_GROUP_OPENPHANTOM's own source, standing for the
 * free-camera exit hotkey row appended after them - see source_count()/source_row() below. Not a
 * cheats_own_id_t, and deliberately outside that enum's range instead of extending it: the hotkey
 * is a row this panel adds, not a cheat cheats_openphantom.c itself offers a name or an on/off for. */
#define HOTKEY_ROW_ID ((uint32_t)CHEATS_OWN_COUNT)

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
        /* +1: the free-camera exit hotkey row, appended after the cheats themselves - see
         * HOTKEY_ROW_ID and its own handling in source_row() below. */
        return (uint32_t)CHEATS_OWN_COUNT + 1u;
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
        out->kind = OVERLAY_ROW_CHEAT;
        copy_label(out->label, cheats_openphantom_name((cheats_own_id_t)id));
        out->on = cheats_openphantom_is_on((cheats_own_id_t)id);
        out->available = cheats_openphantom_is_available((cheats_own_id_t)id);
        /* Free camera specifically also needs an exit hotkey bound before it can be switched ON -
         * cheats_openphantom_toggle() enforces this too, so this is display honesty rather than
         * the only gate: a row that looked clickable but silently refused every click would be
         * worse than one that shows why. Once it IS on, availability no longer depends on this -
         * the row is unreachable anyway with the mouse claimed, and the exit hotkey is how it
         * actually turns back off. */
        if ((cheats_own_id_t)id == CHEATS_OWN_FREECAM && !out->on &&
            cheats_openphantom_freecam_hotkey() == 0) {
            out->available = false;
        }
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
        (void)cheats_openphantom_toggle((cheats_own_id_t)row.id);
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
