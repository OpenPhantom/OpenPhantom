/* mp_wallclock.c: the one clock the session and the channel run on, driven for real.
 *
 * Three properties, none of them provable by reading the code: the first call is the base and
 * reads zero, no call reads less than the one before it, and a real wait is measured as roughly
 * what it was. The last one is the reason the clock is the performance counter rather than the
 * tick count, whose sixteen millisecond steps would make a ten millisecond wait read as zero or
 * as sixteen.
 */
#include "unittest.h"

#include "mp_wallclock.h"

#include <windows.h>

#include <stdint.h>

int main(void)
{
    uint32_t first;
    uint32_t previous;
    uint32_t before;
    uint32_t after;
    int      i;
    int      backwards = 0;

    ut_section("the base is the first call");

    first = mp_wallclock_ms();
    ut_check(first == 0u, "the first reading is zero, so the count never starts near the wrap");

    ut_section("the clock never runs backwards");

    previous = first;
    for (i = 0; i < 1000; ++i) {
        uint32_t now = mp_wallclock_ms();

        if ((int32_t)(now - previous) < 0) {
            ++backwards;
        }
        previous = now;
    }
    ut_check(backwards == 0, "a thousand readings in a row never decrease");

    ut_section("a real wait is measured as roughly itself");

    before = mp_wallclock_ms();
    Sleep(10);
    after = mp_wallclock_ms();
    ut_checkf(after - before >= 8u && after - before <= 30u,
              "ten milliseconds of sleep read as %u ms, inside 8..30", (unsigned)(after - before));

    return ut_summary("mp_wallclock");
}
