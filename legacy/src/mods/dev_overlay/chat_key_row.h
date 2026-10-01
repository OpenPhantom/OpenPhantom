/* chat_key_row.h: which key opens the multiplayer chat, as a row inside the developer menu.
 *
 * The chat belongs to multiplayer.dll, which reads [multiplayer] ChatKey when a session is set up
 * and again about once a second while it runs. This row writes that one key and nothing else. It
 * goes through the file for the same reason the window keys and the subtitle size do: feature DLLs
 * here never depend on each other at run time, and either can be deleted from mods\ without
 * breaking the other. With this DLL gone the key is whatever the file says, and T when it says
 * nothing.
 *
 * The key is written as a NAME, "T" or "F9", and never as a number. The multiplayer reads its keys
 * by name: one letter, one digit, or F1 to F12. A number such as 84 is none of those, so it would
 * read as no key at all and the chat would fall back to T while this row went on showing the key
 * the player chose. That is why this row does not share the code of the other key rows here, which
 * write the code, and why it reads the file with its own grammar rather than overlay_key_from_name:
 * that one reads a bare digit as a key code, so "5" is code 5 there and the 5 key in the
 * multiplayer.
 *
 * The grammar and the keys the multiplayer refuses are repeated here rather than shared, because
 * the two DLLs do not link against each other. A test builds this file beside the multiplayer's
 * own reader and holds the two together.
 *
 * Not locked in a session. [multiplayer] is not part of the content the two sides compare, and a
 * chat key is a matter for the machine it is pressed on.
 */
#ifndef DEV_OVERLAY_CHAT_KEY_ROW_H
#define DEV_OVERLAY_CHAT_KEY_ROW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* T, the key the multiplayer uses when the file names none, or one it cannot use. */
#define CHAT_KEY_ROW_DEFAULT 0x54

/* U, the fallback when the scoreboard's own key is T, so the two never share a key. */
#define CHAT_KEY_ROW_SECOND 0x55

/* Room for the longest name this row writes, "F12", and its terminator. */
#define CHAT_KEY_ROW_NAME_MAX 4u

/* Why a key was turned down, in the order they are asked. The first three are the multiplayer's
 * own refusals as well, so a key refused for one of them is one the multiplayer would not use if
 * the file named it; the last is this panel's own business. */
typedef enum chat_key_refusal {
    CHAT_KEY_ACCEPTED = 0,
    CHAT_KEY_REFUSED_SHAPE,        /* not a letter, a digit or F1 to F12 */
    CHAT_KEY_REFUSED_RESERVED,     /* M, F4, F6, F7, F8, F10, F11 or F12 */
    CHAT_KEY_REFUSED_SCOREBOARD,   /* the key [multiplayer] ScoreboardKey names */
    CHAT_KEY_REFUSED_TAKEN         /* opens this panel, or is bound to a key row here */
} chat_key_refusal_t;

/* A name as the multiplayer reads it: TAB, one letter, one digit, or F1 to F12, case ignored.
 * Answers the virtual key, or 0 for anything else, including NULL and the empty string. */
int32_t chat_key_row_code(const char *name);

/* The name this row writes for `vk`: the letter, the digit, or F1 to F12. False for every other
 * key, and `out` then holds an empty string. */
bool chat_key_row_name(int32_t vk, char *out, size_t size);

/* Whether `vk` may be the chat key, with what else claims keys handed in, so the rule can be driven
 * without a panel. `scoreboard_vk` is the scoreboard's key as the multiplayer reads it;
 * `taken_in_panel` is whether this panel already uses the key. */
chat_key_refusal_t chat_key_row_judge(int32_t vk, int32_t scoreboard_vk, bool taken_in_panel);

/* The key the chat falls back on when the file names none it can use: T, or U when the scoreboard's
 * key is T. The multiplayer's reader falls back the same way. */
int32_t chat_key_row_fallback(int32_t scoreboard_vk);

/* The key the chat opens on, read the way the multiplayer reads it: the fallback when the file
 * names none, a name the multiplayer cannot read, or a key it refuses. Reads the file every
 * call. */
int32_t chat_key_row_get(void);

/* Writes the key's name, or refuses it with a sentence in the band and leaves the file alone.
 * False for a refusal and for a write that did not land. */
bool chat_key_row_set(int32_t vk);

#endif /* DEV_OVERLAY_CHAT_KEY_ROW_H */
