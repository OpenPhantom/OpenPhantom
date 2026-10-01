/* movie_rule.h: who decides when a movie ends, as arithmetic.
 *
 * Pure, so the whole decision runs with no game, no window and no session in the process. Reading
 * the multiplayer's gate, the window and the message pump are elsewhere; what is here is the
 * decision, which is the part worth driving over every value it can be given.
 *
 * In a multiplayer session a movie belongs to the host. The host plays its own and ends it as a
 * player does, by Escape or at its end; a client plays along and cannot end it; the client's ends
 * when the host's world begins to move, which is the one signal a client can see without anything
 * new on the wire. Outside a session nothing here changes a thing: the role is FREE and every way
 * out that has always ended a movie still does.
 */
#ifndef MOVIE_RULE_H
#define MOVIE_RULE_H

#include "common/movie_note.h"

#include <stdbool.h>
#include <stdint.h>

/* What a movie beginning now is, decided once, when it begins. */
typedef enum movie_role {
    MOVIE_ROLE_FREE,     /* no session: every way out that has always ended a movie still does */
    MOVIE_ROLE_ALONE,    /* a client whose host is not connected: its own movie, as with no
                          * session, but the session is still pumped so it can come back */
    MOVIE_ROLE_HOST,     /* the host of a running session */
    MOVIE_ROLE_SKIP,     /* a client whose host's world already moves: not played at all */
    MOVIE_ROLE_LOCKED    /* a client whose host is still in its movie */
} movie_role_t;

/* Which ways out end a movie, and whether the session's thread timers are dispatched meanwhile. */
typedef struct movie_exits {
    bool escape_ends;    /* Escape, and the controller's Start, which arrives as Escape */
    bool focus_ends;     /* the game losing the foreground */
    bool close_ends;     /* the close box */
    bool pump_session;
} movie_exits_t;

/* What the gate says on a turn of a movie that is held for the host. */
typedef enum movie_verdict {
    MOVIE_VERDICT_GO_ON,
    MOVIE_VERDICT_HOST_DONE,          /* the host's payloads grew on the connection it began on */
    MOVIE_VERDICT_NOT_RUNNING,        /* the session does not run any more */
    MOVIE_VERDICT_NOT_CONNECTED,      /* the host is not connected any more */
    MOVIE_VERDICT_OTHER_CONNECTION,   /* connected, but not on the connection the movie began on */
    MOVIE_VERDICT_QUIET               /* the gate has not been filed for MOVIE_RULE_QUIET_MS */
} movie_verdict_t;

/* How long a held movie waits on a gate nobody files any more before it lets the player go. The
 * multiplayer files one every thirty milliseconds from a timer this movie's own pump dispatches,
 * so three seconds of pumping without one is a multiplayer that stopped, not a slow one; a held
 * client would otherwise wait for good. */
#define MOVIE_RULE_QUIET_MS 3000u

/* How much of one turn counts towards that quiet. A turn is a millisecond or two in this DLL's own
 * loop and a frame on the retail player's, and every turn pumps before it asks. A turn that took
 * longer is a stall of the game's thread, a display mode switch or a player being stopped, in
 * which nothing was pumped and nobody could file anything, and it counts for no more than this. */
#define MOVIE_RULE_TURN_CAP_MS 100u

/* What a held movie compares the gate against: the host's count and connection when it began. */
typedef struct movie_baseline {
    uint64_t connection;
    uint32_t host_payloads;
} movie_baseline_t;

/* What a movie loop counts, for the lines that say what it refused and what it dispatched. */
typedef struct movie_counts {
    uint32_t escapes_refused;
    uint32_t focus_losses_refused;
    uint32_t closes_refused;
    uint32_t thread_timers;     /* timer messages with no window dispatched, the session's among
                                 * them; on the retail path the times its pump ran this DLL's own
                                 * timer */
} movie_counts_t;

typedef movie_verdict_t (*movie_poll_fn_t)(void);
typedef void (*movie_waits_fn_t)(void);

/* What the session side hands a movie loop, and what the loop hands back. */
typedef struct movie_loop {
    movie_exits_t    exits;
    movie_poll_fn_t  poll;           /* asked once a turn while the host decides; NULL otherwise */
    movie_waits_fn_t waits;          /* told once when this side's movie ends first */
    const char      *wait_text;      /* what the black picture says meanwhile */
    bool             held_for_host;  /* the movie began held, so its keys are the host's to spend */
    movie_counts_t   counts;
    uint8_t          end;            /* MOVIE_END_*, why the loop stopped */
} movie_loop_t;

/* The role of a movie beginning now, from the gate the multiplayer filed, or from none. A gate
 * that did not read is no session. */
movie_role_t movie_rule_role(bool gate_read, const movie_gate_t *gate);

/* Which ways out end a movie of this role. */
movie_exits_t movie_rule_exits(movie_role_t role);

/* One turn of a held movie. `gate_read` false means the gate did not read this turn, which alone
 * decides nothing: a torn read succeeds on the next one. `quiet_ms` is what movie_rule_quiet_after
 * has made of the turns since the gate's publication count last moved. The host is done when its
 * payload count has grown on the live connection the movie began on; it is an answer about that
 * connection only. */
movie_verdict_t movie_rule_verdict(const movie_baseline_t *begin, bool gate_read,
                                   const movie_gate_t *now, uint32_t quiet_ms);

/* The session let the player go while a held movie still plays: the loop goes on as this side's
 * own movie, with every way out a movie without a session has, and asks the host nothing more.
 * The session is still pumped, in case it comes back for the next movie. */
void movie_rule_let_go(movie_loop_t *loop);

/* The quiet after one more turn: nought when the gate moved in it, otherwise what it was plus the
 * turn, the turn counted for at most MOVIE_RULE_TURN_CAP_MS. It never wraps. */
uint32_t movie_rule_quiet_after(uint32_t quiet_ms, bool gate_moved, uint32_t turn_ms);

#endif /* MOVIE_RULE_H */
