/* mp_socket.c: the Winsock lifetime, driven for real in a process with no game in it.
 *
 * This test exists for two reasons and the first one is not the assertions.
 *
 * The multiplayer feature has to know, before any of the transport is written, whether the Windows
 * socket header compiles under the warning level this project treats as errors and whether the
 * library links. Compiling alone does not answer the second half: with nothing calling into it the
 * linker drops the whole module and the import disappears, so the build stays green while proving
 * nothing. Calling the library from a test is what makes the answer real.
 *
 * The second reason is the reference count, which is the part that can be wrong quietly. Winsock
 * counts its own startups, so an unmatched cleanup from one part of a process tears the library out
 * from under another part still using it. The count here is what lets a session, a self test and a
 * diagnostic each ask for the library without knowing about the others.
 */
#include "unittest.h"

#include "mp_socket.h"

int main(void)
{
    ut_section("the library comes up");

    ut_check(mp_socket_users() == 0u, "nothing is outstanding before the first startup");
    ut_check(mp_socket_version() == 0u, "and there is no version to report");

    if (!mp_socket_startup()) {
        /* A machine without Winsock is not a broken build, and saying so is worth more than a
         * failure nobody can act on. Everything below needs the library, so the run stops here. */
        ut_check(0, "winsock started; if this line failed, this machine has no usable Winsock and "
                    "the rest of the checks were skipped rather than run against nothing");
        return ut_summary("multiplayer socket");
    }
    ut_check(1, "winsock started");
    ut_check(mp_socket_users() == 1u, "one user is outstanding");
    ut_check(mp_socket_version() != 0u, "and the library reported which version it speaks");

    ut_section("the count, which is the part that goes wrong quietly");

    ut_check(mp_socket_startup(), "a second startup succeeds");
    ut_check(mp_socket_users() == 2u, "and is counted rather than folded into the first");

    mp_socket_shutdown();
    ut_check(mp_socket_users() == 1u, "one shutdown leaves the other user holding the library");
    ut_check(mp_socket_version() != 0u, "which is still up");

    mp_socket_shutdown();
    ut_check(mp_socket_users() == 0u, "the last shutdown releases it");
    ut_check(mp_socket_version() == 0u, "and the version goes with it");

    ut_section("a shutdown nobody owed");

    mp_socket_shutdown();
    ut_check(mp_socket_users() == 0u,
             "an unmatched shutdown is refused rather than passed on, because passing it on would "
             "tear the library out from under whatever else in the process is holding it up");

    ut_section("and it can come back");

    ut_check(mp_socket_startup(), "the library starts again after being released");
    ut_check(mp_socket_users() == 1u, "counted from one");
    mp_socket_shutdown();
    ut_check(mp_socket_users() == 0u, "and released again");

    return ut_summary("multiplayer socket");
}
