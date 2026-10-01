/* movie_session.c: a movie in a multiplayer session, seen from this DLL's side of the movie note.
 * See the header.
 */
#include "movie_session.h"

#include "movie_path.h"
#include "movie_text.h"

#include "common/logging.h"
#include "common/movie_note.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* A read of the gate can lose its race with the multiplayer's write. Two reads in a row losing it
 * is already rare; a movie's role is decided once, so it is asked a third time rather than take a
 * lost race for no session. */
#define GATE_READ_TRIES 3u

typedef struct movie_session_state {
    movie_role_t     role;
    bool             begun;            /* this movie has been counted and filed as begun */
    char             name[64];         /* the engine's name for it, for the log */
    char             stem[MOVIE_NOTE_STEM_MAX];
    DWORD            begin_tick;
    DWORD            wait_tick;
    movie_gate_t     gate;             /* the newest gate that read */
    movie_baseline_t baseline;         /* the host's count and connection when the movie began */
    uint32_t         published_seen;   /* the gate's publication count, last read */
    uint32_t         quiet_ms;         /* the turns since that count moved, capped per turn */
    DWORD            turn_tick;        /* when the last turn asked */
    movie_verdict_t  released;         /* the answer that let the player go, or GO_ON */
    movie_state_t    note;             /* what this side last filed */
} movie_session_state_t;

static movie_session_state_t session;

static void file_the_state(void)
{
    (void)movie_note_publish_state(&session.note);   /* a refusal leaves the one before standing */
}

void movie_session_install(void)
{
    memset(&session.note, 0, sizeof session.note);
    session.note.state = MOVIE_STATE_IDLE;
    if (!movie_note_publish_state(&session.note)) {
        log_warning("the movie note could not be filed, so a multiplayer session on this machine "
                    "sees no movie player and holds no movie for its host");
    }
}

static bool read_the_gate(movie_gate_t *out)
{
    unsigned attempt;

    for (attempt = 0; attempt < GATE_READ_TRIES; ++attempt) {
        if (movie_note_read_gate(out)) {
            return true;
        }
    }
    return false;
}

static const char *reason_of(movie_verdict_t verdict)
{
    switch (verdict) {
    case MOVIE_VERDICT_NOT_RUNNING:
        return "the session is no longer running";
    case MOVIE_VERDICT_NOT_CONNECTED:
        return "the host is no longer connected";
    case MOVIE_VERDICT_OTHER_CONNECTION:
        return "the connection to the host is not the one the movie began on";
    case MOVIE_VERDICT_QUIET:
        return "the multiplayer has not filed its movie gate for three seconds of pumping";
    default:
        return "the session's gate says so";
    }
}

static const char *how_of(uint8_t end)
{
    switch (end) {
    case MOVIE_END_NATURAL:
        return "at its own end";
    case MOVIE_END_ESCAPE:
        return "by Escape";
    case MOVIE_END_FOCUS:
        return "by a lost foreground";
    case MOVIE_END_CLOSE:
        return "by the close box";
    case MOVIE_END_HOST:
        return "with the host's";
    case MOVIE_END_ALONE:
        return "alone";
    case MOVIE_END_RETAIL:
        return "by the retail player, at its own end or on a key";
    default:
        return "for no reason it names";
    }
}

static void count_the_movie(uint8_t path, uint8_t state, uint8_t end)
{
    session.begun      = true;
    session.begin_tick = GetTickCount();
    ++session.note.movies_begun;
    session.note.state      = state;
    session.note.end_reason = end;
    session.note.path       = path;
    session.note.locked     = session.role == MOVIE_ROLE_LOCKED;
    memcpy(session.note.stem, session.stem, sizeof session.note.stem);
    session.note.begin_ms = session.begin_tick;
    session.note.end_ms   = end != MOVIE_END_NONE ? session.begin_tick : 0u;
    file_the_state();
}

movie_role_t movie_session_decide(const char *name)
{
    bool gate_read;

    session.begun    = false;
    session.released = MOVIE_VERDICT_GO_ON;
    gate_read        = read_the_gate(&session.gate);
    session.role     = movie_rule_role(gate_read, &session.gate);
    if (session.role == MOVIE_ROLE_FREE) {
        return session.role;
    }

    memset(session.name, 0, sizeof session.name);
    memset(session.stem, 0, sizeof session.stem);
    if (name != NULL) {
        strncpy_s(session.name, sizeof session.name, name, _TRUNCATE);
        if (!movie_path_basename(name, session.stem, sizeof session.stem)) {
            session.stem[0] = '\0';   /* filed with no name rather than with a cut one */
        }
    }
    session.baseline.connection    = session.gate.connection;
    session.baseline.host_payloads = session.gate.host_payloads;
    session.published_seen         = session.gate.published;
    session.quiet_ms               = 0u;
    session.turn_tick              = GetTickCount();

    if (session.role == MOVIE_ROLE_SKIP) {
        log_info("the host is already in its level (its world is moving), so \"%s\" is not played "
                 "here at all", session.name);
        count_the_movie(MOVIE_PATH_NONE, MOVIE_STATE_ENDED, MOVIE_END_SKIPPED);
    } else if (session.role == MOVIE_ROLE_ALONE) {
        log_info("the movie \"%s\" is this side's own: this side is a client of a started session "
                 "whose host is not connected, so it ends as it does without a session, and the "
                 "session is still pumped while it plays", session.name);
    }
    return session.role;
}

void movie_session_begin(uint8_t path)
{
    if (session.role != MOVIE_ROLE_HOST && session.role != MOVIE_ROLE_LOCKED &&
        session.role != MOVIE_ROLE_ALONE) {
        return;
    }
    if (session.begun) {
        session.note.path = path;   /* libVLC did not start, and the retail player takes over */
        file_the_state();
        return;
    }
    count_the_movie(path, MOVIE_STATE_PLAYING, MOVIE_END_NONE);
    if (session.role == MOVIE_ROLE_LOCKED) {
        log_info("the movie \"%s\" belongs to the host: this side is a client of a started "
                 "session, so Escape, the close box and a lost foreground do not end it here; it "
                 "ends when the host's world moves (%u host payload(s) seen before it began)",
                 session.name, (unsigned)session.baseline.host_payloads);
    } else if (session.role == MOVIE_ROLE_HOST) {
        log_info("the movie \"%s\" is the host's: however it ends here, the session's players end "
                 "theirs with it; in a session a lost foreground and the close box do not end it, "
                 "only Escape and its own end", session.name);
    }
}

void movie_session_loop(movie_loop_t *loop)
{
    memset(loop, 0, sizeof *loop);
    loop->exits = movie_rule_exits(session.role);
    loop->end   = MOVIE_END_NONE;
    session.quiet_ms  = 0u;
    session.turn_tick = GetTickCount();
    if (session.role == MOVIE_ROLE_LOCKED && session.released != MOVIE_VERDICT_GO_ON) {
        loop->exits = movie_rule_exits(MOVIE_ROLE_ALONE);   /* let go in the first loop already */
    } else if (session.role == MOVIE_ROLE_LOCKED) {
        loop->poll          = &movie_session_poll;
        loop->waits         = &movie_session_wait;
        loop->wait_text     = movie_text_waiting(session.gate.language);
        loop->held_for_host = true;
    }
}

movie_verdict_t movie_session_poll(void)
{
    return movie_session_poll_at(GetTickCount());
}

movie_verdict_t movie_session_poll_at(uint32_t now)
{
    movie_gate_t    gate;
    bool            gate_read;
    bool            moved;
    movie_verdict_t verdict;

    if (session.role != MOVIE_ROLE_LOCKED) {
        return MOVIE_VERDICT_GO_ON;
    }
    gate_read = movie_note_read_gate(&gate);
    moved     = gate_read && gate.published != session.published_seen;
    if (gate_read) {
        session.gate           = gate;
        session.published_seen = gate.published;
    }
    /* The quiet counts the turns since the gate last moved, each for at most the cap, so a stall
     * of the game's thread, when nothing was pumped, cannot let the player go on its own. */
    session.quiet_ms  = movie_rule_quiet_after(session.quiet_ms, moved,
                                               (uint32_t)(now - session.turn_tick));
    session.turn_tick = now;
    verdict = movie_rule_verdict(&session.baseline, gate_read, &session.gate, session.quiet_ms);
    if (verdict != MOVIE_VERDICT_GO_ON && verdict != MOVIE_VERDICT_HOST_DONE &&
        session.released == MOVIE_VERDICT_GO_ON) {
        session.released = verdict;
        if (session.note.state == MOVIE_STATE_PLAYING) {
            log_info("the movie \"%s\" is this side's own again after %u ms: %s (connected %u, "
                     "running %u), so Escape, the close box and a lost foreground end it as they "
                     "do without a session", session.name,
                     (unsigned)(now - session.begin_tick), reason_of(verdict),
                     (unsigned)session.gate.host_connected, (unsigned)session.gate.running);
            session.note.locked = false;
            file_the_state();
        }
    }
    return verdict;
}

void movie_session_wait(void)
{
    session.wait_tick  = GetTickCount();
    session.note.state = MOVIE_STATE_WAITING;
    file_the_state();
    log_info("\"%s\" ended here after %u ms, before the host's: the picture stays black with "
             "\"Waiting for the host\" until the host's world moves", session.name,
             (unsigned)(session.wait_tick - session.begin_tick));
}

/* What a held movie's end says. The four endings are four different lines, because a success and
 * a failure that read alike in the log are not shippable. */
static void say_a_held_end(const movie_loop_t *loop, DWORD now)
{
    const movie_counts_t *c      = &loop->counts;
    bool                  waited = session.note.state == MOVIE_STATE_WAITING;

    if (loop->end == MOVIE_END_HOST && waited) {
        log_info("the host's world moved after a wait of %u ms: the movie is over for this side "
                 "too; %u Escape press(es), %u lost foreground(s) and %u close request(s) were "
                 "refused", (unsigned)(now - session.wait_tick), (unsigned)c->escapes_refused,
                 (unsigned)c->focus_losses_refused, (unsigned)c->closes_refused);
    } else if (loop->end == MOVIE_END_HOST) {
        log_info("the host's movie is over (its world moved after %u ms of playback here): this "
                 "side's movie ends with it; %u Escape press(es), %u lost foreground(s) and %u "
                 "close request(s) were refused", (unsigned)(now - session.begin_tick),
                 (unsigned)c->escapes_refused, (unsigned)c->focus_losses_refused,
                 (unsigned)c->closes_refused);
    } else if (loop->end == MOVIE_END_ALONE) {
        log_info("the wait for the host ended without it after %u ms: %s (connected %u, running "
                 "%u), so this side goes on alone", (unsigned)(now - session.wait_tick),
                 reason_of(session.released), (unsigned)session.gate.host_connected,
                 (unsigned)session.gate.running);
    } else {
        log_info("the movie \"%s\" ended here %s after %u ms, as this side's own once the session "
                 "had let it go; %u Escape press(es), %u lost foreground(s) and %u close "
                 "request(s) were refused while it was held", session.name, how_of(loop->end),
                 (unsigned)(now - session.begin_tick), (unsigned)c->escapes_refused,
                 (unsigned)c->focus_losses_refused, (unsigned)c->closes_refused);
    }
}

void movie_session_end(const movie_loop_t *loop)
{
    DWORD now;

    if (loop == NULL || !session.begun || session.role == MOVIE_ROLE_FREE ||
        session.role == MOVIE_ROLE_SKIP) {
        return;
    }
    now = GetTickCount();
    if (session.role == MOVIE_ROLE_LOCKED) {
        say_a_held_end(loop, now);
    } else if (session.role == MOVIE_ROLE_HOST) {
        log_info("the host's movie \"%s\" ended here %s after %u ms; %u lost foreground(s) and %u "
                 "close request(s) were refused", session.name, how_of(loop->end),
                 (unsigned)(now - session.begin_tick),
                 (unsigned)loop->counts.focus_losses_refused,
                 (unsigned)loop->counts.closes_refused);
    } else {
        log_info("the movie \"%s\" ended here %s after %u ms, as this side's own", session.name,
                 how_of(loop->end), (unsigned)(now - session.begin_tick));
    }

    /* One line per movie, and a nought in it is the failure: the session was not pumped while the
     * movie played, and a movie longer than the session's timeout has dropped it. More than nought
     * is not the proof that it was, because a timer with no window can be somebody else's; the
     * multiplayer's report counts its own, as its timer pump. */
    if (session.note.path == MOVIE_PATH_BINK) {
        log_info("during \"%s\" the retail player's own pump ran this DLL's timer %u time(s), "
                 "and every thread timer with it; the multiplayer's report counts its own as "
                 "its timer pump", session.name, (unsigned)loop->counts.thread_timers);
    } else {
        log_info("during \"%s\" %u thread timer message(s) were dispatched; the session's is "
                 "one of them, and the multiplayer's report counts its own as its timer pump",
                 session.name, (unsigned)loop->counts.thread_timers);
    }

    session.note.state      = MOVIE_STATE_ENDED;
    session.note.end_reason = loop->end;
    session.note.end_ms     = now;
    file_the_state();
}
