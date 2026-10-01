/* mp_chat_wire.c: the say and the line. See the header. */
#include "mp_chat_wire.h"

#include "mp_chat_rule.h"
#include "mp_roster.h"
#include "mp_snapshot.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

_Static_assert(MP_ROSTER_NAME_MAX == 16u, "the name field on the wire is sixteen bytes");
_Static_assert(MP_CHAT_LINE_HEAD_BYTES == 1u + 1u + 1u + MP_ROSTER_NAME_MAX + 1u,
               "a line's head is the tag, the slot, the serial, the name and the length");
_Static_assert(MP_CHAT_SLOTS == MP_SNAPSHOT_MAX_BODIES, "a line names a world slot");

/* Where the fields of a line stand. */
#define LINE_SLOT_AT   1u
#define LINE_SERIAL_AT 2u
#define LINE_NAME_AT   3u
#define LINE_LENGTH_AT (LINE_NAME_AT + MP_ROSTER_NAME_MAX)

/* A name as the roster takes one, and only zeros behind its terminator, so that a line has one
 * encoding and a decoder that takes it gives the encoder back the same bytes. */
static bool name_is_canonical(const uint8_t *field)
{
    char   name[MP_ROSTER_NAME_MAX];
    size_t end;
    size_t i;

    memcpy(name, field, sizeof name);
    if (!mp_roster_name_is_sound(name)) {
        return false;
    }
    end = strlen(name);
    for (i = end; i < MP_ROSTER_NAME_MAX; ++i) {
        if (field[i] != 0u) {
            return false;
        }
    }
    return true;
}

size_t mp_chat_say_encode(const mp_chat_say_note_t *say, uint8_t *buffer, size_t capacity)
{
    size_t bytes;

    if (say == NULL || buffer == NULL || say->serial == 0u ||
        !mp_chat_rule_text_is_sound(say->text, say->length)) {
        return 0u;
    }
    bytes = MP_CHAT_SAY_HEAD_BYTES + say->length;
    if (capacity < bytes) {
        return 0u;
    }
    buffer[0] = (uint8_t)MP_CHAT_SAY_TAG;
    buffer[1] = say->serial;
    buffer[2] = say->length;
    memcpy(buffer + MP_CHAT_SAY_HEAD_BYTES, say->text, say->length);
    return bytes;
}

size_t mp_chat_line_encode(const mp_chat_line_note_t *line, uint8_t *buffer, size_t capacity)
{
    size_t bytes;

    if (line == NULL || buffer == NULL || line->serial == 0u || line->slot >= MP_CHAT_SLOTS ||
        !mp_roster_name_is_sound(line->name) ||
        !mp_chat_rule_text_is_sound(line->text, line->length)) {
        return 0u;
    }
    bytes = MP_CHAT_LINE_HEAD_BYTES + line->length;
    if (capacity < bytes) {
        return 0u;
    }
    buffer[0]              = (uint8_t)MP_CHAT_LINE_TAG;
    buffer[LINE_SLOT_AT]   = line->slot;
    buffer[LINE_SERIAL_AT] = line->serial;
    memset(buffer + LINE_NAME_AT, 0, MP_ROSTER_NAME_MAX);
    memcpy(buffer + LINE_NAME_AT, line->name, strlen(line->name));
    buffer[LINE_LENGTH_AT] = line->length;
    memcpy(buffer + MP_CHAT_LINE_HEAD_BYTES, line->text, line->length);
    return bytes;
}

bool mp_chat_is_say(const uint8_t *buffer, size_t bytes)
{
    return buffer != NULL && bytes >= 1u && buffer[0] == (uint8_t)MP_CHAT_SAY_TAG;
}

bool mp_chat_is_line(const uint8_t *buffer, size_t bytes)
{
    return buffer != NULL && bytes >= 1u && buffer[0] == (uint8_t)MP_CHAT_LINE_TAG;
}

/* Every length is read before anything that follows it, and the message is taken only at exactly
 * its head plus that length, so no byte past `bytes` is ever read. */
bool mp_chat_say_decode(const uint8_t *buffer, size_t bytes, mp_chat_say_note_t *out)
{
    const char *text;
    size_t      length;

    if (out == NULL || !mp_chat_is_say(buffer, bytes) || bytes < MP_CHAT_SAY_HEAD_BYTES) {
        return false;
    }
    length = buffer[2];
    text   = (const char *)(buffer + MP_CHAT_SAY_HEAD_BYTES);
    if (bytes != MP_CHAT_SAY_HEAD_BYTES + length || buffer[1] == 0u ||
        !mp_chat_rule_text_is_sound(text, length)) {
        return false;
    }
    memset(out, 0, sizeof *out);
    out->serial = buffer[1];
    out->length = (uint8_t)length;
    memcpy(out->text, text, length);
    return true;
}

bool mp_chat_line_decode(const uint8_t *buffer, size_t bytes, mp_chat_line_note_t *out)
{
    const char *text;
    size_t      length;

    if (out == NULL || !mp_chat_is_line(buffer, bytes) || bytes < MP_CHAT_LINE_HEAD_BYTES) {
        return false;
    }
    length = buffer[LINE_LENGTH_AT];
    text   = (const char *)(buffer + MP_CHAT_LINE_HEAD_BYTES);
    if (bytes != MP_CHAT_LINE_HEAD_BYTES + length || buffer[LINE_SLOT_AT] >= MP_CHAT_SLOTS ||
        buffer[LINE_SERIAL_AT] == 0u || !name_is_canonical(buffer + LINE_NAME_AT) ||
        !mp_chat_rule_text_is_sound(text, length)) {
        return false;
    }
    memset(out, 0, sizeof *out);
    out->slot   = buffer[LINE_SLOT_AT];
    out->serial = buffer[LINE_SERIAL_AT];
    memcpy(out->name, buffer + LINE_NAME_AT, MP_ROSTER_NAME_MAX);
    out->length = (uint8_t)length;
    memcpy(out->text, text, length);
    return true;
}
