/* handback_rule: whether a dialogue that has just closed owes the camera back.
 *
 * The three inputs are not interchangeable and the test is mostly about what the rule REFUSES.
 * Getting it wrong in the permissive direction does not fail loudly: it takes the camera away from
 * a cutscene, a menu or the fall-death shot, all of which look like the engine misbehaving rather
 * than like this module doing it.
 */
#include "handback_rule.h"

#include "unittest.h"

int main(void)
{
    ut_section("the case this exists for");
    ut_check(handback_rule_owes_camera(true, 1, 0),
             "the dialogue took the camera, still holds it, and nothing above it is running, "
             "which is the line that strands the camera for the rest of the level");

    ut_section("somebody else's camera is never touched");
    ut_check(!handback_rule_owes_camera(false, 1, 0),
             "a camera the dialogue did not take is left alone, however plainly it is held: this "
             "is what keeps the cutscene opcode, a menu, the tripod gun and the fall-death camera "
             "out of it");
    ut_check(!handback_rule_owes_camera(false, 1, 5),
             "and that holds whatever the lock says, so the two guards are independent rather "
             "than one standing in for the other");

    ut_section("a cutscene above the dialogue keeps the camera");
    ut_check(!handback_rule_owes_camera(true, 1, 5),
             "a cutscene takes the input lock to 5, and a dialogue closing inside one owes it "
             "nothing: the engine's own release refuses the same case and this keeps that guard");
    ut_check(!handback_rule_owes_camera(true, 1, 1),
             "any lock still standing is somebody still running, so 1 is refused as well as 5");
    ut_check(!handback_rule_owes_camera(true, 1, -1),
             "and a lock that has gone negative is not zero either, so it is left standing rather "
             "than read as clear");

    ut_section("nothing owing");
    ut_check(!handback_rule_owes_camera(true, 0, 0),
             "the engine released the camera itself, which is what happens whenever a choice menu "
             "was open, so there is nothing to hand back");
    ut_check(!handback_rule_owes_camera(false, 0, 0),
             "and the empty case does nothing at all");

    return ut_summary("handback_rule");
}
