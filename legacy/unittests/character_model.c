/* character_model.c: the half of the model swap that can be checked without the game.
 *
 * What is under test is the roster and the refusals, which is what is left here now that the
 * decision about which rig may be worn is a measurement on the live pair. That measurement is
 * driven by unittests/character_nodemap.c; this file pins the row bookkeeping around it, and the
 * property that matters is that a build where nothing resolved refuses every row instead of half
 * working.
 *
 * The engine side is deliberately not stubbed. Nothing resolves in a test process, so every row is
 * offered as unavailable, which is exactly the state a player gets on an executable this project
 * does not know, and it is worth pinning down that the panel stays usable there.
 */
#include "unittest.h"

#include "character_model.h"

#include <string.h>

int main(void)
{
    ut_section("the roster is five names plus whatever the data file lists");
    /* The actor table is read out of characters.ini by another module and nothing loaded it here,
     * so what is left is the five the roster starts with. Those five are not a measurement: they
     * are the only assets a player can already be wearing, and their rows exist so that the model
     * he is in reads Current and so that going back is a row rather than a special case. Four of
     * them are the hero asset table's own four names and the fifth is Mace, who rides Qui-Gon's
     * slot. Which of the shipped assets may be WORN is measured on the live pair when a row is
     * chosen. */
    ut_check(character_model_count() == 5u,
             "with no actor table loaded the roster is the five a player can already be in");
    ut_check(character_model_name(0) != NULL, "the first row has a name");
    ut_check(character_model_name(1) != NULL, "so has the second");
    ut_check(character_model_name(2) != NULL, "so has the third");
    ut_check(character_model_name(3) != NULL, "so has Panaka's");
    ut_check(character_model_name(4) != NULL, "so has the Queen's");
    ut_check(character_model_name(5) == NULL, "and a row past the end has none");

    ut_section("which row an asset name belongs to");
    ut_check(character_model_candidate_of("obiwan.baf") == 0, "obiwan is the first");
    ut_check(character_model_candidate_of("quigon.baf") == 1, "quigon is the second");
    ut_check(character_model_candidate_of("mace.baf") == 2, "mace is the third");
    ut_check(character_model_candidate_of("panaka.baf") == 3, "panaka is the fourth");
    ut_check(character_model_candidate_of("queen.baf") == 4, "and the queen is the fifth");
    ut_check(character_model_candidate_of("obiwan") == 0, "the suffix is optional");
    ut_check(character_model_candidate_of("QuiGon.BAF") == 1, "and the case is ignored");
    ut_check(character_model_candidate_of("mace.b3d") == 2,
             "everything after the last dot is the file's, not the actor's");

    ut_section("an asset the roster does not carry has no row");
    /* These three carry Obi-Wan's 48 nodes in his order and would translate perfectly, which is
     * the point: the answer here is about the ROSTER and not about compatibility. They reach the
     * panel through the actor table when it is loaded, like every other asset. */
    ut_check(character_model_candidate_of("quifinal.baf") == -1, "quifinal has no row of its own");
    ut_check(character_model_candidate_of("quigung.baf") == -1, "nor has quigung");
    ut_check(character_model_candidate_of("quiweap.baf") == -1, "nor has quiweap");
    ut_check(character_model_candidate_of("queennpc.baf") == -1,
             "nor has the Queen's stand in, whose name merely begins like hers");
    ut_check(character_model_candidate_of("battdrod.baf") == -1, "nor has a battle droid");
    ut_check(character_model_candidate_of("obiwan2.baf") == -1,
             "and a name that merely begins the same way is not the same asset");
    ut_check(character_model_candidate_of("") == -1, "an empty name names nothing");
    ut_check(character_model_candidate_of(".baf") == -1, "and neither does a bare suffix");
    ut_check(character_model_candidate_of(NULL) == -1, "a missing name is refused, not crashed on");

    ut_section("which hero may wear one");
    /* The hero index does not decide the blade half. What decides it is whether the player's own
     * rig carries a sabreblad01 node, which is a live measurement and not testable here; this
     * answers only whether the slot is one the hero asset table has, because the engine indexes
     * that table of four with this value. */
    ut_check(character_model_hero_is_supported(0), "Obi-Wan may");
    ut_check(character_model_hero_is_supported(1), "so may Qui-Gon, whose slot Mace also rides");
    ut_check(character_model_hero_is_supported(2),
             "so may Panaka, who carries no blade at all and therefore cannot resize one into a "
             "borrowed asset");
    ut_check(character_model_hero_is_supported(3), "and so may the Queen, for the same reason");
    ut_check(!character_model_hero_is_supported(-1), "a slot that does not exist may not");
    ut_check(!character_model_hero_is_supported(4),
             "nor may the first one past the end of the hero asset table");
    ut_check(!character_model_hero_is_supported(99), "nor anything beyond it");

    ut_section("with no engine behind it, every row refuses rather than half works");
    ut_check(!character_model_is_available(0),
             "nothing resolved, so no model may be worn, and the row says so instead of vanishing");
    ut_check(!character_model_is_available(99), "a row past the end is refused as well");
    ut_check(!character_model_is_current(0), "and none of them claims to be on the body");
    ut_check(!character_model_select(0), "choosing one is refused rather than quietly dropped");

    ut_section("the blade lock answers no when there is nothing borrowed");
    /* This is also the call that records that the guard asked. It has to stay safe before anything
     * has resolved, because the guard on Plr_SetBladeSize runs from the moment it is installed and
     * that is long before the panel is ever opened. */
    ut_check(!character_model_borrows_shared_mesh(),
             "no swap has happened, so the mesh under the blade is the player's own");
    ut_check(!character_model_is_available(0),
             "and being asked does not make an unresolved build usable");

    return ut_summary("the model swap's decision");
}
