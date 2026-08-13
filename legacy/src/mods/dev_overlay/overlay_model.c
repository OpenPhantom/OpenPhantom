/* overlay_model.c: the panel's state and the list of rows that follows from it.
 *
 * There is one group per tab today and the structure carries more, on purpose: the diagnostics and
 * the developer tools that will hang off this panel are groups beside the cheats, not a second
 * panel, and a shape that already folds and searches them costs nothing now.
 */
#include "overlay_model.h"

#include "cheats_openphantom.h"
#include "cheats_original.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* One entry per group, per tab. Kept flat because there are two of them and a table of tables for
 * two rows would be harder to read than the switch it replaced. */
typedef struct group_state {
    const char *title;
    bool        expanded;
} group_state_t;

typedef struct overlay_model_state {
    overlay_tab_t tab;
    char          search[OVERLAY_SEARCH_MAX];
    group_state_t groups[OVERLAY_TAB_COUNT];
    overlay_row_t rows[OVERLAY_ROWS_MAX];
    uint32_t      row_count;
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

void overlay_model_reset(void)
{
    uint32_t i;

    model.tab = OVERLAY_TAB_ORIGINAL;
    model.search[0] = '\0';
    model.row_count = 0;

    model.groups[OVERLAY_TAB_ORIGINAL].title = "Original cheats";
    model.groups[OVERLAY_TAB_OPENPHANTOM].title = "OpenPhantom cheats";
    for (i = 0; i < (uint32_t)OVERLAY_TAB_COUNT; ++i) {
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
    if (group < (uint32_t)OVERLAY_TAB_COUNT) {
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

/* How many rows the current tab's source holds, and what one of them looks like. The two sources
 * differ in everything except this shape, so the rest of the file does not care which is open. */
static uint32_t source_count(overlay_tab_t tab)
{
    if (tab == OVERLAY_TAB_ORIGINAL) {
        return cheats_original_count();
    }
    return (uint32_t)CHEATS_OWN_COUNT;
}

static void source_row(overlay_tab_t tab, uint32_t id, overlay_row_t *out)
{
    out->kind = OVERLAY_ROW_CHEAT;
    out->group = (uint32_t)tab;
    out->id = id;
    out->expanded = false;

    if (tab == OVERLAY_TAB_ORIGINAL) {
        copy_label(out->label, cheats_original_name(id));
        out->on = cheats_original_is_on(id);
        out->available = true;         /* a row only exists here once its table resolved */
        return;
    }
    copy_label(out->label, cheats_openphantom_name((cheats_own_id_t)id));
    out->on = cheats_openphantom_is_on((cheats_own_id_t)id);
    out->available = cheats_openphantom_is_available((cheats_own_id_t)id);
}

void overlay_model_rebuild(void)
{
    const uint32_t group = (uint32_t)model.tab;
    const uint32_t count = source_count(model.tab);
    const bool     searching = model.search[0] != '\0';
    overlay_row_t  row;
    uint32_t       matches = 0;
    uint32_t       i;

    model.row_count = 0;

    for (i = 0; i < count; ++i) {
        source_row(model.tab, i, &row);
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
    row.group = group;
    row.id = group;
    append_row(&row);

    if (!row.expanded) {
        return;
    }

    for (i = 0; i < count; ++i) {
        overlay_row_t leaf;

        source_row(model.tab, i, &leaf);
        if (!overlay_model_matches(leaf.label, model.search)) {
            continue;
        }
        append_row(&leaf);
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
    if (row.group == (uint32_t)OVERLAY_TAB_ORIGINAL) {
        (void)cheats_original_toggle(row.id);
        return true;
    }
    (void)cheats_openphantom_toggle((cheats_own_id_t)row.id);
    return true;
}
