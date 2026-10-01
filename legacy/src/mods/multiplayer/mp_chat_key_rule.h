/* mp_chat_key_rule.h: what a window message does to the chat's input line, and which key opens it.
 *
 * Layer 1, pure. The hook on the engine's key handler hands every message it sees to
 * mp_chat_key_decide and does what the answer says; nothing here reads a key, a cell or a clock.
 *
 * ================================ The two ways a key arrives =================================
 *
 * The game's window is an ANSI window, and its message loop runs TranslateMessage before it
 * dispatches. So a key arrives as WM_KEYDOWN, and the character it types arrives as a separate
 * WM_CHAR that the translation put at the head of the queue, before anything else can happen. Text
 * is taken from WM_CHAR only, which is what gets the keyboard layout, Shift, dead keys and AltGr
 * right without asking them; the keys that are not text, Enter, Escape and Backspace, are taken
 * from WM_KEYDOWN, where they carry no layout.
 *
 * That is also why the key that opens the chat must not type itself. Its WM_CHAR is already in the
 * queue when the chat opens on its WM_KEYDOWN, so the one character with the same scan code is
 * dropped. The drop is forgotten at the next WM_KEYDOWN and at the end of the frame, because a key
 * that types nothing, F5 say, leaves no character behind, and a drop that stood would eat the
 * first letter typed after it.
 *
 * ================================ The table ===================================================
 *
 *   shut, WM_KEYDOWN of the chat key, not a repeat, Control up    ask the mods further in first
 *   shut, one of them took it                                     leave it to them, stay shut
 *   shut, nobody took it                                          open
 *   shut, a repeat of the key that closed the chat                swallow it, so a held Escape
 *                                                                 does not go on to the pause
 *   shut, the release or a fresh press of that key                hand it on; the key is up
 *   shut, anything else                                           hand it on; the closing key
 *                                                                 stays down, whatever else is
 *                                                                 pressed
 *   open, WM_KEYDOWN Enter / Escape, not a repeat                 send / cancel
 *   open, WM_KEYDOWN Backspace, a repeat too                      erase one byte
 *   open, any other WM_KEYDOWN, and every repeat                  swallow
 *   open, WM_CHAR of the opening key, same scan code              drop
 *   open, WM_CHAR of a control character or DEL                   swallow
 *   open, WM_CHAR of anything else                                type it
 *   open, WM_DEADCHAR                                             swallow; the letter comes next
 *   open, WM_SYSKEYDOWN and everything else                       hand it on: Alt+F4 and Alt+Tab
 *                                                                 belong to the player
 *
 * The chain is asked first on the opening press because a mod further in may own that key: the
 * developer menu opens on a key of its own and takes every key while it is open. Its answer is
 * the only thing that says so, and it is asked for nothing but that one press.
 *
 * The key that closed the chat is down until its own release, or its own fresh press when the
 * release went to another window. The input module keeps the player's input held on the same
 * state, because Enter still down in a dialogue's choice would be read as a choice when it is let
 * go, and an arrow pressed meanwhile says nothing about Enter.
 */
#ifndef MULTIPLAYER_MP_CHAT_KEY_RULE_H
#define MULTIPLAYER_MP_CHAT_KEY_RULE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The window messages the table knows, as the platform numbers them. */
#define MP_CHAT_MSG_KEY_DOWN     0x0100u
#define MP_CHAT_MSG_KEY_UP       0x0101u
#define MP_CHAT_MSG_CHAR         0x0102u
#define MP_CHAT_MSG_DEAD_CHAR    0x0103u

/* The virtual keys the input line reads as keys rather than as text. */
#define MP_CHAT_VK_BACK          0x08
#define MP_CHAT_VK_RETURN        0x0D
#define MP_CHAT_VK_ESCAPE        0x1B

/* T, the key the chat opens on when the settings name none it can use, and U, the one it opens on
 * instead when the scoreboard is held with T. */
#define MP_CHAT_KEY_DEFAULT      0x54
#define MP_CHAT_KEY_SECOND       0x55

typedef enum mp_chat_key_action {
    MP_CHAT_KEY_PASS = 0,     /* hand the message on as it came */
    MP_CHAT_KEY_ASK_CHAIN,    /* the opening press: ask the mods further in, then answer below */
    MP_CHAT_KEY_OPEN,         /* nobody further in took it: the chat is open, the press is taken */
    MP_CHAT_KEY_LEFT,         /* a mod further in took it: its answer goes back, the chat is shut */
    MP_CHAT_KEY_TYPE,         /* append the character */
    MP_CHAT_KEY_ERASE,        /* remove the last byte */
    MP_CHAT_KEY_SEND,         /* say the line */
    MP_CHAT_KEY_CANCEL,       /* close without saying anything */
    MP_CHAT_KEY_SWALLOW,      /* take it and do nothing */
    MP_CHAT_KEY_DROP          /* the opening key's own character, taken and forgotten */
} mp_chat_key_action_t;

/* The rule's whole memory. All zero is a shut chat with nothing pending. */
typedef struct mp_chat_key_state {
    bool    open;
    bool    drop_pending;   /* the opening key's character may still be in the queue */
    uint8_t drop_scan;      /* and its scan code */
    int32_t closing_key;    /* the key that closed the chat, while it is still down; 0 for none */
} mp_chat_key_state_t;

/* What `message` does, given the chat key and whether Control is held as it arrives. It updates
 * the drop and the closing key as the table says; it opens and closes nothing itself. */
mp_chat_key_action_t mp_chat_key_decide(mp_chat_key_state_t *state, uint32_t message,
                                        uint32_t wparam, uint32_t lparam, int32_t chat_key,
                                        bool control_held);

/* The answer of the mods further in to the opening press, `lparam` being that press's. A non zero
 * answer leaves the chat shut and answers MP_CHAT_KEY_LEFT; zero opens it, with the drop of the
 * press's own character pending, and answers MP_CHAT_KEY_OPEN. */
mp_chat_key_action_t mp_chat_key_chain_answered(mp_chat_key_state_t *state, uint32_t lparam,
                                                int32_t answer);

/* The chat is shut, by `closing_key` while it is down, or by something that is not a key with 0. */
void mp_chat_key_closed(mp_chat_key_state_t *state, int32_t closing_key);

/* The end of a frame: a drop still pending belonged to a key that typed nothing. */
void mp_chat_key_frame_end(mp_chat_key_state_t *state);

/* Why a configured name is not the chat key. */
typedef enum mp_chat_key_verdict {
    MP_CHAT_KEY_NAME_ACCEPTED = 0, /* the name is the chat key */
    MP_CHAT_KEY_NAME_UNKNOWN,     /* not a name this build reads as a key */
    MP_CHAT_KEY_NAME_RESERVED,    /* a key the game, a menu or the scoreboard's default keeps */
    MP_CHAT_KEY_NAME_SCOREBOARD   /* the key the scoreboard is held with */
} mp_chat_key_verdict_t;

/* The key the chat falls back to beside the scoreboard's key `scoreboard_vk`: T, or U when the
 * scoreboard is held with T, so that one key never does both. */
int32_t mp_chat_key_fallback(int32_t scoreboard_vk);

/* The key the chat opens on, from the configured `name` and the scoreboard's `scoreboard_name`,
 * both as the file spells them: the name's key, or the fallback above when the name is refused.
 * `verdict` receives why. Refused are a name the scoreboard's grammar does not read, TAB, M, F4,
 * F6, F7, F8, F10, F11, F12, and the key the scoreboard's name means, TAB when that name is not
 * read either. Nothing else is refused: the developer menu's row shows the key this answers, and a
 * second list here would make it show a key the chat does not open on. */
int32_t mp_chat_key_pick(const char *name, const char *scoreboard_name,
                         mp_chat_key_verdict_t *verdict);

/* The verdict in words, for the log. */
const char *mp_chat_key_verdict_text(mp_chat_key_verdict_t verdict);

#endif /* MULTIPLAYER_MP_CHAT_KEY_RULE_H */
