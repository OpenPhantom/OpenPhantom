/* mp_bridge_report.h: what the bridge prints about the far body.
 *
 * Split from the bridge so that the file that steers a session and the file that describes one
 * can be read apart. The numbers that belong to the bridge itself travel as an argument rather
 * than through a shared state header: there are six of them, and passing them keeps the bridge's
 * own record private to the bridge.
 */
#ifndef MULTIPLAYER_MP_BRIDGE_REPORT_H
#define MULTIPLAYER_MP_BRIDGE_REPORT_H

#include "mp_interp.h"
#include "mp_session.h"
#include "mp_udp.h"

#include <stdbool.h>
#include <stdint.h>

/* The bridge's own counters that the far body's report quotes. */
typedef struct mp_bridge_report_run {
    uint32_t timer_pumps;
    uint32_t frame_pumps;
    uint32_t pumps_refused;
    uint32_t idle_drains;       /* idle pumps that found a payload waiting, and so ran the drain */
    uint32_t idle_payloads;     /* payloads taken between substeps, which used to be discarded */
    uint32_t stall_pumps;       /* timer pumps that ran while no substep had for a second */
    uint32_t longest_pumped_stall_ms;
    uint32_t substep_ends;      /* ends of a substep reached: where the SENDING half now runs.
                                 * Zero here with a joined session means nothing left this side */
    uint32_t ends_without_tick; /* ends that found no task half since the last one, and therefore
                                 * sent nothing: the stamp would have repeated */
    uint32_t joined_substeps;   /* substeps spent with a peer, the denominator of the world line */
    uint32_t content;           /* the content fingerprint, 0 when it could not be read */
    bool     content_decided;   /* whether it has been read at all yet */

    /* What the opening line says. It travels in the struct rather than being read out of the
     * bridge, because that is the difference between a report that lives beside the state and a
     * report that is handed it. */
    const char *shape;          /* "loopback", "UDP host", "UDP client" */
    const char *game_mode_name;
    bool        joined;
    uint32_t    commands_sent;
    uint32_t    commands_applied;
    uint32_t    commands_refused;
    uint32_t    states_in;
    uint32_t    puppet_applies;
    uint32_t    events_sent;
    uint32_t    events_unsent;
    uint32_t    events_in;
    uint32_t    events_unplaced;  /* moments naming a slot no far bank shows */
    uint32_t    notes_refused;    /* a client's note only a host says, or in another's name */
    uint32_t    substep;
    bool        is_loopback;
    bool        is_host;
    uint32_t    loopback_delivered;
    uint32_t    loopback_dropped;
} mp_bridge_report_run_t;

/* What the round has done, as plain numbers.
 *
 * It travels as numbers rather than as the score itself so that this file needs nothing of the
 * module that keeps the table. The round lives with the game mode and the rule set, one layer up,
 * and printing it from there would have put the one line that says whether a deathmatch is being
 * scored somewhere other than the rest of the bridge's report. */
typedef struct mp_bridge_report_round {
    bool     running;
    uint8_t  generation;      /* the world change this round belongs to */
    uint8_t  outcome;         /* running, won by a player, won by a team, drawn */
    uint8_t  winner;
    uint32_t elapsed;         /* substeps since the round began, on the host's own counter */
    uint32_t deaths_taken;
    uint32_t deaths_refused;
    uint32_t deaths_after_end;
    uint32_t deaths_outside;  /* deaths heard with no round running: a co-op level, or a lobby */
    uint32_t boards_sent;
    uint32_t boards_unsent;
    uint32_t boards_taken;
    uint32_t boards_torn;
    uint32_t boards_adopted;
    uint32_t boards_refused;  /* a peer told the authority what the score was */
    uint32_t rounds_ended;
} mp_bridge_report_round_t;

void mp_bridge_report_round(const mp_bridge_report_round_t *round);

void mp_bridge_report_far_body(const mp_interp_t *interp, const mp_bridge_report_run_t *run);

/* The whole of a bridge's report: the opening line, the world, and then the far body, the far
 * players' states, the sessions, the roster and the lobby, each from its own file. `why` is what
 * asked for it. `udp` is NULL for a session over the relay, whose transport reports itself. */
void mp_bridge_report_all(const char *why, const mp_bridge_report_run_t *run,
                          const mp_session_t *host, const mp_session_t *client,
                          const mp_udp_t *udp);

/* What the game socket has been told. Separate from the session line above it because the
 * two answer different questions: the session says what it made of a packet, this says
 * whether there was a packet at all. */
void mp_bridge_report_socket(const mp_udp_t *udp, const mp_session_t *host,
                             const mp_session_t *client);

/* What the two sessions saw, host and client added: only one of them is ever connected. */
void mp_bridge_report_sessions(const mp_session_t *host, const mp_session_t *client);

/* Renames this instance's window so two twins on one screen can be told apart: the unowned
 * visible window of this thread, never by class, because the client renamed its class. */
void mp_bridge_report_caption(bool is_host);

#endif /* MULTIPLAYER_MP_BRIDGE_REPORT_H */
