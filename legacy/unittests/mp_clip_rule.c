/* mp_clip_rule.c: where a body's clip stands against the sender's, and which events a move passes.
 *
 * Three answers two kinds of body share. The distance is the puppet's old one moved here, so the
 * old function is kept below word for word and the new one is held against it over a grid; the
 * window is a time and not a count of substeps, so it is checked at both substep lengths the
 * engine runs; and the span of events is the dispatcher's own half open one, whose ends are where
 * a count would drift from what fires.
 */
#include "unittest.h"

#include "mp_clip_rule.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

/* The distance as mp_puppet_anim.c had it before it moved, the reference. */
static float old_head_distance(float wire_head, float puppet_head, bool loops, float num_frames)
{
    float delta = wire_head - puppet_head;

    if (loops && num_frames > 0.0f) {
        delta = (float)fmod((double)delta + (double)num_frames * 0.5, (double)num_frames);
        if (delta < 0.0f) {
            delta += num_frames;
        }
        delta -= num_frames * 0.5f;
    }
    return delta;
}

static void the_distance(void)
{
    uint32_t differ = 0;
    int      w;
    int      l;

    ut_section("the distance between two playheads");
    ut_near(mp_clip_head_distance(12.0f, 10.0f, false, 30.0f), 2.0f, 1e-6,
            "a clip that does not wrap is measured straight, the wire ahead positive");
    ut_near(mp_clip_head_distance(1.0f, 29.0f, true, 30.0f), 2.0f, 1e-5,
            "on a loop of thirty frames, frame 1 against frame 29 is two frames ahead, not "
            "twenty eight behind: a wrap is not a jump back");
    ut_near(mp_clip_head_distance(29.0f, 1.0f, true, 30.0f), -2.0f, 1e-5,
            "and frame 29 against frame 1 two frames behind");
    ut_near(mp_clip_head_distance(1.0f, 29.0f, false, 30.0f), -28.0f, 1e-6,
            "the same heads on a clip that holds its end are twenty eight apart");
    ut_near(mp_clip_head_distance(1.0f, 29.0f, true, 0.0f), -28.0f, 1e-6,
            "a loop whose length did not read is measured straight rather than guessed");

    for (w = 0; w <= 120; ++w) {
        for (l = 0; l <= 120; ++l) {
            float wire  = (float)w * 0.25f;
            float local = (float)l * 0.25f;

            differ += mp_clip_head_distance(wire, local, true, 30.0f) !=
                              old_head_distance(wire, local, true, 30.0f)
                          ? 1u
                          : 0u;
            differ += mp_clip_head_distance(wire, local, false, 30.0f) !=
                              old_head_distance(wire, local, false, 30.0f)
                          ? 1u
                          : 0u;
        }
    }
    ut_checkf(differ == 0u, "over 29282 pairs the moved distance answers as the puppet's old one "
                            "did (%u differ)", (unsigned)differ);
}

static void the_window(void)
{
    ut_section("the window is ten substeps of world time");
    ut_check(!mp_clip_beyond_window(9.0f, 30.0f, 1.0f / 32.0f),
             "nine frames at thirty a second is 0.3 s, inside 10 substeps of 1/32 s (0.3125 s)");
    ut_check(mp_clip_beyond_window(9.5f, 30.0f, 1.0f / 32.0f),
             "nine and a half frames is past it");
    ut_check(mp_clip_beyond_window(9.0f, 30.0f, 1.0f / 64.0f),
             "at the frame rate cheat's 1/64 s the window is half as long: nine frames is past "
             "it, which a count of substeps would have missed");
    ut_check(!mp_clip_beyond_window(4.5f, 30.0f, 1.0f / 64.0f),
             "and four and a half frames is inside it");
    ut_check(mp_clip_beyond_window(140.0f, 30.0f, 1.0f / 32.0f),
             "a corpse held at the last of 140 frames is far past it");
    ut_check(mp_clip_beyond_window(2.0f, 2.0f, 1.0f / 32.0f),
             "the rate of the clip counts, not its frames: two frames at two a second is past it");
    ut_check(!mp_clip_beyond_window(2.0f, 160.0f, 1.0f / 32.0f),
             "and two frames at 160 a second is inside it");
    ut_check(!mp_clip_beyond_window(100.0f, 0.0f, 1.0f / 32.0f) &&
                 !mp_clip_beyond_window(100.0f, NAN, 1.0f / 32.0f) &&
                 !mp_clip_beyond_window(NAN, 30.0f, 1.0f / 32.0f) &&
                 !mp_clip_beyond_window(0.0f, 30.0f, 1.0f / 32.0f),
             "a rate or a head that is not a positive number starts the clip from its first frame");
    ut_check(mp_clip_beyond_window(9.5f, 30.0f, 0.0f) && !mp_clip_beyond_window(9.0f, 30.0f, NAN),
             "a substep length that did not read is the engine's own 1/32 s");
}

static void the_span(void)
{
    ut_section("the span of events a move passes is the dispatcher's");
    ut_check(mp_clip_event_in_span(0, 0.0f, 3.0f),
             "an event on the first frame fires for a head leaving frame 0");
    ut_check(mp_clip_event_in_span(2, 0.0f, 3.0f), "and one inside the span");
    ut_check(!mp_clip_event_in_span(3, 0.0f, 3.0f),
             "one exactly on the new head does not fire yet: the next advance fires it");
    ut_check(mp_clip_event_in_span(3, 3.0f, 5.0f),
             "and that next advance, from 3, does, so no event fires twice or never");
    ut_check(!mp_clip_event_in_span(5, 2.0f, 2.0f) && !mp_clip_event_in_span(2, 2.0f, 2.0f),
             "an empty span passes nothing");
    ut_check(!mp_clip_event_in_span(1, 2.0f, 5.0f), "an event behind the span does not fire");
}

int main(void)
{
    the_distance();
    the_window();
    the_span();
    return ut_summary("mp_clip_rule");
}
