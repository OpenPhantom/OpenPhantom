/* The far body sampled at the render moment, driven with no game and no clock.
 *
 * The properties are about which sample is shown and how two are blended: at a whole tick the
 * sample is exact in every channel, between two samples the position, heading and twists blend
 * while the discrete state stays the earlier sample's, a lost sample is bracketed by the two
 * around it, a twist present in one sample only blends against zero, and a render tick with
 * nothing near it is an underrun and not an invention.
 */
#include "unittest.h"

#include "mp_interp.h"

#include <math.h>
#include <string.h>

/* A body at tick `tick` walking one unit per tick along x, carrying the tick in its clip so a
 * test can tell which sample it was handed. */
static void make_body(mp_wire_body_t *body, uint32_t tick, float yaw)
{
    memset(body, 0, sizeof *body);
    body->position[0]    = (float)tick;
    body->position[1]    = 10.0f;
    body->orientation[1] = yaw;
    body->alive          = true;
    body->weapon         = (uint8_t)(tick % 6u);
    body->health         = (uint8_t)(100u - tick);
    body->anim.clip[0]   = (uint16_t)(0x40u + tick);
    body->anim.track[0]  = (uint16_t)(tick * 16u);
    body->anim.channel_mask = MP_WIRE_ANIM_BASE_LIVE;
}

static void feed(mp_interp_t *interp, uint32_t tick)
{
    mp_wire_body_t body;

    make_body(&body, tick, 0.0f);
    mp_interp_receive(interp, &body, tick);
}

/* One substep: the tick arrives, the timeline advances, the pose is resolved. */
static bool substep(mp_interp_t *interp, uint32_t tick, mp_interp_pose_t *pose)
{
    feed(interp, tick);
    (void)mp_interp_advance(interp);
    return mp_interp_resolve(interp, pose);
}

static void check_angle_blend(void)
{
    ut_section("the shortest-arc angle blend");
    ut_near(mp_interp_blend_angle(10.0f, 20.0f, 0.5f), 15.0, 1e-4, "an ordinary blend is the mean");
    ut_near(mp_interp_blend_angle(350.0f, 10.0f, 0.5f), 360.0, 1e-4,
            "across 0 it goes the short way to 360, not back through 180");
    ut_near(mp_interp_blend_angle(10.0f, 350.0f, 0.5f), 0.0, 1e-4, "and the same the other way");
}

static void check_exact_sample(void)
{
    static mp_interp_t interp;
    mp_interp_pose_t   pose;
    uint32_t           tick;
    uint32_t           exact = 0u;

    ut_section("at a whole tick the sample is exact in every channel");
    mp_interp_init(&interp, MP_INTERP_DEFAULT_LAG);
    ut_check(!mp_interp_resolve(&interp, &pose), "nothing resolves before the warm-up");
    ut_check(mp_interp_underruns(&interp) == 0u, "and that is not an underrun");
    ut_check(!substep(&interp, 1u, &pose) && !substep(&interp, 2u, &pose) &&
                 !substep(&interp, 3u, &pose),
             "nor during it");
    ut_check(substep(&interp, 4u, &pose), "the fourth sample ends the warm-up");
    ut_check(pose.tick == 1u && pose.state_tick == 1u, "the render tick is 1, the sample's is 1");
    ut_near(pose.position[0], 1.0, 0.0, "the position is sample 1's exactly");
    ut_check(pose.state.anim.clip[0] == 0x41u && pose.state.anim.track[0] == 16u &&
                 pose.state.weapon == 1u && pose.state.health == 99u && pose.state.alive,
             "and so is the whole state record");

    for (tick = 5u; tick <= 40u; ++tick) {
        if (substep(&interp, tick, &pose) && pose.tick == tick - 3u &&
            pose.state_tick == tick - 3u && pose.position[0] == (float)(tick - 3u) &&
            pose.state.anim.clip[0] == 0x40u + tick - 3u) {
            ++exact;
        }
    }
    ut_check(exact == 36u, "and every later substep shows the sample three ticks back, exactly");
    ut_check(mp_interp_underruns(&interp) == 0u, "with no underrun");
}

static void check_blend_at_phase(void)
{
    static mp_interp_t interp;
    mp_interp_pose_t   pose;
    uint32_t           tick;
    uint32_t           substeps;

    ut_section("between two samples, at phase four of eight");
    mp_interp_init(&interp, MP_INTERP_DEFAULT_LAG);
    for (tick = 1u; tick <= 14u; ++tick) {
        (void)substep(&interp, tick, &pose);
    }
    /* Three extra ticks in one substep push the lag to five; the eighth substep there starts a
     * skip slew, and four steps of nine eighths into it the phase is four. */
    feed(&interp, 15u);
    feed(&interp, 16u);
    feed(&interp, 17u);
    (void)mp_interp_advance(&interp);
    for (substeps = 0u, tick = 18u; substeps < 10u; ++substeps, ++tick) {
        (void)substep(&interp, tick, &pose);
    }
    ut_check(mp_timeline_render_phase8(mp_interp_timeline(&interp)) == 4u,
             "(the slew has the render at phase four)");
    ut_check(pose.tick == 22u && pose.state_tick == 22u, "the render tick is 22");
    ut_near(pose.position[0], 22.5, 1e-5, "the position is halfway to sample 23");
    ut_check(pose.state.anim.clip[0] == 0x40u + 22u && pose.state.weapon == 22u % 6u,
             "and the state is sample 22's, not 23's");
}

static void check_lost_sample(void)
{
    static mp_interp_t interp;
    mp_interp_pose_t   pose;
    uint32_t           tick;

    ut_section("a lost sample is bracketed by the two around it");
    mp_interp_init(&interp, MP_INTERP_DEFAULT_LAG);
    for (tick = 1u; tick <= 9u; ++tick) {
        (void)substep(&interp, tick, &pose);
    }
    /* Tick 10 is lost: the substep runs without it. */
    (void)mp_interp_advance(&interp);
    ut_check(mp_interp_resolve(&interp, &pose) && pose.tick == 7u, "the render is at 7");
    for (tick = 11u; tick <= 12u; ++tick) {
        (void)substep(&interp, tick, &pose);
    }
    ut_check(substep(&interp, 13u, &pose) && pose.tick == 10u, "then it reaches the lost tick");
    ut_check(pose.state_tick == 9u, "whose state is the sample before it");
    ut_near(pose.position[0], 10.0, 1e-5, "and whose position is blended halfway from 9 to 11");
    ut_check(substep(&interp, 14u, &pose) && pose.tick == 11u && pose.state_tick == 11u &&
                 pose.position[0] == 11.0f,
             "and the next substep is sample 11 exactly: no jump where the sample was lost");
    ut_check(mp_interp_underruns(&interp) == 0u, "with no underrun");
}

static void check_twists(void)
{
    static mp_interp_t interp;
    mp_interp_pose_t   pose;
    mp_wire_body_t     body;
    uint32_t           tick;
    size_t             index;
    const mp_wire_twist_t *node3 = NULL;
    const mp_wire_twist_t *node5 = NULL;
    const mp_wire_twist_t *node7 = NULL;

    ut_section("the twists blended between two samples, paired by node");
    mp_interp_init(&interp, MP_INTERP_DEFAULT_LAG);
    for (tick = 1u; tick <= 9u; ++tick) {
        make_body(&body, tick, 0.0f);
        if (tick == 9u) {
            body.twist_count   = 2u;
            body.twist[0].node = 3u;
            body.twist[0].pitch = 10.0f;
            body.twist[0].yaw  = 170.0f;
            body.twist[1].node = 5u;
            body.twist[1].yaw  = 40.0f;
        }
        mp_interp_receive(&interp, &body, tick);
        (void)mp_interp_advance(&interp);
    }
    (void)mp_interp_advance(&interp);   /* tick 10 lost */
    make_body(&body, 11u, 90.0f);
    body.twist_count    = 2u;
    body.twist[0].node  = 3u;
    body.twist[0].pitch = 20.0f;
    body.twist[0].yaw   = -170.0f;
    body.twist[1].node  = 7u;
    body.twist[1].pitch = 8.0f;
    mp_interp_receive(&interp, &body, 11u);
    (void)mp_interp_advance(&interp);
    (void)substep(&interp, 12u, &pose);
    ut_check(substep(&interp, 13u, &pose) && pose.tick == 10u && pose.state_tick == 9u,
             "the render is at the lost tick, halfway from 9 to 11");
    ut_near(pose.yaw, 45.0, 1e-4, "the heading is halfway from 0 to 90");
    ut_check(pose.twist_count == 3u, "three nodes: two from the earlier sample, one new");
    for (index = 0; index < pose.twist_count; ++index) {
        if (pose.twist[index].node == 3u) {
            node3 = &pose.twist[index];
        } else if (pose.twist[index].node == 5u) {
            node5 = &pose.twist[index];
        } else if (pose.twist[index].node == 7u) {
            node7 = &pose.twist[index];
        }
    }
    ut_check(node3 != NULL && node5 != NULL && node7 != NULL, "each node once");
    if (node3 != NULL && node5 != NULL && node7 != NULL) {
        ut_near(node3->pitch, 15.0, 1e-4, "a node in both blends its pitch");
        ut_near(fabs((double)node3->yaw), 180.0, 1e-3,
                "and its yaw along the shortest arc, 170 to -170 through 180");
        ut_near(node5->yaw, 20.0, 1e-4, "a node only in the earlier sample blends toward zero");
        ut_near(node7->pitch, 4.0, 1e-4, "a node only in the later one blends up from zero");
    }
    ut_check(substep(&interp, 14u, &pose) && pose.twist_count == 2u &&
                 pose.twist[0].node == 3u && pose.twist[0].pitch == 20.0f,
             "at sample 11 itself the twists are that sample's, exactly");

    ut_section("the heading blends across the wrap");
    mp_interp_init(&interp, MP_INTERP_DEFAULT_LAG);
    for (tick = 1u; tick <= 9u; ++tick) {
        make_body(&body, tick, 350.0f);
        mp_interp_receive(&interp, &body, tick);
        (void)mp_interp_advance(&interp);
    }
    (void)mp_interp_advance(&interp);
    make_body(&body, 11u, 10.0f);
    mp_interp_receive(&interp, &body, 11u);
    (void)mp_interp_advance(&interp);
    (void)substep(&interp, 12u, &pose);
    ut_check(substep(&interp, 13u, &pose) && pose.tick == 10u, "at the lost tick");
    ut_near(pose.yaw, 0.0, 1e-4, "350 to 10 halfway is 0, the short way and folded into 0..360");
}

static void check_underrun(void)
{
    static mp_interp_t interp;
    mp_interp_pose_t   pose;
    uint32_t           tick;

    ut_section("a render tick with no sample near it is an underrun");
    mp_interp_init(&interp, MP_INTERP_DEFAULT_LAG);
    for (tick = 1u; tick <= 4u; ++tick) {
        (void)substep(&interp, tick, &pose);
    }
    /* A jump of the newest tick past the resync lag rebases the render onto ticks the history
     * never received. */
    ut_check(!substep(&interp, 90u, &pose), "the rebased render tick, 87, finds no sample");
    ut_check(mp_interp_underruns(&interp) == 1u, "and that is counted");
    ut_check(!substep(&interp, 91u, &pose) && !substep(&interp, 92u, &pose),
             "nor do 88 and 89: a sample after the render tick never stands in for one before");
    ut_check(mp_interp_underruns(&interp) == 3u, "three underruns in all");
    ut_check(substep(&interp, 93u, &pose) && pose.tick == 90u && pose.state_tick == 90u,
             "and at 90 the render reaches the first sample the history holds");
}

/* One substep with a sample of world `world`. */
static bool substep_in(mp_interp_t *interp, uint32_t tick, uint8_t world, mp_interp_pose_t *pose)
{
    mp_wire_body_t body;

    make_body(&body, tick, 0.0f);
    body.world = world;
    mp_interp_receive(interp, &body, tick);
    (void)mp_interp_advance(interp);
    return mp_interp_resolve(interp, pose);
}

static void check_no_blend_across_worlds(void)
{
    static mp_interp_t interp;
    mp_interp_pose_t   pose;
    uint32_t           tick;

    ut_section("two worlds are never blended: the earlier sample is held until the next is due");
    mp_interp_init(&interp, MP_INTERP_DEFAULT_LAG);
    for (tick = 1u; tick <= 9u; ++tick) {
        (void)substep_in(&interp, tick, 1u, &pose);
    }
    ut_check(mp_interp_world_holds(&interp) == 0u, "one world, nothing held");
    /* Tick 10 never comes, and 11 is the first of the next world. */
    (void)mp_interp_advance(&interp);
    (void)substep_in(&interp, 11u, 2u, &pose);
    (void)substep_in(&interp, 12u, 2u, &pose);
    ut_check(substep_in(&interp, 13u, 2u, &pose) && pose.tick == 10u && pose.state_tick == 9u,
             "the render stands between the last sample of world 1 and the first of world 2");
    ut_checkf(pose.position[0] == 9.0f && pose.state.world == 1u,
              "it shows sample 9 where it is, in world 1, not a point on the line to world 2's "
              "first (x %.2f)", (double)pose.position[0]);
    ut_check(mp_interp_world_holds(&interp) == 1u, "and the hold is counted");
    ut_check(substep_in(&interp, 14u, 2u, &pose) && pose.tick == 11u &&
                 pose.position[0] == 11.0f && pose.state.world == 2u,
             "the next substep is world 2's first sample, whole");
    ut_check(substep_in(&interp, 15u, 2u, &pose) && pose.tick == 12u &&
                 mp_interp_world_holds(&interp) == 1u,
             "and inside one world the blend runs as before");
}

int main(void)
{
    check_angle_blend();
    check_exact_sample();
    check_blend_at_phase();
    check_lost_sample();
    check_twists();
    check_underrun();
    check_no_blend_across_worlds();

    return ut_summary("mp_interp");
}
