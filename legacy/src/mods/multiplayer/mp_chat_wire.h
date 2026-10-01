/* mp_chat_wire.h: the two messages a line of chat travels in.
 *
 * Layer 1, pure. No address and no game in the process.
 *
 *   THE SAY (a player to its host, 0xA9): what the player typed, and nothing about who typed it.
 *
 *       tag (1) | serial (1, 1..255) | length (1, 1..MP_CHAT_TEXT_MAX) | text (length)
 *
 *   THE LINE (the host to every player, the one who said it included, 0xAA): the text, the world
 *   slot of the player who said it and the name the host's own table gives that player.
 *
 *       tag (1) | slot (1, < 16) | serial (1, 1..255) | name (16) | length (1) | text (length)
 *
 * The serial is the typing side's own count of its lines, never 0, and comes back in the line
 * unchanged. The side that said it tells its own line by the slot, which only the host writes; the
 * serial is carried so a reader can match a line to the say it answers, and nothing reads it yet.
 * The name field is the roster's: sixteen bytes, a terminator inside them and zeros behind it. The
 * text follows the rule in mp_chat_rule.h and carries no terminator.
 *
 * The length byte stands at a fixed place in front of everything that varies, so a message is
 * exactly its head plus that length and not a byte more or less, which is as exact a test as a
 * fixed size. Each encoder refuses what its decoder refuses.
 */
#ifndef MULTIPLAYER_MP_CHAT_WIRE_H
#define MULTIPLAYER_MP_CHAT_WIRE_H

#include "mp_chat_rule.h"
#include "mp_roster.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MP_CHAT_SAY_TAG  0xA9u   /* a player's line, said to its host */
#define MP_CHAT_LINE_TAG 0xAAu   /* the host's line, for every player */

#define MP_CHAT_SAY_HEAD_BYTES  3u    /* tag, serial, length */
#define MP_CHAT_LINE_HEAD_BYTES 20u   /* tag, slot, serial, name, length */
#define MP_CHAT_SAY_MAX_BYTES   (MP_CHAT_SAY_HEAD_BYTES + MP_CHAT_TEXT_MAX)
#define MP_CHAT_LINE_MAX_BYTES  (MP_CHAT_LINE_HEAD_BYTES + MP_CHAT_TEXT_MAX)

typedef struct mp_chat_say_note {
    uint8_t serial;
    uint8_t length;
    char    text[MP_CHAT_TEXT_MAX + 1u];   /* a terminator behind the length after a decode */
} mp_chat_say_note_t;

typedef struct mp_chat_line_note {
    uint8_t slot;
    uint8_t serial;
    char    name[MP_ROSTER_NAME_MAX];      /* terminated inside the field */
    uint8_t length;
    char    text[MP_CHAT_TEXT_MAX + 1u];   /* a terminator behind the length after a decode */
} mp_chat_line_note_t;

/* Each encoder answers the length written, or 0 for a message it will not describe: a serial of
 * 0, a text that is not sound, a slot past the table, a name the roster would refuse, or a buffer
 * too small for the whole message. */
size_t mp_chat_say_encode(const mp_chat_say_note_t *say, uint8_t *buffer, size_t capacity);
size_t mp_chat_line_encode(const mp_chat_line_note_t *line, uint8_t *buffer, size_t capacity);

/* Whether a message is a say or a line, by its tag alone: one that carries the tag and does not
 * decode is torn, and its reader claims and counts it rather than leaving it to another. */
bool mp_chat_is_say(const uint8_t *buffer, size_t bytes);
bool mp_chat_is_line(const uint8_t *buffer, size_t bytes);

/* Read whole or refused whole: a length that is not exactly the head plus the text, a serial of
 * 0, a text that is not sound, a slot past the table, or a name that is not sound or has anything
 * but zeros behind its terminator. On false nothing of `out` is to be trusted. */
bool mp_chat_say_decode(const uint8_t *buffer, size_t bytes, mp_chat_say_note_t *out);
bool mp_chat_line_decode(const uint8_t *buffer, size_t bytes, mp_chat_line_note_t *out);

#endif /* MULTIPLAYER_MP_CHAT_WIRE_H */
