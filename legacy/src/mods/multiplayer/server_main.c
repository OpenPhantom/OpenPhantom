/* server_main.c: the dedicated server's entry point, in either of its two shapes.
 *
 * This is deliberately NOT a mod DLL and loads no game. Everything the game needed the engine for,
 * a dedicated server that only exchanges state does not: no window of the game's, no renderer, no
 * assets, no engine process. A player joins it from the ini and not from the game's menu, which
 * offers co-operative play only and would be refused as a different game: `NetRole=2` (or the
 * environment variable `OBI_NET_ROLE=client`), `NetAddress` set to this machine's address and
 * port, and `GameMode=tdm`, because the server plays a team deathmatch and a missing GameMode
 * reads as coop.
 *
 * Two shapes, one binary. With no argument it opens a window: settings, START, and the log
 * scrolling beside them. With `--headless` it runs in a console instead, which is what a machine
 * in a cupboard wants and what a service wrapper can restart.
 *
 * The subsystem is Windows, and the console shape asks for a console rather than the other way
 * round. A console program that opens a window leaves an empty black rectangle behind it for the
 * life of the session; a windows program that attaches to the console it was started from leaves
 * nothing behind and still prints where it was launched.
 *
 * SIZE NOTE: under 200 lines, no seam.
 */
#include "server_config.h"
#include "server_gui.h"
#include "server_run.h"

#include "mp_server_log.h"
#include "mp_wallclock.h"

#include <windows.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* How often the console shape prints where it is. The window refreshes ten times a second; a
 * console being watched over a remote shell wants far less. */
#define CONSOLE_REPORT_MS 5000u

static volatile LONG s_stop;

static BOOL WINAPI on_console_signal(DWORD signal)
{
    (void)signal;
    InterlockedExchange(&s_stop, 1);
    return TRUE;
}

/* The console shape's log listener: straight to standard output, on the log's writer thread. That
 * is the right thread for it, and it is the whole reason the server's own thread never formats a
 * line. */
static void print_line(const char *line, void *context)
{
    (void)context;
    if (line == NULL) {
        return;
    }
    puts(line);
    /* Flushed per line, because a redirected standard output is FULLY buffered: without this a
     * server run under a wrapper and then killed loses everything that had not filled a buffer,
     * which for a quiet server is everything it ever said. A console is line buffered and pays
     * nothing for this; a file pays one write per line, on the writer thread, which is the thread
     * that exists to absorb exactly that. */
    (void)fflush(stdout);
}

/* Attaches to the console this was started from, or makes one when there is none, so that
 * `--headless` prints where a person is looking.
 *
 * It leaves a redirection alone. `obi_dedicated --headless > run.txt` gives this process a real
 * handle for standard output before it starts, and reopening CONOUT$ over the top of that would
 * throw the file away and print to a console instead. Anybody redirecting output is running this
 * under a service wrapper or a scheduler, which is exactly the case where losing the output is
 * worst. So the handle is asked for first, and only an absent one is replaced. */
static void bring_up_a_console(void)
{
    bool have_output = GetStdHandle(STD_OUTPUT_HANDLE) != NULL &&
                       GetStdHandle(STD_OUTPUT_HANDLE) != INVALID_HANDLE_VALUE;

    if (have_output) {
        return;
    }
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
        (void)AllocConsole();
    }
    (void)freopen("CONOUT$", "w", stdout);
    (void)freopen("CONOUT$", "w", stderr);
}

static int run_headless(int argc, char **argv)
{
    server_config_t     config;
    server_run_status_t status;
    uint32_t            last_report_ms;
    uint32_t            last_players = 0xFFFFFFFFu;
    int                 i;

    bring_up_a_console();
    server_config_load(&config);

    /* A port on the command line beats the file. It is the one setting a person overrides per
     * launch, because it is the one that decides whether two servers can share a machine. */
    for (i = 1; i < argc; ++i) {
        int wanted;

        if (strcmp(argv[i], "--headless") == 0) {
            continue;
        }
        wanted = atoi(argv[i]);
        if (wanted >= 1 && wanted <= 65535) {
            config.port = (uint16_t)wanted;
        } else {
            fprintf(stderr, "usage: obi_dedicated [--headless] [port]   (1..65535)\n");
            return 1;
        }
    }

    if (!mp_server_log_open(config.log_path, config.log_categories, config.log_ring)) {
        fprintf(stderr, "the log file could not be opened; the console still shows everything\n");
    }
    mp_server_log_set_line_listener(&print_line, NULL);
    SetConsoleCtrlHandler(&on_console_signal, TRUE);

    if (!server_run_start(&config)) {
        mp_server_log_close();
        return 1;
    }
    printf("clients join from their ini with NetRole=2 (or OBI_NET_ROLE=client), "
           "NetAddress=<this machine>:%u and GameMode=tdm\n", (unsigned)config.port);

    last_report_ms = mp_wallclock_ms();
    while (!s_stop) {
        uint32_t now = mp_wallclock_ms();

        server_run_status(&status);
        if (status.players != last_players) {
            last_players = status.players;
            printf("%u player(s) connected (%u join, %u refused, %u dropped)\n",
                   (unsigned)status.players, (unsigned)status.joins, (unsigned)status.denied,
                   (unsigned)status.drops);
        }
        if (now - last_report_ms >= CONSOLE_REPORT_MS) {
            last_report_ms = now;
            printf("  %u world(s), %u relayed (%u refused, %u client setup(s) and %u other "
                   "note(s) dropped), "
                   "%u setup(s), %u board(s), %u death(s), round %u\n",
                   (unsigned)status.worlds_sent, (unsigned)status.relayed,
                   (unsigned)status.relay_refused, (unsigned)status.setups_refused,
                   (unsigned)status.notes_refused,
                   (unsigned)status.setups_sent,
                   (unsigned)status.boards_sent, (unsigned)status.deaths_seen,
                   (unsigned)status.rounds_begun);
            printf("  the chat: %u line(s) taken, %u refused (%u unsound, %u too fast, %u from "
                   "no seat); %u line(s) sent out as %u copies, %u copies unsent\n",
                   (unsigned)status.chat.taken,
                   (unsigned)(status.chat.unsound + status.chat.too_fast + status.chat.no_seat),
                   (unsigned)status.chat.unsound, (unsigned)status.chat.too_fast,
                   (unsigned)status.chat.no_seat, (unsigned)status.chat.lines_out,
                   (unsigned)status.chat.copies, (unsigned)status.chat.copies_unsent);
        }
        /* The server has its own thread and its own pace; this loop only reports, so it may
         * sleep as long as its slowest report allows. */
        Sleep(200);
    }

    printf("shutting down\n");
    server_run_stop();
    mp_server_log_close();
    return 0;
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command_line, int show)
{
    int i;

    (void)instance;
    (void)previous;
    (void)command_line;
    (void)show;

    for (i = 1; i < __argc; ++i) {
        if (strcmp(__argv[i], "--headless") == 0) {
            return run_headless(__argc, __argv);
        }
    }
    return server_gui_run();
}
