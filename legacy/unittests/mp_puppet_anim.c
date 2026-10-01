/* The puppet's animation decisions, driven over their edges with no game.
 *
 * Every decision here is one the field would answer slowly and expensively: a clip restarted on a
 * head that merely wrapped freezes the walk cycle, an overlay started for an ordinal the weapon
 * setter owns leaves the puppet refusing every later weapon change, a guard that counts fading
 * tracks as busy leaves the puppet without overlays for a tenth of a second after every
 * crossfade, and an event whose tick is read unsigned is performed a second late after a
 * reordered arrival. The numbers are pinned rather than the prose.
 */
#include "unittest.h"

#include "mp_puppet_anim.h"

#include <math.h>
#include <string.h>

/* The clip column of the retail swing table at 0x004B4E00, in decimal. */
static const uint16_t SWING_CLIPS[MP_PUPPET_ANIM_SWING_ROWS] = {
    71, 72, 73, 74, 75, 76, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 84, 90, 91, 82, 83, 87,
    92, 85, 110, 111, 112
};

static mp_puppet_anim_exclusions_t exclusions;

static void base_in(mp_puppet_anim_base_in_t *in, uint32_t clip, float head, int32_t applied,
                    float applied_head, float puppet_head)
{
    memset(in, 0, sizeof *in);
    in->wire_clip    = clip;
    in->wire_head    = head;
    in->wire_live    = true;
    in->contiguous   = true;
    in->applied_clip = applied;
    in->applied_head = applied_head;
    in->puppet_head  = puppet_head;
    in->num_frames   = 40.0f;
}

static void check_base_decisions(void)
{
    mp_puppet_anim_base_in_t  in;
    mp_puppet_anim_decision_t out;

    ut_section("the base channel: a change of ordinal");
    base_in(&in, 0x47u, 12.5f, 0x40, 30.0f, 30.0f);
    in.faded = true;
    mp_puppet_anim_base_decide(&in, &out);
    ut_check(out.action == MP_PUPPET_ANIM_START && out.mode == MP_PUPPET_ANIM_MODE_FADE,
             "a new ordinal after a faded change starts with a crossfade");
    ut_near(out.frames, 12.5, 1e-5, "and is seeded to the wire's head");

    in.faded = false;
    mp_puppet_anim_base_decide(&in, &out);
    ut_check(out.action == MP_PUPPET_ANIM_START && out.mode == MP_PUPPET_ANIM_MODE_CUT,
             "a new ordinal after a cut starts with a cut");

    in.contiguous = false;
    mp_puppet_anim_base_decide(&in, &out);
    ut_check(out.action == MP_PUPPET_ANIM_START && out.mode == MP_PUPPET_ANIM_MODE_FADE,
             "a new ordinal after a cut in a sample that does not follow the last applied one "
             "starts with a crossfade: the fade may have been in the sample that was lost");
    ut_near(out.frames, 12.5, 1e-5, "and is still seeded to the wire's head");

    in.faded = true;
    mp_puppet_anim_base_decide(&in, &out);
    ut_check(out.action == MP_PUPPET_ANIM_START && out.mode == MP_PUPPET_ANIM_MODE_FADE,
             "a faded change after a gap fades as well");

    base_in(&in, 0x47u, 1.0f, 0x47, 30.0f, 30.0f);
    in.contiguous = false;
    mp_puppet_anim_base_decide(&in, &out);
    ut_check(out.action == MP_PUPPET_ANIM_START && out.mode == MP_PUPPET_ANIM_MODE_FADE,
             "and so does a restart of the same ordinal seen across a gap");

    base_in(&in, 0x47u, 45.0f, 0x40, 0.0f, 0.0f);
    mp_puppet_anim_base_decide(&in, &out);
    ut_near(out.frames, 39.5, 1e-5,
            "a head at or past the end is seeded half a frame short of the end, never past it");

    base_in(&in, 0x47u, 0.0f, 0x40, 0.0f, 0.0f);
    mp_puppet_anim_base_decide(&in, &out);
    ut_check(out.action == MP_PUPPET_ANIM_START && out.frames == 0.0f,
             "a head of zero is a start from the first frame with no seed");

    base_in(&in, 0x47u, 5.0f, -1, -1.0f, -1.0f);
    mp_puppet_anim_base_decide(&in, &out);
    ut_check(out.action == MP_PUPPET_ANIM_START, "the first clip after a reset starts");

    ut_section("the base channel: what is only recorded");
    base_in(&in, 71u, 3.0f, 0x40, 30.0f, 30.0f);
    in.event_owned = true;
    mp_puppet_anim_base_decide(&in, &out);
    ut_check(out.action == MP_PUPPET_ANIM_RECORD,
             "a swing clip is recorded and never started from state");

    base_in(&in, 0x47u, 3.0f, 0x40, 30.0f, 30.0f);
    in.wire_live = false;
    mp_puppet_anim_base_decide(&in, &out);
    ut_check(out.action == MP_PUPPET_ANIM_RECORD,
             "a base channel the sender has stopped is recorded and left alone");

    ut_section("the base channel: the same ordinal");
    base_in(&in, 0x47u, 1.0f, 0x47, 30.0f, 30.0f);
    mp_puppet_anim_base_decide(&in, &out);
    ut_check(out.action == MP_PUPPET_ANIM_START,
             "a head thrown back by more than two frames on a clip that does "
             "not wrap is a restart");
    ut_near(out.frames, 1.0, 1e-5, "seeded to the new head");

    base_in(&in, 0x47u, 29.0f, 0x47, 30.0f, 30.0f);
    mp_puppet_anim_base_decide(&in, &out);
    ut_check(out.action == MP_PUPPET_ANIM_NONE,
             "a head one frame back with the puppet one frame off is neither restart nor seek");

    base_in(&in, 0x47u, 32.0f, 0x47, 31.0f, 30.0f);
    mp_puppet_anim_base_decide(&in, &out);
    ut_check(out.action == MP_PUPPET_ANIM_SEEK, "a puppet head two frames off is seeked");
    ut_near(out.frames, 32.0, 1e-5, "to the wire's head");

    base_in(&in, 0x47u, 1.0f, 0x47, 39.6f, 39.6f);
    in.loops = true;
    mp_puppet_anim_base_decide(&in, &out);
    ut_check(out.action == MP_PUPPET_ANIM_NONE,
             "on a looping clip a head that came round is a wrap, a frame and a half apart, not "
             "a clip apart");

    base_in(&in, 0x47u, 2.0f, 0x47, 20.0f, 20.0f);
    in.loops = true;
    mp_puppet_anim_base_decide(&in, &out);
    ut_check(out.action == MP_PUPPET_ANIM_SEEK && out.frames == 2.0f,
             "a looping clip whose head went backwards out of phase is seeked, never restarted");

    base_in(&in, 0x47u, 5.0f, 0x47, 35.0f, 35.0f);
    in.loops = true;
    mp_puppet_anim_base_decide(&in, &out);
    ut_check(out.action != MP_PUPPET_ANIM_START,
             "a thirty frame regression on a loop restarts nothing");

    base_in(&in, 0x47u, 45.0f, 0x47, 40.0f, 30.0f);
    mp_puppet_anim_base_decide(&in, &out);
    ut_check(out.action == MP_PUPPET_ANIM_SEEK, "a seek past the end");
    ut_near(out.frames, 39.5, 1e-5, "is clamped half a frame short of it, forwards as well");

    base_in(&in, 0x47u, 32.0f, 0x47, 31.0f, -1.0f);
    mp_puppet_anim_base_decide(&in, &out);
    ut_check(out.action == MP_PUPPET_ANIM_NONE,
             "with no track of its own on the clip the puppet has nothing to seek");
}

static void overlay_in(mp_puppet_anim_overlay_in_t *in, uint32_t clip, float head, bool live,
                       int32_t puppet_clip, float puppet_head)
{
    memset(in, 0, sizeof *in);
    in->wire_clip    = clip;
    in->wire_head    = head;
    in->wire_live    = live;
    in->applied_clip = -1;
    in->applied_head = -1.0f;
    in->puppet_clip  = puppet_clip;
    in->puppet_head  = puppet_head;
    in->num_frames   = 20.0f;
    in->event_owned  = mp_puppet_anim_overlay_event_owned(&exclusions, clip);
    in->puppet_event_owned =
        puppet_clip >= 0 && mp_puppet_anim_overlay_event_owned(&exclusions, (uint32_t)puppet_clip);
}

static void check_overlay_decisions(void)
{
    mp_puppet_anim_overlay_in_t in;
    mp_puppet_anim_decision_t   out;
    static const uint32_t       owned[] = {
        0x20u, 0x23u, 0x55u, 0x5Du, 0x5Fu, 0x60u, 0x69u, 0x71u
    };
    size_t                      index;

    ut_section("the overlay channel with the live bit");
    overlay_in(&in, 0x2Bu, 3.0f, true, -1, -1.0f);
    mp_puppet_anim_overlay_decide(&in, &out);
    ut_check(out.action == MP_PUPPET_ANIM_START && out.mode == MP_PUPPET_ANIM_MODE_FADE,
             "a live overlay the puppet does not play starts with a crossfade");
    ut_near(out.frames, 3.0, 1e-5, "seeded to the wire's head");

    overlay_in(&in, 0x2Bu, 3.0f, true, 0x2B, 3.2f);
    in.applied_head = 2.0f;
    mp_puppet_anim_overlay_decide(&in, &out);
    ut_check(out.action == MP_PUPPET_ANIM_NONE, "one the puppet already plays is left alone");

    overlay_in(&in, 0x2Bu, 1.0f, true, 0x2B, 12.0f);
    in.applied_head = 11.0f;
    mp_puppet_anim_overlay_decide(&in, &out);
    ut_check(out.action == MP_PUPPET_ANIM_START,
             "the same overlay with its head thrown back is a restart");
    ut_check(out.mode == MP_PUPPET_ANIM_MODE_CUT,
             "and a restart is a cut, the way the engine restarts its own fire clip");

    for (index = 0; index < sizeof owned / sizeof owned[0]; ++index) {
        overlay_in(&in, owned[index], 3.0f, true, -1, -1.0f);
        mp_puppet_anim_overlay_decide(&in, &out);
        ut_checkf(out.action == MP_PUPPET_ANIM_RECORD,
                  "overlay %#x belongs to a starter or an event and is only recorded",
                  (unsigned)owned[index]);
    }

    overlay_in(&in, 0x2Bu, 18.5f, true, -1, -1.0f);
    mp_puppet_anim_overlay_decide(&in, &out);
    ut_check(out.action == MP_PUPPET_ANIM_RECORD,
             "an overlay within two frames of its end on the sender is not started");

    overlay_in(&in, 0x2Bu, 0.0f, false, 0x2B, 15.0f);
    mp_puppet_anim_overlay_decide(&in, &out);
    ut_check(out.action == MP_PUPPET_ANIM_STOP,
             "the bit clearing while the puppet still plays an overlay stops it");

    overlay_in(&in, 0x2Bu, 0.0f, false, 0x2B, 15.0f);
    in.aux_running = true;
    mp_puppet_anim_overlay_decide(&in, &out);
    ut_check(out.action == MP_PUPPET_ANIM_RECORD, "but never while an aux action owns the slot");

    overlay_in(&in, 0x2Bu, 0.0f, false, -1, -1.0f);
    mp_puppet_anim_overlay_decide(&in, &out);
    ut_check(out.action == MP_PUPPET_ANIM_RECORD, "and there is nothing to stop when none plays");

    overlay_in(&in, 0u, 0.0f, true, -1, -1.0f);
    mp_puppet_anim_overlay_decide(&in, &out);
    ut_check(out.action == MP_PUPPET_ANIM_START,
             "ordinal zero with the live bit set is a clip like any other, "
             "not a stand-in for none");

    /* The far player changes weapon: the puppet's setter plays the draw clip 0x21 and installs
     * the aux that waits for its marker. What the wire says meanwhile must not touch that
     * track, and what the state path did not start it must not stop. */
    ut_section("the overlay channel against the aux's own overlay");
    overlay_in(&in, 0x2Bu, 3.0f, true, 0x21, 4.0f);
    in.aux_running = true;
    mp_puppet_anim_overlay_decide(&in, &out);
    ut_check(out.action == MP_PUPPET_ANIM_RECORD && out.withheld,
             "a live gesture is not started over the draw clip a running aux waits on, and the "
             "decision says the start was withheld");

    overlay_in(&in, 0x2Bu, 1.0f, true, 0x2B, 12.0f);
    in.applied_head = 11.0f;
    in.aux_running  = true;
    mp_puppet_anim_overlay_decide(&in, &out);
    ut_check(out.action == MP_PUPPET_ANIM_RECORD && out.withheld,
             "nor is a restart made over the overlay a running aux waits on");

    overlay_in(&in, 0x2Bu, 3.0f, true, -1, -1.0f);
    in.aux_running = true;
    mp_puppet_anim_overlay_decide(&in, &out);
    ut_check(out.action == MP_PUPPET_ANIM_START,
             "an aux whose own track has already retired stands in nobody's way");

    overlay_in(&in, 0x21u, 3.0f, true, 0x21, 4.0f);
    in.aux_running = true;
    mp_puppet_anim_overlay_decide(&in, &out);
    ut_check(out.action == MP_PUPPET_ANIM_RECORD && !out.withheld,
             "an owned ordinal is recorded and not counted as withheld: it would never have "
             "started");

    overlay_in(&in, 0x21u, 0.0f, false, 0x21, 6.0f);
    in.aux_running = false;
    mp_puppet_anim_overlay_decide(&in, &out);
    ut_check(out.action == MP_PUPPET_ANIM_RECORD,
             "the aux has ended and the puppet still plays the draw clip with the bit clear: the "
             "state path did not start it and leaves it to retire itself");

    overlay_in(&in, 0x2Eu, 0.0f, false, 0x2E, 6.0f);
    in.aux_running = false;
    mp_puppet_anim_overlay_decide(&in, &out);
    ut_check(out.action == MP_PUPPET_ANIM_STOP && !in.puppet_event_owned,
             "the aux has ended and the puppet plays an overlay the state path started itself, "
             "the fire clip for one, with the bit clear: stopped");
}

static void check_seek_distance(void)
{
    mp_puppet_anim_base_in_t  in;
    mp_puppet_anim_decision_t out;

    /* The count of seeks alone cannot tell a head that crept past the tolerance from one that lost
     * its place, and the two want opposite fixes. The decision therefore reports HOW FAR it had
     * drifted, and it is the measured drift rather than the clamped write: the write is held away
     * from the clip's end, and a histogram of that would describe the clamp. */
    ut_section("a seek says how far the head had drifted");

    base_in(&in, 0x40u, 20.0f, 0x40, 20.0f, 17.0f);
    mp_puppet_anim_base_decide(&in, &out);
    ut_check(out.action == MP_PUPPET_ANIM_SEEK, "three frames behind is past the tolerance");
    ut_near(out.distance, 3.0, 1e-5, "and the distance is the drift, signed forwards");

    base_in(&in, 0x40u, 17.0f, 0x40, 17.0f, 20.0f);
    mp_puppet_anim_base_decide(&in, &out);
    ut_check(out.action == MP_PUPPET_ANIM_SEEK, "three frames ahead is too");
    ut_near(out.distance, -3.0, 1e-5, "and it reads negative that way round");

    base_in(&in, 0x40u, 20.0f, 0x40, 20.0f, 19.0f);
    mp_puppet_anim_base_decide(&in, &out);
    ut_check(out.action == MP_PUPPET_ANIM_NONE,
             "one frame apart is inside the tolerance and is no seek at all");

    /* The clamp the histogram must not describe: near the end of the clip the write is pulled
     * back, but the drift that earned the seek was still what it was. */
    base_in(&in, 0x40u, 39.9f, 0x40, 39.9f, 30.0f);
    mp_puppet_anim_base_decide(&in, &out);
    ut_check(out.action == MP_PUPPET_ANIM_SEEK && out.frames < 39.9f,
             "a seek close to the end is clamped away from it");
    ut_near(out.distance, 9.9, 1e-5, "and the distance is still the drift, not the clamped write");

    ut_section("the buckets straddle the tolerance");

    ut_check(mp_puppet_anim_seek_bucket(1.6f) == 0u && mp_puppet_anim_seek_bucket(-1.9f) == 0u,
             "just past the tolerance is the first bucket, either sign");
    ut_check(mp_puppet_anim_seek_bucket(2.0f) == 1u && mp_puppet_anim_seek_bucket(3.9f) == 1u,
             "two to four is the second");
    ut_check(mp_puppet_anim_seek_bucket(7.9f) == 2u && mp_puppet_anim_seek_bucket(15.9f) == 3u,
             "then eight and sixteen");
    ut_check(mp_puppet_anim_seek_bucket(16.0f) == MP_PUPPET_ANIM_SEEK_BUCKETS - 1u &&
             mp_puppet_anim_seek_bucket(1000.0f) == MP_PUPPET_ANIM_SEEK_BUCKETS - 1u,
             "and everything beyond is the last, which is a track that lost its place");
}

static void check_predicates(void)
{
    uint32_t flags[MP_PUPPET_ANIM_TRACKS];

    ut_section("what an event owns");
    ut_check(mp_puppet_anim_base_event_owned(&exclusions, 71u), "the first swing row's clip");
    ut_check(mp_puppet_anim_base_event_owned(&exclusions, 112u), "and the last");
    ut_check(!mp_puppet_anim_base_event_owned(&exclusions, 0x55u),
             "the midair row plays on the overlay channel, so on the base channel it is nobody's");
    ut_check(!mp_puppet_anim_base_event_owned(&exclusions, 0x30u), "a walk clip is the state's");
    ut_check(mp_puppet_anim_base_event_owned(&exclusions, 0x47u),
             "and 0x47 is not one: it is 71, the first swing row's clip");
    {
        mp_puppet_anim_exclusions_t empty;

        memset(&empty, 0, sizeof empty);
        ut_check(!mp_puppet_anim_base_event_owned(&empty, 71u),
                 "with no table read, nothing is excluded and the puppet is merely degraded");
        ut_check(!mp_puppet_anim_base_event_owned(NULL, 71u), "and a missing table is the same");
    }
    ut_check(mp_puppet_anim_overlay_event_owned(&exclusions, 0x22u), "a draw clip is the setter's");
    ut_check(!mp_puppet_anim_overlay_event_owned(&exclusions, 0x1Fu) &&
                 !mp_puppet_anim_overlay_event_owned(&exclusions, 0x24u),
             "the ordinals either side of the draw clips are not");
    ut_check(!mp_puppet_anim_overlay_event_owned(&exclusions, 0x5Cu) &&
                 !mp_puppet_anim_overlay_event_owned(&exclusions, 0x6Au),
             "nor those either side of the parry and block clips");
    ut_check(mp_puppet_anim_overlay_event_owned(&exclusions, 0x56u) == false,
             "the held midair pose is the state's to start and stop");

    ut_section("which overlays a seed is allowed on");
    ut_check(!mp_puppet_anim_overlay_seeds(0x20u) && !mp_puppet_anim_overlay_seeds(0x23u),
             "not the draw and holster clips, whose marker the weapon aux waits for");
    ut_check(!mp_puppet_anim_overlay_seeds(0x71u),
             "not the push clip, whose marker frees the bolt");
    ut_check(mp_puppet_anim_overlay_seeds(0x2Bu) && mp_puppet_anim_overlay_seeds(0x60u),
             "a gesture or a block may be seeded");

    ut_section("the loop predicate");
    ut_check(mp_puppet_anim_loops(0u), "no end mode wraps freely");
    ut_check(mp_puppet_anim_loops(0x04u), "the loop mode wraps to its loop start");
    ut_check(mp_puppet_anim_loops(0x08u), "a marker clip without an end mode wraps too");
    ut_check(!mp_puppet_anim_loops(0x01u) && !mp_puppet_anim_loops(0x02u) &&
                 !mp_puppet_anim_loops(0x03u),
             "hold at end and release at end do not");

    ut_section("the track guard, which is the overlay player's own search");
    memset(flags, 0, sizeof flags);
    ut_check(mp_puppet_anim_track_free(flags), "four free tracks");
    flags[0] = 0x3u;
    flags[1] = 0x7u;
    flags[2] = 0x23u;
    flags[3] = 0x1u;
    ut_check(!mp_puppet_anim_track_free(flags), "four busy tracks, none fading, refuse");
    flags[2] = 0x0Bu;
    ut_check(mp_puppet_anim_track_free(flags), "a fading track is one the player would take");
    flags[2] = 0x4Bu;
    ut_check(!mp_puppet_anim_track_free(flags), "unless it holds at its end");
    flags[2] = 0x10Bu;
    ut_check(!mp_puppet_anim_track_free(flags), "or keeps its slot");
    ut_check(!mp_puppet_anim_track_free(NULL), "no flags is no track");
    ut_check(!mp_puppet_anim_overlay_track_free(0u),
             "and an object that is not there has no track either, so no starter runs on it");

    ut_section("the seed window");
    ut_near(mp_puppet_anim_seed_frames(10.0f, 40.0f), 10.0, 1e-6, "inside the clip, the head");
    ut_near(mp_puppet_anim_seed_frames(0.0f, 40.0f), 0.0, 1e-6, "at zero, no seed");
    ut_near(mp_puppet_anim_seed_frames(-1.0f, 40.0f), 0.0, 1e-6, "below zero, no seed");
    ut_near(mp_puppet_anim_seed_frames(39.7f, 40.0f), 39.5, 1e-6,
            "past half a frame before the end, that half frame");
    ut_near(mp_puppet_anim_seed_frames(10.0f, 0.0f), 0.0, 1e-6, "an unknown clip length, no seed");
    ut_near(mp_puppet_anim_seed_frames(10.0f, 0.5f), 0.0, 1e-6, "a clip with no room, no seed");
}

static void check_event_due(void)
{
    ut_section("when an event is due");
    ut_check(mp_puppet_anim_event_due(100u, 0u, false) == MP_PUPPET_ANIM_DUE,
             "without a render tick every event is due at arrival");
    ut_check(mp_puppet_anim_event_due(10u, 10u, true) == MP_PUPPET_ANIM_DUE, "on its tick, due");
    ut_check(mp_puppet_anim_event_due(9u, 10u, true) == MP_PUPPET_ANIM_DUE,
             "one tick behind, due and not counted as late");
    ut_check(mp_puppet_anim_event_due(8u, 10u, true) == MP_PUPPET_ANIM_LATE,
             "two ticks behind, due and counted as late");
    ut_check(mp_puppet_anim_event_due(11u, 10u, true) == MP_PUPPET_ANIM_WAIT,
             "one tick ahead, waits");
    ut_check(mp_puppet_anim_event_due(42u, 10u, true) == MP_PUPPET_ANIM_WAIT,
             "thirty two ahead still waits");
    ut_check(mp_puppet_anim_event_due(43u, 10u, true) == MP_PUPPET_ANIM_FORCED,
             "thirty three ahead is performed at once and counted as forced");
    ut_check(mp_puppet_anim_event_due(5u, 0xFFFFFFFEu, true) == MP_PUPPET_ANIM_WAIT,
             "across the counter's wrap, seven ahead waits rather than reading as four billion");
    ut_check(mp_puppet_anim_event_due(0xFFFFFFFEu, 5u, true) == MP_PUPPET_ANIM_LATE,
             "and seven behind across the wrap is late rather than forced");
}

static void check_muzzle(void)
{
    const float position[3] = { 1.0f, 2.0f, 3.0f };
    float       rotation[3] = { 0.0f, 90.0f, 0.0f };
    float       local[3] = { 0.0f, 1.0f, 0.0f };
    float       out[3];

    ut_section("the muzzle placed by the body's pose");
    mp_puppet_anim_muzzle(position, rotation, local, out);
    ut_near(out[0], 0.0, 1e-5, "an upright body facing yaw 90 puts one unit forward at -x");
    ut_near(out[1], 2.0, 1e-5, "and moves nothing along y");
    ut_near(out[2], 3.0, 1e-5, "or z");

    local[0] = 1.0f;
    local[1] = 0.0f;
    mp_puppet_anim_muzzle(position, rotation, local, out);
    ut_near(out[0], 1.0, 1e-5, "one unit to the body's right at yaw 90 is along +y");
    ut_near(out[1], 3.0, 1e-5, "which is what the first column says");

    rotation[0] = 90.0f;
    rotation[1] = 0.0f;
    local[0] = 0.0f;
    local[1] = 1.0f;
    mp_puppet_anim_muzzle(position, rotation, local, out);
    ut_near(out[2], 4.0, 1e-5, "a positive pitch lifts forward into +z");
    ut_near(out[1], 2.0, 1e-5, "and takes it out of y");

    rotation[0] = 0.0f;
    local[0] = 0.5f;
    local[1] = -1.5f;
    local[2] = 2.0f;
    mp_puppet_anim_muzzle(position, rotation, local, out);
    ut_near(out[0], 1.5, 1e-5, "no rotation is the identity in x");
    ut_near(out[1], 0.5, 1e-5, "in y");
    ut_near(out[2], 5.0, 1e-5, "and in z");

    ut_section("wrapping a heading");
    ut_near(mp_puppet_anim_wrap360(370.0f), 10.0, 1e-5, "370 is 10");
    ut_near(mp_puppet_anim_wrap360(-10.0f), 350.0, 1e-5, "-10 is 350");
    ut_near(mp_puppet_anim_wrap360(360.0f), 0.0, 1e-5, "360 is 0");
    ut_near(mp_puppet_anim_wrap360(0.0f), 0.0, 1e-5, "0 is 0");
    {
        const uint32_t nan_bits = 0x7FC00000u;
        float          nan;

        memcpy(&nan, &nan_bits, sizeof nan);
        ut_near(mp_puppet_anim_wrap360(nan), 0.0, 1e-5,
                "not a number becomes 0 rather than a heading");
    }
}

int main(void)
{
    memset(&exclusions, 0, sizeof exclusions);
    memcpy(exclusions.swing_clip, SWING_CLIPS, sizeof SWING_CLIPS);
    exclusions.swing_count = MP_PUPPET_ANIM_SWING_ROWS;

    check_base_decisions();
    check_overlay_decisions();
    check_seek_distance();
    check_predicates();
    check_event_due();
    check_muzzle();

    ut_section("the engine half with no game");
    mp_puppet_anim_resolve();
    {
        mp_puppet_anim_counters_t counters;

        mp_puppet_anim_counters(&counters);
        ut_check(counters.base_starts_cut == 0u && counters.overlay_starts == 0u,
                 "nothing resolves in a process with no game, and nothing is counted as done");
        ut_check(counters.seeks == 0u && counters.seeks_across_marker == 0u &&
                     counters.overlay_starts_withheld == 0u,
                 "no seek, no seek across a marker and no withheld start either");
        ut_check(!mp_puppet_anim_clip_exists(0u, 5u), "a null object carries no clip");
        mp_puppet_anim_reset(1u);
        mp_puppet_anim_counters(&counters);
        ut_check(counters.refused_ordinals == 0u,
                 "a reset with no swing table cell reads nothing and counts nothing");
    }

    return ut_summary("puppet animation decisions");
}
