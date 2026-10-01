/* mp_canvas.c: the pure half of a list drawn on a canvas widget. See mp_canvas.h. */
#include "mp_canvas.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

void mp_canvas_list_init(mp_canvas_list_t *list, uint32_t row_height, uint32_t header,
                         uint32_t view_height)
{
    if (list == NULL) {
        return;
    }
    memset(list, 0, sizeof *list);
    list->row_height  = row_height > 0u ? row_height : 1u;
    list->header      = header;
    list->view_height = view_height;
    list->selected    = MP_CANVAS_NONE;
    list->hovered     = MP_CANVAS_NONE;
}

uint32_t mp_canvas_list_visible(const mp_canvas_list_t *list)
{
    uint32_t room;

    if (list == NULL || list->view_height <= list->header) {
        return 1u;
    }
    room = (list->view_height - list->header) / list->row_height;
    return room > 0u ? room : 1u;
}

bool mp_canvas_list_scrolls(const mp_canvas_list_t *list)
{
    return list != NULL && list->rows > mp_canvas_list_visible(list);
}

/* The largest `first` that still fills the window, or 0 when everything fits. */
static uint32_t last_first(const mp_canvas_list_t *list)
{
    uint32_t visible = mp_canvas_list_visible(list);

    return list->rows > visible ? list->rows - visible : 0u;
}

static void clamp_window(mp_canvas_list_t *list)
{
    uint32_t limit = last_first(list);

    if (list->first > limit) {
        list->first = limit;
    }
}

/* Scrolls by the least that puts `row` inside the window. */
static void bring_into_view(mp_canvas_list_t *list, uint32_t row)
{
    uint32_t visible = mp_canvas_list_visible(list);

    if (row < list->first) {
        list->first = row;
    } else if (row >= list->first + visible) {
        list->first = row + 1u - visible;
    }
    clamp_window(list);
}

void mp_canvas_list_set_rows(mp_canvas_list_t *list, uint32_t rows)
{
    if (list == NULL) {
        return;
    }
    list->rows = rows;
    if (rows == 0u) {
        list->first    = 0u;
        list->selected = MP_CANVAS_NONE;
        list->hovered  = MP_CANVAS_NONE;
        return;
    }
    if (list->selected >= (int32_t)rows) {
        list->selected = (int32_t)rows - 1;
    }
    if (list->hovered >= (int32_t)rows) {
        list->hovered = MP_CANVAS_NONE;
    }
    clamp_window(list);
}

bool mp_canvas_list_row_at(const mp_canvas_list_t *list, int32_t y, uint32_t *row)
{
    uint32_t index;

    if (list == NULL || row == NULL || y < (int32_t)list->header ||
        y >= (int32_t)list->view_height) {
        return false;
    }
    index = list->first + ((uint32_t)y - list->header) / list->row_height;
    if (index >= list->rows || index >= list->first + mp_canvas_list_visible(list)) {
        return false;
    }
    *row = index;
    return true;
}

bool mp_canvas_list_row_top(const mp_canvas_list_t *list, uint32_t row, int32_t *y)
{
    if (list == NULL || y == NULL || row >= list->rows || row < list->first ||
        row >= list->first + mp_canvas_list_visible(list)) {
        return false;
    }
    *y = (int32_t)(list->header + (row - list->first) * list->row_height);
    return true;
}

void mp_canvas_list_hover(mp_canvas_list_t *list, int32_t x, int32_t y, uint32_t width)
{
    uint32_t row;

    if (list == NULL) {
        return;
    }
    if (x < 0 || x >= (int32_t)width || !mp_canvas_list_row_at(list, y, &row)) {
        list->hovered = MP_CANVAS_NONE;
        return;
    }
    list->hovered = (int32_t)row;
}

void mp_canvas_list_select(mp_canvas_list_t *list, int32_t row)
{
    if (list == NULL) {
        return;
    }
    if (row < 0 || row >= (int32_t)list->rows) {
        list->selected = MP_CANVAS_NONE;
        return;
    }
    list->selected = row;
    bring_into_view(list, (uint32_t)row);
}

void mp_canvas_list_move(mp_canvas_list_t *list, int32_t delta)
{
    int64_t wanted;

    if (list == NULL || list->rows == 0u) {
        return;
    }
    if (list->selected == MP_CANVAS_NONE) {
        /* The first key lands on the row the player can see at the top, not on row zero of a
         * list that may be scrolled halfway down. */
        mp_canvas_list_select(list, (int32_t)list->first);
        return;
    }
    wanted = (int64_t)list->selected + delta;
    if (wanted < 0) {
        wanted = 0;
    }
    if (wanted > (int64_t)list->rows - 1) {
        wanted = (int64_t)list->rows - 1;
    }
    mp_canvas_list_select(list, (int32_t)wanted);
}

void mp_canvas_list_scroll(mp_canvas_list_t *list, int32_t delta)
{
    int64_t wanted;

    if (list == NULL) {
        return;
    }
    wanted = (int64_t)list->first + delta;
    if (wanted < 0) {
        wanted = 0;
    }
    if (wanted > (int64_t)last_first(list)) {
        wanted = (int64_t)last_first(list);
    }
    list->first = (uint32_t)wanted;
}

bool mp_canvas_list_key(mp_canvas_list_t *list, uint32_t vk)
{
    int32_t page;

    if (list == NULL) {
        return false;
    }
    page = (int32_t)mp_canvas_list_visible(list);
    switch (vk) {
    case MP_CANVAS_VK_UP:
        mp_canvas_list_move(list, -1);
        return true;
    case MP_CANVAS_VK_DOWN:
        mp_canvas_list_move(list, 1);
        return true;
    case MP_CANVAS_VK_PRIOR:
        mp_canvas_list_move(list, -page);
        return true;
    case MP_CANVAS_VK_NEXT:
        mp_canvas_list_move(list, page);
        return true;
    case MP_CANVAS_VK_HOME:
        if (list->rows > 0u) {
            mp_canvas_list_select(list, 0);
        }
        return true;
    case MP_CANVAS_VK_END:
        if (list->rows > 0u) {
            mp_canvas_list_select(list, (int32_t)list->rows - 1);
        }
        return true;
    default:
        return false;
    }
}

void mp_canvas_list_thumb(const mp_canvas_list_t *list, float *at, float *length)
{
    uint32_t visible;

    if (at == NULL || length == NULL) {
        return;
    }
    if (list == NULL || !mp_canvas_list_scrolls(list)) {
        *at     = 0.0f;
        *length = 1.0f;
        return;
    }
    visible = mp_canvas_list_visible(list);
    *length = (float)visible / (float)list->rows;
    *at     = (float)list->first / (float)list->rows;
}

uint32_t mp_canvas_tab_at(uint32_t count, int32_t x, uint32_t width)
{
    uint32_t each;

    if (count == 0u || width == 0u || x < 0 || x >= (int32_t)width) {
        return count;
    }
    each = width / count;
    if (each == 0u) {
        return count;
    }
    return (uint32_t)x / each < count ? (uint32_t)x / each : count - 1u;
}
