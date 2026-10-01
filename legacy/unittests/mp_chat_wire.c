/* mp_chat_wire.c: the say and the line, whole or refused whole.
 *
 * Every text length from one byte to the most a line carries goes there and back, one byte more
 * and one byte less are refused, and every one of the 256 byte values is tried inside a text,
 * with the encoder and the decoder asked the same question. Random bytes behind each tag are
 * decoded out of a buffer whose last byte is the last one of a readable page, so a decoder that
 * reads one byte past the message stops the test with an access violation instead of reading
 * whatever lies behind it. Whatever a decoder accepts, its encoder says again byte for byte.
 */
#include "unittest.h"

#include "mp_channel.h"
#include "mp_chat_rule.h"
#include "mp_chat_wire.h"
#include "mp_roster.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* One message of the reliable channel, the room a sender really hands an encoder. */
static uint8_t s_buffer[MP_CHANNEL_MESSAGE_BYTES];

static uint32_t s_seed = 0x6C8E9CF5u;

static uint32_t next_random(void)
{
    s_seed ^= s_seed << 13;
    s_seed ^= s_seed >> 17;
    s_seed ^= s_seed << 5;
    return s_seed;
}

/* A text a line may carry, of `length` bytes, walking the printable range without the backslash. */
static void sound_text(char *text, size_t length, unsigned start)
{
    size_t   i;
    unsigned c = 0x20u + start % 0x5Fu;

    for (i = 0; i < length; ++i) {
        if (c == (unsigned)'\\') {
            c = '/';
        }
        text[i] = (char)c;
        c = c >= 0x7Eu ? 0x20u : c + 1u;
    }
    text[length] = '\0';
}

static void say_of(mp_chat_say_note_t *say, uint8_t serial, size_t length, unsigned start)
{
    memset(say, 0, sizeof *say);
    say->serial = serial;
    say->length = (uint8_t)length;
    sound_text(say->text, length, start);
}

static void line_of(mp_chat_line_note_t *line, uint8_t slot, const char *name, size_t length)
{
    memset(line, 0, sizeof *line);
    line->slot   = slot;
    line->serial = 200u;
    strncpy(line->name, name, sizeof line->name - 1u);
    line->length = (uint8_t)length;
    sound_text(line->text, length, 7u);
}

static bool same_say(const mp_chat_say_note_t *a, const mp_chat_say_note_t *b)
{
    return a->serial == b->serial && a->length == b->length &&
           memcmp(a->text, b->text, a->length) == 0 && b->text[b->length] == '\0';
}

static bool same_line(const mp_chat_line_note_t *a, const mp_chat_line_note_t *b)
{
    return a->slot == b->slot && a->serial == b->serial &&
           strncmp(a->name, b->name, sizeof a->name) == 0 && a->length == b->length &&
           memcmp(a->text, b->text, a->length) == 0 && b->text[b->length] == '\0';
}

static void check_every_length(void)
{
    size_t   length;
    unsigned wrong = 0u;
    unsigned tight = 0u;

    ut_section("every text length from one byte to the most a line carries, there and back");
    for (length = 1u; length <= MP_CHAT_TEXT_MAX; ++length) {
        mp_chat_say_note_t  say;
        mp_chat_say_note_t  said;
        mp_chat_line_note_t line;
        mp_chat_line_note_t heard;
        size_t              bytes;

        say_of(&say, (uint8_t)(1u + length % 255u), length, (unsigned)length);
        bytes = mp_chat_say_encode(&say, s_buffer, sizeof s_buffer);
        if (bytes != MP_CHAT_SAY_HEAD_BYTES + length || !mp_chat_is_say(s_buffer, bytes) ||
            !mp_chat_say_decode(s_buffer, bytes, &said) || !same_say(&say, &said) ||
            mp_chat_say_decode(s_buffer, bytes - 1u, &said) ||
            mp_chat_say_decode(s_buffer, bytes + 1u, &said)) {
            ++wrong;
        }
        if (mp_chat_say_encode(&say, s_buffer, bytes) != bytes ||
            mp_chat_say_encode(&say, s_buffer, bytes - 1u) != 0u) {
            ++tight;
        }

        line_of(&line, (uint8_t)(length % MP_CHAT_SLOTS), "Qui-Gon Jinn", length);
        bytes = mp_chat_line_encode(&line, s_buffer, sizeof s_buffer);
        if (bytes != MP_CHAT_LINE_HEAD_BYTES + length || !mp_chat_is_line(s_buffer, bytes) ||
            !mp_chat_line_decode(s_buffer, bytes, &heard) || !same_line(&line, &heard) ||
            mp_chat_line_decode(s_buffer, bytes - 1u, &heard) ||
            mp_chat_line_decode(s_buffer, bytes + 1u, &heard)) {
            ++wrong;
        }
        if (mp_chat_line_encode(&line, s_buffer, bytes) != bytes ||
            mp_chat_line_encode(&line, s_buffer, bytes - 1u) != 0u) {
            ++tight;
        }
    }
    ut_checkf(wrong == 0u,
              "a say and a line of every length 1..%u come back as they went, and a byte more or "
              "less behind them is refused (%u wrong)", MP_CHAT_TEXT_MAX, wrong);
    ut_checkf(tight == 0u,
              "each fits a buffer of exactly its length and is refused by one a byte short (%u "
              "wrong)", tight);
}

static void check_the_length_byte(void)
{
    mp_chat_say_note_t  say;
    mp_chat_line_note_t line;
    mp_chat_say_note_t  said;
    mp_chat_line_note_t heard;
    size_t              bytes;

    ut_section("the length byte against the message around it");
    say_of(&say, 1u, 10u, 0u);
    bytes = mp_chat_say_encode(&say, s_buffer, sizeof s_buffer);
    s_buffer[2] = 11u;
    ut_check(!mp_chat_say_decode(s_buffer, bytes, &said), "a say whose length byte says one more");
    s_buffer[2] = 9u;
    ut_check(!mp_chat_say_decode(s_buffer, bytes, &said), "or one less, is refused");
    s_buffer[2] = 0u;
    ut_check(!mp_chat_say_decode(s_buffer, MP_CHAT_SAY_HEAD_BYTES, &said),
             "and a say of no text is refused");

    say.length = 0u;
    ut_check(mp_chat_say_encode(&say, s_buffer, sizeof s_buffer) == 0u,
             "the encoder refuses an empty text");
    say_of(&say, 1u, MP_CHAT_TEXT_MAX, 0u);
    say.length = (uint8_t)(MP_CHAT_TEXT_MAX + 1u);
    say.text[MP_CHAT_TEXT_MAX] = 'x';
    ut_check(mp_chat_say_encode(&say, s_buffer, sizeof s_buffer) == 0u,
             "and a text one byte past the most a line carries");
    memset(s_buffer, 'a', sizeof s_buffer);
    s_buffer[0] = (uint8_t)MP_CHAT_SAY_TAG;
    s_buffer[1] = 1u;
    s_buffer[2] = (uint8_t)(MP_CHAT_TEXT_MAX + 1u);
    ut_check(!mp_chat_say_decode(s_buffer, MP_CHAT_SAY_HEAD_BYTES + MP_CHAT_TEXT_MAX + 1u, &said),
             "so does the decoder, with the length and the message agreeing");

    line_of(&line, 3u, "Obi-Wan", 12u);
    bytes = mp_chat_line_encode(&line, s_buffer, sizeof s_buffer);
    s_buffer[MP_CHAT_LINE_HEAD_BYTES - 1u] = 13u;
    ut_check(!mp_chat_line_decode(s_buffer, bytes, &heard), "a line whose length says one more");
    s_buffer[MP_CHAT_LINE_HEAD_BYTES - 1u] = 11u;
    ut_check(!mp_chat_line_decode(s_buffer, bytes, &heard), "or one less, is refused");
}

/* Every byte value inside a text, at the start, the middle and the end, asked of both halves. */
static void check_every_byte(void)
{
    unsigned value;
    unsigned wrong = 0u;
    unsigned taken = 0u;

    ut_section("every byte value inside a text");
    for (value = 0u; value <= 0xFFu; ++value) {
        static const size_t PLACES[] = { 0u, 5u, 9u };
        bool   sound = value >= 0x20u && value <= 0x7Eu && value != (unsigned)'\\';
        size_t p;

        for (p = 0; p < sizeof PLACES / sizeof PLACES[0]; ++p) {
            mp_chat_say_note_t  say;
            mp_chat_say_note_t  said;
            mp_chat_line_note_t line;
            mp_chat_line_note_t heard;
            size_t              bytes;
            size_t              length;

            say_of(&say, 9u, 10u, 3u);
            say.text[PLACES[p]] = (char)value;
            bytes = mp_chat_say_encode(&say, s_buffer, sizeof s_buffer);
            if ((bytes != 0u) != sound) {
                ++wrong;
            }
            /* The decoder gets the same text written straight into the bytes. */
            s_buffer[0] = (uint8_t)MP_CHAT_SAY_TAG;
            s_buffer[1] = 9u;
            s_buffer[2] = 10u;
            memcpy(s_buffer + MP_CHAT_SAY_HEAD_BYTES, say.text, 10u);
            if (mp_chat_say_decode(s_buffer, MP_CHAT_SAY_HEAD_BYTES + 10u, &said) != sound) {
                ++wrong;
            }

            line_of(&line, 1u, "Padme", 10u);
            line.text[PLACES[p]] = (char)value;
            length = mp_chat_line_encode(&line, s_buffer, sizeof s_buffer);
            if ((length != 0u) != sound) {
                ++wrong;
            }
            line.text[PLACES[p]] = 'k';
            length = mp_chat_line_encode(&line, s_buffer, sizeof s_buffer);
            s_buffer[MP_CHAT_LINE_HEAD_BYTES + PLACES[p]] = (uint8_t)value;
            if (length == 0u || mp_chat_line_decode(s_buffer, length, &heard) != sound) {
                ++wrong;
            }
            taken += sound ? 1u : 0u;
        }
    }
    ut_checkf(wrong == 0u,
              "0x20..0x7E without the backslash is taken by both halves and every other byte "
              "refused by both (%u wrong, %u taken)", wrong, taken);
    ut_checkf(taken == 3u * 94u, "which is 94 byte values, in each of the three places (%u)",
              taken);
}

static void check_the_fields(void)
{
    mp_chat_say_note_t  say;
    mp_chat_say_note_t  said;
    mp_chat_line_note_t line;
    mp_chat_line_note_t heard;
    unsigned            slot;
    unsigned            wrong = 0u;
    size_t              bytes;

    ut_section("the serial, the slot and the name");
    say_of(&say, 0u, 4u, 0u);
    ut_check(mp_chat_say_encode(&say, s_buffer, sizeof s_buffer) == 0u,
             "a say with serial 0 is refused by the encoder");
    say.serial = 1u;
    bytes = mp_chat_say_encode(&say, s_buffer, sizeof s_buffer);
    s_buffer[1] = 0u;
    ut_check(bytes != 0u && !mp_chat_say_decode(s_buffer, bytes, &said), "and by the decoder");

    for (slot = 0u; slot <= 0xFFu; ++slot) {
        bool sound = slot < MP_CHAT_SLOTS;

        line_of(&line, (uint8_t)slot, "Anakin", 5u);
        bytes = mp_chat_line_encode(&line, s_buffer, sizeof s_buffer);
        if ((bytes != 0u) != sound) {
            ++wrong;
        }
        line.slot = 1u;
        bytes = mp_chat_line_encode(&line, s_buffer, sizeof s_buffer);
        s_buffer[1] = (uint8_t)slot;
        if (mp_chat_line_decode(s_buffer, bytes, &heard) != sound) {
            ++wrong;
        }
    }
    ut_checkf(wrong == 0u, "a line names slots 0..%u and no other, in both halves (%u wrong)",
              MP_CHAT_SLOTS - 1u, wrong);

    line_of(&line, 2u, "Anakin", 5u);
    line.serial = 0u;
    ut_check(mp_chat_line_encode(&line, s_buffer, sizeof s_buffer) == 0u,
             "a line with serial 0 is refused");
    line_of(&line, 2u, "", 5u);
    ut_check(mp_chat_line_encode(&line, s_buffer, sizeof s_buffer) == 0u,
             "and one with an empty name");
    line_of(&line, 2u, "Ana\\kin", 5u);
    ut_check(mp_chat_line_encode(&line, s_buffer, sizeof s_buffer) == 0u,
             "and one whose name holds a backslash");
    line_of(&line, 2u, "Anakin", 5u);
    memset(line.name, 'x', sizeof line.name);
    ut_check(mp_chat_line_encode(&line, s_buffer, sizeof s_buffer) == 0u,
             "and one whose name has no terminator inside the field");

    line_of(&line, 2u, "Anakin", 5u);
    memset(line.name + 7, 'z', sizeof line.name - 7u);   /* behind the terminator */
    bytes = mp_chat_line_encode(&line, s_buffer, sizeof s_buffer);
    ut_check(bytes != 0u && s_buffer[3 + 6] == 0u && s_buffer[3 + 15] == 0u &&
                 mp_chat_line_decode(s_buffer, bytes, &heard) &&
                 strcmp(heard.name, "Anakin") == 0,
             "what lies behind a name's terminator goes out as zeros");
    s_buffer[3 + 10] = 'z';
    ut_check(!mp_chat_line_decode(s_buffer, bytes, &heard),
             "and a line with anything else there is refused, so every line has one encoding");
    s_buffer[3 + 10] = 0u;
    s_buffer[3] = 0x80u;
    ut_check(!mp_chat_line_decode(s_buffer, bytes, &heard),
             "as is a name with a byte the font has no glyph for");

    ut_check(!mp_chat_is_say(NULL, 0u) && !mp_chat_is_line(NULL, 0u) &&
                 !mp_chat_say_decode(NULL, 0u, &said) && !mp_chat_line_decode(NULL, 0u, &heard),
             "nothing at all is neither");
    s_buffer[0] = (uint8_t)MP_CHAT_LINE_TAG;
    ut_check(mp_chat_is_line(s_buffer, 1u) && !mp_chat_is_say(s_buffer, 1u),
             "a lone tag is claimed by its reader, which then refuses it as torn");
}

/* A readable page with no access behind it. A message laid against the end of the first page has
 * its last byte on the boundary, so reading one byte past it faults. */
static uint8_t *s_page;
static size_t   s_page_size;

static bool set_up_the_guard(void)
{
    SYSTEM_INFO info;
    DWORD       old = 0;

    GetSystemInfo(&info);
    s_page_size = info.dwPageSize;
    s_page = (uint8_t *)VirtualAlloc(NULL, 2u * s_page_size, MEM_RESERVE | MEM_COMMIT,
                                     PAGE_READWRITE);
    return s_page != NULL &&
           VirtualProtect(s_page + s_page_size, s_page_size, PAGE_NOACCESS, &old) != 0;
}

static void check_random_bytes(void)
{
    uint32_t round;
    unsigned said = 0u;
    unsigned lines = 0u;
    unsigned unsettled = 0u;

    ut_section("random bytes behind each tag, against a guard page");
    if (!set_up_the_guard()) {
        ut_check(false, "the guard page could not be set up");
        return;
    }
    for (round = 0; round < 200000u; ++round) {
        size_t              bytes = next_random() % (MP_CHAT_LINE_MAX_BYTES + 8u);
        uint8_t            *at = s_page + s_page_size - bytes;
        mp_chat_say_note_t  say;
        mp_chat_line_note_t line;
        size_t              i;

        for (i = 0; i < bytes; ++i) {
            /* Mostly printable, so the text rule is passed often enough to reach the end. */
            uint32_t r = next_random();

            at[i] = (r & 0x700u) != 0u ? (uint8_t)(0x20u + r % 0x5Fu) : (uint8_t)r;
        }
        if (bytes > 0u) {
            at[0] = (round & 1u) != 0u ? (uint8_t)MP_CHAT_SAY_TAG : (uint8_t)MP_CHAT_LINE_TAG;
        }
        if (bytes > 2u && (round & 2u) != 0u) {
            /* The length byte agreeing with the message, which is what reaches the text. */
            if (at[0] == (uint8_t)MP_CHAT_SAY_TAG && bytes > MP_CHAT_SAY_HEAD_BYTES) {
                at[2] = (uint8_t)(bytes - MP_CHAT_SAY_HEAD_BYTES);
            }
            if (at[0] == (uint8_t)MP_CHAT_LINE_TAG && bytes > MP_CHAT_LINE_HEAD_BYTES) {
                at[MP_CHAT_LINE_HEAD_BYTES - 1u] = (uint8_t)(bytes - MP_CHAT_LINE_HEAD_BYTES);
                at[1] &= 0x0Fu;
                memcpy(at + 3, "Mace\0\0\0\0\0\0\0\0\0\0\0\0", 16u);
            }
        }
        if (mp_chat_say_decode(at, bytes, &say)) {
            ++said;
            if (mp_chat_say_encode(&say, s_buffer, sizeof s_buffer) != bytes ||
                memcmp(s_buffer, at, bytes) != 0) {
                ++unsettled;
            }
        }
        if (mp_chat_line_decode(at, bytes, &line)) {
            ++lines;
            if (mp_chat_line_encode(&line, s_buffer, sizeof s_buffer) != bytes ||
                memcmp(s_buffer, at, bytes) != 0) {
                ++unsettled;
            }
        }
    }
    ut_checkf(said > 0u && lines > 0u,
              "no decoder read past the message, and some random messages decoded (%u says, %u "
              "lines)", said, lines);
    ut_checkf(unsettled == 0u,
              "every message a decoder took, its encoder wrote again byte for byte (%u did not)",
              unsettled);
    VirtualFree(s_page, 0u, MEM_RELEASE);
}

_Static_assert(MP_CHAT_LINE_MAX_BYTES <= MP_CHANNEL_MESSAGE_BYTES,
               "the longest line fits one message of the reliable channel");

int main(void)
{
    check_every_length();
    check_the_length_byte();
    check_every_byte();
    check_the_fields();
    check_random_bytes();
    return ut_summary("mp_chat_wire");
}
