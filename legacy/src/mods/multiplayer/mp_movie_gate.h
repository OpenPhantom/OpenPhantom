/* mp_movie_gate.h: what a movie on this machine owes the session, said to the movie player.
 *
 * The movie player is another DLL and may not be called. It reads a gate this module files through
 * common/movie_note, from both pumps: the frame pump, which runs while frames are drawn, and the
 * timer pump, which is the only one that runs while a movie is on screen or a level loads. The
 * gate says whether a session runs, whether this machine is a client of it, whether that client's
 * host is connected, how many payloads of the host's arrived here and whether one arrived in the
 * last half second. The movie player decides from that alone: a client's movie ends when the count
 * has grown on the same connection, and is not played at all when the host's world already moves.
 *
 * The same pumps read the movie player's own record back. They count the movies it played for the
 * report, and hand a host's newest movie to the follow, which may announce the next world while
 * that movie still runs.
 */
#ifndef MULTIPLAYER_MP_MOVIE_GATE_H
#define MULTIPLAYER_MP_MOVIE_GATE_H

#include <stdbool.h>
#include <stdint.h>

/* One publication, and one read of the movie player's record. `runs` is a session started and not
 * ended, `client` that this machine is a client of it, and both come out of the one reading of the
 * setup note the scene gate is set from; `generation` is the one that note carries. Nothing is
 * filed while no transport stands. */
void mp_movie_gate_pump(bool runs, bool client, uint8_t generation);

/* The transport is down: a gate that says no session, so that no movie waits on a session that is
 * gone. Filed beside the session note's own last word, which is the one place a transport is said
 * to have come down. */
void mp_movie_gate_withdraw(void);

void mp_movie_gate_report(void);

#endif /* MULTIPLAYER_MP_MOVIE_GATE_H */
