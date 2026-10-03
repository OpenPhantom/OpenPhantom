/* common/session_note.h: whether a multiplayer session is running in this process, said by the
 * one mod that knows.
 *
 * WHY THIS EXISTS. The developer overlay holds rows a running session takes away. A level skip
 * takes this machine out of the host's world on its own; a row whose value the session decides,
 * such as the host's draw distance or difficulty on a client, would change what the session puts
 * back or does not read; and a spawned actor that the session cannot name borrows the identity of
 * the placement it was copied from. None of that is the panel's fault and none of it is visible
 * from there: a feature DLL may not call into another one, and there is nothing in the engine
 * that says "a session is running". So the multiplayer says it, through the same channel and in
 * the same shape as the appearance note beside it, and whoever cares listens.
 *
 * What it is not. It is not a lock. A reader learns what was true at the last publication, which
 * is the moment a transport went up or came down, and acts on it when it next draws. Nothing here
 * can stop a row being pressed in the same millisecond a session begins. What it can do is keep a
 * panel from offering, for a whole session, a row the session cannot take.
 *
 * No note at all is the ordinary state of an installation whose multiplayer never armed, and it
 * reads as "no session", which is the same answer that mod gives before its first lobby.
 *
 * THE INPUT HOLD. In a session the pause menu does not stop the world, and neither does typing
 * into the chat, so the player's body goes on being simulated and only its input is held. The
 * multiplayer holds the engine's own readers; enhanced_input turns the view from the raw mouse and
 * walks from the pad's stick past those readers, and learns from this note that it has to stand
 * still too. The flag says that anything of the session holds the input, whoever it is. The flag
 * rides a byte the record always carried and every publisher zeroed, so the shape and its version
 * are unchanged: a reader built before the flag ignores it, and a publisher built before it says
 * "not held".
 */
#ifndef COMMON_SESSION_NOTE_H
#define COMMON_SESSION_NOTE_H

#include <stdbool.h>
#include <stdint.h>

/* The name the record is filed under. Both sides use this literal and nothing else. */
#define SESSION_NOTE_NAME "mp_session"

/* Until the first read that found the note, a reader that asks every frame asks the operating
 * system at most this often: a name nobody has filed costs a failed mapping lookup each time, and
 * in single player nobody ever files it. */
#define SESSION_NOTE_RETRY_MS 1000u

/* A publisher fills the whole record, zeroed first: a field it does not set is published as false
 * and not as whatever the stack held. */
typedef struct session_note {
    bool running;    /* a transport stands: a lobby is open, a session level runs, or an exit is
                      * still saying its last word */
    bool is_host;    /* this machine is the one the others follow. Meaningless while `running` is
                      * false, and published as false there rather than left as it was */
    bool input_held; /* a holder of the session, its pause menu, its chat or a scene that keeps
                      * the host at its place, holds this player's input. Published
                      * as false while `running` is false, for the same reason */
} session_note_t;

/* Says what this process is doing. False when the channel refused, in which case nothing was
 * published and a reader goes on seeing the note before it. */
bool session_note_publish(const session_note_t *note);

/* Reads it back. False when nobody has published one, when the record is not the shape this build
 * knows, or when the read lost its race; `out` is left alone then, and a caller that wants "no
 * session" for those cases zeroes it first. That is deliberate: a refusal is not a fact about the
 * session, and a caller that treats it as one would unlock a row mid-session on a lost race. */
bool session_note_read(session_note_t *out);

/* Whether the session holds this player's input, for a caller on a frame or substep
 * path. False when no note has been filed yet or the read refused: an input path that cannot tell
 * goes on as it always did. Until one read has found the note it asks the operating system at most
 * once per SESSION_NOTE_RETRY_MS of `now_ms`; after that every read is a copy out of memory. */
bool session_note_input_held(uint32_t now_ms);

/* Whether the last read in this module found a note filed in a shape it does not read, which is
 * a module from another build on the other end. No note at all is not this: it answers false. A
 * reader that must not unlock what it cannot judge locks while this is true. */
bool session_note_unreadable(void);

#endif /* COMMON_SESSION_NOTE_H */
