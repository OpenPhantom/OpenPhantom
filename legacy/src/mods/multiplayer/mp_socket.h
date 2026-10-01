/* mp_socket.h: the Winsock lifetime, and nothing else yet.
 *
 * Layer 0. It knows no engine address, includes no engine header, and can therefore be built and
 * driven without the game in the process.
 *
 * What is here is the one piece that has to exist before any socket does: Winsock is a library with
 * a reference counted startup, and getting that wrong is quiet in both directions. Without the
 * startup every socket call fails with an error nobody looks up; with a startup and no matching
 * cleanup the count never reaches zero, which matters in a process that outlives the session.
 *
 * The sockets themselves are the next step. This exists now because it answers a question that is
 * far cheaper to answer early: whether the Windows socket header compiles under the warning level
 * this project treats as errors.
 */
#ifndef MULTIPLAYER_MP_SOCKET_H
#define MULTIPLAYER_MP_SOCKET_H

#include <stdbool.h>
#include <stdint.h>

/* The version asked for. 2.2 has been in every Windows since 98, and asking for it on a system that
 * has less is a refusal rather than a silent downgrade. */
#define MP_SOCKET_VERSION_MAJOR 2
#define MP_SOCKET_VERSION_MINOR 2

/* Reference counted. Every startup that returns true must be matched by exactly one shutdown, and
 * the library is only really wound down when the last one is. Returns false when the library
 * refused, in which case nothing was counted and no shutdown is owed. */
bool mp_socket_startup(void);
void mp_socket_shutdown(void);

/* How many startups are outstanding. Zero means the library is not up. */
uint32_t mp_socket_users(void);

/* The version the library actually agreed to, packed as (major << 8) | minor, or 0 when it is not
 * up. Asking for 2.2 and being given something else is a fact worth having rather than a detail:
 * the library reports what it will speak, not what it was asked for. */
uint16_t mp_socket_version(void);

/* The last error the library reported, or 0. Kept because the call that failed is usually not the
 * call that reports, and a number here is worth more than a boolean at the call site. */
int mp_socket_last_error(void);

#endif /* MULTIPLAYER_MP_SOCKET_H */
