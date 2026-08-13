/* mover_blend.c: the arithmetic that decides what a moving door looks like between two simulation
 * steps.
 *
 * Every way this can be wrong is silent in review and loud on screen, which is why it is tested
 * rather than argued about:
 *
 *   a scale destroyed      the mover snaps to its unscaled size on every drawn frame and grows back
 *                          on the next simulation step, which reads as a flickering model
 *   a guard too tight      a subnode scaled by half fails a raw dot product every frame and is
 *                          never smoothed, so the feature installs and does nothing for it
 *   a guard too loose      a track wrapping from its end back to its start is drawn as a sweep
 *                          across the level rather than as a jump
 *   a basis left skewed    rows blended independently stop being perpendicular, and a basis that is
 *                          not orthogonal shears the geometry it transforms
 *   not the identity at 1  the original 30 fps behaviour stops being a fixed point, and a fix that
 *                          changes the shipped frame rate's output is a regression by definition
 *
 * The twelve floats these tests build are the engine's own subnode pose, not a convenient shape.
 * Subnodes live in an array at mover+0x84 with a stride of 0x9C and a count at mover+0x24, and each
 * one's world matrix begins at +0x14: a 3x3 filling 0x14 to 0x37, then a translation vec3 at 0x38
 * to 0x43. That split is not assumed, it is read out of the engine's own previous-pose copy at
 * 0x00409AC0, which reads [+0x38] into prevWorldT at [+0x7C], and 0x38 is 0x14 plus 36 bytes of
 * rotation.
 *
 * Two numbers out of the authored data set the two guards. The fastest continuous mover turns 29.8
 * degrees in one simulation step, so a rotation guard has to pass that comfortably. The largest
 * wrap in the first level moves 101 degrees and 298 world units about twice a second, so a
 * discontinuity of that size has to be rejected. The threshold sits between them at the cosine of
 * 45 degrees, the threshold the Quake family uses for the same decision, and it leaves half again
 * as much room as the fastest mover needs. Three other movers in the same level wrap by so little
 * that no geometric threshold can catch them at all, which is why the module that calls this also
 * detects a wrap directly, by comparing the track position at mover+0x2C before and after the
 * engine's own tick, and marks those subnodes un-interpolatable for one frame. This file tests the
 * geometry only.
 */
#include "unittest.h"

#include "mover_blend.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>

#define NO_TRANSLATION_LIMIT 0.0f

/* Rotation about the Z axis, rows scaled by `scale`, translated to (tx, ty, tz). */
static void make_world(float *m, double degrees, float scale, float tx, float ty, float tz)
{
    double radians = degrees * 3.14159265358979323846 / 180.0;
    float  c = (float)cos(radians);
    float  s = (float)sin(radians);

    m[0] =  c * scale; m[1] = s * scale; m[2] = 0.0f;
    m[3] = -s * scale; m[4] = c * scale; m[5] = 0.0f;
    m[6] =  0.0f;      m[7] = 0.0f;      m[8] = scale;
    m[MOVER_TRANSLATION + 0] = tx;
    m[MOVER_TRANSLATION + 1] = ty;
    m[MOVER_TRANSLATION + 2] = tz;
}

static float row_length(const float *m, int row)
{
    const float *r = m + row * 3;

    return (float)sqrt((double)(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]));
}

static float row_dot(const float *m, int a, int b)
{
    const float *p = m + a * 3;
    const float *q = m + b * 3;

    return p[0] * q[0] + p[1] * q[1] + p[2] * q[2];
}

static void the_endpoints_are_exact(void)
{
    float previous[MOVER_WORLD_FLOATS];
    float current[MOVER_WORLD_FLOATS];
    float out[MOVER_WORLD_FLOATS];
    int   index;

    make_world(previous, 0.0, 1.0f, 0.0f, 0.0f, 0.0f);
    make_world(current, 20.0, 1.0f, 10.0f, 0.0f, 0.0f);

    /* Alpha reaches exactly 1.0 on every frame at 32 frames per second, so this case is the
     * original behaviour and it has to come back byte for byte as the current pose. */
    ut_check(!mover_blend_world(out, previous, current, 1.0f, NO_TRANSLATION_LIMIT),
             "alpha 1.0 is not a blend");
    for (index = 0; index < MOVER_WORLD_FLOATS; ++index) {
        ut_checkf(out[index] == current[index],
                  "alpha 1.0 must leave the current pose untouched, element %d", index);
    }

    ut_check(!mover_blend_world(out, previous, current, 0.0f, NO_TRANSLATION_LIMIT),
             "alpha 0.0 is not a blend either");
}

static void translation_is_exact_because_that_is_what_a_door_does(void)
{
    float previous[MOVER_WORLD_FLOATS];
    float current[MOVER_WORLD_FLOATS];
    float out[MOVER_WORLD_FLOATS];

    /* A sliding door, a rising lift and a travelling platform are pure translation, and there the
     * blend is not an approximation of the answer, it is the answer. */
    make_world(previous, 0.0, 1.0f, 0.0f, 0.0f, 0.0f);
    make_world(current, 0.0, 1.0f, 8.0f, 4.0f, -2.0f);

    ut_check(mover_blend_world(out, previous, current, 0.25f, NO_TRANSLATION_LIMIT),
             "a pure slide is blended");
    ut_near((double)out[MOVER_TRANSLATION + 0], 2.0, 1e-5, "x at a quarter");
    ut_near((double)out[MOVER_TRANSLATION + 1], 1.0, 1e-5, "y at a quarter");
    ut_near((double)out[MOVER_TRANSLATION + 2], -0.5, 1e-5, "z at a quarter");
}

static void rotation_lands_between_the_samples(void)
{
    float previous[MOVER_WORLD_FLOATS];
    float current[MOVER_WORLD_FLOATS];
    float expected[MOVER_WORLD_FLOATS];
    float out[MOVER_WORLD_FLOATS];
    int   index;

    /* 30 degrees in one step is a shade more than the fastest continuous mover in the authored
     * data, which turns 29.8. Halfway should be 15 degrees. The blend is a normalised lerp rather
     * than a spherical one, and its departure from the true arc grows with the square of the angle,
     * so at an angle this size it is far below anything a renderer shows. The tolerance below is
     * what says so. */
    make_world(previous, 0.0, 1.0f, 0.0f, 0.0f, 0.0f);
    make_world(current, 30.0, 1.0f, 0.0f, 0.0f, 0.0f);
    make_world(expected, 15.0, 1.0f, 0.0f, 0.0f, 0.0f);

    ut_check(mover_blend_world(out, previous, current, 0.5f, NO_TRANSLATION_LIMIT),
             "a 30 degree step is ordinary motion and must be blended");
    for (index = 0; index < 9; ++index) {
        ut_checkf(fabs((double)(out[index] - expected[index])) < 0.005,
                  "element %d should sit near the halfway rotation", index);
    }
}

static void the_basis_stays_orthonormal(void)
{
    float previous[MOVER_WORLD_FLOATS];
    float current[MOVER_WORLD_FLOATS];
    float out[MOVER_WORLD_FLOATS];
    float alpha;

    /* Rows blended independently drift out of perpendicular, and a skewed basis shears the geometry
     * it transforms. This is the property the Gram-Schmidt pass exists for, so it is asserted
     * across the whole sweep rather than at one point. */
    make_world(previous, 0.0, 1.0f, 0.0f, 0.0f, 0.0f);
    make_world(current, 40.0, 1.0f, 0.0f, 0.0f, 0.0f);

    for (alpha = 0.1f; alpha < 0.95f; alpha += 0.1f) {
        ut_checkf(mover_blend_world(out, previous, current, alpha, NO_TRANSLATION_LIMIT),
                  "40 degrees is inside the band, alpha %.1f", (double)alpha);
        ut_checkf(fabs((double)(row_length(out, 0) - 1.0f)) < 1e-4,
                  "row 0 stays unit length at alpha %.1f", (double)alpha);
        ut_checkf(fabs((double)(row_length(out, 1) - 1.0f)) < 1e-4,
                  "row 1 stays unit length at alpha %.1f", (double)alpha);
        ut_checkf(fabs((double)row_dot(out, 0, 1)) < 1e-4,
                  "rows 0 and 1 stay perpendicular at alpha %.1f", (double)alpha);
        ut_checkf(fabs((double)row_dot(out, 0, 2)) < 1e-4,
                  "rows 0 and 2 stay perpendicular at alpha %.1f", (double)alpha);
    }
}

static void a_scaled_subnode_keeps_its_scale(void)
{
    float previous[MOVER_WORLD_FLOATS];
    float current[MOVER_WORLD_FLOATS];
    float out[MOVER_WORLD_FLOATS];
    float alpha;

    /* This is the regression test for the first design. The mover track header carries a bit that
     * composes a scale into the matrix, so a row can legitimately be longer or shorter than one. A
     * plain re-orthonormalisation sets every row to unit length, so a model scaled by two would
     * snap to its unscaled size on every drawn frame and grow back on the next simulation step.
     *
     * The shipped form splits each row into a unit direction and its length and blends the two
     * separately, which needs no special case: for a pure rotation the lengths are 1 at both ends
     * and the arithmetic is what it would have been anyway.
     *
     * One thing here is an assumption and not evidence: that the scale multiplies the rows. If it
     * multiplies the columns instead, the blend is still continuous and still exact at both
     * endpoints, but the intermediate is a mix rather than the exact answer. Worth knowing before
     * anyone reads a wobble here as a mistake in the lerp. */
    make_world(previous, 0.0, 2.0f, 0.0f, 0.0f, 0.0f);
    make_world(current, 20.0, 2.0f, 0.0f, 0.0f, 0.0f);

    for (alpha = 0.1f; alpha < 0.95f; alpha += 0.2f) {
        ut_checkf(mover_blend_world(out, previous, current, alpha, NO_TRANSLATION_LIMIT),
                  "a scaled subnode is still ordinary motion, alpha %.1f", (double)alpha);
        ut_checkf(fabs((double)(row_length(out, 0) - 2.0f)) < 1e-3,
                  "the scale of 2 survives at alpha %.1f, got %.4f",
                  (double)alpha, (double)row_length(out, 0));
    }
}

static void a_changing_scale_is_interpolated_too(void)
{
    float previous[MOVER_WORLD_FLOATS];
    float current[MOVER_WORLD_FLOATS];
    float out[MOVER_WORLD_FLOATS];

    make_world(previous, 0.0, 2.0f, 0.0f, 0.0f, 0.0f);
    make_world(current, 0.0, 4.0f, 0.0f, 0.0f, 0.0f);

    ut_check(mover_blend_world(out, previous, current, 0.5f, NO_TRANSLATION_LIMIT),
             "a growing subnode is blended");
    ut_near((double)row_length(out, 0), 3.0, 1e-3, "halfway between a scale of 2 and 4");
}

static void the_guard_measures_an_angle_and_not_a_length(void)
{
    float previous[MOVER_WORLD_FLOATS];
    float current[MOVER_WORLD_FLOATS];
    float out[MOVER_WORLD_FLOATS];

    /* The other half of the scale defect. A dot product between two rows that are not unit length
     * is not a cosine: a subnode scaled by 0.5 produces 0.25 for no rotation at all and would fail
     * a 0.707 threshold on every single frame, while one scaled by 3 produces 9 and could never
     * fail it however far it turned. Normalising first is what makes the threshold mean 45 degrees
     * for every subnode. */
    make_world(previous, 0.0, 0.5f, 0.0f, 0.0f, 0.0f);
    make_world(current, 5.0, 0.5f, 0.0f, 0.0f, 0.0f);
    ut_check(mover_blend_world(out, previous, current, 0.5f, NO_TRANSLATION_LIMIT),
             "a small turn on a subnode scaled down must still be blended");

    make_world(previous, 0.0, 3.0f, 0.0f, 0.0f, 0.0f);
    make_world(current, 90.0, 3.0f, 0.0f, 0.0f, 0.0f);
    ut_check(!mover_blend_world(out, previous, current, 0.5f, NO_TRANSLATION_LIMIT),
             "a 90 degree jump on a subnode scaled up must still be rejected");
}

static void a_discontinuity_is_not_smoothed(void)
{
    float previous[MOVER_WORLD_FLOATS];
    float current[MOVER_WORLD_FLOATS];
    float out[MOVER_WORLD_FLOATS];
    int   index;

    /* A track wrapping from its end back to its start is a jump, and drawing it as a sweep across
     * the level would be worse than drawing it as a jump. 101 degrees is the rotation half of the
     * largest wrap in the first level, and it has to be refused. */
    make_world(previous, 0.0, 1.0f, 0.0f, 0.0f, 0.0f);
    make_world(current, 101.0, 1.0f, 0.0f, 0.0f, 0.0f);
    ut_check(!mover_blend_world(out, previous, current, 0.5f, NO_TRANSLATION_LIMIT),
             "a 101 degree wrap is rejected");
    for (index = 0; index < MOVER_WORLD_FLOATS; ++index) {
        ut_checkf(out[index] == current[index],
                  "a rejected blend leaves the current pose, element %d", index);
    }

    /* 298 units is the translation half of that same wrap. The limit is a setting rather than a
     * constant here, because how far a mover may legitimately travel in one step is a property of
     * the authored track and not of this arithmetic; the feature ships it at 64 units per step, and
     * a non-positive value disables the test and leaves only the rotation guard. Both directions
     * are checked, because a limit generous enough to pass the wrap is as wrong as one tight enough
     * to reject ordinary travel. */
    make_world(previous, 0.0, 1.0f, 0.0f, 0.0f, 0.0f);
    make_world(current, 0.0, 1.0f, 298.0f, 0.0f, 0.0f);
    ut_check(!mover_blend_world(out, previous, current, 0.5f, 10.0f),
             "a 298 unit jump past a 10 unit limit is rejected");
    ut_check(mover_blend_world(out, previous, current, 0.5f, 400.0f),
             "the same jump under a 400 unit limit is ordinary motion");
}

static void a_degenerate_row_is_refused(void)
{
    float previous[MOVER_WORLD_FLOATS];
    float current[MOVER_WORLD_FLOATS];
    float out[MOVER_WORLD_FLOATS];

    /* A zeroed record is what an uninitialised or freed subnode looks like, and there is no
     * direction to recover from it. Dividing by that length would produce infinities and draw
     * them. */
    make_world(current, 10.0, 1.0f, 0.0f, 0.0f, 0.0f);
    memset(previous, 0, sizeof(previous));

    ut_check(!mover_blend_world(out, previous, current, 0.5f, NO_TRANSLATION_LIMIT),
             "a zeroed previous pose is refused rather than divided by");
    ut_check(out[0] == current[0] && out[4] == current[4],
             "and the current pose is what comes back");
}

int main(void)
{
    the_endpoints_are_exact();
    translation_is_exact_because_that_is_what_a_door_does();
    rotation_lands_between_the_samples();
    the_basis_stays_orthonormal();
    a_scaled_subnode_keeps_its_scale();
    a_changing_scale_is_interpolated_too();
    the_guard_measures_an_angle_and_not_a_length();
    a_discontinuity_is_not_smoothed();
    a_degenerate_row_is_refused();

    return ut_summary("mover_blend");
}
