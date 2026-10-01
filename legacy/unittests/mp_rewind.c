/* mp_rewind.c: where a body was, interpolated, clamped to the window.
 *
 * A body walks along a known line, so the rewind can be checked against the exact answer. Between
 * two stored ticks the sample interpolates; a request older than the window is answered at the
 * window's edge, not refused; a request in the future is answered at the newest. The point of the
 * clamp is that a shot can never rewind arbitrarily far back, so a body cannot be hit where it was
 * a whole second ago.
 */
#include "unittest.h"

#include "mp_rewind.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static float absf(float x)
{
    return x < 0.0f ? -x : x;
}

/* Store ticks base..base+count-1 with x = tick (a body walking along x at one unit per tick). */
static void store_walk(mp_rewind_t *rewind, uint32_t base, uint32_t count)
{
    uint32_t i;

    for (i = 0; i < count; ++i) {
        float position[3];

        position[0] = (float)(base + i);
        position[1] = 0.0f;
        position[2] = 0.0f;
        mp_rewind_store(rewind, base + i, position);
    }
}

static void check_exact_and_interpolated(void)
{
    mp_rewind_t rewind;
    float       out[3];

    ut_section("a stored tick is exact, between two it interpolates");

    mp_rewind_init(&rewind);
    ut_check(!mp_rewind_sample(&rewind, 5u, 0.0f, out), "an empty history samples nothing");

    store_walk(&rewind, 100u, 20u);   /* ticks 100..119, newest 119 */

    ut_check(mp_rewind_sample(&rewind, 115u, 0.0f, out) && absf(out[0] - 115.0f) < 0.001f,
             "exactly on a stored tick gives that position");
    ut_check(mp_rewind_sample(&rewind, 115u, 0.5f, out) && absf(out[0] - 115.5f) < 0.001f,
             "halfway to the next tick is halfway between the positions");
    ut_check(mp_rewind_sample(&rewind, 115u, 0.25f, out) && absf(out[0] - 115.25f) < 0.001f,
             "a quarter of the way is a quarter between");
}

static void check_window_clamp(void)
{
    mp_rewind_t rewind;
    float       out[3];
    uint32_t    oldest = 0;

    ut_section("the rewind is clamped to its window");

    mp_rewind_init(&rewind);
    store_walk(&rewind, 200u, 32u);   /* ticks 200..231, newest 231 */

    ut_check(mp_rewind_oldest_allowed(&rewind, &oldest) && oldest == 231u - MP_REWIND_MAX_TICKS,
             "the oldest allowed is the newest minus the window");

    /* A request far in the past is answered at the window edge, not refused. */
    ut_check(mp_rewind_sample(&rewind, 200u, 0.0f, out),
             "a request older than the window still answers");
    ut_check(absf(out[0] - (float)(231u - MP_REWIND_MAX_TICKS)) < 0.001f,
             "at the window's edge, not the ancient position");

    /* A request in the future is answered at the newest. */
    ut_check(mp_rewind_sample(&rewind, 240u, 0.5f, out) && absf(out[0] - 231.0f) < 0.001f,
             "a future request is answered at the newest sample");
}

static void check_missing_sample(void)
{
    mp_rewind_t rewind;
    float       out[3];
    float       position[3] = { 0.0f, 0.0f, 0.0f };

    ut_section("a missing bracketing sample is refused rather than invented");

    mp_rewind_init(&rewind);
    /* Store only the newest few, so a tick inside the window has no stored sample. */
    mp_rewind_store(&rewind, 300u, position);
    position[0] = 8.0f;
    mp_rewind_store(&rewind, 308u, position);   /* newest 308, window reaches 300 */

    /* Tick 304 is inside the window but was never stored, so its slot holds nothing for 304. */
    ut_check(!mp_rewind_sample(&rewind, 304u, 0.0f, out),
             "a tick inside the window that was never stored refuses");
}

static void check_wraparound(void)
{
    mp_rewind_t rewind;
    float       out[3];

    ut_section("the newest and the window survive the tick counter's wrap");

    mp_rewind_init(&rewind);
    {
        float p[3] = { 1.0f, 0.0f, 0.0f };

        mp_rewind_store(&rewind, 0xFFFFFFFEu, p);
        p[0] = 2.0f;
        mp_rewind_store(&rewind, 0xFFFFFFFFu, p);
        p[0] = 3.0f;
        mp_rewind_store(&rewind, 0u, p);   /* wrapped */
    }

    ut_check(mp_rewind_sample(&rewind, 0u, 0.0f, out) && absf(out[0] - 3.0f) < 0.001f,
             "the wrapped tick is the newest and samples correctly");
    ut_check(mp_rewind_sample(&rewind, 0xFFFFFFFFu, 1.0f, out),
             "and a tick just before the wrap is still in the window");
}

int main(void)
{
    check_exact_and_interpolated();
    check_window_clamp();
    check_missing_sample();
    check_wraparound();

    return ut_summary("mp_rewind");
}
