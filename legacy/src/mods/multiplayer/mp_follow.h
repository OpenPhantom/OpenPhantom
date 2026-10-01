/* mp_follow.h: the host changes the world, and its clients go with it.
 *
 * A session was started once, from the lobby, and that start was the only thing that ever took a
 * client into a level: the lobby screen was its one consumer. When the host's level ended and the
 * campaign loaded the next one, when the host restored a savegame from its pause menu, or when it
 * began the level again, the host stood in a new world and every client stayed in the old one,
 * with nothing that ended the session over it.
 *
 * THE HOST watches its own levels begin. The first level that begins after a start is the one that
 * start named; any later one under the same generation is a world change, and the host names it in
 * its setup note under a new generation: the level the engine is now in, or, when that level came
 * out of a savegame, the file, offered to the clients the way the lobby offers one. The file is
 * seen through a hull on the engine's own restore by name, which every load of a savegame goes
 * through, the pause menu's included.
 *
 * THE CLIENT is the second consumer of a start, from the frame pump. A client in a running level
 * that sees a generation it has not acted on leaves the level through the pair the engine's own
 * "leave level" writes, and once the level has let go it starts the host's world the way the lobby
 * does. The campaign broadcasts "a new game begins" on its way to the title screen, which otherwise
 * withdraws a client from its session; while it is leaving to follow, it stays.
 *
 * What it does not do: a client's own savegame load and its own level exit still take it into a
 * world of its own, the hero a lobby chose is applied at the first start only, and a client that
 * sits on its own continue screen when the host begins the level again is not taken along.
 */
#ifndef MULTIPLAYER_MP_FOLLOW_H
#define MULTIPLAYER_MP_FOLLOW_H

#include <stdbool.h>
#include <stdint.h>

/* What a client does about the host's world, one frame at a time. */
typedef enum mp_follow_step {
    MP_FOLLOW_NOTHING,   /* no world change it has not acted on, or one the lobby screen takes */
    MP_FOLLOW_LEAVE,     /* a level is running and the host is in another world: leave it */
    MP_FOLLOW_WAIT,      /* on its way out of the level, or a start is still being driven */
    MP_FOLLOW_START      /* at the title: start the host's world */
} mp_follow_step_t;

/* The pure decisions, so a test can pin them with no session and no game in the process.
 *
 * The client's step. `leaving` is that this side wrote its leave, `at_title` that the campaign has
 * since said "a new game begins", which it says on its way to the title screen; a start asked for
 * before that would be driven into whatever screen the level's end still shows. A world change it
 * finds at the title without having left anything is not its to take: that is the lobby screen's
 * start. */
mp_follow_step_t mp_follow_client_step(bool new_world, bool level_running, bool leaving,
                                       bool at_title, bool start_pending);

/* Whether a level beginning on the host is a world change: a started session, a level already
 * seen beginning, and the same generation as that one. The first level of a generation is the
 * one its start named. */
bool mp_follow_host_changed_world(bool started, uint8_t generation, bool seen, uint8_t last);

/* Whether a movie that has begun on the host opens the level the host is in: `stem` is the
 * movie's name after its last separator, as the movie note carries it, and `film` the movie the
 * level table names for the level the engine is in. Compared without regard to case, and never
 * true for the ending, which follows the last level and opens nothing. */
bool mp_follow_movie_opens_level(const char *stem, const char *film);

/* The hull on the restore by name. False when it would not take one, and the rest of the module
 * still runs: a host's restore is then followed into its level fresh rather than into the file. */
bool mp_follow_install(void);

/* A level has begun, module message 5. */
void mp_follow_note_level_begin(void);

/* A movie has begun on the host, as the movie note says; `stem` is its name after the last
 * separator. When it is the opening movie of a level the host moved into under the generation it
 * already played a level in, the new world is announced now rather than at the level begin, so
 * the players load it while the movie runs. Once per world: the level begin that follows finds
 * the generation raised and announces nothing. */
void mp_follow_note_host_movie(const char *stem);

/* The same, with the movie the level table names for the level the engine is in already read:
 * `film`, empty or NULL when it could not be. The index and the table are the engine's and read
 * nothing in a test process, so this is the whole of the decision and the announcement that a
 * test can drive, through the real setup note and its real generation. */
void mp_follow_note_host_movie_in(const char *stem, const char *film);

/* Once per drawn frame, after the session's own verdict. */
void mp_follow_tick(void);

/* The campaign's "a new game begins", module message 0x17, on a client. True when this side is
 * leaving its level to follow the host, which is then the way to the title and keeps the session;
 * false when the player left on their own, which is a withdrawal. */
bool mp_follow_take_new_game(void);

/* Whether a client left its level for its host's next world and has not arrived yet. */
bool mp_follow_leaving(void);

/* The session is gone: nothing is on its way anywhere, and no level begin is remembered. */
void mp_follow_forget(void);

void mp_follow_report(void);

#endif /* MULTIPLAYER_MP_FOLLOW_H */
