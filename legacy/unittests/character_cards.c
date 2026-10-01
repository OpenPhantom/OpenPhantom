/* character_cards.c: the four vertex contract the sabre glow cards stand on.
 *
 * The number under test is not a tuning value and not a margin. Two callers in the image fetch a
 * node's mesh into a buffer of exactly forty eight bytes, and the routine they call loops to the
 * MESH's own vertex count with no bound of its own. A blade card is a quad, so on the models the
 * engine shipped the loop writes four vertices and stops. Point that routine at a node carrying a
 * hand, which is what a model swap does to a record holding a node index, and the tenth vertex
 * lands on the caller's saved frame pointer and return address.
 *
 * So the property is one sided and total: the count handed back may never exceed four, whatever
 * the mesh claims, including the sizes a corrupt or hostile header could claim.
 */
#include "unittest.h"

#include "character_cards.h"

#include <stdint.h>

int main(void)
{
    uint32_t claimed;

    ut_section("a card sized mesh is copied whole");
    /* Below the bound the engine's own loop is what runs, so these are the numbers this module
     * must agree with rather than override. */
    ut_check(character_cards_copy_count(0u) == 0u, "an empty mesh copies nothing");
    ut_check(character_cards_copy_count(1u) == 1u, "one vertex copies one");
    ut_check(character_cards_copy_count(3u) == 3u, "three copy three");
    ut_check(character_cards_copy_count(4u) == 4u, "and a full card copies its four");

    ut_section("anything larger is cut to the four the callers hold");
    ut_check(character_cards_copy_count(5u) == CHARACTER_CARD_VERTICES,
             "five is the first count that would write past the buffer");
    ut_check(character_cards_copy_count(9u) == CHARACTER_CARD_VERTICES,
             "nine stops one vertex short of the saved frame pointer");
    ut_check(character_cards_copy_count(10u) == CHARACTER_CARD_VERTICES,
             "ten is the first whose last vertex covers the saved frame pointer and the "
             "return address");
    ut_check(character_cards_copy_count(600u) == CHARACTER_CARD_VERTICES,
             "a body mesh is cut to the same four");
    ut_check(character_cards_copy_count(0xFFFFFFFFu) == CHARACTER_CARD_VERTICES,
             "and so is a count no allocation could hold");

    ut_section("the bound holds for every count, not just the ones named above");
    /* The claim is one sided, so it is worth driving rather than sampling: there is no vertex
     * count at all for which more than four may be written. */
    for (claimed = 0u; claimed < 4096u; ++claimed) {
        if (character_cards_copy_count(claimed) > CHARACTER_CARD_VERTICES) {
            break;
        }
    }
    ut_checkf(claimed == 4096u,
              "no count from 0 to 4095 asks for more than four vertices (first offender %u)",
              claimed);

    ut_section("which meshes fit the contract");
    ut_check(character_cards_mesh_fits(4u), "a quad fits and reaches the engine unchanged");
    ut_check(!character_cards_mesh_fits(5u), "one more does not");
    ut_check(character_cards_mesh_fits(0u),
             "and a mesh with no vertices fits, so the engine keeps its own answer for it");

    ut_section("nothing is installed and nothing has been bounded");
    /* No engine in a test process, so every pattern resolves to nothing. What is worth pinning is
     * that the module then says so and stays out of the way rather than half arming. */
    ut_check(!character_cards_is_armed(), "the module starts disarmed");
    ut_check(!character_cards_install(), "an install with no host executable is refused");
    ut_check(!character_cards_is_armed(), "and a refused install leaves nothing armed");
    ut_check(character_cards_bound_count() == 0u, "the bound has never answered");

    return ut_summary("character cards");
}
