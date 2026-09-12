/* object_track.c: remembering where each drawn object was a simulation step ago.
 *
 * The engine's own previous position cannot be trusted for a character riding a platform: the
 * carry overwrites it with the current one inside the same step, so the pair goes flat and there
 * is nothing to blend. This table keeps that previous position instead, and everything below is a
 * property of a SEQUENCE rather than of any one call: the same answer has to come back for every
 * frame inside a step, and it has to move on exactly once when the step does.
 *
 * Two of these checks are regressions for faults that reached the game and were played.
 *
 * The first is the stationary case. An earlier version inferred the step boundary from the
 * position changing, which needed no clock and worked for anything moving. A character standing
 * still never changes position, so its previous was never brought forward, and the blend swung it
 * between where it last walked and where it now stood, once per step, for as long as it stood
 * there. It cured the judder on a platform and planted the same judder on solid ground.
 *
 * The second is the travel limit, which is checked in the blend section. It was set to 64 by
 * copying the mover's own limit, and a lift travels nothing like a character does.
 */
#include "unittest.h"

#include "object_track.h"

#include <math.h>
#include <string.h>

#define KEY_A 0x10000000u
#define KEY_B 0x20000000u

static void set(float *v, float x, float y, float z)
{
    v[0] = x;
    v[1] = y;
    v[2] = z;
}

/* Shared by the sections below, which run in order and hand state on to each other the
 * way one main used to. */
static float    pos[3];
static float    previous[3];
static float    drawn[3];
static bool     answered;
static bool     every_frame_agreed;
static uint32_t step;
static uint32_t gap = 0u;
static int      i;

static void test_unseen_object(void)
{
    ut_section("an object nobody has seen before");

    object_track_reset();
    set(pos, 1.0f, 2.0f, 3.0f);
    answered = object_track_sample(KEY_A, 1u, pos, previous, &gap);
    ut_check(!answered, "the first sighting has no previous step to answer with");

    answered = object_track_sample(KEY_A, 1u, pos, previous, &gap);
    ut_check(!answered, "and neither does a second frame on that same step");
}

static void test_step_that_moved(void)
{
    ut_section("a step that moved it");

    set(pos, 1.5f, 2.0f, 3.0f);
    answered = object_track_sample(KEY_A, 2u, pos, previous, &gap);
    ut_check(answered, "on the next step, where it was is known");
    ut_check(previous[0] == 1.0f && previous[1] == 2.0f && previous[2] == 3.0f,
             "and that is exactly the position it held on the step before");
}

static void test_frames_inside_a_step(void)
{
    ut_section("the frames inside one step");

    /* bapobj_drawAll asks once per object per FRAME, so the same step arrives three or four times
     * over at 100 fps, and the answer must not move on with it. */
    every_frame_agreed = true;
    for (i = 0; i < 8; ++i) {
        float again[3];

        if (!object_track_sample(KEY_A, 2u, pos, again, &gap) ||
            memcmp(again, previous, sizeof again) != 0) {
            every_frame_agreed = false;
        }
    }
    ut_check(every_frame_agreed, "every later frame on the same step gets the same answer back");
}

static void test_object_that_stops(void)
{
    ut_section("an object that stops moving");

    /* The regression. It walks for a few steps, then stands still, and what matters is that its
     * previous catches up with its current rather than being left where it last walked. Drawn
     * from a stale previous, a standing character is swung back and forth once per step, which is
     * what shipped to a play session and was reported as judder on solid ground. */
    object_track_reset();
    step = 1u;
    for (i = 0; i < 4; ++i) {
        set(pos, (float)i, 0.0f, 0.0f);
        (void)object_track_sample(KEY_A, step, pos, previous, &gap);
        ++step;
    }
    set(pos, 3.0f, 0.0f, 0.0f);          /* it has stopped: the same position from now on */
    answered = object_track_sample(KEY_A, step, pos, previous, &gap);
    ut_check(answered && previous[0] == 3.0f,
             "the step it stops on still answers with where it was, which is where it stopped");

    ++step;
    answered = object_track_sample(KEY_A, step, pos, previous, &gap);
    ut_check(answered && previous[0] == 3.0f,
             "and a further step of standing still leaves previous equal to current");

    object_track_blend(previous, pos, 0.5f, 2.0f, drawn);
    ut_check(drawn[0] == 3.0f && drawn[1] == 0.0f && drawn[2] == 0.0f,
             "so a standing character is drawn where it stands, at any alpha");
}

static void test_two_objects_kept_apart(void)
{
    ut_section("two objects do not collide");

    object_track_reset();
    set(pos, 10.0f, 0.0f, 0.0f);
    (void)object_track_sample(KEY_A, 1u, pos, previous, &gap);
    set(pos, 50.0f, 0.0f, 0.0f);
    (void)object_track_sample(KEY_B, 1u, pos, previous, &gap);

    set(pos, 11.0f, 0.0f, 0.0f);
    answered = object_track_sample(KEY_A, 2u, pos, previous, &gap);
    ut_check(answered && previous[0] == 10.0f, "one object answers with its own history");

    set(pos, 51.0f, 0.0f, 0.0f);
    answered = object_track_sample(KEY_B, 2u, pos, previous, &gap);
    ut_check(answered && previous[0] == 50.0f, "and the other with its own, not the first's");
}

static void test_full_table(void)
{
    ut_section("a table with no room left");

    object_track_reset();
    for (i = 0; i < (int)OBJECT_TRACK_SLOTS; ++i) {
        set(pos, (float)i, 0.0f, 0.0f);
        (void)object_track_sample((uintptr_t)(0x1000u + (unsigned)i * 0x40u), 1u, pos,
                                  previous, &gap);
    }
    set(pos, 999.0f, 0.0f, 0.0f);
    answered = object_track_sample(0xFEED0000u, 1u, pos, previous, &gap);
    ut_check(!answered, "an object arriving at a full table gets no answer");

    /* A first sighting also answers false once, so that alone proves nothing. A slot really
     * granted would gain a history on the next step; a refusal never does. */
    set(pos, 1000.0f, 0.0f, 0.0f);
    answered = object_track_sample(0xFEED0000u, 2u, pos, previous, &gap);
    ut_check(!answered,
             "and still none on the next step, so it was refused rather than merely new");
}

static void test_stale_slot(void)
{
    ut_section("a slot nobody has claimed for a long time");

    object_track_reset();
    set(pos, 1.0f, 0.0f, 0.0f);
    (void)object_track_sample(KEY_A, 1u, pos, previous, &gap);
    for (i = 0; i <= (int)OBJECT_TRACK_STALE_FRAMES + 1; ++i) {
        object_track_frame();
    }
    for (i = 0; i < (int)OBJECT_TRACK_SLOTS; ++i) {
        set(pos, (float)i, 0.0f, 0.0f);
        (void)object_track_sample((uintptr_t)(0x2000u + (unsigned)i * 0x40u), 2u, pos,
                                  previous, &gap);
    }

    /* The old entry must be GONE, not merely outnumbered: coming back reads as a first sighting
     * rather than answering with a position from before it went stale. */
    set(pos, 2.0f, 0.0f, 0.0f);
    answered = object_track_sample(KEY_A, 3u, pos, previous, &gap);
    ut_check(!answered, "a slot left by an object nobody has drawn for a long time is taken over");
}

static void test_step_gap(void)
{
    ut_section("how many steps apart the two samples are");

    object_track_reset();
    set(pos, 0.0f, 0.0f, 0.0f);
    (void)object_track_sample(KEY_A, 10u, pos, previous, &gap);
    set(pos, 1.0f, 0.0f, 0.0f);
    gap = 0u;
    answered = object_track_sample(KEY_A, 11u, pos, previous, &gap);
    ut_check(answered && gap == 1u, "consecutive steps are one step apart");

    /* Below 32 frames a second one frame spans several steps, so the record moves forward by
     * more than one and the two samples it holds are further apart than the blend's own weight
     * assumes. This is the number that lets the weight correct for it. */
    set(pos, 3.0f, 0.0f, 0.0f);
    gap = 0u;
    answered = object_track_sample(KEY_A, 14u, pos, previous, &gap);
    ut_check(answered && gap == 3u, "a frame that spanned three steps reports all three");

    /* Every frame inside that step must be told the same thing, or the weight changes between
     * frames that are meant to divide one step evenly and the object jumps between them. */
    gap = 0u;
    answered = object_track_sample(KEY_A, 14u, pos, previous, &gap);
    ut_check(answered && gap == 3u, "and reports it again for every later frame on that step");
}

static void test_weight(void)
{
    ut_section("the weight the frame rate decides");

    ut_near(object_track_weight(0.0f, 1u), 0.0f, 0.0001,
            "at one step apart the weight is the alpha, as the engine itself used");
    ut_near(object_track_weight(0.5f, 1u), 0.5f, 0.0001, "at the middle of a step");
    ut_near(object_track_weight(1.0f, 1u), 1.0f, 0.0001, "and at the end of one");

    /* The point of the whole exercise: the drawn moment is one step behind the simulation at
     * every frame rate. With the samples two steps apart, an alpha of 0 asks for the moment
     * halfway between them. */
    ut_near(object_track_weight(0.0f, 2u), 0.5f, 0.0001,
            "two steps apart, the start of a step is halfway between the two samples");
    ut_near(object_track_weight(1.0f, 2u), 1.0f, 0.0001, "and the end of it is the later sample");
    ut_near(object_track_weight(0.0f, 4u), 0.75f, 0.0001,
            "four steps apart, three quarters of the way along");

    /* A gap this wide is a hitch, a level change or an object coming back on screen, and the two
     * samples have nothing to do with each other. Tending to one draws it where it is, which is
     * the right answer and costs no branch of its own. */
    ut_check(object_track_weight(0.0f, 1000u) > 0.998f,
             "a gap wide enough to be a hitch draws the object where it is");

    ut_check(!isfinite(object_track_weight((float)NAN, 4u)),
             "an alpha that is not a number is passed on rather than turned into one");
}

static void test_blend(void)
{
    ut_section("the blend");

    set(previous, 0.0f, 0.0f, 0.0f);
    set(pos, 4.0f, 8.0f, 12.0f);

    object_track_blend(previous, pos, 0.0f, 0.0f, drawn);
    ut_check(drawn[0] == 0.0f && drawn[1] == 0.0f && drawn[2] == 0.0f,
             "an alpha of zero draws the object where it was");

    object_track_blend(previous, pos, 1.0f, 0.0f, drawn);
    ut_check(drawn[0] == 4.0f && drawn[1] == 8.0f && drawn[2] == 12.0f,
             "an alpha of one draws it where it is, as the engine drew before");

    object_track_blend(previous, pos, 0.25f, 0.0f, drawn);
    ut_near(drawn[0], 1.0f, 0.0001, "a quarter of the way along, on X");
    ut_near(drawn[1], 2.0f, 0.0001, "and on Y");
    ut_near(drawn[2], 3.0f, 0.0001, "and on Z");
}

static void test_teleport_refused(void)
{
    ut_section("a jump too large to be a character's step");

    /* The other regression. A carried character moves 0.045 units in a step and a walking one
     * about 0.017, both measured in play. A spawn or a reused object lands tens of units away,
     * and the four worst measured in one session were 35, 44, 125 and 163. A limit of 64, copied
     * from the mover's own, let the first two through and drew them smeared across the level. */
    set(previous, 0.0f, 0.0f, 0.0f);
    set(pos, 35.78f, 0.0f, 0.0f);
    object_track_blend(previous, pos, 0.5f, 2.0f, drawn);
    ut_check(drawn[0] == 35.78f,
             "the 35 unit case that flew is refused and drawn where it landed");

    object_track_blend(previous, pos, 0.5f, 64.0f, drawn);
    ut_near(drawn[0], 17.89f, 0.001,
            "and the limit that shipped would have smeared it, so it is not 64");

    set(pos, 0.045f, 0.0f, 0.0f);
    object_track_blend(previous, pos, 0.5f, 2.0f, drawn);
    ut_near(drawn[0], 0.0225f, 0.0001,
            "a carried character's real step is well inside the limit and is still blended");

    /* The limit is on the whole vector, not on one axis: three components each inside it can be a
     * diagonal that is not. */
    set(pos, 1.5f, 1.5f, 1.5f);
    object_track_blend(previous, pos, 0.5f, 2.0f, drawn);
    ut_check(drawn[0] == 1.5f,
             "a diagonal past the limit is refused even though no single axis passes it");

    object_track_blend(previous, pos, 0.5f, 0.0f, drawn);
    ut_near(drawn[0], 0.75f, 0.0001, "a limit of zero switches the test off");
}

static void test_non_finite_values(void)
{
    ut_section("values a distance test cannot rank");

    /* The root rules ask for NaN and infinity wherever a float can carry them, and here they were
     * a field bug entire. An earlier version of this section pinned the OPPOSITE of what
     * follows: it required a previous position that was not a number to draw the object where it
     * is, on the argument that the engine drew there before this existed. The engine did not.
     * It computed the blend, got a NaN, and drew nothing, and an object whose previous position
     * was never written stayed invisible by exactly that. Drawing it where it is put a pale bar
     * the length of a lightsaber out of the player's hand, and it took eight bisect runs to reach
     * this function because every mode of the patch goes through it. So: the answer for a NaN is
     * retail's, which is a NaN, and the blend says it refused so the caller can count it. */
    set(previous, 0.0f, 0.0f, 0.0f);
    set(pos, 1.0f, 2.0f, 3.0f);

    set(previous, (float)NAN, 0.0f, 0.0f);
    ut_check(!object_track_blend(previous, pos, 0.5f, 2.0f, drawn),
             "a previous position that is not a number is refused");
    ut_check(isnan(drawn[0]),
             "and the axis that was not a number stays that way, which draws nothing, as retail");
    ut_check(drawn[1] == 1.0f && drawn[2] == 1.5f,
             "while the other two axes carry the ordinary blend, as the replaced bytes did");

    set(previous, 0.0f, (float)INFINITY, 0.0f);
    ut_check(!object_track_blend(previous, pos, 0.5f, 2.0f, drawn) && !isfinite(drawn[1]),
             "an endless one on any axis is refused and stays endless");

    set(previous, (float)NAN, (float)NAN, (float)NAN);
    ut_check(!object_track_blend(previous, pos, 0.5f, 2.0f, drawn) && isnan(drawn[0]) &&
                 isnan(drawn[1]) && isnan(drawn[2]),
             "a previous position that is nothing but NaN is nothing but NaN when drawn");

    set(previous, 0.0f, 0.0f, 0.0f);
    ut_check(!object_track_blend(previous, pos, (float)NAN, 2.0f, drawn) && isnan(drawn[0]),
             "an alpha that is not a number takes every axis with it, as retail's multiply did");

    set(previous, 0.0f, 0.0f, 0.0f);
    set(pos, (float)NAN, 2.0f, 3.0f);
    ut_check(!object_track_blend(previous, pos, 0.5f, 2.0f, drawn) && isnan(drawn[0]),
             "and a current position that is not a number is not invented either");
    set(pos, 1.0f, 2.0f, 3.0f);

    /* The limit has to clear sqrt(14), the diagonal from the origin to (1,2,3). An earlier
     * version of this check asked for 2.0 and got the current position back, because the blend
     * was right to refuse it. */
    object_track_blend(previous, pos, 0.5f, 8.0f, drawn);
    ut_near(drawn[0], 0.5f, 0.0001,
            "and an ordinary blend still works after all of that");
}

int main(void)
{
    test_unseen_object();
    test_step_that_moved();
    test_frames_inside_a_step();
    test_object_that_stops();
    test_two_objects_kept_apart();
    test_full_table();
    test_stale_slot();
    test_step_gap();
    test_weight();
    test_blend();
    test_teleport_refused();
    test_non_finite_values();

    return ut_summary("object track");
}
