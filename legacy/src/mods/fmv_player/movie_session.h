/* movie_session.h: a movie in a multiplayer session, seen from this DLL's side of the movie note.
 *
 * The multiplayer files a gate that says whether a session runs and which side of it this machine
 * is on. This file reads it when a movie begins and on every turn while the movie is held for the
 * host, decides through movie_rule.c, says in the log what it decided, and files this side's movie
 * state back for the multiplayer.
 *
 * With no session there is no gate. The role is then FREE, and nothing here logs, files or holds
 * anything: the movie plays exactly as it did before this file existed.
 */
#ifndef MOVIE_SESSION_H
#define MOVIE_SESSION_H

#include "movie_rule.h"

#include <stdint.h>

/* Files the first state record, with no movie in it, so a multiplayer can tell that a movie
 * player is present before any movie has played. Called once the hook stands. */
void movie_session_install(void);

/* A movie begins: reads the gate and decides its role. `name` is the engine's own, kept for the
 * log. A skipped movie is logged and filed as ended here, and nothing else is done with it. */
movie_role_t movie_session_decide(const char *name);

/* The movie is about to play through `path` (MOVIE_PATH_*): counted, filed and logged once. A
 * second call for the same movie, when libVLC did not start and the retail player takes over, only
 * files the new path. Nothing in a FREE movie. */
void movie_session_begin(uint8_t path);

/* The loop a movie of the decided role runs: its ways out, and for a held one the question it asks
 * every turn and the line its black wait shows. The quiet starts again with every loop, so a movie
 * libVLC gave back to the retail player does not carry the time in between into the second one.
 * A movie the session has already let go stays let go. */
void movie_session_loop(movie_loop_t *loop);

/* One turn of a held movie: reads the gate and answers through movie_rule_verdict. The first
 * answer that lets the player go while the movie still plays is logged and filed here. */
movie_verdict_t movie_session_poll(void);

/* The same, at the tick count `now`, which is what the quiet is measured on: each question adds
 * the time since the one before it, capped per turn. */
movie_verdict_t movie_session_poll_at(uint32_t now);

/* This side's movie is over first: filed as waiting, and said. */
void movie_session_wait(void);

/* The movie is over, for `loop->end`: logged with what the loop refused and dispatched, and filed
 * as ended. Nothing in a FREE movie. */
void movie_session_end(const movie_loop_t *loop);

#endif /* MOVIE_SESSION_H */
