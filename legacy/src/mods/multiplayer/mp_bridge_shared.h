/* mp_bridge_shared.h: what the bridge's pump and its install share, and nothing else.
 *
 * mp_bridge.c is the pump and sits on the size limit; mp_bridge_install.c puts the transport up
 * and takes it down, and mp_bridge_statement.c learns what a join is judged by. All three touch
 * the one state structure and the sessions and sockets it runs on, which stay the pump's own
 * statics and are handed over as pointers, so that no file's objects become process globals. No
 * other file includes this.
 */
#ifndef MULTIPLAYER_MP_BRIDGE_SHARED_H
#define MULTIPLAYER_MP_BRIDGE_SHARED_H

#include "mp_bridge_drain.h"
#include "mp_loopback.h"
#include "mp_relay_transport.h"
#include "mp_session.h"
#include "mp_transport.h"
#include "mp_udp.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Everything one bridge is. The three shapes it runs in are the mode: loopback is both ends in
 * one process, host and client are one end each over the real socket, and together the two of
 * them are the first two-machine game. */
typedef struct mp_bridge_state {
    mp_bridge_mode_t      mode;
    bool                  public_net;     /* host or client over the relay, not the LAN socket */
    bool                  joined;
    uint32_t              joins_seen;     /* the sessions' join count the last reset was for */
    bool                  join_failed_logged;

    bool                  in_tick;        /* a tick half or a bank window is running */
    uint32_t              joined_substeps;/* substeps this side spent with a peer, which is what
                                           * the states it sent and expected to receive count */
    uint32_t              substep_ends;   /* how often the end of a substep was reached at all,
                                           * so a sampling point that is never entered is a
                                           * number rather than a silence */
    bool                  substep_end_missing_logged;
    bool                  tick_ran_this_substep; /* the task half ran since the last substep end */
    uint32_t              substep_ends_without_tick;
    mp_bridge_drain_t     drain;          /* the reading half, with the counters it moves */

    uint32_t              substep;        /* the sender tick both roles stamp with */

    uintptr_t             pump_timer;     /* the thread timer's id, 0 when none was armed */
    uint32_t              timer_pumps;
    uint32_t              frame_pumps;
    uint32_t              pumps_refused;  /* idle pumps that found a tick half running */
    bool                  pump_refusal_logged;
    uint32_t              idle_drains;    /* idle pumps that found a payload to take */
    uint32_t              idle_payloads;  /* payloads they took, which no substep would have */
    uint32_t              last_substep_ms;  /* the wall clock of the last substep's first half */
    uint32_t              stall_pumps;      /* timer pumps with no substep for a second or more */
    uint32_t              longest_pumped_stall_ms;

    uint32_t              events_sent;
    uint32_t              events_unsent;  /* sends that reached NOBODY; a partial one has not */

    uint8_t               game_mode;      /* co-op or deathmatch, as the two sides agreed it */
    const char           *game_mode_name; /* its word, handed in by the feature, for the report */
    uint32_t              content;        /* the statement folded into one number, for the level's
                                           * content note; 0 while unknown */
    uint32_t              content_tries;  /* substeps that found the damage table not yet read */
    bool                  content_decided;
    bool                  connect_pending; /* client: the join waits for the statement */
    uint32_t              host_endpoint;   /* client: where it joins once the join is due */
} mp_bridge_state_t;

/* The state and everything it runs on, as the pump owns them. */
typedef struct mp_bridge_shared {
    mp_bridge_state_t *state;
    mp_loopback_t     *net;
    mp_udp_t          *udp;
    mp_relay_transport_t *relay;
    mp_session_t      *host;
    mp_session_t      *client;
    mp_transport_t    *host_transport;
    mp_transport_t    *client_transport;
} mp_bridge_shared_t;

const mp_bridge_shared_t *mp_bridge_shared(void);

/* Everything that indexes the far side's ticks or latches what was last applied from it, run on
 * every arrival and on every install: a restarted peer counts from tick one, which an old
 * history refuses. */
void mp_bridge_reset_peer_state(void);

#endif /* MULTIPLAYER_MP_BRIDGE_SHARED_H */
