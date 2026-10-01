/* mp_chat_layout.c: the rows of the chat box, measured with the font's own advance table.
 *
 * The widths the box is laid out in are the ones the drawing module hands in: forty percent of the
 * screen, at most 520 pixels, less the padding on both sides. 640 by 480 gives the narrowest box a
 * shipped mode makes, 1024 by 768 gives 393, and a screen 1300 pixels wide reaches the cap.
 */
#include "unittest.h"

#include "mp_chat_layout.h"
#include "mp_chat_rule.h"
#include "mp_menu_metrics.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The box's inner widths: 640 by 480, 1024 by 768, and anything at the cap. The rows are held to
 * two narrower ones as well, down to the least the layout takes. */
static const int32_t BOX_WIDTHS[] = { 240, 393, 504 };
static const int32_t WIDTHS[]     = { 240, 393, 504, MP_CHAT_LAYOUT_MIN_WIDTH, 100 };

static int32_t measure(const char *text, size_t start, size_t length)
{
    int32_t width = 0;
    size_t  i;

    for (i = start; i < start + length; ++i) {
        width += MP_MENU_ADVANCE_SYSFONT[(unsigned char)text[i] & 0x7Fu];
    }
    return width;
}

/* Every row of every line against the box: nothing wider, the name's colon inside it, and the
 * rows of one line covering its text with nothing but spaces left out between them. */
static bool rows_hold(const mp_chat_layout_line_t *lines, const mp_chat_layout_row_t *rows,
                      size_t count, int32_t width, bool from_start)
{
    size_t k;

    for (k = 0; k < count; ++k) {
        const mp_chat_layout_row_t  *row  = &rows[k];
        const mp_chat_layout_line_t *line = &lines[row->line];
        const int32_t                text_w = measure(line->text, row->text_start,
                                                      row->text_length);
        size_t                       gap;

        if (row->text_x + text_w > width) {
            return false;
        }
        if (row->named &&
            measure(line->name, 0u, row->name_length) + measure(": ", 0u, 2u) != row->text_x) {
            return false;
        }
        if (k > 0u && rows[k - 1u].line == row->line) {
            const mp_chat_layout_row_t *before = &rows[k - 1u];

            if (row->named || row->name_length != 0u || row->text_x != MP_CHAT_LAYOUT_INDENT) {
                return false;
            }
            for (gap = before->text_start + before->text_length; gap < row->text_start; ++gap) {
                if (line->text[gap] != ' ') {
                    return false;
                }
            }
        } else if (from_start && (!row->named || row->text_start != 0u)) {
            return false;
        }
    }
    return true;
}

static void check_a_line_wraps(void)
{
    mp_chat_layout_line_t line;
    mp_chat_layout_row_t  rows[8];
    size_t                count;
    char                  word[130];

    ut_section("a line wraps at a space, and a word too wide for a row is broken");

    line.name   = "Ann";
    line.text   = "hello there";
    line.age_ms = 0u;
    count = mp_chat_layout_rows(&line, 1u, 240, false, rows, 8u);
    ut_check(count == 1u && rows[0].name_length == 3u && rows[0].text_start == 0u &&
                 rows[0].text_length == 11u,
             "a short line is one row, the name and the whole text");
    ut_check(rows[0].text_x == measure("Ann: ", 0u, 5u), "the text starts behind the name's colon");

    /* "Ann: " is 42 pixels and "aaaaa " 55, so the name's row holds three words of the twelve and
     * every row after it, 230 pixels behind the indent, holds four. */
    line.text = "aaaaa aaaaa aaaaa aaaaa aaaaa aaaaa aaaaa aaaaa aaaaa aaaaa aaaaa aaaaa";
    count = mp_chat_layout_rows(&line, 1u, 240, false, rows, 8u);
    ut_checkf(count == 4u, "twelve words take four rows of 240: three, four, four, one (%u)",
              (unsigned)count);
    ut_check(rows_hold(&line, rows, count, 240, true),
             "every row fits, and they break at spaces only");
    ut_check(rows[0].text_length == 17u && rows[1].text_start == 18u &&
                 rows[1].text_length == 23u && rows[3].text_length == 5u,
             "each break falls at the end of a word, and the next row begins with one");

    line.text = "aaaaaaaaaaaaaaaaaaaaa";
    count = mp_chat_layout_rows(&line, 1u, 240, false, rows, 8u);
    ut_check(count == 2u && rows[0].name_length == 3u && rows[0].text_length == 0u &&
                 rows[1].text_length == 21u && rows_hold(&line, rows, count, 240, true),
             "a word that fits a whole row but not the name's row moves to the next one whole");

    memset(word, 'W', 120u);
    word[120] = '\0';
    line.text = word;
    ut_checkf(mp_chat_layout_row_count(&line, 240) == 11u,
              "a word wider than any row is broken: 120 W take eleven rows at 240 (%u)",
              (unsigned)mp_chat_layout_row_count(&line, 240));
    count = mp_chat_layout_rows(&line, 1u, 240, false, rows, 8u);
    ut_check(count == 6u && rows[0].name_length == 0u && rows[0].text_x == MP_CHAT_LAYOUT_INDENT,
             "so the box shows its last six rows, and the name's row is not among them");
    ut_check(rows_hold(&line, rows, count, 240, false), "and none of them is wider than the box");
    ut_check(rows[5].text_start + rows[5].text_length == 120u,
             "and the bottom row ends where the word does");

    line.name = "WWWWWWWWWWWWWWW";
    line.text = "hi";
    count = mp_chat_layout_rows(&line, 1u, MP_CHAT_LAYOUT_MIN_WIDTH, false, rows, 8u);
    ut_check(count >= 1u && rows[0].name_length < 15u &&
                 rows_hold(&line, rows, count, MP_CHAT_LAYOUT_MIN_WIDTH, true),
             "a name wider than the row is cut to it");
    ut_check(mp_chat_layout_rows(&line, 1u, MP_CHAT_LAYOUT_MIN_WIDTH - 1, false, rows, 8u) == 0u &&
                 mp_chat_layout_row_count(&line, MP_CHAT_LAYOUT_MIN_WIDTH - 1) == 0u,
             "a box narrower than the least width lays out nothing");
}

/* Random lines of every printable byte but the backslash, which no line carries, at every width,
 * with random names up to the roster's fifteen. */
static void check_random_lines(void)
{
    mp_chat_layout_line_t lines[6];
    mp_chat_layout_row_t  rows[MP_CHAT_LAYOUT_ROWS];
    char                  texts[6][MP_CHAT_TEXT_MAX + 1u];
    char                  names[6][16];
    unsigned              failures = 0u;
    unsigned              round;
    size_t                w;

    ut_section("no row is ever wider than the box, whatever the text");

    srand(20260929u);
    for (round = 0u; round < 4000u; ++round) {
        size_t i;
        size_t count = 1u + (size_t)(rand() % 6);

        for (i = 0; i < count; ++i) {
            size_t length = (size_t)(rand() % (int)(MP_CHAT_TEXT_MAX + 1u));
            size_t name   = (size_t)(rand() % 16);
            size_t k;

            for (k = 0; k < length; ++k) {
                char c = (char)(0x20 + rand() % 95);

                texts[i][k] = (c == '\\' || rand() % 5 == 0) ? ' ' : c;
            }
            texts[i][length] = '\0';
            for (k = 0; k < name; ++k) {
                names[i][k] = (char)(0x21 + rand() % 94);
            }
            names[i][name] = '\0';
            lines[i].name   = names[i];
            lines[i].text   = texts[i];
            lines[i].age_ms = 0u;
        }
        for (w = 0; w < sizeof WIDTHS / sizeof WIDTHS[0]; ++w) {
            size_t made = mp_chat_layout_rows(lines, count, WIDTHS[w], false, rows,
                                              MP_CHAT_LAYOUT_ROWS);

            if (made > MP_CHAT_LAYOUT_ROWS || !rows_hold(lines, rows, made, WIDTHS[w], false) ||
                (made > 0u && rows[made - 1u].line != count - 1u)) {
                ++failures;
            }
        }
    }
    ut_checkf(failures == 0u, "4000 random boxes at five widths: every row fits, the newest line "
              "is at the bottom, never more than six rows (%u failed)", failures);
}

static void check_six_rows_from_the_bottom(void)
{
    mp_chat_layout_line_t lines[9];
    mp_chat_layout_row_t  rows[10];
    size_t                count;
    size_t                i;

    ut_section("six rows, the newest line at the bottom, the oldest shown losing its top");

    for (i = 0; i < 9u; ++i) {
        lines[i].name   = "Bob";
        lines[i].text   = "hi";
        lines[i].age_ms = (uint32_t)(9u - i) * 100u;
    }
    count = mp_chat_layout_rows(lines, 9u, 240, false, rows, 10u);
    ut_check(count == 6u, "nine short lines show six rows");
    ut_check(rows[0].line == 3u && rows[5].line == 8u, "lines 4 to 9, the newest at the bottom");

    /* Fifty nine letters: nineteen behind the name, twenty three on each row after it. */
    lines[6].text = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    count = mp_chat_layout_rows(lines, 9u, 240, false, rows, 10u);
    ut_check(count == 6u && rows[0].line == 5u && rows[1].line == 6u && rows[3].line == 6u &&
                 rows[5].line == 8u,
             "a line of three rows takes three of the six");
    count = mp_chat_layout_rows(lines, 9u, 240, false, rows, 4u);
    ut_check(count == 4u && rows[0].line == 6u && rows[0].name_length == 0u && rows[3].line == 8u,
             "with room for four the three row line shows its last two, without its name");
    ut_check(mp_chat_layout_rows(lines, 0u, 240, false, rows, 10u) == 0u,
             "no lines, no rows");
    ut_check(mp_chat_layout_rows(NULL, 3u, 240, false, rows, 10u) == 0u &&
                 mp_chat_layout_rows(lines, 3u, 240, false, NULL, 10u) == 0u,
             "and nothing is written without somewhere to read or write");
}

static void check_the_fade(void)
{
    static const struct {
        uint32_t age;
        int      low;
        int      high;
    } table[] = {
        { 0u, 255, 255 }, { 11999u, 255, 255 }, { 12000u, 255, 255 },
        { 12625u, 185, 197 }, { 13250u, 122, 133 }, { 13875u, 58, 70 }, { 14499u, 0, 1 },
    };
    mp_chat_layout_line_t line;
    mp_chat_layout_row_t  rows[2];
    size_t                i;
    int                   last = 256;
    bool                  falling = true;

    ut_section("a line stands twelve seconds and fades out over two and a half");

    line.name = "Ann";
    line.text = "hi";
    for (i = 0; i < sizeof table / sizeof table[0]; ++i) {
        int alpha;

        line.age_ms = table[i].age;
        if (mp_chat_layout_rows(&line, 1u, 240, false, rows, 2u) != 1u) {
            ut_checkf(table[i].high == 0 || table[i].low == 0,
                      "at %u ms a faded line takes no row", (unsigned)table[i].age);
            continue;
        }
        alpha = rows[0].alpha;
        ut_checkf(alpha >= table[i].low && alpha <= table[i].high,
                  "at %u ms the line is drawn at alpha %d", (unsigned)table[i].age, alpha);
        falling = falling && alpha <= last;
        last = alpha;
    }
    ut_check(falling, "and the alpha never rises with age");
    line.age_ms = 14500u;
    ut_check(mp_chat_layout_rows(&line, 1u, 240, false, rows, 2u) == 0u,
             "at 14.5 s the line is gone and takes no row");
    line.age_ms = 600000u;
    ut_check(mp_chat_layout_rows(&line, 1u, 240, true, rows, 2u) == 1u && rows[0].alpha == 255u,
             "while the player types it is shown in full, however old");
}

static void check_the_input_row(void)
{
    char     text[MP_CHAT_TEXT_MAX + 1u];
    unsigned failures = 0u;
    size_t   length;
    size_t   w;

    ut_section("the input row shows the end of the text that fits, and is "
               "never wider than the box");

    memset(text, 'W', MP_CHAT_TEXT_MAX);
    text[MP_CHAT_TEXT_MAX] = '\0';
    for (w = 0; w < sizeof BOX_WIDTHS / sizeof BOX_WIDTHS[0]; ++w) {
        for (length = 0; length <= MP_CHAT_TEXT_MAX; ++length) {
            static const char *const prefixes[] = { "Say:", "Sagen:", "Dire :", "Di':", "Decir:" };
            size_t                   p;

            for (p = 0; p < 5u; ++p) {
                const int32_t box   = BOX_WIDTHS[w];
                size_t        start = mp_chat_layout_input_start(prefixes[p], text, length, box);
                int32_t       fixed = measure(prefixes[p], 0u, strlen(prefixes[p])) +
                                      measure(" _", 0u, 2u);
                int32_t       shown = measure(text, start, length - start);

                if (start > length || fixed + shown > box ||
                    (start > 0u && fixed + shown + measure(text, start - 1u, 1u) <= box)) {
                    ++failures;
                }
            }
        }
    }
    ut_checkf(failures == 0u, "every length of the widest text in the three boxes and the five "
              "prefixes: the row fits and shows as much as fits (%u failed)", failures);
    ut_check(mp_chat_layout_input_start("Say:", "hello", 5u, 240) == 0u,
             "a short text is shown whole");
    ut_check(mp_chat_layout_input_start("Say:", text, MP_CHAT_TEXT_MAX, 240) > 100u,
             "120 W at 640 by 480 shows only its end");
    ut_check(mp_chat_layout_input_start("Say:", NULL, 0u, 240) == 0u, "no text starts at nought");
}

int main(void)
{
    check_a_line_wraps();
    check_random_lines();
    check_six_rows_from_the_bottom();
    check_the_fade();
    check_the_input_row();
    return ut_summary("mp_chat_layout");
}
