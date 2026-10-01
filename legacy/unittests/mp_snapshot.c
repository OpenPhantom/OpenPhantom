/* mp_snapshot.c: the delta encoding over its edges.
 *
 * A full snapshot round-trips. A delta against a baseline carries only the bodies that changed, and
 * the unchanged ones come back from the baseline, so a snapshot where one body moved is far smaller
 * than the full one and still decodes to the same state. The refusals are the interesting half: a
 * delta applied against the wrong baseline, or one whose slot claims to be unchanged when the
 * baseline has no body there, is refused whole rather than decoded into an invented body, because
 * that is the case a lossy link produces and a quiet accept would desync the two sides.
 */
#include "unittest.h"

#include "mp_snapshot.h"
#include "mp_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static mp_wire_body_t a_body(float x, uint8_t weapon)
{
    mp_wire_body_t body;

    memset(&body, 0, sizeof body);
    body.position[0] = x;
    body.position[1] = 2.0f;
    body.position[2] = 3.0f;
    body.orientation[1] = 90.0f;
    body.alive  = true;
    body.weapon = weapon;
    body.hero   = 1u;
    body.anim.clip[0] = 5u;
    body.anim.channel_mask = 0xFu;
    return body;
}

static bool bodies_match(const mp_wire_body_t *a, const mp_wire_body_t *b)
{
    /* Compared through the wire's own tolerance, since encoding quantises position and angle. */
    int i;

    for (i = 0; i < 3; ++i) {
        float dp = a->position[i] - b->position[i];
        float da = a->orientation[i] - b->orientation[i];

        if (dp < 0) {
            dp = -dp;
        }
        if (da < 0) {
            da = -da;
        }
        if (dp > MP_WIRE_POSITION_ERROR || da > MP_WIRE_ANGLE_ERROR) {
            return false;
        }
    }
    return a->weapon == b->weapon && a->hero == b->hero && a->alive == b->alive &&
           a->health == b->health && a->anim.clip[0] == b->anim.clip[0];
}

static void check_full_round_trip(void)
{
    mp_snapshot_t current;
    mp_snapshot_t decoded;
    uint8_t       packet[512];
    size_t        bytes = 0;
    mp_wire_body_t p0 = a_body(10.0f, 1u);
    mp_wire_body_t p1 = a_body(20.0f, 3u);

    ut_section("a full snapshot round-trips");

    mp_snapshot_clear(&current);
    mp_snapshot_set_body(&current, 0, &p0);
    mp_snapshot_set_body(&current, 2, &p1);
    current.tick = 100u;

    ut_check(mp_snapshot_encode(&current, NULL, packet, sizeof packet, &bytes),
             "a full snapshot encodes");
    ut_check(mp_snapshot_decode(packet, bytes, NULL, &decoded), "and decodes with no baseline");
    ut_check(decoded.tick == 100u, "the tick survives");
    ut_check(mp_snapshot_has_body(&decoded, 0) && mp_snapshot_has_body(&decoded, 2),
             "both present slots come back");
    ut_check(!mp_snapshot_has_body(&decoded, 1), "and the empty slot stays empty");
    ut_check(bodies_match(&decoded.body[0], &p0) && bodies_match(&decoded.body[2], &p1),
             "the bodies survive to the wire's tolerance");
}

static void check_delta(void)
{
    mp_snapshot_t baseline;
    mp_snapshot_t current;
    mp_snapshot_t decoded;
    uint8_t       full[512];
    uint8_t       delta[512];
    size_t        full_bytes = 0;
    size_t        delta_bytes = 0;
    mp_wire_body_t p0 = a_body(10.0f, 1u);
    mp_wire_body_t p1 = a_body(20.0f, 3u);
    mp_wire_body_t p0_moved = a_body(11.0f, 1u);

    ut_section("a delta carries only what changed");

    mp_snapshot_clear(&baseline);
    mp_snapshot_set_body(&baseline, 0, &p0);
    mp_snapshot_set_body(&baseline, 1, &p1);
    baseline.tick = 200u;

    mp_snapshot_clear(&current);
    mp_snapshot_set_body(&current, 0, &p0_moved);   /* body 0 moved */
    mp_snapshot_set_body(&current, 1, &p1);          /* body 1 unchanged */
    current.tick = 201u;

    ut_check(mp_snapshot_encode(&current, NULL, full, sizeof full, &full_bytes),
             "the full snapshot encodes");
    ut_check(mp_snapshot_encode(&current, &baseline, delta, sizeof delta, &delta_bytes),
             "the delta encodes");
    ut_check(delta_bytes < full_bytes, "the delta is smaller than the full snapshot");

    ut_check(mp_snapshot_decode(delta, delta_bytes, &baseline, &decoded),
             "the delta decodes against its baseline");
    ut_check(decoded.tick == 201u, "the current tick, not the baseline's");
    ut_check(bodies_match(&decoded.body[0], &p0_moved), "the moved body is the new one");
    ut_check(bodies_match(&decoded.body[1], &p1), "the unchanged body came from the baseline");
}

static void check_removal(void)
{
    mp_snapshot_t baseline;
    mp_snapshot_t current;
    mp_snapshot_t decoded;
    uint8_t       delta[512];
    size_t        bytes = 0;
    mp_wire_body_t p0 = a_body(10.0f, 1u);
    mp_wire_body_t p1 = a_body(20.0f, 3u);

    ut_section("a body that leaves is gone in the delta");

    mp_snapshot_clear(&baseline);
    mp_snapshot_set_body(&baseline, 0, &p0);
    mp_snapshot_set_body(&baseline, 1, &p1);
    baseline.tick = 300u;

    mp_snapshot_clear(&current);
    mp_snapshot_set_body(&current, 0, &p0);   /* body 1 is gone */
    current.tick = 301u;

    ut_check(mp_snapshot_encode(&current, &baseline, delta, sizeof delta, &bytes), "it encodes");
    ut_check(mp_snapshot_decode(delta, bytes, &baseline, &decoded), "it decodes");
    ut_check(mp_snapshot_has_body(&decoded, 0), "the staying body is here");
    ut_check(!mp_snapshot_has_body(&decoded, 1), "and the one that left is gone");
}

static void check_refusals(void)
{
    mp_snapshot_t baseline;
    mp_snapshot_t wrong_baseline;
    mp_snapshot_t current;
    mp_snapshot_t decoded;
    uint8_t       delta[512];
    size_t        bytes = 0;
    uint32_t      base_tick = 0;
    mp_wire_body_t p0 = a_body(10.0f, 1u);

    ut_section("a delta against the wrong baseline is refused");

    mp_snapshot_clear(&baseline);
    mp_snapshot_set_body(&baseline, 0, &p0);
    baseline.tick = 400u;

    mp_snapshot_clear(&current);
    mp_snapshot_set_body(&current, 0, &p0);
    current.tick = 401u;
    mp_snapshot_encode(&current, &baseline, delta, sizeof delta, &bytes);

    ut_check(mp_snapshot_baseline_tick(delta, bytes, &base_tick) && base_tick == 400u,
             "the packet names the baseline it needs");

    mp_snapshot_clear(&wrong_baseline);
    wrong_baseline.tick = 399u;
    ut_check(!mp_snapshot_decode(delta, bytes, &wrong_baseline, &decoded),
             "a baseline of the wrong tick is refused");
    ut_check(!mp_snapshot_has_body(&decoded, 0), "and nothing is half applied");

    ut_check(!mp_snapshot_decode(delta, bytes, NULL, &decoded),
             "so is no baseline at all when one was expected");

    ut_check(!mp_snapshot_decode(delta, 3u, &baseline, &decoded),
             "a truncated packet is refused rather than read past");
}

/* A body that only turned a node is a changed body. The host player standing still and aiming,
 * looking about or flinching changes nothing but its twists, and a codec that called that "same"
 * would freeze the far chest until the body moved. */
static void check_twist_delta(void)
{
    mp_snapshot_t baseline;
    mp_snapshot_t current;
    mp_snapshot_t decoded;
    uint8_t       delta[512];
    size_t        same_bytes = 0;
    size_t        twist_bytes = 0;
    mp_wire_body_t p0 = a_body(10.0f, 1u);
    mp_wire_body_t p0_twisted = p0;

    ut_section("a twist alone makes a body different");

    p0_twisted.twist_count    = 1;
    p0_twisted.twist[0].node  = 2;
    p0_twisted.twist[0].pitch = 0.0f;
    p0_twisted.twist[0].yaw   = 25.0f;

    mp_snapshot_clear(&baseline);
    mp_snapshot_set_body(&baseline, 0, &p0);
    baseline.tick = 400u;

    mp_snapshot_clear(&current);
    mp_snapshot_set_body(&current, 0, &p0);
    current.tick = 401u;
    ut_check(mp_snapshot_encode(&current, &baseline, delta, sizeof delta, &same_bytes),
             "an unchanged body encodes");

    mp_snapshot_set_body(&current, 0, &p0_twisted);
    ut_check(mp_snapshot_encode(&current, &baseline, delta, sizeof delta, &twist_bytes),
             "a body that only turned a node encodes");
    ut_check(twist_bytes > same_bytes, "and is sent in full, not as the baseline's copy");
    ut_check(mp_snapshot_decode(delta, twist_bytes, &baseline, &decoded), "and decodes");
    ut_check(decoded.body[0].twist_count == 1u && decoded.body[0].twist[0].node == 2,
             "with its twist");
    ut_near(decoded.body[0].twist[0].yaw, 25.0f, MP_WIRE_ANGLE_ERROR, "and the angle");
}

/* A body that only lost a hit point is a changed body. A far player standing still and being
 * shot changes nothing but its health, and a codec that called that "same" would hold the far
 * side's display at the old value until the body moved. */
static void check_health_delta(void)
{
    mp_snapshot_t baseline;
    mp_snapshot_t current;
    mp_snapshot_t decoded;
    uint8_t       delta[512];
    size_t        same_bytes = 0;
    size_t        hurt_bytes = 0;
    mp_wire_body_t p0 = a_body(10.0f, 1u);
    mp_wire_body_t p0_hurt = p0;

    ut_section("a change of health alone makes a body different");

    p0.health      = 100u;
    p0_hurt.health = 97u;

    mp_snapshot_clear(&baseline);
    mp_snapshot_set_body(&baseline, 0, &p0);
    baseline.tick = 500u;

    mp_snapshot_clear(&current);
    mp_snapshot_set_body(&current, 0, &p0);
    current.tick = 501u;
    ut_check(mp_snapshot_encode(&current, &baseline, delta, sizeof delta, &same_bytes),
             "an unchanged body encodes");

    mp_snapshot_set_body(&current, 0, &p0_hurt);
    ut_check(mp_snapshot_encode(&current, &baseline, delta, sizeof delta, &hurt_bytes),
             "a body that only lost three points encodes");
    ut_check(hurt_bytes > same_bytes, "and is sent in full, not as the baseline's copy");
    ut_check(mp_snapshot_decode(delta, hurt_bytes, &baseline, &decoded), "and decodes");
    ut_check(decoded.body[0].health == 97u, "with the new health");
}

int main(void)
{
    check_full_round_trip();
    check_delta();
    check_removal();
    check_refusals();
    check_twist_delta();
    check_health_delta();

    return ut_summary("mp_snapshot");
}
