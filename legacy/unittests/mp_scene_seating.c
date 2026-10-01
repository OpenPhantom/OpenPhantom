/* The seating of a scene for everybody: every player handed a seat of its own around the place,
 * and a player no seat around the place answered for handed one beside a seat handed out first.
 *
 * mp_scene_flow.c walks the machines of a scene; this file drives the seating against stand-ins
 * for the seat search. Two stand-ins: one whose candidates are blocked by direction, whatever the
 * anchor, and the ramp of a scene from a field run, whose answer depends on where the anchor
 * stands.
 *
 * In that field run slot 2 set the scene off on a walkway one to two units wide, under a crawl
 * space. Of the sixteen candidates around it one was free, the host took it, and slot 1 was held
 * 38 units away under the bars and heard nothing. The seating searches a player like that beside
 * the seats it has handed out.
 */
#include "unittest.h"

#include "mp_scene_flow.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ==============================================================================================
 * The stand-ins for the one seat search.
 * ============================================================================================ */

static const float RINGS[2] = { MP_SEAT_RING_NEAR, MP_SEAT_RING_FAR };

/* The seat search as mp_seat_probe does it beside a body: two rings of eight, 2 and 4 units, from
 * the direction of the seated player's slot, a candidate refused when it is blocked or closer than
 * a unit to a body. The blocked candidates are the test's to choose; so is one anchor the search
 * refuses, as it refuses one on a mover. */
typedef struct fake_world {
    bool     anchor_refused;
    bool     blocked[2][MP_SEAT_RING_STEPS];
    bool     refuse_one;
    float    refused[3];
    unsigned searches;
} fake_world_t;

static bool same_point(const float a[3], const float b[3])
{
    return fabsf(a[0] - b[0]) < 0.01f && fabsf(a[1] - b[1]) < 0.01f;
}

static mp_scene_probe_t fake_probe(void *context, const float anchor[3], uint8_t slot,
                                   const mp_seat_body_t *bodies, size_t body_count,
                                   float seat[3])
{
    fake_world_t *world = (fake_world_t *)context;
    size_t        start = mp_seat_rule_ring_start(slot);
    size_t        ring;
    size_t        step;

    ++world->searches;
    if (world->anchor_refused || (world->refuse_one && same_point(anchor, world->refused))) {
        return MP_SCENE_PROBE_ANCHOR;
    }
    for (ring = 0u; ring < 2u; ++ring) {
        for (step = 0u; step < MP_SEAT_RING_STEPS; ++step) {
            size_t direction = (start + step) % MP_SEAT_RING_STEPS;
            float  offset[2];
            float  candidate[3];

            if (world->blocked[ring][direction]) {
                continue;
            }
            mp_seat_rule_ring_offset(start, step, RINGS[ring], offset);
            candidate[0] = anchor[0] + offset[0];
            candidate[1] = anchor[1] + offset[1];
            candidate[2] = anchor[2];
            if (mp_seat_rule_near_a_body(candidate, bodies, body_count,
                                         MP_SEAT_BODY_CLEARANCE)) {
                continue;
            }
            memcpy(seat, candidate, sizeof candidate);
            return MP_SCENE_PROBE_FOUND;
        }
    }
    return MP_SCENE_PROBE_NONE;
}

/* The ramp of that scene, as the level's surfaces around 136.55 111.29 37.00 have it: a walkway
 * along y, within a unit of x 136.55, its floor at 37.00, a crawl space over it from y 107.0 to
 * 113.3, and nothing to stand on beside it. `end` is where the walkway stops in positive y. */
typedef struct ramp {
    float    x;
    float    floor_z;
    float    crawl_from;
    float    crawl_to;
    float    end;
    unsigned searches;
} ramp_t;

static bool ramp_is_free(const ramp_t *ramp, const float candidate[3])
{
    bool on_the_walkway = fabsf(candidate[0] - ramp->x) < 1.0f && candidate[1] <= ramp->end;
    bool under_a_crawl  = candidate[1] >= ramp->crawl_from && candidate[1] <= ramp->crawl_to;

    return on_the_walkway && !under_a_crawl;
}

static mp_scene_probe_t ramp_probe(void *context, const float anchor[3], uint8_t slot,
                                   const mp_seat_body_t *bodies, size_t body_count,
                                   float seat[3])
{
    ramp_t *ramp  = (ramp_t *)context;
    size_t  start = mp_seat_rule_ring_start(slot);
    size_t  ring;
    size_t  step;

    ++ramp->searches;
    for (ring = 0u; ring < 2u; ++ring) {
        for (step = 0u; step < MP_SEAT_RING_STEPS; ++step) {
            float offset[2];
            float candidate[3];

            mp_seat_rule_ring_offset(start, step, RINGS[ring], offset);
            candidate[0] = anchor[0] + offset[0];
            candidate[1] = anchor[1] + offset[1];
            candidate[2] = ramp->floor_z;
            if (!ramp_is_free(ramp, candidate) ||
                mp_seat_rule_near_a_body(candidate, bodies, body_count,
                                         MP_SEAT_BODY_CLEARANCE)) {
                continue;
            }
            memcpy(seat, candidate, sizeof candidate);
            return MP_SCENE_PROBE_FOUND;
        }
    }
    return MP_SCENE_PROBE_NONE;
}

static float distance(const float a[3], const float b[3])
{
    float dx = a[0] - b[0];
    float dy = a[1] - b[1];
    float dz = a[2] - b[2];

    return sqrtf(dx * dx + dy * dy + dz * dz);
}

/* No two seated players within a unit of each other. */
static bool seats_apart(const mp_scene_sitter_t *sitters, size_t count)
{
    size_t i;
    size_t j;

    for (i = 0u; i < count; ++i) {
        for (j = i + 1u; j < count; ++j) {
            if (sitters[i].seated && sitters[j].seated &&
                distance(sitters[i].seat, sitters[j].seat) < MP_SEAT_BODY_CLEARANCE) {
                return false;
            }
        }
    }
    return true;
}

/* ==============================================================================================
 * Around the place.
 * ============================================================================================ */

static const float ANCHOR[3] = { 100.0f, 50.0f, 10.0f };

/* The real capacity: the host and the three far players a session holds. The trigger, slot 2,
 * stands on the anchor and is not seated; the host and the other two are. */
static void four_players(mp_seat_body_t bodies[4], mp_scene_sitter_t sitters[MP_SCENE_SITTERS])
{
    size_t i;

    memset(bodies, 0, 4u * sizeof bodies[0]);
    memset(sitters, 0, MP_SCENE_SITTERS * sizeof sitters[0]);
    for (i = 0u; i < 4u; ++i) {
        bodies[i].known       = true;
        bodies[i].stands      = true;
        bodies[i].position[0] = 20.0f * (float)i;
        bodies[i].position[1] = 0.0f;
        bodies[i].position[2] = 10.0f;
        sitters[i].slot       = (uint8_t)i;
        sitters[i].wanted     = i != 2u;
    }
    memcpy(bodies[2].position, ANCHOR, sizeof ANCHOR);
}

static void check_the_seating(void)
{
    mp_seat_body_t           bodies[4];
    mp_scene_sitter_t        sitters[MP_SCENE_SITTERS];
    fake_world_t             world;
    mp_scene_seating_tally_t tally;
    size_t                   i;
    bool                     off_anchor = true;

    four_players(bodies, sitters);
    memset(&world, 0, sizeof world);

    ut_section("four players, every seat its own, the anchor never one");
    ut_check(mp_scene_seat_everyone(ANCHOR, bodies, 4u, sitters, MP_SCENE_SITTERS, &fake_probe,
                                    &world) == MP_SCENE_SEATING_DONE,
             "the seating ran");
    ut_check(sitters[0].seated && sitters[1].seated && sitters[3].seated && !sitters[2].seated,
             "the host and the two others seated, the one the script meant left where it is");
    for (i = 0u; i < MP_SCENE_SITTERS; ++i) {
        if (sitters[i].seated) {
            off_anchor = off_anchor && distance(sitters[i].seat, ANCHOR) >= 1.5f;
        }
    }
    ut_check(seats_apart(sitters, MP_SCENE_SITTERS),
             "no two players were handed seats within a unit of each other");
    ut_check(off_anchor, "and nobody on the anchor");
    ut_checkf(world.searches == 3u, "one search a player: %u", world.searches);
    ut_check(!sitters[0].chained && !sitters[1].chained && !sitters[3].chained,
             "every one of them around the place, none beside a seat");
    tally = mp_scene_seating_tally(sitters, MP_SCENE_SITTERS, MP_SCENE_SEATING_DONE);
    ut_checkf(tally.wanted == 3u && tally.unseated == 0u && tally.around_none == 0u,
              "the report counts three searches, none that found nothing (%u, %u)",
              (unsigned)tally.wanted, (unsigned)tally.unseated);

    /* No seat is shared, and the third is seated beside a seat handed out first, on nobody's.
     * Leaving it with no seat at all was the defect of the ramp scene, and a check that said
     * "the third none at all" would hold that defect as a rule. */
    ut_section("the second ring only when the first is full; a gap is filled beside a seat, never "
               "shared");
    four_players(bodies, sitters);
    memset(&world, 0, sizeof world);
    for (i = 0u; i < MP_SEAT_RING_STEPS; ++i) {
        world.blocked[0][i] = i != 0u;   /* one free near candidate, at direction 0 */
        world.blocked[1][i] = i >= 1u;   /* one free far one, at direction 0 as well */
    }
    (void)mp_scene_seat_everyone(ANCHOR, bodies, 4u, sitters, MP_SCENE_SITTERS, &fake_probe,
                                 &world);
    ut_check(sitters[0].seated &&
                 fabsf(distance(sitters[0].seat, ANCHOR) - MP_SEAT_RING_NEAR) < 0.001f,
             "the host takes the one near seat");
    ut_check(sitters[1].seated &&
                 fabsf(distance(sitters[1].seat, ANCHOR) - MP_SEAT_RING_FAR) < 0.001f,
             "the next one the far ring, the near seat being taken by the first");
    ut_checkf(sitters[3].seated && sitters[3].chained && sitters[3].beside_slot == 0u &&
                  fabsf(distance(sitters[3].seat, ANCHOR) - 6.0f) < 0.001f,
              "and the third beside the host's seat, six units out (%.2f)",
              (double)distance(sitters[3].seat, ANCHOR));
    ut_check(seats_apart(sitters, MP_SCENE_SITTERS), "on nobody's seat");
    tally = mp_scene_seating_tally(sitters, MP_SCENE_SITTERS, MP_SCENE_SEATING_DONE);
    ut_checkf(tally.around_none == 1u && tally.beside_a_seat == 1u && tally.unseated == 0u,
              "one found none around the place and one beside a seat (%u, %u)",
              (unsigned)tally.around_none, (unsigned)tally.beside_a_seat);

    ut_section("an anchor on a mover, over water or a drop gathers nobody");
    memset(sitters, 0, sizeof sitters);
    memset(&world, 0, sizeof world);
    world.anchor_refused = true;
    sitters[0].wanted = true;
    sitters[1].wanted = true;
    sitters[1].slot   = 1u;
    ut_check(mp_scene_seat_everyone(ANCHOR, bodies, 4u, sitters, 2u, &fake_probe, &world) ==
                     MP_SCENE_SEATING_ANCHOR_REFUSED &&
                 !sitters[0].seated && !sitters[1].seated && world.searches == 1u,
             "the first search says so and nobody is searched after it");
    tally = mp_scene_seating_tally(sitters, 2u, MP_SCENE_SEATING_ANCHOR_REFUSED);
    ut_checkf(tally.wanted == 2u && tally.unseated == 2u && tally.around_none == 0u,
              "two players to seat and two without a seat, none of them for the chain (%u, %u)",
              (unsigned)tally.wanted, (unsigned)tally.unseated);
}

/* ==============================================================================================
 * Beside a seat handed out first.
 * ============================================================================================ */

static const float RAMP_ANCHOR[3] = { 136.55f, 111.29f, 37.00f };

/* The ramp scene: the host, slot 0, far away; slot 1 far away; slot 2 set it off and stands on the
 * place. */
static void scene_2(mp_seat_body_t bodies[3], mp_scene_sitter_t sitters[MP_SCENE_SITTERS])
{
    static const float HOST_FAR[3]  = { 136.0f, 151.0f, 37.0f };
    static const float SLOT1_FAR[3] = { 150.0f, 80.0f, 37.0f };

    memset(bodies, 0, 3u * sizeof bodies[0]);
    memset(sitters, 0, MP_SCENE_SITTERS * sizeof sitters[0]);
    memcpy(bodies[0].position, HOST_FAR, sizeof HOST_FAR);
    memcpy(bodies[1].position, SLOT1_FAR, sizeof SLOT1_FAR);
    memcpy(bodies[2].position, RAMP_ANCHOR, sizeof RAMP_ANCHOR);
    bodies[0].known = bodies[1].known = bodies[2].known = true;
    sitters[0].wanted = true;
    sitters[0].slot   = 0u;
    sitters[1].wanted = true;
    sitters[1].slot   = 1u;
    sitters[2].slot   = 2u;
}

static ramp_t the_ramp(float end)
{
    ramp_t ramp;

    memset(&ramp, 0, sizeof ramp);
    ramp.x          = RAMP_ANCHOR[0];
    ramp.floor_z    = RAMP_ANCHOR[2];
    ramp.crawl_from = 107.0f;
    ramp.crawl_to   = 113.3f;
    ramp.end        = end;
    return ramp;
}

static void check_the_ramp_of_scene_2(void)
{
    static const float HOST_SEAT[3]  = { 136.55f, 115.29f, 37.00f };
    static const float       SLOT1_SEAT[3] = { 136.55f, 117.29f, 37.00f };
    mp_seat_body_t           bodies[3];
    mp_scene_sitter_t        sitters[MP_SCENE_SITTERS];
    mp_scene_seating_tally_t tally;
    ramp_t                   ramp = the_ramp(120.0f);

    ut_section("the ramp scene: slot 1 is seated beside the host's seat, further up the walkway");
    scene_2(bodies, sitters);
    ut_check(mp_scene_seat_everyone(RAMP_ANCHOR, bodies, 3u, sitters, MP_SCENE_SITTERS,
                                    &ramp_probe, &ramp) == MP_SCENE_SEATING_DONE,
             "the seating ran");
    ut_checkf(sitters[0].seated && !sitters[0].chained && distance(sitters[0].seat, HOST_SEAT) <
                                                              0.01f,
              "the host on the one free point around the place, 136.55 115.29 as in the field "
              "run (%.2f %.2f)", (double)sitters[0].seat[0], (double)sitters[0].seat[1]);
    ut_checkf(sitters[1].seated && sitters[1].chained && sitters[1].beside_slot == 0u,
              "slot 1 beside the seat of slot 0 (seated %d)", (int)sitters[1].seated);
    ut_checkf(distance(sitters[1].seat, SLOT1_SEAT) < 0.01f,
              "at 136.55 117.29 37.00 (%.2f %.2f %.2f)", (double)sitters[1].seat[0],
              (double)sitters[1].seat[1], (double)sitters[1].seat[2]);
    ut_checkf(fabsf(distance(sitters[1].seat, RAMP_ANCHOR) - 6.0f) < 0.01f,
              "6.00 units from the place (%.2f)", (double)distance(sitters[1].seat, RAMP_ANCHOR));
    ut_checkf(ramp.searches == 3u && sitters[1].tried_beside == 1u,
              "two searches around the place and one beside a seat (%u)", ramp.searches);
    tally = mp_scene_seating_tally(sitters, MP_SCENE_SITTERS, MP_SCENE_SEATING_DONE);
    ut_checkf(tally.wanted == 2u && tally.unseated == 0u && tally.beside_a_seat == 1u,
              "the report: 2 search(es), 0 of them found nothing, 1 beside a seat (%u, %u, %u)",
              (unsigned)tally.wanted, (unsigned)tally.unseated, (unsigned)tally.beside_a_seat);

    ut_section("the walkway ends past the host's seat: nothing answers, and the gap is counted");
    ramp = the_ramp(116.0f);
    scene_2(bodies, sitters);
    (void)mp_scene_seat_everyone(RAMP_ANCHOR, bodies, 3u, sitters, MP_SCENE_SITTERS, &ramp_probe,
                                 &ramp);
    ut_check(sitters[0].seated, "the host is seated as before");
    ut_checkf(!sitters[1].seated && !sitters[1].chained && sitters[1].tried_beside == 1u,
              "slot 1 has no seat, around the place nor beside the %u seat handed out",
              (unsigned)sitters[1].tried_beside);
    tally = mp_scene_seating_tally(sitters, MP_SCENE_SITTERS, MP_SCENE_SEATING_DONE);
    ut_checkf(tally.unseated == 1u && tally.around_none == 1u && tally.beside_a_seat == 0u,
              "and the report counts 1 search that found nothing, none at all beside a seat "
              "(%u, %u)", (unsigned)tally.unseated, (unsigned)tally.beside_a_seat);
}

/* The second pass searches beside every seat handed out so far, those it hands out itself
 * included, and a seat the search refuses as an anchor is only no place to search from. */
static void check_the_chain(void)
{
    mp_seat_body_t    bodies[4];
    mp_scene_sitter_t sitters[MP_SCENE_SITTERS];
    fake_world_t      world;
    size_t            i;

    ut_section("a seat found beside a seat is itself one the next player is seated beside");
    four_players(bodies, sitters);
    memset(&world, 0, sizeof world);
    for (i = 0u; i < MP_SEAT_RING_STEPS; ++i) {
        world.blocked[0][i] = i != 0u;
        world.blocked[1][i] = true;
    }
    ut_check(mp_scene_seat_everyone(ANCHOR, bodies, 4u, sitters, MP_SCENE_SITTERS, &fake_probe,
                                    &world) == MP_SCENE_SEATING_DONE,
             "the seating ran");
    ut_check(sitters[0].seated && !sitters[0].chained, "the host around the place");
    ut_check(sitters[1].seated && sitters[1].chained && sitters[1].beside_slot == 0u,
             "slot 1 beside the host's seat");
    ut_checkf(sitters[3].seated && sitters[3].chained && sitters[3].beside_slot == 1u &&
                  sitters[3].tried_beside == 2u,
              "slot 3 beside slot 1's, after the host's had nothing left (%u tried)",
              (unsigned)sitters[3].tried_beside);
    ut_check(seats_apart(sitters, MP_SCENE_SITTERS), "and no seat shared");

    ut_section("a seat the search refuses as an anchor is skipped, and nothing else is undone");
    four_players(bodies, sitters);
    memset(&world, 0, sizeof world);
    for (i = 0u; i < MP_SEAT_RING_STEPS; ++i) {
        world.blocked[0][i] = i != 0u;
        world.blocked[1][i] = i >= 1u;
    }
    world.refuse_one = true;
    world.refused[0] = ANCHOR[0] + MP_SEAT_RING_NEAR;   /* the host's seat, on a mover */
    world.refused[1] = ANCHOR[1];
    ut_check(mp_scene_seat_everyone(ANCHOR, bodies, 4u, sitters, MP_SCENE_SITTERS, &fake_probe,
                                    &world) == MP_SCENE_SEATING_DONE,
             "the seating is done, not refused for everybody");
    ut_check(sitters[0].seated && sitters[1].seated,
             "the host and slot 1 keep the seats the first pass handed them");
    ut_checkf(sitters[3].seated && sitters[3].chained && sitters[3].beside_slot == 1u &&
                  sitters[3].tried_beside == 2u,
              "slot 3 beside slot 1's seat, the host's skipped (%u tried)",
              (unsigned)sitters[3].tried_beside);
    ut_check(seats_apart(sitters, MP_SCENE_SITTERS), "on nobody's seat");
}

int main(void)
{
    check_the_seating();
    check_the_ramp_of_scene_2();
    check_the_chain();
    return ut_summary("the scene's seating");
}
