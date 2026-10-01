/* server_run.h: the server on a thread, so that a window can own the screen and the server can
 * own its own pace.
 *
 * WHY A THREAD. The server has to step every fifteen milliseconds whatever else is happening, and
 * a window has to answer its message queue whatever else is happening. Running the server inside
 * a message loop makes the first hostage to the second: dragging the window, opening a menu or
 * holding a scrollbar all block the queue, and a blocked queue would be a server that stops
 * sending. So the server gets a thread of its own and the window never touches it except through
 * the two calls below.
 *
 * What crosses between them. Starting hands over a COPY of the settings, so the window may go on
 * editing its own while the server runs on the one it was given. Coming back the other way there
 * is only the status block, which is numbers, read while the server is between steps.
 *
 * SIZE NOTE: under 200 lines, no seam.
 */
#ifndef MULTIPLAYER_SERVER_RUN_H
#define MULTIPLAYER_SERVER_RUN_H

#include "server_config.h"

#include "mp_chat_rule.h"

#include <stdbool.h>
#include <stdint.h>

/* What the screen shows about a running server. Numbers only: anything that needed a lock to be
 * read would be a lock the server thread has to take. */
typedef struct server_run_status {
    bool     running;
    uint16_t port;
    uint32_t players;
    uint32_t joins;
    uint32_t denied;
    uint32_t drops;
    uint32_t worlds_sent;
    uint32_t relayed;
    uint32_t relay_refused;
    uint32_t setups_refused;
    uint32_t notes_refused;
    uint32_t copy_wishes;
    uint32_t setups_sent;
    uint32_t boards_sent;
    uint32_t deaths_seen;
    mp_chat_host_counts_t chat;   /* how many lines, never what they said */

    uint8_t  outcome;      /* MP_SCORE_RUNNING, or one of the three ways a round ends */
    uint8_t  winner;
    uint8_t  generation;
    uint32_t round_elapsed_s;
    uint32_t round_remaining_s;
    uint32_t rounds_begun;
} server_run_status_t;

/* Brings up the socket and starts the thread. False when the port could not be bound, and the
 * reason is logged rather than only returned. */
bool server_run_start(const server_config_t *config);

/* Asks the thread to finish its step and stops it. Safe to call when nothing is running. */
void server_run_stop(void);

/* A snapshot for the screen. Safe at any time; answers a cleared block when nothing runs. */
void server_run_status(server_run_status_t *out);

#endif /* MULTIPLAYER_SERVER_RUN_H */
