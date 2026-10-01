/* mp_socket.c: bringing Winsock up and down, counted.
 *
 * Layer 0, so no engine anywhere in this file: no engine address and no engine include.
 *
 * The reference count is not ceremony. `WSAStartup` and `WSACleanup` are themselves counted by the
 * library, so an unmatched cleanup from one part of a process tears the library out from under
 * another part that is still using it. Counting here means this module can be asked to start the
 * library by a session, by a self test and by a diagnostic in the same run without any of them
 * having to know about the others.
 *
 * The library is loaded by the first call and not by the DLL: the import is delay loaded, so a
 * player who never switches multiplayer on never has ws2_32 mapped into the process on account of
 * this feature.
 *
 * A green build is not the proof that Winsock links. The first build had ws2_32 on the link line
 * and an import table naming KERNEL32 alone: nothing called mp_socket_startup yet, so the linker
 * dropped this module and the library with it. The unit test, which calls into the library for
 * real in a console process, is what makes the answer real.
 */
#include "mp_socket.h"

#include "common/logging.h"

#include <winsock2.h>

#include <stdbool.h>
#include <stdint.h>

typedef struct mp_socket_state {
    uint32_t users;
    uint16_t version;
    int      last_error;
} mp_socket_state_t;

static mp_socket_state_t socket_state;

uint32_t mp_socket_users(void)      { return socket_state.users; }
uint16_t mp_socket_version(void)    { return socket_state.version; }
int      mp_socket_last_error(void) { return socket_state.last_error; }

bool mp_socket_startup(void)
{
    WSADATA data;
    int     result;

    if (socket_state.users != 0) {
        ++socket_state.users;
        return true;
    }

    result = WSAStartup(MAKEWORD(MP_SOCKET_VERSION_MAJOR, MP_SOCKET_VERSION_MINOR), &data);
    if (result != 0) {
        /* WSAStartup is the one call that returns its error directly rather than through
         * WSAGetLastError, because the library it would ask is the one that failed to come up. */
        socket_state.last_error = result;
        log_error("winsock refused to start (%d), so there is no transport", result);
        return false;
    }

    socket_state.version = (uint16_t)((HIBYTE(data.wVersion) & 0xFF) |
                                      ((LOBYTE(data.wVersion) & 0xFF) << 8));
    socket_state.users   = 1;

    if (LOBYTE(data.wVersion) != MP_SOCKET_VERSION_MAJOR ||
        HIBYTE(data.wVersion) != MP_SOCKET_VERSION_MINOR) {
        /* Not fatal, and worth a line: the library reports what it will speak rather than what it
         * was asked for, and a difference here explains later behaviour that otherwise looks like
         * a bug in this code. */
        log_warning("winsock is up but speaks %u.%u rather than the %u.%u that was asked for",
                    (unsigned)LOBYTE(data.wVersion), (unsigned)HIBYTE(data.wVersion),
                    (unsigned)MP_SOCKET_VERSION_MAJOR, (unsigned)MP_SOCKET_VERSION_MINOR);
    } else {
        log_info("winsock %u.%u is up", (unsigned)LOBYTE(data.wVersion),
                 (unsigned)HIBYTE(data.wVersion));
    }
    return true;
}

void mp_socket_shutdown(void)
{
    if (socket_state.users == 0) {
        /* A cleanup nobody owed. Reported rather than passed on, because passing it on would tear
         * the library out from under whatever else in the process is holding it up. */
        log_warning("winsock shutdown called with nothing outstanding, ignored");
        return;
    }

    --socket_state.users;
    if (socket_state.users != 0) {
        return;
    }

    if (WSACleanup() != 0) {
        socket_state.last_error = WSAGetLastError();
        log_warning("winsock cleanup reported %d", socket_state.last_error);
    }
    socket_state.version = 0;
}
