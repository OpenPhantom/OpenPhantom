/* bink_cells.h: the retail Bink player's two cells, found and written, and the watch this DLL keeps
 * on that player while a movie of a session plays through it.
 *
 * The retail player ends a movie only at its own end or on a character key, and the key only when
 * the caller asked for that with its flags. A client in a session must not skip the host's movie,
 * which costs nothing: the flags are handed over as 0. Ending the client's movie when the host's
 * is over is the other half, and the player offers no call for it. It does keep an abort cell that
 * its loop reads every round, and this file finds that cell and writes it.
 *
 * Both cells are read out of the movie window's own handler, from instructions behind the head of
 * its switch, with no address written down; see bink_cells.c for the bytes.
 */
#ifndef BINK_CELLS_H
#define BINK_CELLS_H

#include "movie_rule.h"

#include <stdbool.h>

typedef int (__cdecl *bink_movie_fn_t)(const char *name, int param2, int param3);

/* Finds both cells. Call once, at install time. False when the pattern does not match exactly
 * once, when an instruction around an operand is not the one expected, or when the two encodings
 * of the abort cell disagree; a client's retail movie then runs to its own end and waits there for
 * the host. The line it logs says which. */
bool bink_cells_resolve(void);

/* Plays one movie of a session through the retail player, `original`, under a thread timer of this
 * DLL's own that the player's pump dispatches every round. The timer counts those rounds into
 * `loop->counts.thread_timers`. For a movie held for the host (`loop->poll` set) the key skip is
 * handed 0 instead of `param2`, and the timer asks `loop->poll` each round: once the host is done
 * it writes the abort cell every round until the player has ended; should the session let the
 * player go first, it gives the key skip back instead.
 *
 * `loop->end` comes back as MOVIE_END_HOST when the abort cell ended the movie, and as
 * MOVIE_END_RETAIL otherwise. Returns what `original` returned. */
int bink_cells_play(bink_movie_fn_t original, const char *name, int param2, int param3,
                    movie_loop_t *loop);

#endif /* BINK_CELLS_H */
