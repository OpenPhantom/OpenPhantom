/* input_mode: whether the pad stick may drive the player, given the engine's live binding set.
 *
 * The interesting half is the unresolved case. This decision runs on every substep of every
 * session, so getting it wrong in the strict direction does not produce a subtle fault: it
 * produces a player who cannot move at all, on a build where nothing else is known to be wrong.
 */
#include "input_mode.h"

#include "unittest.h"

int main(void)
{
    ut_section("the engine's own modes");
    ut_check(input_mode_allows_movement(true, INPUT_MODE_GAMEPLAY),
             "ordinary play drives, which is the mode a dialogue hands back to on its way out");
    ut_check(!input_mode_allows_movement(true, 4),
             "a dialogue with a choice menu does not, which is the fault this repairs: the menu "
             "scrolled and the player walked at the same time");
    ut_check(!input_mode_allows_movement(true, 1),
             "and neither does any other binding set, because gameplay is the only one named "
             "rather than every other one being listed");
    ut_check(!input_mode_allows_movement(true, 2), "mode 2 stands down");
    ut_check(!input_mode_allows_movement(true, 3), "mode 3 stands down");

    ut_section("a mode that could not be read");
    ut_check(input_mode_allows_movement(false, 4),
             "an unresolved site drives anyway, even reading a mode that would otherwise refuse: "
             "the cost of being wrong this way is one awkward conversation, and the cost of being "
             "wrong the other way is a player who cannot move at all");
    ut_check(input_mode_allows_movement(false, INPUT_MODE_GAMEPLAY),
             "and the value is not consulted at all when there is nothing to consult");

    ut_section("values the engine never sets");
    ut_check(!input_mode_allows_movement(true, -1),
             "a negative mode is not gameplay, so it stands down rather than reading as one");
    ut_check(!input_mode_allows_movement(true, 9999),
             "and neither is one past the end of the engine's own table");

    return ut_summary("input_mode");
}
