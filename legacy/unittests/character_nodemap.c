/* character_nodemap.c: the half of the name translation that can be checked without the game.
 *
 * What is driven here is the mapping, the floor and the rest pose shift, which is where the
 * decisions are. Everything below them reads engine memory: the clip copy, the substitution hook
 * and the two model lookups all need a live model and a live puppet, so they are checked by the
 * log line the swap writes rather than here.
 */
#include "unittest.h"

#include "character_nodemap.h"

#include <stdint.h>
#include <string.h>

/* Obi-Wan's rig, cut down to the names that decide anything: the spine, both arms, one leg, the
 * two he alone carries and the mount. The real table is 48 names in this order. */
static const char *const REFERENCE[] = {
    "dummy01", "waist", "sabrewaist", "chest", "neck", "head", "ponytail",
    "ruparm", "rforarm", "rhand", "weapon", "sabre",
    "luparm", "lforarm", "lhand",
    "rthigh", "rcalf", "rfoot"
};
#define REFERENCE_COUNT ((uint32_t)(sizeof REFERENCE / sizeof REFERENCE[0]))

/* The shape 56 of the shipped assets have: the same body, none of the Jedi parts, and one node of
 * its own. */
static const char *const HUMANOID[] = {
    "dummy01", "waist", "midsect", "chest", "neck", "head",
    "ruparm", "rforarm", "rhand",
    "luparm", "lforarm", "lhand",
    "rthigh", "rcalf", "rfoot"
};
#define HUMANOID_COUNT ((uint32_t)(sizeof HUMANOID / sizeof HUMANOID[0]))

/* The shape 110 of them have: the root and nothing else the player's rig knows. */
static const char *const PROP[] = { "dummy01", "body", "lid" };
#define PROP_COUNT ((uint32_t)(sizeof PROP / sizeof PROP[0]))

static void check_the_map_is_by_name_and_not_by_position(void)
{
    int32_t       map[NODEMAP_MAX_NODES];
    nodemap_fit_t fit;

    ut_section("a track reaches a joint by name, whatever position either sits in");
    ut_check(character_nodemap_build(REFERENCE, REFERENCE_COUNT, HUMANOID, HUMANOID_COUNT,
                                     map, &fit),
             "a humanoid rig can be measured against the player's");

    ut_check(map[0] == 0, "the root finds the root");
    ut_check(map[1] == 1, "the waist finds the waist");

    /* The point of the whole module: the target's chest is its node 3 and the player's is his node
     * 3 only by accident here, while the hands are at 8 and 9. */
    ut_check(map[8] == 9, "the target's rhand is driven by the player's rhand, not by his node 8");
    ut_check(map[11] == 14, "and the left hand likewise");
    ut_check(map[14] == 17, "and the right foot");

    ut_check(map[2] == NODEMAP_NO_TRACK,
             "a node the player's rig has no name for gets no track and holds its rest pose");
    ut_check(fit.held == 1u, "and it is the only one");
    ut_check(fit.matched == HUMANOID_COUNT - 1u, "every other node is driven");
    ut_check(fit.dropped == REFERENCE_COUNT - fit.matched,
             "the player's tracks with no node to drive are dropped");
    ut_check(fit.reference_nodes == REFERENCE_COUNT, "the reference width is reported");
    ut_check(fit.target_nodes == HUMANOID_COUNT, "so is the target's");
}

static void check_the_floor(void)
{
    int32_t       map[NODEMAP_MAX_NODES];
    nodemap_fit_t fit;

    ut_section("the floor is the waist, the chest and the head");
    ut_check(character_nodemap_build(REFERENCE, REFERENCE_COUNT, HUMANOID, HUMANOID_COUNT,
                                     map, &fit), "the humanoid measures");
    ut_check(fit.spine, "it carries the waist, the chest and the head");
    ut_check(character_nodemap_fit_is_offered(&fit), "so it is offered");

    ut_check(character_nodemap_build(REFERENCE, REFERENCE_COUNT, PROP, PROP_COUNT, map, &fit),
             "so does a rig that shares only its root");
    ut_check(fit.matched == 1u, "and exactly one of its nodes is driven");
    ut_check(fit.held == 2u, "the other two hold their rest pose");
    ut_check(!fit.spine, "it carries none of the three the floor names");
    ut_check(!character_nodemap_fit_is_offered(&fit),
             "so it is refused: it would ride along without being animated");
}

static void check_a_name_is_claimed_once(void)
{
    static const char *const twins[] = { "dummy01", "chest", "chest", "waist", "head" };
    int32_t       map[NODEMAP_MAX_NODES];
    nodemap_fit_t fit;

    ut_section("one name, one track");
    ut_check(character_nodemap_build(REFERENCE, REFERENCE_COUNT, twins, 5u, map, &fit),
             "a target that repeats a name still measures");
    ut_check(map[1] == 3, "the first of the two takes the track");
    ut_check(map[2] == NODEMAP_NO_TRACK,
             "the second gets none, because two nodes driven by one track would fight over it");
    ut_check(fit.matched == 4u, "four of the five are driven");
}

static void check_the_case_does_not_matter(void)
{
    static const char *const shouted[] = { "DUMMY01", "Waist", "cHeSt", "HEAD" };
    int32_t       map[NODEMAP_MAX_NODES];
    nodemap_fit_t fit;

    ut_section("the case of a node name decides nothing");
    ut_check(character_nodemap_build(REFERENCE, REFERENCE_COUNT, shouted, 4u, map, &fit),
             "a rig that shouts its node names measures");
    ut_check(fit.matched == 4u, "and every one of them is found");
    ut_check(fit.spine, "including the three the floor names");
}

static void check_the_refusals(void)
{
    static const char *const one[] = { "dummy01" };
    static const char *const wide[NODEMAP_MAX_NODES + 1] = { "dummy01" };
    int32_t       map[NODEMAP_MAX_NODES + 1];
    nodemap_fit_t fit;

    ut_section("what is refused rather than guessed at");
    ut_check(!character_nodemap_build(NULL, REFERENCE_COUNT, one, 1u, map, &fit),
             "a missing reference table is refused, not crashed on");
    ut_check(!character_nodemap_build(REFERENCE, REFERENCE_COUNT, NULL, 1u, map, &fit),
             "so is a missing target table");
    ut_check(!character_nodemap_build(REFERENCE, REFERENCE_COUNT, one, 1u, NULL, &fit),
             "so is a missing map");
    ut_check(!character_nodemap_build(REFERENCE, REFERENCE_COUNT, one, 1u, map, NULL),
             "so is a missing fit");
    ut_check(!character_nodemap_build(REFERENCE, 0u, one, 1u, map, &fit),
             "an empty reference rig has nothing to translate from");
    ut_check(!character_nodemap_build(REFERENCE, REFERENCE_COUNT, one, 0u, map, &fit),
             "and an empty target has nothing to translate to");

    /* The per track keyframe cursor array is 64 entries and the track's clock is the word above it,
     * so a wider rig would write its last cursors over the clock. The widest one the game ships has
     * 48 nodes, which is why this has never been anything but a guard. */
    ut_check(!character_nodemap_build(REFERENCE, REFERENCE_COUNT, wide, NODEMAP_MAX_NODES + 1u,
                                      map, &fit),
             "a rig wider than the cursor array is refused");
    ut_check(!character_nodemap_build(wide, NODEMAP_MAX_NODES + 1u, REFERENCE, REFERENCE_COUNT,
                                      map, &fit),
             "in either direction");

    ut_check(!character_nodemap_fit_is_offered(NULL), "and a missing fit is not offered");
}

static void check_the_engine_side_declines_without_a_model(void)
{
    nodemap_fit_t  fit;
    nodemap_body_t body;

    ut_section("with no engine behind it nothing arms");
    ut_check(character_nodemap_node_count(0u) == 0u, "a null model has no nodes");
    ut_check(character_nodemap_find(0u, "waist") == NODEMAP_NO_TRACK,
             "and nothing can be found on it");
    ut_check(character_nodemap_find(0u, NULL) == NODEMAP_NO_TRACK, "nor can a missing name");
    ut_check(!character_nodemap_measure(0u, 0u, &fit), "two null models cannot be measured");
    ut_check(!character_nodemap_measure(0u, 0u, NULL), "and neither can a missing answer");
    ut_check(!character_nodemap_is_armed(), "nothing is armed before a swap");
    memset(&body, 0, sizeof body);
    body.local = true;
    ut_check(!character_nodemap_arm(&body), "and a null handle cannot arm it");
    ut_check(!character_nodemap_arm(NULL), "nor can a missing body");
    ut_check(!character_nodemap_is_armed(), "and a refused arm leaves nothing armed");
}

/* The measured shape of the problem, cut to three joints. The player's forearm hangs 0.13 below
 * his upper arm; a small droid's hangs 0.05 below its own. A keyframe entry carries the ABSOLUTE
 * offset, so the droid used to be handed 0.13 and grew a human arm. */
static const nodemap_rest_t REFERENCE_REST[] = {
    { { 0.0f,  0.0f,  0.5f  }, {   0.0f, 0.0f, 0.0f } },   /* dummy01 */
    { { 0.1f, -0.04f, 0.13f }, {   0.0f, 0.0f, 0.0f } },   /* ruparm  */
    { { 0.03f, 0.0f, -0.13f }, { -75.0f, 0.0f, 0.0f } }    /* rforarm */
};
static const nodemap_rest_t TARGET_REST[] = {
    { { 0.0f,  0.0f,  0.2f  }, {  0.0f, 0.0f,  0.0f } },   /* dummy01 */
    { { 0.04f, 0.0f, -0.05f }, {  0.0f, 0.0f, 90.0f } },   /* its rforarm, at ordinal 1 */
    /* a node the reference has no name for */
    { { 0.0f,  0.0f,  0.0f  }, {  0.0f, 0.0f,  0.0f } }
};

static void check_the_shift_is_the_difference_of_the_two_rest_poses(void)
{
    static const int32_t map[] = { 0, 2, NODEMAP_NO_TRACK };
    nodemap_rest_t rebase[3];

    ut_section("a joint keeps its own build and borrows only the movement");
    ut_check(character_nodemap_rebase(REFERENCE_REST, 3u, TARGET_REST, 3u, map, rebase),
             "two rest poses and a map measure");

    /* The engine subtracts the TARGET's rest and adds it back, so at full weight the two cancel
     * and the joint lands on the reference's absolute pose. Adding this shift to the entry first
     * is what leaves `clip - referenceRest` in the middle, which is movement and nothing else. */
    ut_check(rebase[1].pos[2] == TARGET_REST[1].pos[2] - REFERENCE_REST[2].pos[2],
             "the forearm's shift is its own offset minus the one the clip was authored with");
    ut_near(rebase[1].pos[2], 0.08, 1e-6, "which is the whole difference in bone length");
    ut_near(rebase[1].euler[2], 90.0, 1e-6, "and the rest orientation is carried the same way");
    ut_near(rebase[1].euler[0], 75.0, 1e-6,
            "in every component, including the one the clip tilts");

    ut_near(rebase[0].pos[2], -0.3, 1e-6, "the root is shifted by its own difference");

    /* A node with no track never reaches a shifted entry, and a shift for it would be a shift
     * applied to somebody else's record if the map ever changed under it. */
    ut_check(character_nodemap_shift_is_zero(&rebase[2]),
             "a node that holds its rest pose is shifted by nothing");
    ut_check(!character_nodemap_shift_is_zero(&rebase[1]), "a driven one is not");
    ut_check(character_nodemap_shift_is_zero(NULL), "and a missing shift changes nothing");
}

static void check_an_identical_rig_costs_nothing(void)
{
    static const int32_t map[] = { 0, 1, 2 };
    nodemap_rest_t rebase[3];
    uint32_t       j;

    ut_section("a rig that is the player's own build is left exactly as it was");
    ut_check(character_nodemap_rebase(REFERENCE_REST, 3u, REFERENCE_REST, 3u, map, rebase),
             "a rig measured against itself measures");
    for (j = 0; j < 3u; ++j) {
        ut_check(character_nodemap_shift_is_zero(&rebase[j]),
                 "every joint's shift is exactly zero, so it keeps the asset's own keyframes");
    }
}

static void check_the_entry_moves_and_the_rates_do_not(void)
{
    /* One 0x38 byte keyframe entry: time, flags, offset, orientation, then the two rate triples
     * the compositor extrapolates with. */
    float entry[14];
    const nodemap_rest_t shift = { { 1.0f, 2.0f, 3.0f }, { 10.0f, 20.0f, 30.0f } };
    uint32_t i;

    ut_section("the shift moves the pose and leaves the rates alone");
    for (i = 0; i < 14u; ++i) {
        entry[i] = (float)i;
    }
    character_nodemap_shift_entry(entry, &shift);

    ut_check(entry[0] == 0.0f, "the frame time is untouched");
    ut_check(entry[1] == 1.0f, "so is the flags word");
    ut_check(entry[2] == 2.0f + 1.0f, "the offset moves by the shift");
    ut_check(entry[3] == 3.0f + 2.0f, "in every component");
    ut_check(entry[4] == 4.0f + 3.0f, "of the three");
    ut_check(entry[5] == 5.0f + 10.0f, "and so does the orientation");
    ut_check(entry[6] == 6.0f + 20.0f, "in every component");
    ut_check(entry[7] == 7.0f + 30.0f, "of the three");

    /* The derivative of a constant is zero, so a shifted pose still moves at the authored rate. */
    for (i = 8u; i < 14u; ++i) {
        ut_check(entry[i] == (float)i, "a rate is not a position and is not shifted");
    }

    character_nodemap_shift_entry(NULL, &shift);
    character_nodemap_shift_entry(entry, NULL);
    ut_check(entry[2] == 3.0f, "a missing entry or a missing shift changes nothing");
}

static void check_the_rebase_refusals(void)
{
    static const int32_t map[] = { 0, 1, 2 };
    nodemap_rest_t rebase[NODEMAP_MAX_NODES + 1];

    ut_section("what the shift refuses rather than guesses at");
    ut_check(!character_nodemap_rebase(NULL, 3u, TARGET_REST, 3u, map, rebase),
             "a missing reference rest table is refused");
    ut_check(!character_nodemap_rebase(REFERENCE_REST, 3u, NULL, 3u, map, rebase),
             "so is a missing target rest table");
    ut_check(!character_nodemap_rebase(REFERENCE_REST, 3u, TARGET_REST, 3u, NULL, rebase),
             "so is a missing map");
    ut_check(!character_nodemap_rebase(REFERENCE_REST, 3u, TARGET_REST, 3u, map, NULL),
             "so is a missing answer");
    ut_check(!character_nodemap_rebase(REFERENCE_REST, 0u, TARGET_REST, 3u, map, rebase),
             "an empty reference rig has no rest pose to measure from");
    ut_check(!character_nodemap_rebase(REFERENCE_REST, 3u, TARGET_REST, 0u, map, rebase),
             "and an empty target has none to measure to");
    ut_check(!character_nodemap_rebase(REFERENCE_REST, NODEMAP_MAX_NODES + 1u, TARGET_REST, 3u,
                                       map, rebase),
             "a rig wider than the cursor array is refused here as well");
}

/* A map that points outside the reference table cannot be trusted to name a rest pose either, and
 * the answer is the same one the compositor gives such a node: nothing. */
static void check_a_map_out_of_range_shifts_nothing(void)
{
    static const int32_t map[] = { 0, 99, -7 };
    nodemap_rest_t rebase[3];

    ut_section("a map entry outside the reference rig drives nothing and shifts nothing");
    ut_check(character_nodemap_rebase(REFERENCE_REST, 3u, TARGET_REST, 3u, map, rebase),
             "the pair still measures");
    ut_check(character_nodemap_shift_is_zero(&rebase[1]), "the out of range entry is zero");
    ut_check(character_nodemap_shift_is_zero(&rebase[2]), "so is the negative one");
    ut_check(!character_nodemap_shift_is_zero(&rebase[0]), "and the one in range is not");
}

int main(void)
{
    check_the_map_is_by_name_and_not_by_position();
    check_the_floor();
    check_a_name_is_claimed_once();
    check_the_case_does_not_matter();
    check_the_refusals();
    check_the_shift_is_the_difference_of_the_two_rest_poses();
    check_an_identical_rig_costs_nothing();
    check_the_entry_moves_and_the_rates_do_not();
    check_the_rebase_refusals();
    check_a_map_out_of_range_shifts_nothing();
    check_the_engine_side_declines_without_a_model();

    return ut_summary("the name translation between two skeletons");
}
