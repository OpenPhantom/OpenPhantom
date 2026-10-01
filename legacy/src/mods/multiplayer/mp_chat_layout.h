/* mp_chat_layout.h: where the rows of the chat box go, as arithmetic with no engine in it.
 *
 * Layer 1, pure. The box is drawn in the engine's built in font at the size it was drawn for, so a
 * width here is a width in pixels; it is measured with the font's own advance table, the same one
 * the scoreboard measures with. The engine's text drawer wraps on its own at the right edge of the
 * screen and carries the rest to the left edge, under the box, so the box never hands it a row
 * wider than itself: every line is wrapped here first.
 *
 * A line reads `Name: text`. Its first row carries the name and as much of the text as fits after
 * it; the rows after that are indented a little, so that where one line ends and the next begins
 * stays visible, and break at the last space that fits. A word wider than a whole row is broken
 * where the row ends. A name wider than a row is cut to the row, which only a very narrow screen
 * and a very wide name can make happen.
 *
 * The box shows six rows, the newest line at the bottom, and a line too tall for what is left at
 * the top shows its last rows. A line stands fully for twelve seconds and then fades out over two
 * and a half; a line that has faded takes no row. While the player types every line is shown in
 * full again, faded or not, which is how one reads what one is answering.
 */
#ifndef MULTIPLAYER_MP_CHAT_LAYOUT_H
#define MULTIPLAYER_MP_CHAT_LAYOUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* How many rows of lines the box shows. */
#define MP_CHAT_LAYOUT_ROWS 6u

/* How long a line stands before it fades, and how long the fade takes. */
#define MP_CHAT_LAYOUT_SHOW_MS 12000u
#define MP_CHAT_LAYOUT_FADE_MS 2500u

/* How far in a continued row starts, in pixels. */
#define MP_CHAT_LAYOUT_INDENT 10

/* The narrowest box that lays anything out. Below it a row may not hold one glyph after the
 * indent, and a row that cannot move forward cannot be wrapped. */
#define MP_CHAT_LAYOUT_MIN_WIDTH 64

typedef struct mp_chat_layout_line {
    const char *name;     /* terminated; NULL reads as empty */
    const char *text;     /* terminated; NULL reads as empty */
    uint32_t    age_ms;   /* how long this machine has shown the line */
} mp_chat_layout_line_t;

/* One row, top to bottom. The name, when the row carries one, is drawn at the row's left edge with
 * a colon behind it; the text from `text_x` on. */
typedef struct mp_chat_layout_row {
    size_t  line;           /* which of the lines given */
    bool    named;          /* the line's first row, which carries its name and colon */
    size_t  name_length;    /* bytes of the name on this row; 0 on a continued row */
    size_t  text_start;     /* the first byte of the line's text on this row */
    size_t  text_length;    /* how many bytes of it */
    int32_t text_x;         /* where the text starts, in pixels from the row's left edge */
    uint8_t alpha;          /* 255 is opaque */
} mp_chat_layout_row_t;

/* How far `length` bytes of `text` carry the pen, in pixels. A byte outside the printable ASCII
 * range is none; no line holds one. */
int32_t mp_chat_layout_width(const char *text, size_t length);

/* How many rows `line` takes in a box `width` pixels wide, fading ignored. 0 for a box narrower
 * than MP_CHAT_LAYOUT_MIN_WIDTH. */
size_t mp_chat_layout_row_count(const mp_chat_layout_line_t *line, int32_t width);

/* The rows of `lines`, oldest first in the array, that the box shows: at most `capacity` and at
 * most MP_CHAT_LAYOUT_ROWS, top to bottom, the newest line's last row at the bottom. `typing`
 * shows every line in full. Answers how many rows were written. */
size_t mp_chat_layout_rows(const mp_chat_layout_line_t *lines, size_t count, int32_t width,
                           bool typing, mp_chat_layout_row_t *rows, size_t capacity);

/* The input row: `prefix`, a space, the end of the typed text and a cursor. Answers the first byte
 * of the typed text to draw, so that the four together fit `width`; the start of a text too long
 * for the row scrolls out to the left, and the end being typed stays in view. */
size_t mp_chat_layout_input_start(const char *prefix, const char *text, size_t length,
                                  int32_t width);

#endif /* MULTIPLAYER_MP_CHAT_LAYOUT_H */
