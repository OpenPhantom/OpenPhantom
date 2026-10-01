/* mp_chat_rule.c: what a typed byte becomes, what a line may hold, how fast a player may say one,
 * and what a host makes of a say.
 *
 * Every one of the 256 byte values is typed on the Western code page and on another one and held
 * against a table written out here, independently of the one in the module. The bucket is driven
 * through its burst, its refill in one step and a millisecond at a time, the wrap of the clock, a
 * clock that runs backwards and a rest long enough to read as one. The host rule is driven with the
 * buffer size its two callers give it.
 */
#include "unittest.h"

#include "mp_chat_rule.h"
#include "mp_chat_wire.h"
#include "mp_roster.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The Western code page's letters above 0x7E that are written out, and what each becomes. Every
 * other byte above 0x7E becomes a question mark. */
typedef struct spelled {
    uint8_t     byte;
    const char *as;
} spelled_t;

static const spelled_t WESTERN[] = {
    { 0x8Au, "S" },  { 0x8Cu, "OE" }, { 0x8Eu, "Z" },  { 0x9Au, "s" },  { 0x9Cu, "oe" },
    { 0x9Eu, "z" },  { 0x9Fu, "Y" },  { 0xA1u, "!" },  { 0xBFu, "?" },
    { 0xC0u, "A" },  { 0xC1u, "A" },  { 0xC2u, "A" },  { 0xC3u, "A" },  { 0xC4u, "Ae" },
    { 0xC5u, "A" },  { 0xC6u, "AE" }, { 0xC7u, "C" },  { 0xC8u, "E" },  { 0xC9u, "E" },
    { 0xCAu, "E" },  { 0xCBu, "E" },  { 0xCCu, "I" },  { 0xCDu, "I" },  { 0xCEu, "I" },
    { 0xCFu, "I" },  { 0xD1u, "N" },  { 0xD2u, "O" },  { 0xD3u, "O" },  { 0xD4u, "O" },
    { 0xD5u, "O" },  { 0xD6u, "Oe" }, { 0xD8u, "O" },  { 0xD9u, "U" },  { 0xDAu, "U" },
    { 0xDBu, "U" },  { 0xDCu, "Ue" }, { 0xDDu, "Y" },  { 0xDFu, "ss" },
    { 0xE0u, "a" },  { 0xE1u, "a" },  { 0xE2u, "a" },  { 0xE3u, "a" },  { 0xE4u, "ae" },
    { 0xE5u, "a" },  { 0xE6u, "ae" }, { 0xE7u, "c" },  { 0xE8u, "e" },  { 0xE9u, "e" },
    { 0xEAu, "e" },  { 0xEBu, "e" },  { 0xECu, "i" },  { 0xEDu, "i" },  { 0xEEu, "i" },
    { 0xEFu, "i" },  { 0xF1u, "n" },  { 0xF2u, "o" },  { 0xF3u, "o" },  { 0xF4u, "o" },
    { 0xF5u, "o" },  { 0xF6u, "oe" }, { 0xF8u, "o" },  { 0xF9u, "u" },  { 0xFAu, "u" },
    { 0xFBu, "u" },  { 0xFCu, "ue" }, { 0xFDu, "y" },  { 0xFFu, "y" },
};

#define ANOTHER_CODE_PAGE 1250u   /* Central European: 0xB3 is a letter there, not a 3 */

/* What the test expects a byte to become, from the rule as the header states it: nothing for a
 * control character and DEL, a slash for a backslash, itself for the rest of ASCII, and above
 * that the table on the Western code page and a question mark on any other. */
static const char *expected(unsigned byte, uint32_t code_page)
{
    static char single[2];
    size_t      i;

    if (byte < 0x20u || byte == 0x7Fu) {
        return "";
    }
    if (byte == (unsigned)'\\') {
        return "/";
    }
    if (byte < 0x7Fu) {
        single[0] = (char)byte;
        single[1] = '\0';
        return single;
    }
    if (code_page != MP_CHAT_CODE_PAGE_WESTERN) {
        return "?";
    }
    for (i = 0; i < sizeof WESTERN / sizeof WESTERN[0]; ++i) {
        if (WESTERN[i].byte == byte) {
            return WESTERN[i].as;
        }
    }
    return "?";
}

static void check_every_typed_byte(uint32_t code_page)
{
    unsigned byte;
    unsigned wrong = 0u;
    unsigned spelled = 0u;

    for (byte = 0u; byte <= 0xFFu; ++byte) {
        char        line[MP_CHAT_TEXT_MAX + 1u];
        const char *want = expected(byte, code_page);
        size_t      got;

        memset(line, '#', sizeof line);
        memcpy(line, "ab", 2u);
        got = mp_chat_rule_append_typed(line, 2u, MP_CHAT_TEXT_MAX, (uint8_t)byte, code_page);
        if (got != strlen(want) || memcmp(line + 2, want, got) != 0 || line[2u + got] != '\0' ||
            memcmp(line, "ab", 2u) != 0) {
            ++wrong;
        }
        spelled += got > 1u ? 1u : 0u;
    }
    ut_checkf(wrong == 0u, "code page %u: each of the 256 bytes becomes what the table says, "
              "with a terminator behind it (%u wrong)", (unsigned)code_page, wrong);
    if (code_page == MP_CHAT_CODE_PAGE_WESTERN) {
        ut_checkf(spelled == 11u,
                  "and eleven of them take two bytes: the umlauts, the sharp s and the "
                  "ligatures (%u)", spelled);
    } else {
        ut_checkf(spelled == 0u, "and none takes two bytes there (%u)", spelled);
    }
}

static void check_typing(void)
{
    char   line[4u + 1u];
    size_t length = 0u;

    ut_section("what a typed byte becomes");
    check_every_typed_byte(MP_CHAT_CODE_PAGE_WESTERN);
    check_every_typed_byte(ANOTHER_CODE_PAGE);
    check_every_typed_byte(0u);

    memset(line, '#', sizeof line);
    length += mp_chat_rule_append_typed(line, length, 4u, 'G', MP_CHAT_CODE_PAGE_WESTERN);
    length += mp_chat_rule_append_typed(line, length, 4u, 'r', MP_CHAT_CODE_PAGE_WESTERN);
    length += mp_chat_rule_append_typed(line, length, 4u, 0xFCu, MP_CHAT_CODE_PAGE_WESTERN);
    ut_check(length == 4u && memcmp(line, "Grue", 5u) == 0,
             "a transliteration fills the field to its capacity, terminator behind it");
    ut_check(mp_chat_rule_append_typed(line, 4u, 4u, 'x', MP_CHAT_CODE_PAGE_WESTERN) == 0u &&
                 line[4] == '\0',
             "a full field takes nothing more");
    ut_check(mp_chat_rule_append_typed(line, 3u, 4u, 0xDFu, MP_CHAT_CODE_PAGE_WESTERN) == 0u &&
                 memcmp(line, "Gru", 4u) == 0,
             "and a transliteration that no longer fits whole takes nothing, not half of it");
    ut_check(mp_chat_rule_append_typed(line, 3u, 4u, '!', MP_CHAT_CODE_PAGE_WESTERN) == 1u &&
                 memcmp(line, "Gru!", 5u) == 0,
             "while one byte still fits in the one place left");
    line[4] = '#';
    ut_check(mp_chat_rule_append_typed(line, 5u, 4u, 'x', MP_CHAT_CODE_PAGE_WESTERN) == 0u &&
                 line[4] == '#',
             "a length past the capacity writes nothing at all");
    ut_check(mp_chat_rule_append_typed(NULL, 0u, 4u, 'x', MP_CHAT_CODE_PAGE_WESTERN) == 0u,
             "and no field takes nothing");
}

static void check_trimming(void)
{
    char   line[16];
    size_t length;

    ut_section("the spaces at both ends");
    memcpy(line, "  a b  ", 8u);
    length = mp_chat_rule_trim(line, 7u);
    ut_check(length == 3u && memcmp(line, "a b", 4u) == 0,
             "both ends go, the space inside stays, and a terminator stands behind");
    memcpy(line, "   ", 4u);
    ut_check(mp_chat_rule_trim(line, 3u) == 0u && line[0] == '\0',
             "a line of spaces is nothing to say");
    memcpy(line, "abc#", 4u);
    ut_check(mp_chat_rule_trim(line, 3u) == 3u && line[3] == '#',
             "a line with nothing to trim is left alone, and nothing is written behind it");
    memcpy(line, "  xy", 4u);
    ut_check(mp_chat_rule_trim(line, 4u) == 2u && memcmp(line, "xy", 3u) == 0,
             "leading spaces alone");
    memcpy(line, "xy  #", 5u);
    ut_check(mp_chat_rule_trim(line, 4u) == 2u && memcmp(line, "xy", 3u) == 0 && line[4] == '#',
             "trailing spaces alone, and the terminator lands inside the old length");
    ut_check(mp_chat_rule_trim(line, 0u) == 0u && mp_chat_rule_trim(NULL, 3u) == 0u,
             "an empty line and no line are nothing");
}

static void check_sound_text(void)
{
    char     text[MP_CHAT_TEXT_MAX + 2u];
    unsigned byte;
    unsigned wrong = 0u;

    ut_section("what a line may hold");
    memset(text, 'a', sizeof text);
    ut_check(!mp_chat_rule_text_is_sound(text, 0u), "not nothing");
    ut_check(mp_chat_rule_text_is_sound(text, 1u) &&
                 mp_chat_rule_text_is_sound(text, MP_CHAT_TEXT_MAX),
             "one byte up to the most a line carries");
    ut_check(!mp_chat_rule_text_is_sound(text, MP_CHAT_TEXT_MAX + 1u), "and not one more");
    for (byte = 0u; byte <= 0xFFu; ++byte) {
        bool sound = byte >= 0x20u && byte <= 0x7Eu && byte != (unsigned)'\\';

        text[3] = (char)byte;
        if (mp_chat_rule_text_is_sound(text, 8u) != sound) {
            ++wrong;
        }
    }
    ut_checkf(wrong == 0u, "only 0x20..0x7E without the backslash (%u wrong)", wrong);
    ut_check(!mp_chat_rule_text_is_sound(NULL, 3u), "and no text is not a line");
}

/* How many lines a bucket gives when asked every `step` milliseconds from `from` for `span`. */
static unsigned taken_over(uint32_t from, uint32_t span, uint32_t step, uint32_t burst,
                           uint32_t every)
{
    mp_chat_pace_t pace;
    uint32_t       t;
    unsigned       taken = 0u;

    memset(&pace, 0, sizeof pace);
    for (t = 0u; t < span; t += step) {
        taken += mp_chat_pace_take(&pace, from + t, burst, every) ? 1u : 0u;
    }
    return taken;
}

static void check_the_bucket(void)
{
    mp_chat_pace_t pace;
    unsigned       i;
    unsigned       taken = 0u;

    ut_section("the bucket");
    memset(&pace, 0, sizeof pace);
    for (i = 0u; i < 4u; ++i) {
        taken += mp_chat_pace_take(&pace, 5000u, 3u, 1000u) ? 1u : 0u;
    }
    ut_checkf(taken == 3u, "a burst of three at once, and the fourth waits (%u)", taken);
    ut_check(!mp_chat_pace_take(&pace, 5999u, 3u, 1000u), "a millisecond short of a second, none");
    ut_check(mp_chat_pace_take(&pace, 6000u, 3u, 1000u), "a second later, one");
    ut_check(!mp_chat_pace_take(&pace, 6000u, 3u, 1000u), "and only one");
    taken = 0u;
    for (i = 0u; i < 5u; ++i) {
        taken += mp_chat_pace_take(&pace, 60000u, 3u, 1000u) ? 1u : 0u;
    }
    ut_checkf(taken == 3u, "a long silence fills the bucket to its burst and no further (%u)",
              taken);

    taken = taken_over(0u, 7000u, 1u, 5u, 700u);
    ut_checkf(taken == 5u + 9u,
              "asked every millisecond for seven seconds at five and one per 700 ms: 14 lines, "
              "none lost to rounding (%u)", taken);
    taken = taken_over(0u, 10000u, 3u, 3u, 1000u);
    ut_checkf(taken == 3u + 9u, "asked every three milliseconds for ten seconds: 12 (%u)", taken);
    ut_checkf(taken_over(0xFFFFF000u, 7000u, 1u, 5u, 700u) == 14u,
              "the same across the wrap of the clock (%u)",
              taken_over(0xFFFFF000u, 7000u, 1u, 5u, 700u));

    memset(&pace, 0, sizeof pace);
    for (i = 0u; i < 5u; ++i) {
        (void)mp_chat_pace_take(&pace, 100000u, 5u, 700u);
    }
    ut_check(!mp_chat_pace_take(&pace, 90000u, 5u, 700u),
             "a clock that runs backwards fills nothing");
    ut_check(!mp_chat_pace_take(&pace, 90699u, 5u, 700u) &&
                 mp_chat_pace_take(&pace, 90700u, 5u, 700u),
             "and the bucket fills again from the new time, not only once the clock is back past "
             "the old mark");

    /* A seat that rested past half the clock's range reads as a clock that ran backwards. Left
     * anchored at its old mark it would stay shut for up to another 24.8 days. */
    memset(&pace, 0, sizeof pace);
    for (i = 0u; i < 5u; ++i) {
        (void)mp_chat_pace_take(&pace, 5u, 5u, 700u);
    }
    ut_check(!mp_chat_pace_take(&pace, 5u + 0x80000000u + 10u, 5u, 700u),
             "after a rest of more than 24.8 days the first ask fills nothing");
    ut_check(mp_chat_pace_take(&pace, 5u + 0x80000000u + 710u, 5u, 700u),
             "and 700 ms later the bucket gives a line again");

    memset(&pace, 0, sizeof pace);
    ut_check(mp_chat_pace_take(&pace, 0u, 1u, 1000u) && !mp_chat_pace_take(&pace, 0u, 1u, 1000u),
             "a bucket of one gives one");
    mp_chat_pace_give_back(&pace, 1u, 1000u);
    ut_check(mp_chat_pace_take(&pace, 0u, 1u, 1000u), "a line given back can be taken again");
    mp_chat_pace_give_back(&pace, 1u, 1000u);
    mp_chat_pace_give_back(&pace, 1u, 1000u);
    ut_check(mp_chat_pace_take(&pace, 0u, 1u, 1000u) && !mp_chat_pace_take(&pace, 0u, 1u, 1000u),
             "and giving back never holds more than the burst");
    ut_check(!mp_chat_pace_take(NULL, 0u, 3u, 1000u), "no bucket gives nothing");
}

/* A say as a player's side encodes it. */
static size_t say_bytes(uint8_t *buffer, uint8_t serial, const char *text)
{
    mp_chat_say_note_t say;

    memset(&say, 0, sizeof say);
    say.serial = serial;
    say.length = (uint8_t)strlen(text);
    memcpy(say.text, text, say.length);
    return mp_chat_say_encode(&say, buffer, MP_CHAT_SAY_MAX_BYTES);
}

static void check_the_host_rule(void)
{
    static mp_chat_host_t host;
    uint8_t               say[MP_CHAT_SAY_MAX_BYTES];
    uint8_t               line[MP_CHAT_LINE_MAX_BYTES];
    size_t                bytes;
    size_t                line_bytes = 0;
    mp_chat_line_note_t   heard;
    unsigned              i;
    unsigned              lines = 0u;
    unsigned              too_fast = 0u;

    ut_section("what a host makes of a say");
    memset(&host, 0, sizeof host);

    bytes = say_bytes(say, 7u, "  hello there ");
    ut_check(mp_chat_rule_host_line(&host, say, bytes, 2u, "Ann", 1000u, line, sizeof line,
                                    &line_bytes) == MP_CHAT_HOST_LINE,
             "a player's say becomes a line");
    ut_check(mp_chat_line_decode(line, line_bytes, &heard) && heard.slot == 2u &&
                 heard.serial == 7u && strcmp(heard.name, "Ann") == 0 &&
                 heard.length == 11u && strcmp(heard.text, "hello there") == 0,
             "naming the slot and the name the host gave it, the serial it said, the text trimmed");
    ut_check(host.counts.taken == 1u && host.counts.said_here == 0u, "counted as taken");

    bytes = say_bytes(say, 1u, "mine");
    ut_check(mp_chat_rule_host_line(&host, say, bytes, MP_CHAT_HOST_SLOT, "Host", 1000u, line,
                                    sizeof line, &line_bytes) == MP_CHAT_HOST_LINE &&
                 host.counts.said_here == 1u && host.counts.taken == 1u,
             "a line for the listen host's own slot is counted as said here");

    ut_check(mp_chat_rule_host_line(&host, say, bytes, MP_CHAT_SLOTS, "Ann", 1000u, line,
                                    sizeof line, &line_bytes) == MP_CHAT_HOST_NO_SEAT &&
                 host.counts.no_seat == 1u,
             "a say from a slot past the table is refused and counted");
    ut_check(mp_chat_rule_host_line(&host, say, bytes - 1u, 2u, "Ann", 1000u, line, sizeof line,
                                    &line_bytes) == MP_CHAT_HOST_UNSOUND &&
                 host.counts.unsound == 1u,
             "a torn say is refused as unsound");
    bytes = say_bytes(say, 2u, "     ");
    ut_check(bytes != 0u && mp_chat_rule_host_line(&host, say, bytes, 2u, "Ann", 1000u, line,
                                                   sizeof line, &line_bytes) ==
                                MP_CHAT_HOST_UNSOUND &&
                 host.counts.unsound == 2u,
             "as is a say of spaces alone, which the wire carries and the trim empties");
    bytes = say_bytes(say, 2u, "short buffer");
    ut_check(mp_chat_rule_host_line(&host, say, bytes, 2u, "Ann", 1000u, line,
                                    MP_CHAT_LINE_MAX_BYTES - 1u, &line_bytes) ==
                 MP_CHAT_HOST_UNSOUND,
             "and a line buffer short of the longest line is refused before the bucket is asked");
    for (i = 0u; i < 5u; ++i) {
        lines += mp_chat_rule_host_line(&host, say, bytes, 2u, "Ann", 1000u, line, sizeof line,
                                        &line_bytes) == MP_CHAT_HOST_LINE
                     ? 1u
                     : 0u;
    }
    ut_checkf(lines == 4u,
              "none of the refusals cost slot 2 a line: of five more at once four go, one having "
              "gone before (%u)", lines);
    lines = 0u;
    too_fast = host.counts.too_fast;

    bytes = say_bytes(say, 3u, "who am I");
    ut_check(mp_chat_rule_host_line(&host, say, bytes, 4u, NULL, 1000u, line, sizeof line,
                                    &line_bytes) == MP_CHAT_HOST_LINE &&
                 mp_chat_line_decode(line, line_bytes, &heard) &&
                 strcmp(heard.name, "Player") == 0,
             "a slot with no name is called what the roster calls it");
    ut_check(mp_chat_rule_host_line(&host, say, bytes, 5u, "Ma\\ce", 1000u, line, sizeof line,
                                    &line_bytes) == MP_CHAT_HOST_LINE &&
                 mp_chat_line_decode(line, line_bytes, &heard) &&
                 strcmp(heard.name, "Ma?ce") == 0,
             "and a name the roster would refuse is cleaned the roster's way");

    for (i = 0u; i < 7u; ++i) {
        bytes = say_bytes(say, (uint8_t)(10u + i), "again");
        lines += mp_chat_rule_host_line(&host, say, bytes, 6u, "Bob", 2000u, line, sizeof line,
                                        &line_bytes) == MP_CHAT_HOST_LINE
                     ? 1u
                     : 0u;
    }
    too_fast = host.counts.too_fast - too_fast;
    ut_checkf(lines == MP_CHAT_PACE_HOST_BURST && too_fast == 2u,
              "seven at once from one slot: the host's burst of five, two refused and counted "
              "(%u, %u)", lines, too_fast);
    ut_check(mp_chat_rule_host_line(&host, say, bytes, 7u, "Cy", 2000u, line, sizeof line,
                                    &line_bytes) == MP_CHAT_HOST_LINE,
             "while another slot has a bucket of its own");
    ut_check(mp_chat_rule_host_line(&host, say, bytes, 6u, "Bob", 2699u, line, sizeof line,
                                    &line_bytes) == MP_CHAT_HOST_TOO_FAST &&
                 mp_chat_rule_host_line(&host, say, bytes, 6u, "Bob", 2700u, line, sizeof line,
                                        &line_bytes) == MP_CHAT_HOST_LINE,
             "and the full slot gets one again after 700 ms");

    memset(&host.counts, 0, sizeof host.counts);
    mp_chat_rule_host_sent(&host, 3u, 2u);
    mp_chat_rule_host_sent(&host, 2u, 2u);
    mp_chat_rule_host_sent(&host, 0u, 0u);
    ut_check(host.counts.lines_out == 3u && host.counts.copies == 4u &&
                 host.counts.copies_unsent == 1u,
             "three lines out: four copies reached a player and one connected player was missed");
}

int main(void)
{
    check_typing();
    check_trimming();
    check_sound_text();
    check_the_bucket();
    check_the_host_rule();
    return ut_summary("mp_chat_rule");
}
