/* mover_evenness.c: the instrument that judges whether a mover draws evenly.
 *
 * It is worth testing because two earlier versions of it were wrong in ways that a play session
 * could not reveal, and both wrong answers were believed.
 *
 * The first divided the largest consecutive-frame step by the smallest, which is a speed range
 * rather than jitter: a lift easing into its stop scores three or six while moving perfectly
 * smoothly. It reported a fault where none existed and credited a change that provably drew
 * identical output.
 *
 * The second treated the second of a subnode's two calls in a frame as a break in the history.
 * The draw calls both a matrix product and a rigid invert for each subnode and this DLL redirects
 * both, so the chain reset on every second call and a real play session reported nothing judged
 * at all.
 *
 * Both are checked here against motion whose answer is already known.
 */
#include "unittest.h"

#include "mover_evenness.h"

#include <string.h>

int main(void)
{
    mover_evenness_state_t state;
    float                  at[3];
    uint32_t               frame;
    uint32_t               judged;
    uint32_t               uneven;
    int                    i;

    ut_section("switched off, which is how it ships");

    mover_evenness_enable(false);
    ut_check(!mover_evenness_enabled(), "off is off");
    memset(&state, 0, sizeof state);
    at[0] = 0.0f; at[1] = 0.0f; at[2] = 0.0f;
    for (frame = 1u; frame <= 10u; ++frame) {
        at[0] = (float)frame;
        mover_evenness_note(&state, at, frame, true);
    }
    ut_check(!state.seen, "and nothing is recorded, so it costs nothing while it is off");

    ut_section("even motion at any speed");

    mover_evenness_enable(true);
    memset(&state, 0, sizeof state);
    for (frame = 1u; frame <= 40u; ++frame) {
        at[0] = (float)frame * 0.05f;
        mover_evenness_note(&state, at, frame, true);
    }
    ut_check(state.have_before && state.have_middle,
             "even motion builds a history and is judged");
    mover_evenness_counts(&judged, &uneven);
    ut_checkf(judged == 37u && uneven == 0u,
              "forty even frames read %u judged and %u uneven, expecting 37 and 0: the first "
              "step has no step before it and the last has none after",
              (unsigned)judged, (unsigned)uneven);

    ut_section("stepped motion, the thing the instrument exists to see");

    /* A mover creeping for two frames and then taking a whole step at once: every jump disagrees
     * with the creeps either side of it and every creep with the jumps. The creep is above the
     * floor on purpose, since a frame that does not move at all is not judged. */
    mover_evenness_enable(true);
    memset(&state, 0, sizeof state);
    at[0] = 0.0f;
    for (frame = 1u; frame <= 40u; ++frame) {
        at[0] += ((frame % 3u) == 0u) ? 0.15f : 0.01f;
        mover_evenness_note(&state, at, frame, true);
    }
    mover_evenness_counts(&judged, &uneven);
    ut_checkf(judged > 0u && uneven == judged,
              "a mover stepping every third frame is uneven on every judged frame, %u of %u",
              (unsigned)uneven, (unsigned)judged);

    ut_section("drawn twice a frame, as the engine really does it");

    /* The regression. Both redirected consumers ask about the same subnode in the same frame, and
     * the second call must be ignored rather than break the chain. */
    mover_evenness_enable(true);
    memset(&state, 0, sizeof state);
    for (frame = 1u; frame <= 40u; ++frame) {
        at[0] = (float)frame * 0.05f;
        mover_evenness_note(&state, at, frame, true);
        mover_evenness_note(&state, at, frame, true);
    }
    ut_check(state.have_before && state.have_middle,
             "two calls a frame still leave a usable history rather than resetting it");
    mover_evenness_counts(&judged, &uneven);
    ut_checkf(judged == 37u && uneven == 0u,
              "and the second call neither judges again nor breaks the chain: %u judged and %u "
              "uneven, expecting 37 and 0", (unsigned)judged, (unsigned)uneven);

    ut_section("a gap is not a step");

    mover_evenness_enable(true);
    memset(&state, 0, sizeof state);
    at[0] = 0.0f;
    mover_evenness_note(&state, at, 1u, true);
    at[0] = 100.0f;
    mover_evenness_note(&state, at, 40u, true);
    ut_check(!state.have_before && !state.have_middle,
             "a subnode that went off screen and came back starts its history again");

    ut_section("a mover coming to rest");

    /* All three steps have to clear the floor, so the frame a mover stops on is not judged. A
     * subnode that has stopped moving must not be reported as uneven for stopping. */
    mover_evenness_enable(true);
    memset(&state, 0, sizeof state);
    at[0] = 0.0f;
    for (i = 1; i <= 20; ++i) {
        at[0] += (i < 10) ? 0.05f : 0.0f;
        mover_evenness_note(&state, at, (uint32_t)i, true);
    }
    mover_evenness_counts(&judged, &uneven);
    ut_checkf(judged == 6u && uneven == 0u,
              "eight moving steps are judged on the six with a moving neighbour either side, and "
              "stopping is not uneven: %u judged and %u uneven, expecting 6 and 0",
              (unsigned)judged, (unsigned)uneven);

    /* The report writes the window to the log and starts a new one, so the counts it leaves
     * behind are the observable half of that. */
    mover_evenness_report();
    mover_evenness_counts(&judged, &uneven);
    ut_check(judged == 0u && uneven == 0u, "the report starts a new window");

    /* An empty window must be silent rather than printed empty, and the counts are the condition
     * that decides it. */
    mover_evenness_enable(true);
    mover_evenness_report();
    mover_evenness_counts(&judged, &uneven);
    ut_check(judged == 0u, "an empty window reports nothing and stays empty");

    mover_evenness_note(NULL, at, 1u, true);
    mover_evenness_note(&state, NULL, 1u, true);
    mover_evenness_counts(&judged, &uneven);
    ut_check(judged == 0u && state.stamp == 20u,
             "being handed nothing judges nothing and touches no history, which a diagnostic "
             "must manage");

    return ut_summary("mover evenness");
}
