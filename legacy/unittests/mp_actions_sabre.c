/* mp_actions_sabre.c: the sabre sender in a process with no game.
 *
 * The four hulls are engine choreography and are proven in game: a swing row travels, a block's
 * clip is read after the engine chose it, a disarm is noted only while the blade was armed. What a
 * test without a game can pin is the same claim every engine-facing module here makes, which is
 * that with nothing resolved the install stands no hull, says so, and counts nothing, and that a
 * null listener installs nothing at all.
 */
#include "unittest.h"

#include "mp_actions_sabre.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint32_t listener_calls;

static void listener(uint8_t action, uint8_t operand)
{
    (void)action;
    (void)operand;
    ++listener_calls;
}

static void check_without_a_game(void)
{
    ut_section("in a process with no game");

    ut_check(mp_actions_sabre_install(NULL) == 0u,
             "a null listener installs nothing, because nothing would receive a catch");
    ut_check(mp_actions_sabre_installed() == 0u, "and no hull is counted as standing");

    ut_check(mp_actions_sabre_install(&listener) == 0u,
             "with no site resolved no hull stands");
    ut_check(mp_actions_sabre_installed() == 0u, "and the module says so");
    ut_check(mp_actions_sabre_install(&listener) == 0u,
             "a second install answers the first one's count rather than installing again");

    ut_check(mp_actions_sabre_caught() == 0u, "nothing was caught");
    ut_check(mp_actions_sabre_refused() == 0u, "nothing was refused");
    ut_check(listener_calls == 0u, "and the listener was never called");
}

int main(void)
{
    check_without_a_game();

    return ut_summary("mp_actions_sabre");
}
