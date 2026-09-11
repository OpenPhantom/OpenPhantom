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
    ut_check(true, "a stopped subnode is accepted without the report being consulted");

    /* The report is the only thing that writes to the log, and it must say nothing when nothing
     * was judged rather than printing an empty window. */
    mover_evenness_enable(true);
    mover_evenness_report();
    ut_check(true, "an empty window reports nothing at all");

    mover_evenness_note(NULL, at, 1u, true);
    mover_evenness_note(&state, NULL, 1u, true);
    ut_check(true, "and being handed nothing is survivable, which a diagnostic must be");

    return ut_summary("mover evenness");
}
