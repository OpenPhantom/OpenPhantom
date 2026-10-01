/* server_run.c: the server thread. See the header for why the server does not live in a message
 * loop.
 */
#include "server_run.h"

#include "mp_score.h"
#include "mp_server.h"
#include "mp_server_log.h"
#include "mp_transport.h"
#include "mp_udp.h"
#include "mp_wallclock.h"

#include "common/text.h"

#include <windows.h>

#include <stdio.h>
#include <string.h>

/* The loop's pace, and not the world's: the server sends one world per 31.25 ms tick whatever
 * this is, so a faster loop only shortens how long a handshake round trip or a relayed message
 * waits. Under the default timer resolution a sleep of fifteen wakes after about sixteen and
 * occasionally after thirty-one, which can skip a tick and never repeats one. */
#define SERVER_STEP_MS 15u

typedef struct server_run {
    mp_server_t     server;   /* a session is about 3.9 megabytes; not stack material */
    mp_udp_t        udp;
    mp_transport_t  transport;
    server_config_t config;

    HANDLE        thread;
    volatile LONG stop;
    volatile LONG running;

    /* Written by the server thread between steps, read by the window whenever it likes. It is
     * guarded rather than left to chance: the block is four dozen words, so a reader without a
     * guard can see a player count from one moment beside a score from another, and at five
     * writes and ten reads a second the guard costs nothing measurable. */
    CRITICAL_SECTION    status_lock;
    bool                status_lock_ready;
    server_run_status_t status;
} server_run_t;

static server_run_t run;

static void publish_status(void)
{
    server_run_status_t next;

    memset(&next, 0, sizeof next);
    next.running       = true;
    next.port          = mp_udp_local_port(&run.udp);
    next.players       = (uint32_t)mp_server_connected(&run.server);
    next.joins         = mp_session_joins(&run.server.session);
    next.denied        = mp_session_denied(&run.server.session);
    next.drops         = mp_session_drops(&run.server.session);
    next.worlds_sent   = run.server.worlds_sent;
    next.relayed       = run.server.relayed;
    next.relay_refused = run.server.relay_refused;
    next.setups_refused = run.server.setups_refused;
    next.notes_refused = run.server.notes_refused;
    next.copy_wishes   = run.server.copy_wishes;
    next.setups_sent   = run.server.setups_sent;
    next.boards_sent   = run.server.boards_sent;
    next.deaths_seen   = run.server.deaths_seen;
    next.chat          = run.server.chat.counts;

    next.outcome           = mp_match_outcome(&run.server.match, &next.winner);
    next.generation        = run.server.match.setup.generation;
    next.rounds_begun      = run.server.match.rounds_begun;
    next.round_elapsed_s   = mp_score_elapsed(&run.server.match.score) / 32u;
    next.round_remaining_s = mp_score_remaining(&run.server.match.score) / 32u;

    EnterCriticalSection(&run.status_lock);
    run.status = next;
    LeaveCriticalSection(&run.status_lock);
}

static DWORD WINAPI server_thread(LPVOID unused)
{
    uint32_t last_status_ms = 0;

    (void)unused;
    while (!run.stop) {
        uint32_t now = mp_wallclock_ms();

        mp_server_tick(&run.server, now);
        /* The screen is refreshed on its own cadence rather than every step: a status block is
         * four dozen words and rewriting it two hundred times a second would be the one part of
         * this loop that exists only for the window. */
        if (last_status_ms == 0u || now - last_status_ms >= 200u) {
            last_status_ms = now == 0u ? 1u : now;
            publish_status();
        }
        Sleep(SERVER_STEP_MS);
    }
    /* A stop says goodbye. Without it every client found out from its connected timeout, thirty
     * seconds, and then spent a minute trying to come back to a server that was gone. */
    if (mp_server_connected(&run.server) != 0u) {
        char line[96];

        text_format(line, sizeof line, "said goodbye to %u player(s) on the way out",
                    (unsigned)mp_server_connected(&run.server));
        mp_server_log_text(MP_SERVER_LOG_SESSION, line);
    }
    mp_session_disconnect(&run.server.session);
    return 0;
}

bool server_run_start(const server_config_t *config)
{
    if (config == NULL || run.running) {
        return false;
    }
    memset(&run, 0, sizeof run);
    run.config = *config;
    (void)server_config_clamp(&run.config);
    InitializeCriticalSection(&run.status_lock);
    run.status_lock_ready = true;

    if (!mp_udp_init(&run.udp, run.config.port)) {
        char line[128];

        text_format(line, sizeof line, "the UDP socket did not come up on port %u (error %d)",
                    (unsigned)run.config.port, mp_udp_last_error(&run.udp));
        mp_server_log_text(MP_SERVER_LOG_SESSION, line);
        DeleteCriticalSection(&run.status_lock);
        run.status_lock_ready = false;
        return false;
    }
    run.transport = mp_udp_transport(&run.udp);
    /* The seed feeds only the fallback generator; the salts come from the system's
     * cryptographic one. The wall clock reads zero on its first call by construction, which
     * is why the tick count is what is handed in. */
    mp_server_init(&run.server, &run.transport, GetTickCount());
    mp_session_set_name(&run.server.session, run.config.name);
    mp_session_set_password(&run.server.session, run.config.password);
    mp_session_set_capacity(&run.server.session, run.config.slots);
    mp_session_set_mode(&run.server.session, run.config.setup.mode);
    mp_server_host_match(&run.server, &run.config.setup);

    run.thread = CreateThread(NULL, 0, &server_thread, NULL, 0, NULL);
    if (run.thread == NULL) {
        mp_server_log_text(MP_SERVER_LOG_SESSION, "the server thread could not be started");
        mp_udp_shutdown(&run.udp);
        DeleteCriticalSection(&run.status_lock);
        run.status_lock_ready = false;
        return false;
    }
    InterlockedExchange(&run.running, 1);
    publish_status();
    {
        char line[160];

        text_format(line, sizeof line,
                    "listening on UDP port %u, up to %u player(s), %s, %u point(s), %u s",
                    (unsigned)mp_udp_local_port(&run.udp), (unsigned)run.config.slots,
                    run.config.setup.level, (unsigned)run.config.setup.rules.score_limit,
                    (unsigned)run.config.setup.rules.time_limit_s);
        mp_server_log_text(MP_SERVER_LOG_SESSION, line);
    }
    return true;
}

void server_run_stop(void)
{
    bool joined = true;

    if (!run.running) {
        return;
    }
    InterlockedExchange(&run.stop, 1);
    if (run.thread != NULL) {
        /* Bounded, so a hung thread cannot hang the window that asked it to stop. */
        joined = WaitForSingleObject(run.thread, 3000) == WAIT_OBJECT_0;
        CloseHandle(run.thread);
        run.thread = NULL;
    }
    mp_udp_shutdown(&run.udp);
    InterlockedExchange(&run.running, 0);
    EnterCriticalSection(&run.status_lock);
    memset(&run.status, 0, sizeof run.status);
    LeaveCriticalSection(&run.status_lock);
    /* Deleted only after a join that came back. A thread that did not may still be inside the
     * section, and a section deleted under it is a crash in a program that was stopping. */
    if (joined) {
        DeleteCriticalSection(&run.status_lock);
        run.status_lock_ready = false;
    } else {
        mp_server_log_text(MP_SERVER_LOG_SESSION, "the server thread did not stop within three "
                           "seconds, so its socket was closed under it");
    }
    mp_server_log_text(MP_SERVER_LOG_SESSION, "the server has stopped");
}

void server_run_status(server_run_status_t *out)
{
    if (out == NULL) {
        return;
    }
    if (!run.running || !run.status_lock_ready) {
        memset(out, 0, sizeof *out);
        return;
    }
    EnterCriticalSection(&run.status_lock);
    *out = run.status;
    LeaveCriticalSection(&run.status_lock);
}
