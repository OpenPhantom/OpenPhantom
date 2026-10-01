/* mp_dialog.h: a line of conversation on the wire, which is a NUMBER.
 *
 * Layer 1, pure logic. No engine, no address, no socket.
 *
 * =================================== Why this is nearly free =================================
 *
 * The engine's conversation object holds NO branch logic and NO conversation graph. It is a
 * rendezvous block plus a renderer: the AI virtual machine speaks into it with
 * `Dialog_SpeakSingle`,
 * appends option rows with `Dialog_AddChoice`, and polls it until the player has picked.
 *
 * So the content of a spoken line is `lineId`, an index into the level's dialogue book, capped at
 * 0x2800 by the two asserts in `DLG_LineText` and `DLG_TextLength`. Both machines loaded the same
 * book from the same level and the same VOICE.LAB. **The wire carries the number and the receiver
 * produces the subtitle and the voice out of its own copy.** Eighteen bytes for a story beat.
 *
 * ==================================== What is NOT in the note =================================
 *
 * **The camera group.** `Dialog_SpeakSingle` calls `bapview_overrideOn(cameraGroup)` when it is
 * not negative, and a scene camera is the host's answer to where the host's player is standing.
 * Taking a second player's camera away for a conversation they may be nowhere near is worse than
 * letting them keep it, so the receiver passes -1 and the note does not carry the group at all.
 *
 * **The speaker.** It is a pointer into the sender's own actor pool and means nothing here. It
 * matters only for the same-speaker test, which the receiver settles with the engine's own
 * `Dialog_ForceRestart` instead.
 *
 * **The position is in the note and is not optional dressing.** The bark plays through
 * `bapsound_playVoice(wav, &hVoiceBark, pos)`, and a voice with no place skips the whole distance
 * path: it would play at full volume wherever the players were standing. With the speaker's
 * own place it rolls off the way every other voice in the game does, and the place is also what
 * the judgement of the line measures on the far machine (mp_voice_rule).
 *
 * ================================== Only the host is answered =================================
 *
 * Only the host talks to people. A client hears and reads what the host is told and what the host
 * answers, when it stands near enough, and it is never asked anything: no answer menu travels and
 * no pick comes back. So the notes are the spoken line (host to client) and the host's pick (host
 * to client), and nothing goes the other way.
 *
 * SIZE NOTE: under 150 lines, no seam.
 */
#ifndef MULTIPLAYER_MP_DIALOG_H
#define MULTIPLAYER_MP_DIALOG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The next free tag when this message was added. */
#define MP_DIALOG_TAG 0x9Cu

/* RETIRED. The answer menu's rows travelled under this tag while a client could answer; the
 * number stays claimed because a tag once used on the wire is not reissued to a message that
 * would be read by an older build's reader. */
#define MP_DIALOG_CHOICE_TAG 0x9Du

/* The host's answer, to be heard and read on the client. */
#define MP_DIALOG_PICK_TAG   0x9Eu

/* The engine's own cap on a dialogue id, byte-proven: `DLG_LineText` (0x004310FF) and
 * `DLG_TextLength` (0x00431096) both compare against 0x27FF before asserting. An id at or past this
 * is refused rather than passed on, because the assert it would hit is compiled out of the retail
 * build and what follows is a read off the end of the book. */
#define MP_DIALOG_MAX_ID 0x2800u

/* tag, level (2), line (2), whether a place came with it (1), the place (12).
 *
 * A place is three axes of the wire's own position format, which every other position in this
 * feature travels in: fixed point at 1/256 of a unit, in a u32 each. The 24 bits named by
 * MP_WIRE_POSITION_LIMIT are the VALUE's range and not the field's width; the field is four
 * bytes, and reading that limit as a width is how this note was first declared three bytes
 * short, which the encoder then refused every single time: the writer's sticky overflow tripped
 * on the last axis, the encoder returned 0, and not one line travelled until the unit test
 * pinned the length from the outside. */
#define MP_DIALOG_BYTES 18u

/* tag, level (2), line (2): the host's pick. It carries the LINE and not the row index, because
 * the line is what the receiver says again; a row means nothing on a machine that has no menu. */
#define MP_DIALOG_PICK_BYTES 5u

/* The engine's own cap on the rows of a menu, byte-proven: `Dialog_AddChoice` compares the count
 * against 9 before it appends, and that 9 is a required byte of the pattern this feature matches
 * it with. The host's pick watcher walks the rows with it. */
#define MP_DIALOG_MAX_CHOICES 9u

typedef struct mp_dialog_line {
    uint16_t level;         /* the sender's level identity, so a line cannot cross a level load */
    uint16_t line;          /* the index into the dialogue book */
    bool     has_position;  /* false when the sender's speaker had no place, which is legal */
    float    position[3];
} mp_dialog_line_t;

size_t mp_dialog_encode(const mp_dialog_line_t *line, uint8_t *buffer, size_t capacity);
bool   mp_dialog_is(const uint8_t *buffer, size_t bytes);
bool   mp_dialog_decode(const uint8_t *buffer, size_t bytes, mp_dialog_line_t *out);

size_t mp_dialog_encode_pick(uint16_t level, uint16_t line, uint8_t *buffer, size_t capacity);
bool   mp_dialog_is_pick(const uint8_t *buffer, size_t bytes);
bool   mp_dialog_decode_pick(const uint8_t *buffer, size_t bytes, uint16_t *level, uint16_t *line);

/* How far a line carries is not this file's any more. It was a written 100 units and a rule of its
 * own for the client alone; it is now the one judgement every machine makes of every line, with the
 * reach read out of the voice's own code (mp_voice_rule). */

#endif /* MULTIPLAYER_MP_DIALOG_H */
