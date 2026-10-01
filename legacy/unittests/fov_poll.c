/* fov_poll.c: how often the frame poll may ask the settings file whether ExtraDegrees moved.
 *
 * The question costs one file attribute query, and it was asked on every frame, so at 240 frames a
 * second it was asked 240 times to learn about a setting a person drags by hand. The throttle keeps
 * the developer menu's live preview of the field of view at about 33 steps a second and drops the
 * rest. Every way it can be wrong is silent: a throttle that never opens freezes the preview, one
 * that opens on every frame saves nothing, and one that mishandles the tick count's wrap after 49.7
 * days stops looking for good.
 */
#include "unittest.h"

#include "fov_poll.h"

#include <stdint.h>

static void test_the_period(void)
{
    fov_poll_t poll = { 0 };

    ut_section("the period between two looks");

    ut_check(fov_poll_due(&poll, 1000u, FOV_POLL_PERIOD_MS),
             "the first look is due whatever the clock reads");
    ut_check(poll.looks == 1u, "and it is counted");
    ut_check(!fov_poll_due(&poll, 1000u, FOV_POLL_PERIOD_MS), "the same millisecond again is not");
    ut_check(!fov_poll_due(&poll, 1029u, FOV_POLL_PERIOD_MS), "29 ms later is not");
    ut_check(fov_poll_due(&poll, 1030u, FOV_POLL_PERIOD_MS), "30 ms later is");
    ut_check(poll.looks == 2u, "and that is the second look");
    ut_check(!fov_poll_due(&poll, 1059u, FOV_POLL_PERIOD_MS),
             "the period runs from the last look, not from the first");
    ut_check(fov_poll_due(&poll, 1060u, FOV_POLL_PERIOD_MS), "so 30 ms after the second is due");
    ut_check(poll.taken == 0u, "looking takes nothing: a change is counted by the caller");
    ut_check(FOV_POLL_PERIOD_MS == 30u, "the period is 30 ms");
}

static void test_the_first_look_at_zero(void)
{
    fov_poll_t poll = { 0 };

    ut_section("a clock that reads zero");

    ut_check(fov_poll_due(&poll, 0u, FOV_POLL_PERIOD_MS),
             "the first look is due at a tick count of 0, which a stored time of 0 cannot tell "
             "from never having looked");
    ut_check(!fov_poll_due(&poll, 10u, FOV_POLL_PERIOD_MS), "and 10 ms later the next is not");
}

static void test_the_wrap(void)
{
    fov_poll_t poll = { 0 };

    ut_section("the tick count's wrap after 49.7 days");

    ut_check(fov_poll_due(&poll, 0xFFFFFFF0u, FOV_POLL_PERIOD_MS), "a look just before the wrap");
    ut_check(!fov_poll_due(&poll, 0x0000000Du, FOV_POLL_PERIOD_MS),
             "29 ms later, across the wrap, is not due");
    ut_check(fov_poll_due(&poll, 0x0000000Eu, FOV_POLL_PERIOD_MS),
             "30 ms later, across the wrap, is: the subtraction is unsigned");
}

static void test_a_period_of_zero(void)
{
    fov_poll_t poll = { 0 };

    ut_section("a period of zero");

    ut_check(fov_poll_due(&poll, 5u, 0u) && fov_poll_due(&poll, 5u, 0u),
             "a period of zero looks on every call, which is what the poll did before");
}

/* Frames arriving every `step_ms` for one second, counted. */
static uint32_t looks_in_a_second(uint32_t step_ms)
{
    fov_poll_t poll = { 0 };
    uint32_t   now;

    for (now = 0u; now < 1000u; now += step_ms) {
        (void)fov_poll_due(&poll, now, FOV_POLL_PERIOD_MS);
    }
    return poll.looks;
}

static void test_the_rate(void)
{
    uint32_t fast = looks_in_a_second(4u);
    uint32_t slow = looks_in_a_second(16u);
    uint32_t crawl = looks_in_a_second(50u);

    ut_section("looks in a second of frames");

    ut_checkf(fast >= 30u && fast <= 34u,
              "at a frame every 4 ms the file is asked %u times a second, not 250", fast);
    ut_checkf(slow >= 30u && slow <= 34u,
              "at a frame every 16 ms it is asked %u times, about the same", slow);
    ut_checkf(crawl == 20u,
              "at a frame every 50 ms every frame looks, %u of 20, because the period has passed",
              crawl);
}

int main(void)
{
    test_the_period();
    test_the_first_look_at_zero();
    test_the_wrap();
    test_a_period_of_zero();
    test_the_rate();

    return ut_summary("fov poll");
}
