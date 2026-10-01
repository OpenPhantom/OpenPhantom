/* mp_chat_rule.c: the rules of a chat line. See the header. */
#include "mp_chat_rule.h"

#include "mp_chat_wire.h"
#include "mp_roster.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

_Static_assert(MP_CHAT_TEXT_MAX <= 0xFFu, "a text length travels in one byte");

/* The Western code page above 0x7E, by the byte less 0x80: what a letter is written as when the
 * font has no glyph for it, or NULL for a question mark. Accents and the caron fall away, the
 * German umlauts and the sharp s are written out, the ligatures of French and Danish are spelled,
 * and the inverted marks of Spanish stand upright. Letters with no plain letter under them, the
 * eth and the thorn, and every sign that is not a letter become a question mark. */
static const char *const WESTERN[128] = {
    /* 0x80 */ NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    /* 0x88 */ NULL, NULL, "S",  NULL, "OE", NULL, "Z",  NULL,
    /* 0x90 */ NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    /* 0x98 */ NULL, NULL, "s",  NULL, "oe", NULL, "z",  "Y",
    /* 0xA0 */ NULL, "!",  NULL, NULL, NULL, NULL, NULL, NULL,
    /* 0xA8 */ NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    /* 0xB0 */ NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    /* 0xB8 */ NULL, NULL, NULL, NULL, NULL, NULL, NULL, "?",
    /* 0xC0 */ "A",  "A",  "A",  "A",  "Ae", "A",  "AE", "C",
    /* 0xC8 */ "E",  "E",  "E",  "E",  "I",  "I",  "I",  "I",
    /* 0xD0 */ NULL, "N",  "O",  "O",  "O",  "O",  "Oe", NULL,
    /* 0xD8 */ "O",  "U",  "U",  "U",  "Ue", "Y",  NULL, "ss",
    /* 0xE0 */ "a",  "a",  "a",  "a",  "ae", "a",  "ae", "c",
    /* 0xE8 */ "e",  "e",  "e",  "e",  "i",  "i",  "i",  "i",
    /* 0xF0 */ NULL, "n",  "o",  "o",  "o",  "o",  "oe", NULL,
    /* 0xF8 */ "o",  "u",  "u",  "u",  "ue", "y",  NULL, "y",
};

static bool line_byte(char c)
{
    unsigned byte = (unsigned)(unsigned char)c;

    return byte >= 0x20u && byte <= 0x7Eu && c != '\\';
}

/* What one typed byte becomes, as a string; empty for a byte with no place in a line. */
static const char *spelling(uint8_t typed, uint32_t code_page, char single[2])
{
    const char *western;

    if (typed < 0x20u || typed == 0x7Fu) {
        return "";   /* a control character, or DEL, which Ctrl and Backspace together type */
    }
    if (typed == (uint8_t)'\\') {
        return "/";
    }
    if (typed < 0x7Fu) {
        single[0] = (char)typed;
        single[1] = '\0';
        return single;
    }
    if (code_page != MP_CHAT_CODE_PAGE_WESTERN) {
        return "?";
    }
    western = WESTERN[typed - 0x80u];
    return western != NULL ? western : "?";
}

size_t mp_chat_rule_append_typed(char *line, size_t length, size_t capacity, uint8_t typed,
                                 uint32_t code_page)
{
    char        single[2];
    const char *as;
    size_t      bytes;

    if (line == NULL || length > capacity) {
        return 0u;
    }
    as    = spelling(typed, code_page, single);
    bytes = strlen(as);
    if (bytes > capacity - length) {
        bytes = 0u;   /* half a transliteration would say something else */
    }
    memcpy(line + length, as, bytes);
    line[length + bytes] = '\0';
    return bytes;
}

size_t mp_chat_rule_trim(char *line, size_t length)
{
    size_t first = 0u;
    size_t end   = length;

    if (line == NULL) {
        return 0u;
    }
    while (first < end && line[first] == ' ') {
        ++first;
    }
    while (end > first && line[end - 1u] == ' ') {
        --end;
    }
    if (first == 0u && end == length) {
        return length;
    }
    memmove(line, line + first, end - first);
    line[end - first] = '\0';
    return end - first;
}

bool mp_chat_rule_text_is_sound(const char *text, size_t length)
{
    size_t i;

    if (text == NULL || length == 0u || length > MP_CHAT_TEXT_MAX) {
        return false;
    }
    for (i = 0; i < length; ++i) {
        if (!line_byte(text[i])) {
            return false;
        }
    }
    return true;
}

bool mp_chat_pace_take(mp_chat_pace_t *pace, uint32_t now_ms, uint32_t burst, uint32_t every_ms)
{
    uint32_t full;
    uint32_t elapsed;

    if (pace == NULL || burst == 0u || every_ms == 0u) {
        return false;
    }
    full = burst * every_ms;
    if (!pace->started || pace->level_ms > full) {
        pace->started  = true;
        pace->level_ms = full;   /* also what a bucket asked with a smaller size keeps */
        pace->last_ms  = now_ms;
    }
    /* The difference wraps with the clock, so a count that passes 2^32 is a small step. A
     * difference past half the range is a clock that went backwards, or a rest of more than 24.8
     * days that reads like one. It fills nothing, and the bucket is anchored at the new time, so it
     * fills again from there; left at the old mark, it would fill nothing until the clock passed
     * that mark, which after such a rest is up to another 24.8 days. */
    elapsed = now_ms - pace->last_ms;
    if ((int32_t)elapsed > 0) {
        pace->last_ms  = now_ms;
        pace->level_ms = elapsed >= full - pace->level_ms ? full : pace->level_ms + elapsed;
    } else if ((int32_t)elapsed < 0) {
        pace->last_ms = now_ms;
    }
    if (pace->level_ms < every_ms) {
        return false;
    }
    pace->level_ms -= every_ms;
    return true;
}

void mp_chat_pace_give_back(mp_chat_pace_t *pace, uint32_t burst, uint32_t every_ms)
{
    uint32_t full = burst * every_ms;

    if (pace == NULL || !pace->started) {
        return;
    }
    pace->level_ms = pace->level_ms >= full || every_ms >= full - pace->level_ms
                         ? full
                         : pace->level_ms + every_ms;
}

/* The line for every player out of a trimmed say: who said it and what the host calls them, and
 * the say's serial and text. */
static mp_chat_host_verdict_t build_line(const mp_chat_say_note_t *say, uint8_t slot,
                                         const char *name, uint8_t *line, size_t capacity,
                                         size_t *line_bytes)
{
    mp_chat_line_note_t note;

    memset(&note, 0, sizeof note);
    note.slot   = slot;
    note.serial = say->serial;
    mp_roster_name_clean(name, note.name);
    note.length = say->length;
    memcpy(note.text, say->text, say->length);
    *line_bytes = mp_chat_line_encode(&note, line, capacity);
    return *line_bytes != 0u ? MP_CHAT_HOST_LINE : MP_CHAT_HOST_UNSOUND;
}

/* The order of the questions is the order of their cost to the player: a say that cannot be a line
 * is refused before the bucket is asked, so a torn or empty one never costs its slot a line. */
static mp_chat_host_verdict_t judge(mp_chat_host_t *host, const uint8_t *say, size_t say_bytes,
                                    uint8_t slot, const char *name, uint32_t now_ms,
                                    uint8_t *line, size_t capacity, size_t *line_bytes)
{
    mp_chat_say_note_t note;

    if (line == NULL || line_bytes == NULL || capacity < MP_CHAT_LINE_MAX_BYTES) {
        return MP_CHAT_HOST_UNSOUND;
    }
    if (slot >= MP_CHAT_SLOTS) {
        return MP_CHAT_HOST_NO_SEAT;
    }
    if (!mp_chat_say_decode(say, say_bytes, &note)) {
        return MP_CHAT_HOST_UNSOUND;
    }
    note.length = (uint8_t)mp_chat_rule_trim(note.text, note.length);
    if (note.length == 0u) {
        return MP_CHAT_HOST_UNSOUND;
    }
    if (!mp_chat_pace_take(&host->pace[slot], now_ms, MP_CHAT_PACE_HOST_BURST,
                           MP_CHAT_PACE_HOST_EVERY_MS)) {
        return MP_CHAT_HOST_TOO_FAST;
    }
    return build_line(&note, slot, name, line, capacity, line_bytes);
}

mp_chat_host_verdict_t mp_chat_rule_host_line(mp_chat_host_t *host, const uint8_t *say,
                                              size_t say_bytes, uint8_t slot, const char *name,
                                              uint32_t now_ms, uint8_t *line, size_t capacity,
                                              size_t *line_bytes)
{
    mp_chat_host_verdict_t verdict;

    if (line_bytes != NULL) {
        *line_bytes = 0u;
    }
    if (host == NULL) {
        return MP_CHAT_HOST_UNSOUND;
    }
    verdict = judge(host, say, say_bytes, slot, name, now_ms, line, capacity, line_bytes);
    switch (verdict) {
    case MP_CHAT_HOST_LINE:
        if (slot == MP_CHAT_HOST_SLOT) {
            ++host->counts.said_here;
        } else {
            ++host->counts.taken;
        }
        break;
    case MP_CHAT_HOST_TOO_FAST:
        ++host->counts.too_fast;
        break;
    case MP_CHAT_HOST_NO_SEAT:
        ++host->counts.no_seat;
        break;
    case MP_CHAT_HOST_UNSOUND:
    default:
        ++host->counts.unsound;
        break;
    }
    return verdict;
}

void mp_chat_rule_host_sent(mp_chat_host_t *host, size_t peers, size_t reached)
{
    if (host == NULL) {
        return;
    }
    ++host->counts.lines_out;
    host->counts.copies += (uint32_t)reached;
    host->counts.copies_unsent += peers > reached ? (uint32_t)(peers - reached) : 0u;
}
