/* mp_bridge_lobby_late.h: a client whose lobby opens on a session that is already running.
 *
 * Layer 3, the lobby's side. A client took every start whose generation it had not played, and
 * a fresh lobby has played none, so a player who joined a running session was in the level the
 * moment the host's setup arrived, with hero 0 and without ever pressing ready. The rule that a
 * start waits for this player's ready is mp_lobby_start_may_be_taken, and the lobby screen's frame
 * is the one place that asks it before taking a start. This module answers the question the
 * screen shows, whether this lobby holds a running session's start, and keeps the account of what
 * happened to one: held for want of ready, taken, and taken while not ready, which must never
 * happen and which is counted wherever the start was taken from, so a screen that does not ask the
 * rule shows up as a number.
 *
 * It also remembers whether the start this side last took from its lobby was of a session that
 * was already running when the lobby first heard its host. The arrival asks: a player who comes
 * late finds the players below its slot standing, and a seat held for them would be a seat held
 * for nobody.
 */
#ifndef MULTIPLAYER_MP_BRIDGE_LOBBY_LATE_H
#define MULTIPLAYER_MP_BRIDGE_LOBBY_LATE_H

#include <stdbool.h>
#include <stdint.h>

/* CLIENT: whether this lobby holds the host's setup with STARTED and without ENDED, under a
 * generation this side has not acted on: the start of a session whose host already plays, not
 * taken yet. The lobby's band says so while it is true and this player is not ready. */
bool mp_bridge_lobby_joins_running_session(void);

/* From the lobby's tick, told whether a client's lobby screen is open. While a running session's
 * start waits for this player's ready, the wait is measured, and said once per lobby. */
void mp_bridge_lobby_late_tick(bool client_lobby_open);

/* From the lobby, after it took the host's setup, told whether its screen is open. The first
 * setup a lobby hears decides whether its session was already running when it came. */
void mp_bridge_lobby_late_heard_setup(bool lobby_open);

/* From the lobby, after a start was taken, told whether its screen is open. A start taken with the
 * screen shut is the follow's, a world change, and no join. One taken from the screen while this
 * player is not ready is counted, whoever asked the rule first. */
void mp_bridge_lobby_late_took_start(bool lobby_open);

/* From the lobby's reset: a new lobby has heard nothing, held nothing and taken nothing. */
void mp_bridge_lobby_late_reset(void);

/* Whether the start last taken from a lobby was of a session already running when that lobby
 * first heard its host. False after a world change and in a lobby's own start. */
bool mp_bridge_lobby_late_start(void);

/* What a client's lobby did with the starts it was offered, over the run. */
typedef struct mp_bridge_lobby_late_counts {
    uint32_t taken;             /* starts taken from a lobby */
    uint32_t held;              /* running sessions' starts held for want of ready */
    uint32_t longest_ms;        /* the longest such wait that has ended */
    uint32_t taken_not_ready;   /* starts taken from a lobby while this player was not ready:
                                 * must be 0 */
} mp_bridge_lobby_late_counts_t;

void mp_bridge_lobby_late_counts(mp_bridge_lobby_late_counts_t *out);

/* The report's lines of a join: on a host, the players who joined its running level; on a client,
 * its lobby's start and what its lobby dropped of a running level. */
void mp_bridge_lobby_late_report(bool is_host);

#endif /* MULTIPLAYER_MP_BRIDGE_LOBBY_LATE_H */
