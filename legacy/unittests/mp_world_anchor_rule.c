/* mp_world_anchor_rule.c: which body the host's world is woken and kept by. */
#include "unittest.h"

#include "mp_world_anchor_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LOCAL_BODY 0x00ABC000u
#define FAR_BODY_1 0x00B01000u
#define FAR_BODY_2 0x00B02000u
#define FAR_BODY_3 0x00B03000u

static void put(mp_world_anchor_candidate_t *c, uint32_t body, float x, float y, float z,
                bool stands)
{
    c->body        = body;
    c->position[0] = x;
    c->position[1] = y;
    c->position[2] = z;
    c->stands      = stands;
}

/* Three far players, the nearest to the origin standing second. */
static void three_far_players(mp_world_anchor_candidate_t c[3])
{
    put(&c[0], FAR_BODY_1, 100.0f, 0.0f, 0.0f, true);
    put(&c[1], FAR_BODY_2, 10.0f, 0.0f, 0.0f, true);
    put(&c[2], FAR_BODY_3, 50.0f, 0.0f, 0.0f, true);
}

/* With no session that widens, the engine's answer is the answer, alive or dead. That covers
 * single player, a client and a host whose gate is not armed: every case where nothing changes. */
static void test_without_widening_the_engine_answers(void)
{
    mp_world_anchor_candidate_t c[3];
    size_t                      chosen = 99u;
    const float                 died_at[3] = { 0.0f, 0.0f, 0.0f };

    ut_section("no session that widens the range");
    three_far_players(c);
    ut_check(mp_world_anchor_rule_pick(false, LOCAL_BODY, c, 3u, died_at, &chosen) ==
                 MP_WORLD_ANCHOR_ENGINE,
             "a living player is the engine's own answer");
    ut_check(mp_world_anchor_rule_pick(false, 0u, c, 3u, died_at, &chosen) ==
                 MP_WORLD_ANCHOR_ENGINE,
             "and so is a dead one: without widening nothing stands in for him");
    ut_check(chosen == 99u, "and no candidate is named");
}

/* A host whose player lives is measured as the engine measures, and the range gate widens that
 * one question to the far players itself. */
static void test_a_living_host_is_the_engines(void)
{
    mp_world_anchor_candidate_t c[3];
    size_t                      chosen = 99u;
    const float                 died_at[3] = { 0.0f, 0.0f, 0.0f };

    ut_section("a host whose player lives");
    three_far_players(c);
    ut_check(mp_world_anchor_rule_pick(true, LOCAL_BODY, c, 3u, died_at, &chosen) ==
                 MP_WORLD_ANCHOR_ENGINE,
             "the living local body is never replaced");
    ut_check(chosen == 99u, "and no candidate is named");
}

/* The defect a run with four players showed: the host lay dead for half the level, and nothing
 * woke or went for anybody while the clients played on. */
static void test_a_dead_host_takes_the_nearest_standing(void)
{
    mp_world_anchor_candidate_t c[3];
    size_t                      chosen = 99u;
    const float                 died_at[3] = { 0.0f, 0.0f, 0.0f };
    const float                 died_far[3] = { 120.0f, 0.0f, 0.0f };

    ut_section("a host whose player is dead");
    three_far_players(c);
    ut_check(mp_world_anchor_rule_pick(true, 0u, c, 3u, died_at, &chosen) ==
                 MP_WORLD_ANCHOR_FAR,
             "a standing far player stands in for the dead host");
    ut_check(chosen == 1u, "the one standing nearest to where the host died");
    ut_check(mp_world_anchor_rule_pick(true, 0u, c, 3u, died_far, &chosen) ==
                     MP_WORLD_ANCHOR_FAR && chosen == 0u,
             "and the nearest is measured from the death, not from the origin");

    c[1].stands = false;
    ut_check(mp_world_anchor_rule_pick(true, 0u, c, 3u, died_at, &chosen) ==
                     MP_WORLD_ANCHOR_FAR && chosen == 2u,
             "a far player who does not stand is passed over");
    c[1].stands = true;
    c[1].body   = 0u;
    ut_check(mp_world_anchor_rule_pick(true, 0u, c, 3u, died_at, &chosen) ==
                     MP_WORLD_ANCHOR_FAR && chosen == 2u,
             "and so is a bank with no body built, however near its pose");
}

static void test_nobody_standing_is_the_engines_nothing(void)
{
    mp_world_anchor_candidate_t c[3];
    size_t                      chosen = 99u;
    const float                 died_at[3] = { 0.0f, 0.0f, 0.0f };

    ut_section("a dead host with nobody standing");
    three_far_players(c);
    c[0].stands = false;
    c[1].stands = false;
    c[2].stands = false;
    ut_check(mp_world_anchor_rule_pick(true, 0u, c, 3u, died_at, &chosen) ==
                 MP_WORLD_ANCHOR_NOBODY,
             "nothing, the engine's own answer, when no far player stands");
    ut_check(chosen == 99u, "and no candidate is named");
    ut_check(mp_world_anchor_rule_pick(true, 0u, NULL, 0u, died_at, &chosen) ==
                 MP_WORLD_ANCHOR_NOBODY,
             "and when there are no far players at all");
}

/* Where the body lies could not be read. Any standing player is a better anchor than none, and
 * the first one in bank order is taken so the answer does not wander between substeps. */
static void test_an_unknown_death_takes_the_first_standing(void)
{
    mp_world_anchor_candidate_t c[3];
    size_t                      chosen = 99u;

    ut_section("the place of the death could not be read");
    three_far_players(c);
    c[0].stands = false;
    ut_check(mp_world_anchor_rule_pick(true, 0u, c, 3u, NULL, &chosen) == MP_WORLD_ANCHOR_FAR,
             "a standing far player is still taken");
    ut_check(chosen == 1u, "the first standing one in bank order");
    ut_check(mp_world_anchor_rule_pick(true, 0u, c, 3u, NULL, NULL) == MP_WORLD_ANCHOR_FAR,
             "and a caller that does not want the index may leave it out");
}

/* The rule as it was written before it asked the seat's anchor order, kept here as the reference:
 * the anchor now takes its answer from the one rule for "the standing player nearest the death",
 * and must give the same answer the old copy gave in every case, ties included. */
static float reference_distance(const float a[3], const float b[3])
{
    float dx = a[0] - b[0];
    float dy = a[1] - b[1];
    float dz = a[2] - b[2];

    return dx * dx + dy * dy + dz * dz;
}

static mp_world_anchor_answer_t reference_pick(bool widens, uint32_t local,
                                               const mp_world_anchor_candidate_t *candidates,
                                               size_t count, const float *died_at, size_t *chosen)
{
    bool   found = false;
    size_t best = 0u;
    float  best_distance = 0.0f;
    size_t index;

    if (!widens || local != 0u) {
        return MP_WORLD_ANCHOR_ENGINE;
    }
    for (index = 0u; candidates != NULL && index < count; ++index) {
        float distance = 0.0f;

        if (!candidates[index].stands || candidates[index].body == 0u) {
            continue;
        }
        if (died_at != NULL) {
            distance = reference_distance(candidates[index].position, died_at);
        }
        if (!found || distance < best_distance) {
            found         = true;
            best          = index;
            best_distance = distance;
        }
    }
    if (!found) {
        return MP_WORLD_ANCHOR_NOBODY;
    }
    if (chosen != NULL) {
        *chosen = best;
    }
    return MP_WORLD_ANCHOR_FAR;
}

/* A small generator of its own, so the cases are the same on every run. Positions come from a
 * grid of a few values, which makes equal distances, the case the tie rule is for, common. */
static uint32_t next_random(uint32_t *state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state >> 8;
}

static float grid_value(uint32_t *state)
{
    static const float VALUES[5] = { -10.0f, -2.5f, 0.0f, 2.5f, 10.0f };

    return VALUES[next_random(state) % 5u];
}

static void test_the_same_answer_as_the_rule_it_replaced(void)
{
    uint32_t state = 20260925u;
    uint32_t cases;
    uint32_t differing = 0u;
    uint32_t far_answers = 0u;
    uint32_t ties = 0u;

    ut_section("the one rule for the nearest standing player answers as the old copy did");
    for (cases = 0u; cases < 20000u; ++cases) {
        mp_world_anchor_candidate_t c[3];
        float                       died_at[3];
        bool                        widens = (next_random(&state) % 8u) != 0u;
        uint32_t                    local  = (next_random(&state) % 6u) == 0u ? LOCAL_BODY : 0u;
        bool                        known_death = (next_random(&state) % 5u) != 0u;
        size_t                      count = (size_t)(next_random(&state) % 4u);
        size_t                      chosen_new = 99u;
        size_t                      chosen_old = 99u;
        size_t                      i;
        mp_world_anchor_answer_t    answer_new;
        mp_world_anchor_answer_t    answer_old;

        for (i = 0u; i < 3u; ++i) {
            put(&c[i], (next_random(&state) % 5u) == 0u ? 0u : FAR_BODY_1 + (uint32_t)i,
                grid_value(&state), grid_value(&state), grid_value(&state),
                (next_random(&state) % 3u) != 0u);
        }
        died_at[0] = grid_value(&state);
        died_at[1] = grid_value(&state);
        died_at[2] = grid_value(&state);
        answer_new = mp_world_anchor_rule_pick(widens, local, c, count,
                                               known_death ? died_at : NULL, &chosen_new);
        answer_old = reference_pick(widens, local, c, count, known_death ? died_at : NULL,
                                    &chosen_old);
        if (answer_new != answer_old || chosen_new != chosen_old) {
            ++differing;
        }
        if (answer_old == MP_WORLD_ANCHOR_FAR) {
            ++far_answers;
            for (i = 0u; i < count; ++i) {
                if (i != chosen_old && c[i].stands && c[i].body != 0u && known_death &&
                    reference_distance(c[i].position, died_at) ==
                        reference_distance(c[chosen_old].position, died_at)) {
                    ++ties;
                    break;
                }
            }
        }
    }
    ut_checkf(differing == 0u,
              "20000 cases, %u of them a far body standing in and %u of those a tie in distance: "
              "%u answered differently", (unsigned)far_answers, (unsigned)ties,
              (unsigned)differing);
}

int main(void)
{
    test_without_widening_the_engine_answers();
    test_a_living_host_is_the_engines();
    test_a_dead_host_takes_the_nearest_standing();
    test_nobody_standing_is_the_engines_nothing();
    test_an_unknown_death_takes_the_first_standing();
    test_the_same_answer_as_the_rule_it_replaced();

    return ut_summary("mp_world_anchor_rule");
}
