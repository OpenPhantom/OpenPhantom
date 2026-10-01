/* common/movie_note.h: what the movie player and the multiplayer tell each other about a movie,
 * so that in a session the host's movie decides when everybody's ends.
 *
 * Two feature DLLs that may not call each other. The movie player plays the pre-rendered movies,
 * through libVLC or through the retail Bink player; the multiplayer knows whether a session runs,
 * which side of it this machine is on, and whether the host's world has begun to move. What passes
 * between them is two records filed through common/shared_note, each with exactly one writer:
 *
 *   movie_state   written by the movie player: how many movies this side began in a session, what
 *                 the newest one is called, whether it plays, waits for the host or has ended, and
 *                 how it ended;
 *   movie_gate    written by the multiplayer: whether a session runs, whether this machine is a
 *                 client of it, whether that client's host is connected, how many payloads of the
 *                 host's arrived here, and whether one arrived in the last half second.
 *
 * STATE, NOT EVENTS. Both records are rewritten whole, and a reader acts on what they say now. The
 * movie player reads the gate when a movie begins and on every turn of its loop while that movie
 * belongs to the host; the multiplayer reads the state from its pumps, both of them, because only
 * its timer pump runs while a movie is on screen.
 *
 * A count, not a time. The multiplayer's clock is a performance counter with a base of its own,
 * which the movie player cannot read. So "the host's movie is over" is a count that has grown: the
 * movie player notes the count and the connection when a movie begins, and ends the movie once the
 * count is higher on the same connection. The host sends payloads only from its substeps, and a
 * host that is in its movie runs none.
 *
 * No note at all is what a machine without a multiplayer session has. The movie player reads that
 * as no session, which is exactly how it behaves there. A multiplayer that finds no state record
 * knows the movie player is absent, switched off or older, and says so in its report.
 *
 * A read that is refused changes nothing on the reader's side, as with every record filed this
 * way: a torn one succeeds a turn later, and one of another shape fails every time, which a reader
 * treats as no record.
 */
#ifndef COMMON_MOVIE_NOTE_H
#define COMMON_MOVIE_NOTE_H

#include <stdbool.h>
#include <stdint.h>

/* The names the records are filed under. Both sides use these literals and nothing else. */
#define MOVIE_STATE_NOTE_NAME "movie_state"
#define MOVIE_GATE_NOTE_NAME  "movie_gate"

/* A movie's name after its last separator, "scene1" for "movie\scene1". The engine names eleven
 * movies and the longest is six characters; one that does not fit is published with no name. */
#define MOVIE_NOTE_STEM_MAX 16u

/* Where the newest movie is. */
#define MOVIE_STATE_IDLE    0u   /* none has begun in a session yet */
#define MOVIE_STATE_PLAYING 1u
#define MOVIE_STATE_WAITING 2u   /* this side's movie is over first, and it holds black for the
                                  * host's */
#define MOVIE_STATE_ENDED   3u

/* How it ended. */
#define MOVIE_END_NONE    0u
#define MOVIE_END_NATURAL 1u   /* the file's own end */
#define MOVIE_END_ESCAPE  2u
#define MOVIE_END_FOCUS   3u   /* the game lost the foreground */
#define MOVIE_END_CLOSE   4u   /* the close box */
#define MOVIE_END_HOST    5u   /* the host's world moved */
#define MOVIE_END_SKIPPED 6u   /* not played: the host's world was already moving */
#define MOVIE_END_ALONE   7u   /* the session went away while this side waited for its host */
#define MOVIE_END_RETAIL  8u   /* the retail player ended it, at its own end or on a key; it does
                                * not say which */
#define MOVIE_END_MAX     MOVIE_END_RETAIL

/* Which player the newest movie went through. */
#define MOVIE_PATH_NONE 0u
#define MOVIE_PATH_VLC  1u
#define MOVIE_PATH_BINK 2u

typedef struct movie_state {
    uint16_t movies_begun;             /* every movie this side began while a session ran, for the
                                        * life of the process; a skipped one counts */
    uint8_t  state;                    /* MOVIE_STATE_* */
    uint8_t  end_reason;               /* MOVIE_END_*, NONE until the movie has ended */
    uint8_t  path;                     /* MOVIE_PATH_* */
    bool     locked;                   /* the newest movie belongs to the host */
    char     stem[MOVIE_NOTE_STEM_MAX];   /* printable, terminated inside the field */
    uint32_t begin_ms;                 /* the movie player's own tick count */
    uint32_t end_ms;
} movie_state_t;

typedef struct movie_gate {
    bool     running;          /* a session was started and has not ended */
    bool     client;           /* this machine is a client of it; false while it does not run */
    bool     host_connected;   /* the client's connection to its host is up; false on a host */
    bool     host_moving;      /* a payload of the host's arrived in the last half second, on that
                                * connection; false on a host */
    uint8_t  generation;       /* the world generation the host's setup note carries */
    uint8_t  language;         /* the language_t the session's own texts are drawn in */
    uint64_t connection;       /* the client's connection to its host, 0 when none */
    uint32_t host_payloads;    /* every payload of the host's that arrived, refused ones included;
                                * it only grows */
    uint32_t published;        /* bumped at every publication, so a reader can tell a gate that
                                * is still being written from one nobody writes any more */
} movie_gate_t;

/* Files a record under its name. False when a field is out of its range or the stem is not a
 * terminated printable name, or when the channel refused; nothing is published then and a reader
 * goes on seeing the record before it.
 *
 * The gate is filed in its one consistent shape: a machine whose session does not run is nobody's
 * client, and a machine that is no client has no host to be connected to or to see moving. */
bool movie_note_publish_state(const movie_state_t *state);
bool movie_note_publish_gate(const movie_gate_t *gate);

/* Reads one back. False when nobody has published it, when it is torn, or when it is of another
 * shape or holds anything the publisher would have refused; `out` is left alone then. */
bool movie_note_read_state(movie_state_t *out);
bool movie_note_read_gate(movie_gate_t *out);

#endif /* COMMON_MOVIE_NOTE_H */
