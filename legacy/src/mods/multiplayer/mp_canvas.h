/* mp_canvas.h: the pure half of a list drawn on a canvas widget.
 *
 * Layer 1, pure. No engine, no widget, no pixel: a list that knows how many rows it has, how tall
 * one is, how tall the window is, which row is scrolled to the top, which is selected and which
 * the pointer is over. Everything a scrolling list decides is decided here, in local pixels of the
 * canvas rectangle, so it is driven in a test rather than in the field.
 *
 * The reason this file exists is a row of the engine's own menu toolkit: the toolkit dispatches
 * draw, input and activate through a class table with one row per widget type, and a row that no
 * authored screen uses can be taken over. That gives a widget type whose draw, input and activate
 * are ours, called by the engine in its own pipeline. What the engine hands that widget is a
 * rectangle, a pointer position, a click, a key. What this file turns those into is a row.
 *
 * The one rule a scrolling list keeps or loses its user over: the selection follows the row and
 * the window follows the selection. Moving the selection past the last visible row scrolls the
 * window by exactly one; scrolling the window (a wheel, a scrollbar) moves the window and leaves
 * the selection where it is, even out of sight. Both are pinned in the test.
 */
#ifndef MULTIPLAYER_MP_CANVAS_H
#define MULTIPLAYER_MP_CANVAS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The virtual key codes the canvas answers, spelled out so the pure half stays free of windows.h.
 * These are the values WM_KEYDOWN carries and the engine's window procedure passes through to the
 * focused widget as kind 4. */
#define MP_CANVAS_VK_TAB    0x09u
#define MP_CANVAS_VK_PRIOR  0x21u   /* page up */
#define MP_CANVAS_VK_NEXT   0x22u   /* page down */
#define MP_CANVAS_VK_END    0x23u
#define MP_CANVAS_VK_HOME   0x24u
#define MP_CANVAS_VK_UP     0x26u
#define MP_CANVAS_VK_DOWN   0x28u

#define MP_CANVAS_NONE (-1)

typedef struct mp_canvas_list {
    uint32_t rows;         /* how many rows exist */
    uint32_t row_height;   /* one row, in canvas pixels */
    uint32_t header;       /* pixels above the first row: the column titles */
    uint32_t view_height;  /* the canvas rectangle's height */
    uint32_t first;        /* the row at the top of the window */
    int32_t  selected;     /* MP_CANVAS_NONE or a row */
    int32_t  hovered;      /* MP_CANVAS_NONE or a row under the pointer */
} mp_canvas_list_t;

void mp_canvas_list_init(mp_canvas_list_t *list, uint32_t row_height, uint32_t header,
                         uint32_t view_height);

/* The list grew or shrank. The window and the selection are clamped so both still name a row that
 * exists; a selection that fell off the end lands on the new last row rather than on nothing, so a
 * server that just vanished does not leave the keyboard with no place to stand. */
void mp_canvas_list_set_rows(mp_canvas_list_t *list, uint32_t rows);

/* How many rows the window shows at once. At least one, so a window shorter than a row is not a
 * division by zero in every caller. */
uint32_t mp_canvas_list_visible(const mp_canvas_list_t *list);

/* Whether the list is longer than the window, which is when a scrollbar is worth drawing. */
bool mp_canvas_list_scrolls(const mp_canvas_list_t *list);

/* The row under local y, or false when y is on the header, below the rows, or past the window. */
bool mp_canvas_list_row_at(const mp_canvas_list_t *list, int32_t y, uint32_t *row);

/* The top of a row in local pixels, or false when it is scrolled out of the window. */
bool mp_canvas_list_row_top(const mp_canvas_list_t *list, uint32_t row, int32_t *y);

/* The pointer moved to local (x, y); outside the rectangle either coordinate is negative or past
 * the width and height, and then nothing is hovered. */
void mp_canvas_list_hover(mp_canvas_list_t *list, int32_t x, int32_t y, uint32_t width);

/* Selects a row and brings it into the window by the shortest scroll. A row past the end selects
 * nothing. */
void mp_canvas_list_select(mp_canvas_list_t *list, int32_t row);

/* Moves the selection by `delta` rows, clamped at both ends, scrolling by exactly as much as it
 * takes to keep it in view. With nothing selected the first move selects the first visible row. */
void mp_canvas_list_move(mp_canvas_list_t *list, int32_t delta);

/* Moves the WINDOW by `delta` rows, clamped, and leaves the selection where it is. */
void mp_canvas_list_scroll(mp_canvas_list_t *list, int32_t delta);

/* Answers a key: up and down move by one, page up and down by the window, home and end to the
 * ends. True when the key was one of those, so the caller knows whether to play the navigation
 * sound. */
bool mp_canvas_list_key(mp_canvas_list_t *list, uint32_t vk);

/* The scrollbar's thumb: where along the track it starts and how long it is, both as fractions
 * of the track. A list that does not scroll answers a thumb the whole track long, at the top. */
void mp_canvas_list_thumb(const mp_canvas_list_t *list, float *at, float *length);

/* A strip of equal tabs across `width`: which one local x lands on, or `count` when none. */
uint32_t mp_canvas_tab_at(uint32_t count, int32_t x, uint32_t width);

#endif /* MULTIPLAYER_MP_CANVAS_H */
