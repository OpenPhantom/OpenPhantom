/* mp_chat.h: the chat of a session, as this machine runs it.
 *
 * Layer 3. A player says a line; its host decides who said it and gives the line to every player,
 * the one who said it included; every machine keeps the newest lines it was given for its chat box.
 * A client sends only its text (mp_chat_wire.h). A listen host answers its clients' says with the
 * same rule a dedicated server uses (mp_chat_rule.h), and its own player's line goes through that
 * rule too, into its own history and out to every client.
 *
 * THE CLOCK. Every time here is mp_wallclock_ms(), the millisecond clock the bridge runs its
 * sessions on: a line that arrives is dated with its session's clock, which is that one, and a
 * caller that says a line hands it in. A reader that measures how old a line is subtracts its
 * `shown_ms` from mp_wallclock_ms().
 *
 * Nothing here writes a text or a name into a log. The report counts lines and nothing else, and
 * no line of the log is written per line of chat.
 *
 * Nothing here runs without a session: the bridge binds the sessions where it binds the lobby's, a
 * say with none bound is refused, and the one exit of a session forgets everything.
 */
#ifndef MULTIPLAYER_MP_CHAT_H
#define MULTIPLAYER_MP_CHAT_H

#include "mp_chat_rule.h"
#include "mp_roster.h"
#include "mp_session.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* How many lines this machine keeps. The box shows six; the rest are there for a box that shows
 * more while the player types. */
#define MP_CHAT_HISTORY 32u

typedef struct mp_chat_line {
    uint8_t  slot;                             /* the world slot of the player who said it */
    char     name[MP_ROSTER_NAME_MAX];         /* what the host calls that player, terminated */
    char     text[MP_CHAT_TEXT_MAX + 1u];      /* terminated */
    uint32_t shown_ms;   /* mp_wallclock_ms() when this machine put it into the history */
} mp_chat_line_t;

typedef enum mp_chat_say_result {
    MP_CHAT_SAY_SENT,        /* on its way to the host, or on a host in the history and out */
    MP_CHAT_SAY_TOO_FAST,    /* held back by this side's own bucket; the text is still the
                              * caller's to send a moment later */
    MP_CHAT_SAY_EMPTY,       /* nothing left after the trim, or a text no line may carry */
    MP_CHAT_SAY_UNSENT,      /* the channel to the host took nothing; the bucket keeps its line */
    MP_CHAT_SAY_NO_SESSION
} mp_chat_say_result_t;

/* The two sessions and which one this machine speaks through, bound where the lobby's are. */
void mp_chat_bind(mp_session_t *host, mp_session_t *client, bool is_client);

/* This machine's player says `length` bytes of `text` at `now_ms`, which is mp_wallclock_ms().
 * The spaces at both ends are trimmed; a text longer than MP_CHAT_TEXT_MAX is refused as empty
 * rather than cut, because a line cut on its way out says something its player did not type. */
mp_chat_say_result_t mp_chat_say(const char *text, size_t length, uint32_t now_ms);

/* A host's reader of a note a client sent: true when it is a say, which is then answered by the
 * host rule and counted however it goes. `sender_slot` is the slot the host told that client. */
bool mp_chat_take_say(size_t peer_index, uint8_t sender_slot, const uint8_t *note, size_t bytes);

/* A client's reader of a note its host sent: true when it is a line, which is then kept or
 * counted as torn. `own_slot` is the slot the host told this machine, and MP_CHAT_SLOTS while it
 * has not been told one, so that no line counts as this side's own before then. */
bool mp_chat_take_line(uint8_t own_slot, const uint8_t *note, size_t bytes);

/* Up to `max` of the newest lines, oldest first and newest last, as pointers into the history,
 * and how many. A pointer stays good until the next line arrives or the history is forgotten. */
size_t mp_chat_newest(const mp_chat_line_t **out, size_t max);

/* Forgets the history, the buckets and the bound sessions, and keeps the counts for the report.
 * Called from the one exit of a session. */
void mp_chat_forget(void);

/* What the typing side counts, on every machine. `said` is every line this side's player said
 * with a session bound, and every one of them ends in exactly one of the next four. */
typedef struct mp_chat_side_counts {
    uint32_t said;
    uint32_t sent;
    uint32_t held_back;
    uint32_t empty;
    uint32_t unsent;
    uint32_t came_back;   /* this side's own lines, back from the host */
    uint32_t shown;       /* other players' lines */
    uint32_t torn;
    uint32_t after_end;   /* says and lines that arrived after the session's end, left unread */
} mp_chat_side_counts_t;

/* Everything the report prints, as numbers: this side's counts, and a host's, which only a
 * machine that was ever bound as one prints. The counts outlive the session like the report. */
typedef struct mp_chat_counts {
    mp_chat_side_counts_t side;
    mp_chat_host_counts_t host;
    bool                  was_host;
} mp_chat_counts_t;

void mp_chat_counts(mp_chat_counts_t *out);

/* The report's lines: this side's on every machine that ran a session, the host's on a machine
 * that was ever bound as one. Counts only. */
void mp_chat_report(void);

#endif /* MULTIPLAYER_MP_CHAT_H */
