/* mp_chat_layout.c: the rows of the chat box. See the header. */
#include "mp_chat_layout.h"

#include "mp_chat_rule.h"
#include "mp_menu_metrics.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* What follows a name on its row, and what ends the input row. */
static const char NAME_TAIL[] = ": ";
static const char CURSOR[]    = "_";

/* Every row moves at least one byte forward, and a text is at most MP_CHAT_TEXT_MAX bytes, so no
 * line takes more rows than this. */
#define ROWS_PER_LINE_MAX (MP_CHAT_TEXT_MAX + 1u)

/* No position a row can end at. */
#define NO_SPACE ((size_t)-1)

static int32_t advance(char c)
{
    const unsigned char glyph = (unsigned char)c;

    return glyph < 128u ? (int32_t)MP_MENU_ADVANCE_SYSFONT[glyph] : 0;
}

int32_t mp_chat_layout_width(const char *text, size_t length)
{
    int32_t width = 0;
    size_t  i;

    if (text == NULL) {
        return 0;
    }
    for (i = 0; i < length && text[i] != '\0'; ++i) {
        width += advance(text[i]);
    }
    return width;
}

/* Full for the time a line stands, then down to nought over the fade. Typing shows everything. */
static uint8_t alpha_of(uint32_t age_ms, bool typing)
{
    uint32_t left;

    if (typing || age_ms < MP_CHAT_LAYOUT_SHOW_MS) {
        return 255u;
    }
    if (age_ms - MP_CHAT_LAYOUT_SHOW_MS >= MP_CHAT_LAYOUT_FADE_MS) {
        return 0u;
    }
    left = MP_CHAT_LAYOUT_FADE_MS - (age_ms - MP_CHAT_LAYOUT_SHOW_MS);
    return (uint8_t)((left * 255u + MP_CHAT_LAYOUT_FADE_MS / 2u) / MP_CHAT_LAYOUT_FADE_MS);
}

/* As much of the name as fits a row with its colon, and where the text starts behind it. */
static size_t name_fit(const char *name, int32_t width, int32_t *text_x)
{
    const int32_t tail   = mp_chat_layout_width(NAME_TAIL, sizeof NAME_TAIL - 1u);
    size_t        length = strlen(name);

    while (length > 0u && mp_chat_layout_width(name, length) + tail > width) {
        --length;
    }
    *text_x = mp_chat_layout_width(name, length) + tail;
    return length;
}

/* Where the row that starts at `pos` ends, with `room` pixels for it; `next` receives where the
 * next row starts. The break is at the last space that fits, and the spaces around it belong to
 * neither row. With no space to break at, the row after a name hands its first word to the next
 * row when that word fits a whole row there, and every other row breaks the word where it ends. */
static size_t row_end(const char *text, size_t pos, size_t length, int32_t room, bool named,
                      int32_t full_room, size_t *next)
{
    int32_t used  = 0;
    size_t  space = NO_SPACE;
    size_t  i     = pos;
    size_t  word;

    while (i < length && used + advance(text[i]) <= room) {
        used += advance(text[i]);
        if (text[i] == ' ') {
            space = i;
        }
        ++i;
    }
    if (i >= length || text[i] == ' ') {
        *next = i;
    } else if (space != NO_SPACE && space > pos) {
        *next = i = space;
    } else if (named) {
        word = pos;
        while (word < length && text[word] != ' ') {
            ++word;
        }
        if (i == pos || mp_chat_layout_width(text + pos, word - pos) <= full_room) {
            i = pos;
        }
        *next = i;
    } else {
        if (i == pos) {
            ++i;   /* a row that cannot hold one glyph still moves on */
        }
        *next = i;
    }
    while (i > pos && text[i - 1u] == ' ') {
        --i;
    }
    return i;
}

/* Every row of one line, written while there is room in `rows`; answers how many it takes. */
static size_t wrap(const mp_chat_layout_line_t *line, size_t index, int32_t width,
                   mp_chat_layout_row_t *rows, size_t capacity)
{
    const char *text   = line->text != NULL ? line->text : "";
    const char *name   = line->name != NULL ? line->name : "";
    size_t      length = strnlen(text, MP_CHAT_TEXT_MAX);
    size_t      pos    = 0u;
    size_t      count  = 0u;
    int32_t     text_x = 0;
    size_t      name_length = name_fit(name, width, &text_x);
    bool        named  = true;

    while (named || pos < length) {
        const int32_t x = named ? text_x : MP_CHAT_LAYOUT_INDENT;
        size_t        next = length;
        size_t        end;

        if (!named) {
            while (pos < length && text[pos] == ' ') {
                ++pos;
            }
            if (pos >= length) {
                break;
            }
        }
        end = row_end(text, pos, length, width - x, named, width - MP_CHAT_LAYOUT_INDENT, &next);
        if (rows != NULL && count < capacity) {
            rows[count].line        = index;
            rows[count].named       = named;
            rows[count].name_length = named ? name_length : 0u;
            rows[count].text_start  = pos;
            rows[count].text_length = end - pos;
            rows[count].text_x      = x;
            rows[count].alpha       = 255u;
        }
        ++count;
        pos   = next;
        named = false;
    }
    return count;
}

size_t mp_chat_layout_row_count(const mp_chat_layout_line_t *line, int32_t width)
{
    if (line == NULL || width < MP_CHAT_LAYOUT_MIN_WIDTH) {
        return 0u;
    }
    return wrap(line, 0u, width, NULL, 0u);
}

size_t mp_chat_layout_rows(const mp_chat_layout_line_t *lines, size_t count, int32_t width,
                           bool typing, mp_chat_layout_row_t *rows, size_t capacity)
{
    mp_chat_layout_row_t line_rows[ROWS_PER_LINE_MAX];
    const size_t         wanted = capacity < MP_CHAT_LAYOUT_ROWS ? capacity : MP_CHAT_LAYOUT_ROWS;
    size_t               taken = 0u;
    size_t               oldest = count;
    size_t               cut = 0u;
    size_t               written = 0u;
    size_t               i;

    if (lines == NULL || rows == NULL || width < MP_CHAT_LAYOUT_MIN_WIDTH) {
        return 0u;
    }
    /* From the newest line up, until the rows are spent; the oldest line shown may lose its top. */
    for (i = count; i > 0u && taken < wanted; --i) {
        size_t need;

        if (alpha_of(lines[i - 1u].age_ms, typing) == 0u) {
            continue;
        }
        need   = wrap(&lines[i - 1u], i - 1u, width, NULL, 0u);
        oldest = i - 1u;
        if (taken + need > wanted) {
            cut   = taken + need - wanted;
            taken = wanted;
            break;
        }
        taken += need;
    }
    for (i = oldest; i < count && written < taken; ++i) {
        const uint8_t alpha = alpha_of(lines[i].age_ms, typing);
        size_t        made;
        size_t        k;

        if (alpha == 0u) {
            continue;
        }
        made = wrap(&lines[i], i, width, line_rows, ROWS_PER_LINE_MAX);
        for (k = i == oldest ? cut : 0u; k < made && written < taken; ++k) {
            rows[written]       = line_rows[k];
            rows[written].alpha = alpha;
            ++written;
        }
    }
    return written;
}

size_t mp_chat_layout_input_start(const char *prefix, const char *text, size_t length,
                                  int32_t width)
{
    const int32_t room = width -
                         mp_chat_layout_width(prefix, prefix != NULL ? strlen(prefix) : 0u) -
                         mp_chat_layout_width(" ", 1u) -
                         mp_chat_layout_width(CURSOR, sizeof CURSOR - 1u);
    int32_t       used  = 0;
    size_t        start = length;

    if (text == NULL) {
        return 0u;
    }
    while (start > 0u && used + advance(text[start - 1u]) <= room) {
        used += advance(text[start - 1u]);
        --start;
    }
    return start;
}
