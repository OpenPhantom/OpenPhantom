/* character_prop.c: the arithmetic that decides where a borrowed weapon lands.
 *
 * Everything else in that module reads engine memory: the two model walks, the second render
 * handle and the visibility table all need a live model, so they are checked by the log line the
 * swap writes. What is driven here is the composition, the inverse and the one identity the whole
 * design rests on, which is that
 *
 *     compose(compose(hand, inverse(rest)), weaponRestChain) == compose(hand, weaponRelativeToHand)
 *
 * so the weapon keeps the offset, the angle and the scale it has in the player's own hand without
 * any of those three ever being named.
 */
#include "unittest.h"

#include "character_prop.h"

#include <math.h>
#include <stdint.h>

static const float IDENTITY[12] = {
    1.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 1.0f,
    0.0f, 0.0f, 0.0f
};

/* A rotation of `deg` about the z axis with a translation, in the engine's row vector layout. */
static void rigid(float out[12], float deg, float tx, float ty, float tz)
{
    const float rad = deg * 3.14159265358979f / 180.0f;
    const float c = (float)cos(rad);
    const float s = (float)sin(rad);

    out[0]  = c;
    out[1]  = s;
    out[2]  = 0.0f;
    out[3]  = -s;
    out[4]  = c;
    out[5]  = 0.0f;
    out[6]  = 0.0f;
    out[7]  = 0.0f;
    out[8]  = 1.0f;
    out[9]  = tx;
    out[10] = ty;
    out[11] = tz;
}

static void scaled(float out[12], const float source[12], float factor)
{
    int i;

    for (i = 0; i < 12; ++i) {
        out[i] = source[i] * ((i < 9) ? factor : 1.0f);
    }
}

static float length_between(const float left[3], const float right[3])
{
    const double dx = left[0] - right[0];
    const double dy = left[1] - right[1];
    const double dz = left[2] - right[2];

    return (float)sqrt(dx * dx + dy * dy + dz * dz);
}

static int matrices_agree(const float left[12], const float right[12], float tolerance)
{
    int i;

    for (i = 0; i < 12; ++i) {
        float difference = left[i] - right[i];

        if (difference < 0.0f) {
            difference = -difference;
        }
        if (!(difference <= tolerance)) {
            return 0;
        }
    }
    return 1;
}

static void check_the_composition_is_local_then_parent(void)
{
    float parent[12];
    float local[12];
    float out[12];
    float point[3] = { 1.0f, 0.0f, 0.0f };
    float direct[3];
    float staged[3];

    ut_section("composing two matrices");

    rigid(parent, 90.0f, 10.0f, 0.0f, 0.0f);
    rigid(local, 0.0f, 0.0f, 2.0f, 0.0f);

    character_prop_mat_compose(out, parent, IDENTITY);
    ut_check(matrices_agree(out, parent, 1e-6f), "an identity local leaves the parent alone");

    character_prop_mat_compose(out, IDENTITY, local);
    ut_check(matrices_agree(out, local, 1e-6f), "an identity parent leaves the local alone");

    /* The order is the claim: a point must go through the local first and the parent second. */
    character_prop_mat_compose(out, parent, local);
    character_prop_mat_point(direct, point, out);
    character_prop_mat_point(staged, point, local);
    character_prop_mat_point(staged, staged, parent);
    ut_check(fabs(direct[0] - staged[0]) < 1e-5 && fabs(direct[1] - staged[1]) < 1e-5 &&
             fabs(direct[2] - staged[2]) < 1e-5,
             "one composed transform and the two applied in turn put a point in the same place");
    ut_check(direct[0] != 0.0f || direct[1] != 0.0f,
             "and the staged form wrote through its own output without eating its input");

    /* Hand computed: the local moves (1,0,0) to (1,2,0); the parent turns that a quarter turn to
     * (-2,1,0) and adds ten along x. */
    ut_near(direct[0], 8.0f, 1e-4, "the composed x is the turned offset plus the parent's own");
    ut_near(direct[1], 1.0f, 1e-4, "the composed y is the turned offset");
    ut_near(direct[2], 0.0f, 1e-4, "nothing leaves the plane");
}

static void check_the_inverse_is_an_inverse(void)
{
    float chain[12];
    float inverse[12];
    float out[12];
    float stretched[12];

    ut_section("the inverse of a rest chain");

    rigid(chain, 37.0f, 0.15f, -0.11f, 0.44f);
    ut_check(character_prop_mat_skew(chain) < 1e-6f, "a rotation does not depart from orthonormal");
    ut_check(character_prop_mat_invert(inverse, chain), "a rigid chain can be inverted");

    character_prop_mat_compose(out, chain, inverse);
    ut_check(matrices_agree(out, IDENTITY, 1e-5f),
             "the inverse applied first and the chain second is the identity");
    character_prop_mat_compose(out, inverse, chain);
    ut_check(matrices_agree(out, IDENTITY, 1e-5f), "and so is the other order");

    /* The refusal that matters: a chain carrying scale has a transpose that is not its inverse,
     * and a weapon placed with it would be somewhere else entirely. */
    scaled(stretched, chain, 1.4f);
    ut_check(character_prop_mat_skew(stretched) > 0.001f,
             "a chain carrying scale is measurably not a rotation");
    ut_check(!character_prop_mat_invert(out, stretched),
             "and it is refused rather than inverted wrongly");
}

/* The identity the placement rests on, stated as a test rather than as a comment. */
static void check_the_weapon_keeps_its_hold(void)
{
    float rest[12];        /* the reference rig's rest chain, root to right hand   */
    float relative[12];    /* the weapon's own transform, relative to that hand    */
    float weapon_rest[12]; /* so the weapon's rest chain is relative then rest     */
    float hand[12];        /* the borrowed rig's right hand, live, in world        */
    float inverse[12];
    float place[12];
    float landed[12];
    float wanted[12];

    ut_section("the weapon keeps the hold it has in the player's own hand");

    rigid(rest, 25.0f, 0.15f, -0.12f, -0.02f);
    rigid(relative, -75.0f, -0.02f, 0.06f, -0.02f);
    character_prop_mat_compose(weapon_rest, rest, relative);

    rigid(hand, 140.0f, 12.5f, -3.25f, 1.75f);

    ut_check(character_prop_mat_invert(inverse, rest), "the rest chain inverts");
    character_prop_mat_compose(place, hand, inverse);

    character_prop_mat_compose(landed, place, weapon_rest);
    character_prop_mat_compose(wanted, hand, relative);
    ut_check(matrices_agree(landed, wanted, 1e-4f),
             "the weapon lands exactly where the borrowed hand's own frame would put it");

    /* Nothing about the weapon's offset appears in `place`, which is why no table per asset is
     * needed: a second weapon with a different hold follows without changing anything. */
    {
        float other[12];
        float other_rest[12];
        float other_landed[12];
        float other_wanted[12];

        rigid(other, 12.0f, 0.30f, -0.40f, 0.05f);
        character_prop_mat_compose(other_rest, rest, other);
        character_prop_mat_compose(other_landed, place, other_rest);
        character_prop_mat_compose(other_wanted, hand, other);
        ut_check(matrices_agree(other_landed, other_wanted, 1e-4f),
                 "a second weapon with another hold follows the same placement matrix");
    }
}

/* The borrowed body's authored scale rides in without being asked for, and NO FURTHER factor is
 * applied for the rig's own build. That is a decision with a number behind it: over the 129 rigs
 * carrying a right hand, the BEST POSSIBLE uniform factor, fitted per rig from the very quantity a
 * correction would be meant to fix, moves the count landing inside the target's own hand sphere
 * from 128 down to 127 and makes the worst case worse. The algebra a factor would need is here
 * anyway, because it is one multiply on the offset before the compose. */
static void check_the_scale_of_the_borrowed_body_comes_along(void)
{
    float rest[12];
    float relative[12];
    float weapon_rest[12];
    float hand[12];
    float small_hand[12];
    float inverse[12];
    float place[12];
    float landed[12];
    float plain[12];
    float length;
    float small_length;

    ut_section("the borrowed body's scale");

    rigid(rest, 0.0f, 0.10f, 0.0f, 0.50f);
    rigid(relative, 0.0f, 0.0f, 0.05f, 0.0f);
    character_prop_mat_compose(weapon_rest, rest, relative);
    rigid(hand, 0.0f, 4.0f, 4.0f, 1.0f);

    ut_check(character_prop_mat_invert(inverse, rest), "the rest chain inverts");
    character_prop_mat_compose(place, hand, inverse);
    character_prop_mat_compose(plain, place, weapon_rest);
    length = plain[0] * plain[0] + plain[1] * plain[1] + plain[2] * plain[2];

    /* The draw hands the joint builder a matrix that already carries the object's scale, so a
     * borrowed rig authored at half size arrives here as a hand matrix with a half length basis. */
    scaled(small_hand, hand, 0.5f);
    character_prop_mat_compose(place, small_hand, inverse);
    character_prop_mat_compose(landed, place, weapon_rest);
    small_length = landed[0] * landed[0] + landed[1] * landed[1] + landed[2] * landed[2];

    ut_near(length, 1.0f, 1e-4, "at scale one the weapon is drawn at its own size");
    ut_near(small_length, 0.25f, 1e-4,
            "on a half scale rig the weapon is drawn at half size, without being asked to be");

    /* And it stays in the hand rather than beside it: the offset shrinks with the body. */
    ut_near(landed[9] - small_hand[9], (plain[9] - hand[9]) * 0.5f, 1e-4,
            "the offset from the hand shrinks by the same factor");
}

/* A node's matrix is anchored on the mesh's own zero, and the joint is at mesh local -pivot. The
 * placement therefore has to compose the two JOINT frames, and the claim here is that shifting the
 * inverse by the difference of the two pivots is exactly that composition. */
static void translated(float out[12], const float source[3], float sign)
{
    int i;

    for (i = 0; i < 12; ++i) {
        out[i] = (i < 9) ? ((i % 4 == 0) ? 1.0f : 0.0f) : (sign * source[i - 9]);
    }
}

static void check_the_pivot_makes_it_a_joint_to_joint_matrix(void)
{
    const float reference_pivot[3] = { -0.016479f, -0.113031f, -0.460723f };
    const float target_pivot[3]    = {  0.562461f, -0.449158f, -0.326447f };
    float reference[12];
    float target[12];
    float shift[12];
    float reference_joint[12];
    float target_joint[12];
    float inverse[12];
    float wanted[12];
    float actual[12];

    ut_section("the pivot is where the joint is");

    rigid(reference, 12.0f, 0.152092f, -0.116461f, -0.021519f);
    rigid(target, -31.0f, 0.654202f, -0.417129f, -0.037994f);

    /* The joint frame of a node: its own matrix, entered at mesh local -pivot. */
    translated(shift, reference_pivot, -1.0f);
    character_prop_mat_compose(reference_joint, reference, shift);
    translated(shift, target_pivot, -1.0f);
    character_prop_mat_compose(target_joint, target, shift);

    ut_check(character_prop_mat_invert(inverse, reference_joint), "a joint frame inverts");
    character_prop_mat_compose(wanted, target_joint, inverse);

    ut_check(character_prop_mat_invert(inverse, reference), "and so does the node frame");
    character_prop_mat_reparent(actual, inverse, reference_pivot, target_pivot);
    character_prop_mat_compose(actual, target, actual);

    ut_check(matrices_agree(actual, wanted, 1e-5f),
             "the shifted inverse is the two joint frames composed, to the last bit");

    /* And the point of it: a weapon held at an offset from the reference joint arrives at the same
     * offset from the borrowed joint, turned by the borrowed hand and by nothing else. */
    {
        const float held[3] = { -0.021f, 0.030f, -0.026f };
        float reference_point[3];
        float landed[3];
        float from_joint[3];
        int   i;

        character_prop_mat_point(reference_point, held, reference_joint);
        character_prop_mat_point(landed, reference_point, actual);
        for (i = 0; i < 3; ++i) {
            from_joint[i] = landed[i] - target_joint[9 + i];
        }
        character_prop_mat_point(landed, held, target_joint);
        ut_near(from_joint[0], landed[0] - target_joint[9], 1e-5,
                "the weapon keeps its offset from the joint, along x");
        ut_near(from_joint[1], landed[1] - target_joint[10], 1e-5, "along y");
        ut_near(from_joint[2], landed[2] - target_joint[11], 1e-5, "and along z");
    }
}

/* THE MEASURED CASE, and the reason there is a correction at all. Every number below was read out
 * of obiwan.baf and anakin.baf and replayed through the engine's own concatHierarchy. The bare
 * inverse lays the two MODELLING ORIGINS on each other, which is nowhere in particular; exchanging
 * the two pivots lays the two WRIST JOINTS on each other, which is the term concatHierarchy adds
 * for the parent of every child node and therefore the only thing that changes when a weapon
 * subtree is re-parented from one rig's hand to another's. */
static void check_the_anakin_case(void)
{
    const float reference[12] = {
        1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f,
        0.152092f, -0.116461f, -0.021519f
    };
    /* anakin's chest carries a 5 degree bind rotation, which his right hand inherits. */
    const float target[12] = {
        1.0f, 0.0f, 0.0f, 0.0f, 0.996195f, 0.087156f, 0.0f, -0.087156f, 0.996195f,
        0.654202f, -0.417129f, -0.037994f
    };
    const float reference_pivot[3] = { -0.016479f, -0.113031f, -0.460723f };
    const float target_pivot[3]    = {  0.562461f, -0.449158f, -0.326447f };
    /* mesh+0x58 of obiwan's right hand and mesh+0x54 of anakin's, the pair bapobj_nodeSphere
     * answers with. Read here to say where the weapon LANDS; applied nowhere. */
    const float reference_fist[3]  = {  0.008408f,  0.131463f,  0.412226f };
    const float sabre[3]           = {  0.147123f,  0.027249f,  0.412832f };
    const float anakin_hand[3]     = {  0.091741f,  0.013011f,  0.306230f };
    const float anakin_radius      =  0.048147f;
    float reference_hand[3];
    float inverse[12];
    float place[12];
    float landed[3];
    float in_his_own_hand;
    float without;
    float on_the_joint;

    ut_section("obiwan's sabre in anakin's hand");

    ut_check(character_prop_mat_invert(inverse, reference), "obiwan's rest chain inverts");

    character_prop_mat_point(reference_hand, reference_fist, reference);
    in_his_own_hand = length_between(sabre, reference_hand);

    character_prop_mat_compose(place, target, inverse);
    character_prop_mat_point(landed, sabre, place);
    without = length_between(landed, anakin_hand);

    character_prop_mat_reparent(place, inverse, reference_pivot, target_pivot);
    character_prop_mat_compose(place, target, place);
    character_prop_mat_point(landed, sabre, place);
    on_the_joint = length_between(landed, anakin_hand);

    ut_near(in_his_own_hand, 0.0286, 1e-4,
            "obiwan's sabre sits 0.0286 from his own fist, which is where the engine puts it");
    ut_near(without, 0.6531, 1e-3, "without the pivots the sabre lands two thirds of a rig away");
    ut_near(on_the_joint, 0.0307, 1e-3, "and with them it lands inside anakin's hand");
    ut_check(on_the_joint < anakin_radius,
             "inside the sphere anakin's own rig answers for that hand");
}

/* The claim the whole file stands on, written out the long way. The two node records below are
 * obiwan.baf slot 10 "weapon", a child of "rhand", and slot 35 "sabre", a child of "weapon". The
 * chain is built the way rdThing_buildWorldMatrices builds it, once with the reference hand's pivot
 * as the parent term and once with the target hand's, and the question is whether the placement
 * matrix carries the first onto the second. It has to, because that is the only term that differs.
 *
 * This is the assertion the fist anchor could not make. Anchoring on the hand mesh's centre also
 * composed two frames, but not the two the engine composes, and over the shipped rigs the two
 * answers do not agree. */
static void node_local(float out[12], const float pivot[3], const float rest[12],
                       const float parent_pivot[3])
{
    float shift[12];
    int   i;

    translated(shift, pivot, 1.0f);
    character_prop_mat_compose(out, rest, shift);
    for (i = 0; i < 3; ++i) {
        out[9 + i] -= parent_pivot[i];
    }
}

static void check_the_engine_composition_is_reproduced(void)
{
    static const float WEAPON_PIVOT[3] = { 0.0f, 0.0f, 0.0f };
    static const float WEAPON_REST[12] = {
        1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f,
        -0.019800f, 0.055800f, -0.020800f
    };
    /* The hilt carries a -75 degree bind pitch, so the basis is not the identity and a composition
     * in the wrong order would show. */
    static const float SABRE_PIVOT[3] = { 0.054900f, -0.011100f, 0.005000f };
    static const float SABRE_REST[12] = {
        1.0f, 0.0f, 0.0f, 0.0f, 0.258819f, -0.965926f, 0.0f, 0.965926f, 0.258819f,
        -0.001600f, -0.025100f, -0.005500f
    };
    const float reference_pivot[3] = { -0.016479f, -0.113031f, -0.460723f };
    const float target_pivot[3]    = {  0.562461f, -0.449158f, -0.326447f };
    float reference[12];
    float target[12];
    float weapon_here[12];
    float weapon_there[12];
    float sabre_local[12];
    float chain[12];
    float rest_chain[12];
    float truth[12];
    float inverse[12];
    float place[12];
    float landed[12];

    ut_section("the engine's own composition, re-parented");

    rigid(reference, 12.0f, 0.152092f, -0.116461f, -0.021519f);
    rigid(target, -31.0f, 0.654202f, -0.417129f, -0.037994f);

    node_local(sabre_local, SABRE_PIVOT, SABRE_REST, WEAPON_PIVOT);

    /* On the reference rig: the two locals, then its rest chain to the hand. */
    node_local(weapon_here, WEAPON_PIVOT, WEAPON_REST, reference_pivot);
    character_prop_mat_compose(chain, weapon_here, sabre_local);
    character_prop_mat_compose(rest_chain, reference, chain);

    /* What the engine would build if those same two nodes hung on the TARGET's hand. */
    node_local(weapon_there, WEAPON_PIVOT, WEAPON_REST, target_pivot);
    character_prop_mat_compose(chain, weapon_there, sabre_local);
    character_prop_mat_compose(truth, target, chain);

    /* What the placement produces: the reference model drawn against the re-parenting matrix. */
    ut_check(character_prop_mat_invert(inverse, reference), "the rest chain inverts");
    character_prop_mat_reparent(place, inverse, reference_pivot, target_pivot);
    character_prop_mat_compose(place, target, place);
    character_prop_mat_compose(landed, place, rest_chain);

    ut_check(matrices_agree(landed, truth, 1e-6f),
             "the drawn weapon is the matrix the borrowed rig would have built for that node");

    /* And the reason it is not merely close: the difference between the two chains is one
     * translation, and the placement carries exactly that one. */
    character_prop_mat_compose(landed, target, inverse);
    character_prop_mat_compose(landed, landed, rest_chain);
    ut_check(!matrices_agree(landed, truth, 1e-3f),
             "while the bare inverse, without the pivots, is not that matrix at all");
}

/* THE CRASH, as a number. The module hides the borrowed rig's own weapon nodes on the body's render
 * handle, and those are indices into a table the engine sizes by the worn model's NODE COUNT. The
 * eleven slots below are what the shipped weapon table names on a hero rig, read out of WMAIN's own
 * configuration at 0x004B4CE0 and out of obiwan.baf, quigon.baf and mace.baf, which carry one
 * identical 48 node skeleton: `weapon` at 10, `sabre` at 35 and gun01..gun09 at 34 down to 26.
 *
 * Written into baron's twenty-one word table, ten of the eleven land outside it. The pool prefixes
 * every block with sixteen bytes and hands consecutive same size blocks out 112 bytes apart, so
 * word 27 is `hidden + 108`, which is the pool descriptor pointer in the header of the block
 * allocated straight after it. That is thing+0x2c, and zeroing it is what took the process down in
 * the next rdThing_freeArrays. */
static void check_the_hidden_slots_are_bounded_by_the_worn_rig(void)
{
    static const uint32_t HERO_WEAPON_SLOTS[11] = { 10, 35, 34, 33, 32, 31, 30, 28, 27, 26, 29 };
    uint32_t i;
    uint32_t outside_baron = 0;

    ut_section("the borrowed rig's weapon slots against the table they index");

    ut_check(character_prop_slots_outside(HERO_WEAPON_SLOTS, 11u, 48u) == 0u,
             "on the rig they were resolved against, every slot is inside the table");
    ut_check(character_prop_slots_outside(HERO_WEAPON_SLOTS, 11u, 21u) == 10u,
             "on baron's twenty-one node rig, ten of the eleven are outside it");
    ut_check(character_prop_slots_outside(HERO_WEAPON_SLOTS, 11u, 22u) == 10u,
             "and on a twenty-two node rig they are outside it too");

    /* The one that did the damage, named rather than left to the count: word 27 of a 21 word table
     * is 108 bytes past its start, which is the pool header of the block behind it. */
    for (i = 0; i < 11u; ++i) {
        if (HERO_WEAPON_SLOTS[i] == 27u) {
            outside_baron = 1u;
        }
    }
    ut_check(outside_baron == 1u,
             "and slot 27, the one that lands on the next block's pool header, is among them");

    ut_check(character_prop_slots_outside(NULL, 11u, 21u) == 0u,
             "no list is no writes rather than eleven of them");
    ut_check(character_prop_slots_outside(HERO_WEAPON_SLOTS, 0u, 0u) == 0u,
             "and an empty list against an empty table asks for nothing");
}

static void check_a_degenerate_chain_is_refused(void)
{
    float flat[12] = {
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f
    };
    float out[12];

    ut_section("a chain that is not a rotation");

    ut_check(character_prop_mat_skew(flat) > 0.001f, "a collapsed axis is measurably wrong");
    ut_check(!character_prop_mat_invert(out, flat), "and the placement declines rather than guess");
    ut_check(!character_prop_is_armed(), "nothing is armed without a game behind it");
    ut_check(!character_prop_node_sphere(NULL, 0, out, out),
             "and the muzzle answer declines when nothing is drawn");
}

/* The drawn weapon is four meshes and the engine asks for one sphere.
 *
 * The numbers below are `obiwan.baf`, the player's own model, read out of the asset and resolved
 * through its rest chain: the four nodes the equipped `sabre` subtree makes visible, in the order
 * the visibility walk reaches them, with each mesh's own authored centre and radius.
 *
 * What matters is where the answer lands. A hero's swing resolves `sabreblad01` and the engine
 * answers that node's mesh sphere, so the blade's centre IS the contact point the game was built
 * around. Answering with the hilt alone, which is the node the weapon table names, misses it by
 * 0.2725 on a character 0.881 tall; the merged sphere misses it by 0.0335. The swing rows carry a
 * contact radius of 0.15 to 0.25, so the first error is larger than the whole contact sphere. */
static float distance(const float a[3], const float b[3])
{
    const float dx = a[0] - b[0];
    const float dy = a[1] - b[1];
    const float dz = a[2] - b[2];

    return (float)sqrt((double)(dx * dx + dy * dy + dz * dz));
}

static void check_the_drawn_weapon_is_one_sphere(void)
{
    static const float PART[4][3] = {
        { 0.1471f, 0.0272f, 0.4128f },     /* sabre, the hilt        */
        { 0.1487f, 0.2903f, 0.4843f },     /* sabreblad01, the blade */
        { 0.1485f, 0.3116f, 0.4897f },     /* sabreglow1             */
        { 0.1487f, 0.3135f, 0.4905f }      /* sabreglow2             */
    };
    static const float PART_RADIUS[4] = { 0.0697f, 0.2250f, 0.2482f, 0.2512f };
    float centre[3];
    float radius;
    int   i;

    ut_section("the sphere of a weapon that is more than one mesh");

    centre[0] = PART[0][0];
    centre[1] = PART[0][1];
    centre[2] = PART[0][2];
    radius = PART_RADIUS[0];
    for (i = 1; i < 4; ++i) {
        character_prop_sphere_merge(centre, &radius, PART[i], PART_RADIUS[i]);
    }

    for (i = 0; i < 4; ++i) {
        ut_check(distance(centre, PART[i]) + PART_RADIUS[i] <= radius + 1e-4f,
                 "every mesh the handle draws is inside the answered sphere");
    }
    ut_near(radius, 0.3088f, 1e-3, "and the sphere is no larger than it has to be");
    ut_check(distance(centre, PART[1]) < 0.05f,
             "the answer lands on the blade, which is where a hero's own contact node is");
    ut_check(distance(PART[0], PART[1]) > 0.25f,
             "answering with the hilt alone missed it by more than the whole contact radius");

    ut_section("a single mesh weapon is answered exactly as it was");
    centre[0] = PART[0][0];
    centre[1] = PART[0][1];
    centre[2] = PART[0][2];
    radius = PART_RADIUS[0];
    character_prop_sphere_merge(centre, &radius, PART[0], PART_RADIUS[0]);
    ut_near(radius, PART_RADIUS[0], 1e-6, "merging a sphere with itself changes nothing");
    ut_near(centre[1], PART[0][1], 1e-6, "and it does not move");

    ut_section("containment is decided, not averaged");
    centre[0] = 0.0f;
    centre[1] = 0.0f;
    centre[2] = 0.0f;
    radius = 1.0f;
    character_prop_sphere_merge(centre, &radius, PART[0], 0.01f);
    ut_near(radius, 1.0f, 1e-6, "a sphere already inside the answer is dropped");
    character_prop_sphere_merge(centre, &radius, centre, 4.0f);
    ut_near(radius, 4.0f, 1e-6, "and a sphere that swallows the answer replaces it");
}

int main(void)
{
    check_the_composition_is_local_then_parent();
    check_the_inverse_is_an_inverse();
    check_the_weapon_keeps_its_hold();
    check_the_pivot_makes_it_a_joint_to_joint_matrix();
    check_the_anakin_case();
    check_the_engine_composition_is_reproduced();
    check_the_scale_of_the_borrowed_body_comes_along();
    check_the_hidden_slots_are_bounded_by_the_worn_rig();
    check_the_drawn_weapon_is_one_sphere();
    check_a_degenerate_chain_is_refused();

    return ut_summary("character prop");
}
