/* mp_session_now.h: whether this machine is a client of a session that was started and has not
 * ended, read once out of the host's setup note and the drain's role.
 *
 * Layer 3. Five things ask it: the scene gate, the movie gate, the doors out of a world, the
 * level's own switches and the director's fog. They asked it in three places before, each with
 * its own read of the note, and one of them took the role from a flag set once at arming rather
 * than from the drain. The decision itself is mp_scene_client_of_a_started_session; this is the
 * one place its inputs are read. A sixth asks only its `runs`, the half without the role: the
 * judgement of a spoken line, which a host carries out as much as a client. The second question
 * below, whether this machine plays in a running session at all, is read here for the same reason.
 */
#ifndef MULTIPLAYER_MP_SESSION_NOW_H
#define MULTIPLAYER_MP_SESSION_NOW_H

#include <stdbool.h>
#include <stdint.h>

/* `runs` and `generation`, when given, come out of the same read in the same call, so the three
 * answers cannot come from two different notes. A side that holds no setup note is in no session,
 * and its generation is 0. */
bool mp_session_now_client_of_a_started_session(bool *runs, uint8_t *generation);

/* Whether this machine plays in a running session: what the host's settings, the world holds and
 * the hero carry ask before they act, one answer for all three, because three answers of their
 * own had come apart (a host's goodbye left one of them holding and another carrying). There are
 * two ways into a session, and they are told apart by who put the transport up
 * (mp_bridge_armed_by_menu):
 *
 *   the menu, with a setup note:
 *     a host:    its own setup note says started and not ended;
 *     a client:  the host's note says the same, AND the connection to the host stands;
 *   the menu, with no setup note:
 *     nobody plays. That is a lobby before the host's first note, and every lobby reset at a
 *     session's end, which the exit does in the same pump that takes a host's goodbye;
 *   the ini's way, which has no lobby and so no setup note while its session runs:
 *     a client:  the connection to the host stands;
 *     a host:    a peer of its session is connected.
 *
 * Every connection is read live from the sessions, never from the bridge's own joined flag, which
 * only a substep writes. False while no transport stands. `client`, when given, says whether this
 * machine is a client of the session, from the drain's role, whatever the answer. */
bool mp_session_now_plays_in_a_running_session(bool *client);

#endif /* MULTIPLAYER_MP_SESSION_NOW_H */
