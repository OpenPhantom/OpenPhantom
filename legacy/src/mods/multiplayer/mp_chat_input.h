/* mp_chat_input.h: the chat's key, its input line, and the one way it closes.
 *
 * Layer 3. A hook on the head of the engine's key handler sees every window message of a running
 * level before the engine and before the developer menu, which hooks the same head. The table in
 * mp_chat_key_rule decides what each message does; this module does it: it opens the chat, types
 * into the line, says the line through mp_chat, and closes.
 *
 * While the player types, the player's input is held, the same way a session's pause menu holds
 * it: the engine's four input readers answer "nothing pressed" for every game action, and the
 * other mods' mouse and pad paths learn it from the session note. The world, the substeps and the
 * wire go on; the body stands, falls and can be hit. Nothing here touches the control state the
 * dialogue writes, the player's module state or the simulation gate. The hold is the chat's own
 * holder in mp_armed, so neither the pause menu nor the chat can free the other's.
 *
 * The one way out is mp_chat_input_close, whatever the reason: Enter, Escape, a menu that opens, a
 * movie, the level ending or loading, the session ending, the player's repair lock. The frame pump
 * looks for the reasons no key brings once a frame, the end of a session calls it from the
 * session's own exit, and the repair calls it for the player who asked to be let go. A chat
 * closed by a key keeps the input held until that key is up again, for at most a second: Enter
 * still down after the hold was gone would be read by a dialogue as a choice when it is let go.
 *
 * With no transport nothing here runs. The hook, which cannot be taken out again, closes a chat
 * that is still open and hands every message on.
 *
 * Nothing here writes a text into a log: the report counts keys and characters.
 */
#ifndef MULTIPLAYER_MP_CHAT_INPUT_H
#define MULTIPLAYER_MP_CHAT_INPUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Why the chat closed, one count each in the report. */
typedef enum mp_chat_close {
    MP_CHAT_CLOSE_ENTER = 0,   /* sent, or nothing to send */
    MP_CHAT_CLOSE_ESCAPE,
    MP_CHAT_CLOSE_MENU,        /* a menu of the engine's opened over the level */
    MP_CHAT_CLOSE_MOVIE,
    MP_CHAT_CLOSE_LEVEL,       /* the level ended, or a load holds the simulation */
    MP_CHAT_CLOSE_SESSION,
    MP_CHAT_CLOSE_REPAIR,      /* the player's repair lock, pressed in the developer menu */
    MP_CHAT_CLOSE_COUNT
} mp_chat_close_t;

/* On a session's way in: the hook once for the process, the cells it reads, and the key read from
 * the settings. False when the hook could not be placed; the chat then never opens, and the lines
 * of the others are still shown as long as the level outcome and the menu on show resolved. Without
 * those two the chat neither opens nor draws. A missing movie cell reads as no movie. */
bool mp_chat_input_arm(void);

/* Once a frame, from the frame pump: the reasons to close, the end of the frame for the table, the
 * hold left by a closing key, and once a second of the wall clock the key read again. */
void mp_chat_input_frame(void);

/* The reasons to close that no key brings, from a pump that runs when no frame is drawn as well:
 * a movie is on screen then and only the timer pump runs. */
void mp_chat_input_look(void);

/* The one way out. Closes an open chat, forgets its line and counts `reason`; a chat that is shut
 * already only lets go of a hold its closing key left. */
void mp_chat_input_close(mp_chat_close_t reason);

/* Whether the player is typing into the chat right now, for a key polled past the hook. */
bool mp_chat_input_is_typing(void);

/* Whether the chat belongs on screen: a transport stands, a level runs, and no menu, no movie and
 * no load is over it. The same question decides whether the chat may open. */
bool mp_chat_input_in_play(void);

/* The line being typed while the chat is open, NULL while it is shut. `refused` receives whether
 * the last Enter sent nothing, within the second after it, at `now_ms` of the wall clock. */
const char *mp_chat_input_line(uint32_t now_ms, size_t *length, bool *refused);

/* The report's line. Counts only. */
void mp_chat_input_report(void);

#endif /* MULTIPLAYER_MP_CHAT_INPUT_H */
