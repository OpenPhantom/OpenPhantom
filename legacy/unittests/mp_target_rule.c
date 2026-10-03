/* Which calls the target resolver's hull leaves to the engine, and for which actors the host stays
 * the player.
 *
 * The one caller that must not be extended is the conversation menu's facing test. Before this
 * rule, the hull answered it with the nearest far player like every other kind 0 call; the
 * reference below is that behaviour, and the rule is held to differing from it at exactly one
 * place, the facing test's own return address. The actors the engine's answer is kept for are
 * walked at the end, over every input the rule can be given.
 */
#include "unittest.h"

#include "mp_target_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The retail return address of the facing test's call, 0x0043566F plus the five bytes of the call.
 * In the game it is read out of the running image; here it only has to be some address. */
#define FACING_TEST_RETURN 0x00435674u

/* Three of the nine other callers of the resolver in the retail image, by their return addresses,
 * the second of them the range check behind opcode 0x107, and a nought. Any address that is not
 * in the list must be extended as before. */
static const uintptr_t OTHER_CALLERS[] = { 0x0042BB0Cu, 0x0042E20Fu, 0x0042E9D9u, 0x00000000u };

/* What the hull did before: nobody was left to the engine. */
static bool the_old_hull(uintptr_t caller)
{
    (void)caller;
    return false;
}

static void check_the_facing_test_is_left_alone(void)
{
    const uintptr_t returns[] = { FACING_TEST_RETURN };
    size_t          i;

    ut_section("the facing test is answered by the engine, and nobody else is");

    ut_check(mp_target_left_to_the_engine(returns, 1u, FACING_TEST_RETURN),
             "the facing test's own call is left to the engine's answer for this machine's player");
    ut_check(mp_target_left_to_the_engine(returns, 1u, FACING_TEST_RETURN) !=
                 the_old_hull(FACING_TEST_RETURN),
             "and that is the one place the rule differs from the hull before it");
    for (i = 0; i < sizeof OTHER_CALLERS / sizeof OTHER_CALLERS[0]; ++i) {
        ut_checkf(mp_target_left_to_the_engine(returns, 1u, OTHER_CALLERS[i]) ==
                      the_old_hull(OTHER_CALLERS[i]),
                  "a call returning to %08X is extended as before", (unsigned)OTHER_CALLERS[i]);
    }
    ut_check(!mp_target_left_to_the_engine(returns, 1u, FACING_TEST_RETURN + 1u),
             "the address one byte on is somebody else's call");
}

static void check_the_list_fails_open(void)
{
    const uintptr_t unresolved[] = { 0u };
    const uintptr_t two[]        = { 0x00401000u, FACING_TEST_RETURN };

    ut_section("a list that resolved nothing leaves the hull as it was");

    ut_check(!mp_target_left_to_the_engine(NULL, 0u, FACING_TEST_RETURN),
             "with no list at all, the facing test is extended like everything else");
    ut_check(!mp_target_left_to_the_engine(unresolved, 1u, 0u),
             "a nought entry is a site that did not resolve, and it never matches a nought caller");
    ut_check(!mp_target_left_to_the_engine(two, 0u, FACING_TEST_RETURN),
             "a count of nought reads no entry, whatever the array holds");
    ut_check(mp_target_left_to_the_engine(two, 2u, FACING_TEST_RETURN),
             "the list is searched to its end, not only its first entry");
}

/* What the hull kept before: every resolution it saw became the actor's last answer, the menu's
 * facing test included. */
static bool the_old_memo(bool left_to_the_engine)
{
    (void)left_to_the_engine;
    return true;
}

static void check_the_facing_test_is_not_kept(void)
{
    ut_section("a call left to the engine does not become the actor's last answer");

    ut_check(!mp_target_answer_is_kept(true),
             "the facing test answers with this machine's player by construction, so its answer "
             "is not kept and cannot overwrite a far player's fresh answer for the same actor");
    ut_check(mp_target_answer_is_kept(true) != the_old_memo(true),
             "and that is the one place the memo differs from the hull before it");
    ut_check(mp_target_answer_is_kept(false) == the_old_memo(false),
             "every other resolution is kept as before");
}

/* The far answer as the hull gave it before: the attacker outright, otherwise the nearest that beat
 * the engine, and nobody asked whether its player stood. */
static int the_old_pick(const mp_target_far_t *far, size_t count, float engine_distance)
{
    int    best = -1;
    float  best_distance = engine_distance;
    size_t i;

    for (i = 0; i < count; ++i) {
        if (!far[i].readable) {
            continue;
        }
        if (far[i].attacker || far[i].distance < best_distance) {
            best          = (int)i;
            best_distance = far[i].distance;
            if (far[i].attacker) {
                break;
            }
        }
    }
    return best;
}

/* A far body at `distance` squared, standing or not. */
static mp_target_far_t body_at(float distance, bool stands)
{
    mp_target_far_t far;

    far.readable = true;
    far.stands   = stands;
    far.attacker = false;
    far.distance = distance;
    return far;
}

static void check_a_far_player_lying_dead_is_nobody(void)
{
    const float     ENGINE = 400.0f;   /* the engine's own answer, twenty units away */
    mp_target_far_t far[3];
    bool            by_aggro = false;
    bool            passed = false;

    ut_section("a far player lying dead is nobody's target");

    far[0] = body_at(1.0f, false);     /* the corpse beside the fan */
    far[1] = body_at(100.0f, true);
    far[2] = body_at(0.0f, false);
    far[2].readable = false;
    ut_check(mp_target_rule_far_pick(far, 3u, ENGINE, &by_aggro, &passed) == 1 && passed &&
                 !by_aggro,
             "a dead far body nearer than a living one: the living one is answered, and the dead "
             "one is counted as passed over");
    ut_check(the_old_pick(far, 3u, ENGINE) == 0,
             "where the hull before answered the corpse");

    far[0].attacker = true;
    ut_check(mp_target_rule_far_pick(far, 3u, ENGINE, &by_aggro, &passed) == 1 && !by_aggro,
             "a dead attacker does not win on the memory of its hit");
    ut_check(the_old_pick(far, 3u, ENGINE) == 0, "where the hull before turned on it");

    far[1].stands = false;
    ut_check(mp_target_rule_far_pick(far, 3u, ENGINE, &by_aggro, &passed) == -1 && passed,
             "with every far player dead the engine's own answer stands");

    far[0] = body_at(1.0f, true);
    far[1] = body_at(100.0f, true);
    far[1].attacker = true;
    ut_check(mp_target_rule_far_pick(far, 3u, ENGINE, &by_aggro, &passed) == 1 && by_aggro &&
                 !passed,
             "a living attacker still wins outright over a nearer body");
    ut_check(mp_target_rule_far_pick(NULL, 3u, ENGINE, &by_aggro, &passed) == -1,
             "and no table is the engine's answer");
}

/* Where every far player stands, the rule is the hull before it, case for case. */
static void check_the_living_are_answered_as_before(void)
{
    const float     DISTANCES[4] = { 0.0f, 50.0f, 400.0f, 900.0f };
    mp_target_far_t far[3];
    unsigned        cases = 0u;
    unsigned        apart = 0u;
    size_t          a;
    size_t          b;
    size_t          c;
    size_t          who;

    ut_section("with everybody standing, the answer is the one the hull gave before");
    for (a = 0; a < 4u; ++a) {
        for (b = 0; b < 4u; ++b) {
            for (c = 0; c < 4u; ++c) {
                for (who = 0; who < 4u; ++who) {
                    bool by_aggro = false;
                    bool passed = false;

                    far[0] = body_at(DISTANCES[a], true);
                    far[1] = body_at(DISTANCES[b], true);
                    far[2] = body_at(DISTANCES[c], true);
                    if (who < 3u) {
                        far[who].attacker = true;
                    }
                    far[2].readable = (a + b) % 2u == 0u;
                    ++cases;
                    if (mp_target_rule_far_pick(far, 3u, 400.0f, &by_aggro, &passed) !=
                            the_old_pick(far, 3u, 400.0f) ||
                        passed) {
                        ++apart;
                    }
                }
            }
        }
    }
    ut_checkf(apart == 0u, "%u of %u cases answer otherwise", apart, cases);
}

/* The few actors the host stays the player for. Before the rule nobody was: the reference is "no
 * claim", and the rule is held to differing from it only where the engine answered, somebody is
 * joined, and the actor is one of the three kinds. */
static void check_for_whom_the_host_stays_the_player(void)
{
    mp_target_claim_evidence_t e;
    unsigned                   code;
    unsigned                   wrong   = 0u;
    unsigned                   claimed = 0u;

    ut_section("the engine's answer is kept for the scene's actors, a taker, and the actor of a "
               "scene just ended");

    e = (mp_target_claim_evidence_t){ true, true, true, false, false };
    ut_check(mp_target_rule_claim(&e) == MP_TARGET_CLAIM_SCENE,
             "an actor of the host's scene means the host");
    e = (mp_target_claim_evidence_t){ true, true, false, true, false };
    ut_check(mp_target_rule_claim(&e) == MP_TARGET_CLAIM_TAKER,
             "an actor that took here and has not given back means the host");
    e = (mp_target_claim_evidence_t){ true, true, false, false, true };
    ut_check(mp_target_rule_claim(&e) == MP_TARGET_CLAIM_AFTER,
             "the actor of a scene just ended means the host");
    e = (mp_target_claim_evidence_t){ true, true, true, true, true };
    ut_check(mp_target_rule_claim(&e) == MP_TARGET_CLAIM_SCENE,
             "the scene is named first where more than one holds");
    e = (mp_target_claim_evidence_t){ true, true, false, true, true };
    ut_check(mp_target_rule_claim(&e) == MP_TARGET_CLAIM_TAKER, "then the taker");

    ut_section("and for nobody else, nor with the host dead, nor with nobody joined");

    e = (mp_target_claim_evidence_t){ true, true, false, false, false };
    ut_check(mp_target_rule_claim(&e) == MP_TARGET_CLAIM_NONE,
             "any other actor goes on answering every player while a scene stands");
    e = (mp_target_claim_evidence_t){ false, true, true, true, true };
    ut_check(mp_target_rule_claim(&e) == MP_TARGET_CLAIM_NONE,
             "with no answer of the engine's, the host dead, the rule that follows is asked as "
             "before: no target is not kept for a scene's actor");
    e = (mp_target_claim_evidence_t){ true, false, true, true, true };
    ut_check(mp_target_rule_claim(&e) == MP_TARGET_CLAIM_NONE,
             "with nobody joined there is nobody to weigh, and nothing is kept");
    ut_check(mp_target_rule_claim(NULL) == MP_TARGET_CLAIM_NONE, "no evidence keeps nothing");

    for (code = 0u; code < 32u; ++code) {
        mp_target_claim_t claim;
        bool              may;

        e.engine_answered = (code & 1u) != 0u;
        e.joined          = (code & 2u) != 0u;
        e.of_the_scene    = (code & 4u) != 0u;
        e.taker           = (code & 8u) != 0u;
        e.after_the_scene = (code & 16u) != 0u;
        claim = mp_target_rule_claim(&e);
        may   = e.engine_answered && e.joined &&
                (e.of_the_scene || e.taker || e.after_the_scene);
        wrong   += (claim != MP_TARGET_CLAIM_NONE) == may ? 0u : 1u;
        claimed += claim != MP_TARGET_CLAIM_NONE ? 1u : 0u;
    }
    ut_checkf(wrong == 0u && claimed == 7u,
              "over all thirty two inputs the answer is kept in exactly the seven it may be "
              "(%u wrong, %u kept)", wrong, claimed);
}

int main(void)
{
    check_the_facing_test_is_left_alone();
    check_the_list_fails_open();
    check_the_facing_test_is_not_kept();
    check_a_far_player_lying_dead_is_nobody();
    check_the_living_are_answered_as_before();
    check_for_whom_the_host_stays_the_player();

    return ut_summary("the target rule");
}
