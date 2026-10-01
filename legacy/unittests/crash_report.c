/* crash_report.c: when the running count of first-chance access violations goes into the log.
 *
 * The reporter names the first eight distinct faulting sites and only counts the rest. A count
 * printed only in a real crash report would leave a process that survives its faults with eight
 * lines and never the total, so a ninth site, and every fault after it, would stay invisible. The
 * count goes out when it first reaches 16, 64, 256 and 1024, and once more as the process ends.
 * What can be wrong is quiet either way: a milestone missed leaves the total out of the log again,
 * and one printed twice is the first step towards a flood from a probe that faults on every frame.
 */
#include "unittest.h"

#include "crash_report.h"

static void test_below_and_at_the_milestones(void)
{
    ut_section("the four milestones");

    ut_check(crash_report_av_milestone(0, 0) == 0, "no faults, nothing to report");
    ut_check(crash_report_av_milestone(9, 0) == 0,
             "nine is past the table of eight and still short of the first milestone");
    ut_check(crash_report_av_milestone(15, 0) == 0, "fifteen is one short");
    ut_check(crash_report_av_milestone(16, 0) == 16, "sixteen is the first");
    ut_check(crash_report_av_milestone(17, 16) == 0, "and once reported it is not reported again");
    ut_check(crash_report_av_milestone(63, 16) == 0, "sixty three waits for the next");
    ut_check(crash_report_av_milestone(64, 16) == 64, "sixty four is the second");
    ut_check(crash_report_av_milestone(256, 64) == 256, "two hundred and fifty six the third");
    ut_check(crash_report_av_milestone(1024, 256) == 1024, "one thousand and twenty four the last");
    ut_check(crash_report_av_milestone(100000, 1024) == 0,
             "past the last one only the process end reports, however fast it keeps counting");
}

static void test_a_jump_past_several(void)
{
    ut_section("a count that jumped past several at once");

    ut_check(crash_report_av_milestone(300, 16) == 256,
             "a count that went from under 64 to 300 while the latch was held reports once, at the "
             "largest milestone it passed");
    ut_check(crash_report_av_milestone(300, 256) == 0, "and not again for the ones it skipped");
    ut_check(crash_report_av_milestone(5000, 0) == 1024,
             "a count first seen at 5000 reports the last milestone and is done");
}

static void test_nonsense_in(void)
{
    ut_section("nonsense in");

    ut_check(crash_report_av_milestone(-1, 0) == 0, "a negative count reports nothing");
    ut_check(crash_report_av_milestone(64, 1024) == 0,
             "a count below what was already reported reports nothing");
}

int main(void)
{
    test_below_and_at_the_milestones();
    test_a_jump_past_several();
    test_nonsense_in();

    return ut_summary("crash report");
}
