/* character_ground.c: the half of the ground mark correction that can be checked without the game.
 *
 * What is driven here is the arithmetic: the engine's own 3x4 transform, its out of range rule, and
 * the property the whole module exists for: that taking the mesh centre out of the worn rig
 * instead of out of the player's own moves the mark by more than a quarter of the worn body, on
 * the rigs the panel offers.
 *
 * The numbers are the shipped assets. Each is the bounding box centre the loader computes for the
 * mesh that hangs on a node, in the model's own units, read out of the .baf files. They are what
 * the engine transforms; nothing here is invented to make a case.
 *
 * Obi-Wan's rig is the reference because it is what a player wearing a borrowed model is still
 * animated as. His nodes 16, 22 and 24 are `gun19`, `gun13` and `gun11`, three of the twenty four
 * weapon models he carries as children of his hand, and all three share one mesh. That is the
 * single most telling fact in this file: on a rig whose left foot is node 16, the footprint was
 * being drawn at the bounding box centre of a holstered blaster.
 */
#include "unittest.h"

#include "character_ground.h"

#include <math.h>

/* The rest pose of a joint: no rotation, and the joint's own world position as the translation.
 * Three columns of three and a translation, in the engine's own order. */
static void rest_matrix(float m[12], float x, float y, float z)
{
    m[0]  = 1.0f;
    m[1]  = 0.0f;
    m[2]  = 0.0f;
    m[3]  = 0.0f;
    m[4]  = 1.0f;
    m[5]  = 0.0f;
    m[6]  = 0.0f;
    m[7]  = 0.0f;
    m[8]  = 1.0f;
    m[9]  = x;
    m[10] = y;
    m[11] = z;
}

static float distance(const float a[3], const float b[3])
{
    float dx = a[0] - b[0];
    float dy = a[1] - b[1];
    float dz = a[2] - b[2];

    return sqrtf(dx * dx + dy * dy + dz * dz);
}

/* One offered rig: the mesh centre its own left foot node carries, the centre Obi-Wan's node of
 * the same ORDINAL carries, and how tall the rig is in its own units. */
typedef struct rig_case {
    const char *name;
    float       worn[3];
    float       hero[3];
    float       body;
} rig_case;

static const rig_case CASES[] = {
    /* left foot at node 22, which is Obi-Wan's gun13 */
    { "sithgoon", { -1.4196f, -0.8241f, 0.0383f }, { -0.1053f, 0.0468f, 0.4102f }, 0.9628f },
    /* left foot at node 24, Obi-Wan's gun11 */
    { "bigguy",   { -0.8058f,  0.1539f, 0.0375f }, { -0.1053f, 0.0468f, 0.4102f }, 1.3080f },
    /* left foot at node 16, Obi-Wan's gun19 */
    { "anakin",   { -0.6962f,  0.4670f, 0.0245f }, { -0.1053f, 0.0468f, 0.4102f }, 0.6774f },
    { "pitdroid", { -0.2348f, -0.0312f, 0.0247f }, { -0.1053f, 0.0468f, 0.4102f }, 0.7746f }
};
#define CASE_COUNT ((int)(sizeof CASES / sizeof CASES[0]))

/* The one rig in the roster that shares Obi-Wan's node order: its foot really is his node 47, so
 * the two answers nearly agree and the correction has almost nothing to do. */
static const float QUIGUNG_FOOT[3] = { -0.2598f, 0.0732f, 0.0743f };
static const float OBIWAN_FOOT[3]  = { -0.2724f, 0.0744f, 0.0778f };

static void check_the_transform_is_the_engines(void)
{
    float m[12];
    float out[3];
    const float point[3] = { 2.0f, 3.0f, 5.0f };

    ut_section("the 3x4 transform");

    rest_matrix(m, 0.0f, 0.0f, 0.0f);
    ut_check(character_ground_transform(m, point, out) != 0, "an identity transform is accepted");
    ut_near(out[0], 2.0f, 1e-6f, "an identity leaves x alone");
    ut_near(out[1], 3.0f, 1e-6f, "an identity leaves y alone");
    ut_near(out[2], 5.0f, 1e-6f, "an identity leaves z alone");

    rest_matrix(m, -0.0889f, -0.0245f, 0.0709f);
    (void)character_ground_transform(m, point, out);
    ut_near(out[0], 2.0f - 0.0889f, 1e-6f, "the last three floats are the translation, not a row");

    /* A matrix that answers differently under the other reading. The engine's first output word
     * takes the matrix at 0x00, 0x0c and 0x18, which is elements 0, 3 and 6, so the nine rotation
     * floats are three columns. Reading them as three rows would give 3.0 here instead of 2.0. */
    m[0]  = 0.0f;
    m[1]  = 1.0f;
    m[2]  = 0.0f;
    m[3]  = 1.0f;
    m[4]  = 0.0f;
    m[5]  = 0.0f;
    m[6]  = 0.0f;
    m[7]  = 0.0f;
    m[8]  = 1.0f;
    m[9]  = 0.0f;
    m[10] = 0.0f;
    m[11] = 0.0f;
    (void)character_ground_transform(m, point, out);
    ut_near(out[0], 3.0f, 1e-6f, "element 3 multiplies y, which is the column reading");
    ut_near(out[1], 2.0f, 1e-6f, "and element 1 multiplies x into the second output word");
}

static void check_a_bad_number_is_refused_rather_than_written(void)
{
    float m[12];
    float out[3] = { 11.0f, 22.0f, 33.0f };
    float point[3] = { 1.0f, 1.0f, 1.0f };

    ut_section("what a transform refuses");

    rest_matrix(m, 0.0f, 0.0f, 0.0f);
    point[1] = (float)NAN;
    ut_check(character_ground_transform(m, point, out) == 0,
             "a point that is not a number is refused");
    ut_near(out[0], 11.0f, 0.0f, "and the caller's own position is left exactly as it was");

    point[1] = 1.0f;
    m[4] = (float)INFINITY;
    ut_check(character_ground_transform(m, point, out) == 0,
             "an infinite matrix element is refused");
    ut_near(out[1], 22.0f, 0.0f, "and again nothing was written");

    rest_matrix(m, 0.0f, 0.0f, 0.0f);
    ut_check(character_ground_transform(NULL, point, out) == 0, "a missing matrix is refused");
    ut_check(character_ground_transform(m, NULL, out) == 0, "a missing point is refused");
    ut_check(character_ground_transform(m, point, NULL) == 0, "a missing output is refused");
}

static void check_the_out_of_range_rule_is_the_engines(void)
{
    ut_section("a node index past the end");

    ut_check(character_ground_node_of(5, 48u) == 5u, "an index inside the model is itself");
    ut_check(character_ground_node_of(47, 48u) == 47u, "and so is the last one");
    ut_check(character_ground_node_of(48, 48u) == 0u,
             "one past the end is CLAMPED to the root, which is what the engine does rather than "
             "refusing");
    ut_check(character_ground_node_of(-1, 48u) == 0u, "a negative index answers the root too");
    ut_check(character_ground_node_of(0, 0u) == 0u, "an empty model answers the root");

    /* quigung's left foot is node 47. A player who is Panaka has 42 nodes and the engine clamps
     * against HIS count, which is how a rig with a perfectly good foot ends up printing from the
     * root. Clamping against the rig being worn keeps the foot. */
    ut_check(character_ground_node_of(47, 42u) == 0u,
             "clamped against the player's own 42 nodes, quigung's foot becomes the root");
    ut_check(character_ground_node_of(47, 48u) == 47u,
             "clamped against the rig being worn, it stays the foot");
}

static void check_the_mark_moves_by_more_than_a_quarter_of_the_body(void)
{
    float m[12];
    float was[3];
    float now[3];
    int   i;

    ut_section("how far the ground marks were out, on the shipped rigs");

    for (i = 0; i < CASE_COUNT; ++i) {
        float shift;

        /* The same joint matrix for both, because that is exactly the defect: the engine takes the
         * right matrix and applies it to the wrong point. */
        rest_matrix(m, -0.0889f, -0.0245f, 0.0709f);
        (void)character_ground_transform(m, CASES[i].hero, was);
        (void)character_ground_transform(m, CASES[i].worn, now);
        shift = distance(was, now);

        ut_checkf(shift > 0.25f * CASES[i].body,
                  "%s: the print moves by %d/1000 of a unit, more than a quarter of the %d/1000 "
                  "the body itself stands", CASES[i].name, (int)(shift * 1000.0f),
                  (int)(CASES[i].body * 1000.0f));
        ut_checkf(shift > 0.1f,
                  "%s: and by more than 100/1000 of a unit, most of the 125/1000 the print is wide",
                  CASES[i].name);
    }

    ut_near(distance(CASES[0].hero, CASES[0].worn), 1.6199f, 1e-3f,
            "sithgoon, the worst of them, was printing 1.62 units from its foot");
}

static void check_a_rig_in_the_players_own_order_barely_moves(void)
{
    float m[12];
    float was[3];
    float now[3];

    ut_section("the rig that shares the player's node order");

    rest_matrix(m, -0.0889f, -0.0245f, 0.0709f);
    (void)character_ground_transform(m, OBIWAN_FOOT, was);
    (void)character_ground_transform(m, QUIGUNG_FOOT, now);

    ut_check(distance(was, now) < 0.02f,
             "quigung's foot really is Obi-Wan's node 47, so the correction moves it by almost "
             "nothing and the rig that looked right before still looks right");
}

int main(void)
{
    check_the_transform_is_the_engines();
    check_a_bad_number_is_refused_rather_than_written();
    check_the_out_of_range_rule_is_the_engines();
    check_the_mark_moves_by_more_than_a_quarter_of_the_body();
    check_a_rig_in_the_players_own_order_barely_moves();

    return ut_summary("character ground");
}
