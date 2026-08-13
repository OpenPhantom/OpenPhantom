/* framerate_stats.c: the one judgement the frame window makes, and it used to get it wrong.
 *
 * The substep counter this instrument reports on is shared. Besides the simulation loop, a menu
 * frame ticks it once per rendered frame, so a window taken outside gameplay used to be printed as
 * "about one substep per frame at the frame rate", with an interpolation alpha that never moved,
 * and it read exactly like a measurement of the simulation. A machine pass over one field log
 * found 41 windows out of 750 contaminated that way, and an open item was raised against the
 * simulation on the strength of them before the instrument itself turned out to be the fault.
 *
 * The verdict is now decided by comparing the counter against the simulation clock, which only the
 * substep loop advances. That comparison is pure arithmetic, and both of its failure directions are
 * silent: too strict and every honest gameplay window is thrown away, too loose and the menu
 * windows come back. So it is checked against the cases the field log actually produced.
 *
 * Where the two operands come from. The tail of the substep loop at 0x004757DB is 31 bytes and
 * carries both of them:
 *
 *     A1 60884B00       mov  eax,[0x004B8860]     the substep counter
 *     83 C0 01          add  eax,1
 *     A3 60884B00       mov  [0x004B8860],eax
 *     D9 05 28878600    fld  [0x00868728]         the simulation clock
 *     D8 05 14878600    fadd [0x00868714]         the substep, while the loop is running
 *     D9 1D 28878600    fstp [0x00868728]
 *
 * An operand census over the code section of the retail executable is what says the two cells are
 * not interchangeable. Each cell's four byte little endian encoding was searched for at any
 * alignment and every hit classified by the opcode in front of it. The counter at 0x004B8860 has
 * 23 references and three writers: the loop above at 0x004757E3, a menu frame at 0x0045DC7A, and a
 * reset at 0x0048477B. The simulation clock at 0x00868728 has seven references and two writers,
 * 0x004757F4 inside the loop and 0x0047562D in the time module's own reset, and all seven of those
 * references lie between 0x0047562D and 0x00475807. Nothing else in the image reads or writes it,
 * which is what makes it the cell that separates a gameplay window from a menu window.
 *
 * The substep period is deliberately not read out of the engine, and the third cell above is the
 * reason. sys_runSubsteps at 0x004756FC saves the incoming frame delta into a local at 0x00475734,
 * pins 0x00868714 to 1/32 at 0x00475740 or to 1/64 at 0x0047574C depending on the flag it tests at
 * 0x00475737, and writes the frame delta back at 0x00475820 on the way out. Anything reading that
 * cell from a per-frame hook therefore reads the frame delta, which at 160 fps is five times
 * smaller than the substep, and a substep count derived from it would come out five times too
 * large. Within a window that nothing but the substep loop touched, the simulation advance divided
 * by the tick delta is the period by construction, and whether that quotient lands on a rate the
 * engine's own selector can choose is exactly the test for whether anything else ticked the
 * counter.
 */
#include "unittest.h"

#include "framerate_stats.h"

#include <stdbool.h>
#include <stdint.h>

#define SUBSTEP_32 0.031250f    /* the shipped simulation period */
#define SUBSTEP_64 0.015625f    /* what the engine's own "60fps" cheat selects instead */

#define CLOCK_PRESENT true
#define CLOCK_ABSENT  false

static void a_clean_gameplay_window_is_accepted(void)
{
    /* The shape 691 of the 750 windows in the field log had: a 60 frame window at a 160 fps cap
     * runs about 21 substeps, and the clock advances by exactly that many periods. */
    ut_check(framerate_stats_classify_window(CLOCK_PRESENT, 21.0f * SUBSTEP_32, 21u)
             == WINDOW_VERDICT_CLEAN,
             "21 substeps and 21 periods of clock is an ordinary gameplay window");

    /* The frame rate does not enter the verdict, so a much longer window is the same case. */
    ut_check(framerate_stats_classify_window(CLOCK_PRESENT, 320.0f * SUBSTEP_32, 320u)
             == WINDOW_VERDICT_CLEAN,
             "ten seconds of gameplay is still a clean window");

    /* The engine can legitimately be running at 64 Hz when the simulation rate is not pinned. The
     * selector at 0x00475737 reads one flag and writes either 1/32 or 1/64 into the period cell, so
     * both quotients are honest measurements and rejecting the second would report a real
     * measurement as contamination. */
    ut_check(framerate_stats_classify_window(CLOCK_PRESENT, 42.0f * SUBSTEP_64, 42u)
             == WINDOW_VERDICT_CLEAN,
             "a 64 Hz window is a rate the selector can choose, so it is clean");
}

static void the_menu_window_that_started_this_is_rejected(void)
{
    /* This is the regression test. The menu writer at 0x0045DC7A ticks the shared counter once per
     * rendered frame while the simulation stands still, so the old code reported 60 "substeps" over
     * a window that contained no simulation at all, at whatever the frame rate happened to be.
     *
     * The alpha in the same log line said so and was misread. At loop exit both clocks are below
     * one substep, so the alpha at 0x0086871C is always in (0,1] whenever sys_runSubsteps runs at
     * all, including when the loop body iterated zero times. A window reporting alpha constant
     * 0.000 therefore does not mean the loop failed to iterate. It means the function was never
     * called, because the only other writer of that cell is the init zero at 0x00475ACB. */
    ut_check(framerate_stats_classify_window(CLOCK_PRESENT, 0.0f, 60u) == WINDOW_VERDICT_IDLE,
             "a counter that moved while the simulation clock did not is not a substep count");

    ut_check(framerate_stats_classify_window(CLOCK_PRESENT, 0.0f, 300u) == WINDOW_VERDICT_IDLE,
             "a long menu window is still a menu window");

    /* A genuinely paused or loading window ticks neither, and it is the same verdict: there is no
     * simulation in it to report on. */
    ut_check(framerate_stats_classify_window(CLOCK_PRESENT, 0.0f, 0u) == WINDOW_VERDICT_IDLE,
             "a window with no simulation and no ticks reports nothing about the simulation");
}

static void a_window_that_straddles_gameplay_and_a_menu_is_rejected(void)
{
    /* 20 substeps of real gameplay plus 40 menu frames. This is the case that produced the 41
     * windows reading above 40 Hz: the count is too high for the amount of simulation in it. */
    ut_check(framerate_stats_classify_window(CLOCK_PRESENT, 20.0f * SUBSTEP_32, 60u)
             == WINDOW_VERDICT_MIXED,
             "20 substeps reported as 60 ticks is not a rate worth printing");
}

static void the_clock_going_backwards_is_a_level_load(void)
{
    /* The simulation clock is reset when a level is loaded, so a window containing that reset shows
     * a negative advance. Reporting it as a rate would divide by a number that means nothing.
     *
     * The threshold either side of zero is 0.0001 s, two hundred times smaller than the shortest
     * substep the selector can choose. That is wide enough to absorb the rounding of a float clock
     * which has been accumulating for a whole level, so a window whose advance is a few ulps either
     * way is called idle rather than announced as a level load. */
    ut_check(framerate_stats_classify_window(CLOCK_PRESENT, -12.5f, 40u) == WINDOW_VERDICT_RESET,
             "a clock that went backwards means a level loaded inside the window");
}

static void an_unresolved_clock_never_claims_a_verdict(void)
{
    /* Without the clock there is nothing to compare the counter against, and the honest answer is
     * to say the number is unverified rather than to guess either way. */
    ut_check(framerate_stats_classify_window(CLOCK_ABSENT, 21.0f * SUBSTEP_32, 21u)
             == WINDOW_VERDICT_UNAVAILABLE,
             "a clean-looking pair is still unverified when the clock did not resolve");
    ut_check(framerate_stats_classify_window(CLOCK_ABSENT, 0.0f, 60u)
             == WINDOW_VERDICT_UNAVAILABLE,
             "a menu-looking pair is unverified for the same reason");
}

static void simulation_without_any_tick_is_impossible_and_never_clean(void)
{
    /* The clock is only advanced inside the loop that ticks the counter, which is what the six
     * instructions at 0x004757DB show, so this pair cannot occur while both cells are the ones they
     * are believed to be. If it ever appears in a log, one of the two operands was resolved to the
     * wrong cell, and the one thing the verdict must not do is call that clean. */
    ut_check(framerate_stats_classify_window(CLOCK_PRESENT, 1.0f, 0u) != WINDOW_VERDICT_CLEAN,
             "an advance with no ticks means an operand is wrong, not that the window is good");
}

static void the_tolerance_reaches_exactly_this_far(void)
{
    /* The band is plus or minus ten per cent of a period the selector can choose, so a couple of
     * stray ticks in a 21 substep window are absorbed and the reported rate is up to ten per cent
     * low. Pinned here so that widening or narrowing the band is a visible change rather than a
     * silent one. */
    ut_check(framerate_stats_classify_window(CLOCK_PRESENT, 21.0f * SUBSTEP_32, 23u)
             == WINDOW_VERDICT_CLEAN,
             "two stray ticks in 21 substeps are inside the band");
    ut_check(framerate_stats_classify_window(CLOCK_PRESENT, 21.0f * SUBSTEP_32, 24u)
             == WINDOW_VERDICT_MIXED,
             "three stray ticks in 21 substeps are outside it");
}

static void exactly_double_the_ticks_aliases_onto_the_faster_rate(void)
{
    /* A known limitation, recorded here rather than left to be discovered. The two rates the
     * selector can choose are a factor of two apart, so a 32 Hz window carrying exactly as many
     * stray ticks as it has substeps is arithmetically indistinguishable from an honest 64 Hz
     * window and is reported clean, at half the true period.
     *
     * It needs the window to be long enough in real time to hold that much simulation, which at a
     * high frame rate and the shipped window length it is not, and the line it produces names 64 Hz
     * where a pinned build only ever runs 32. Both are why this is documented rather than defended
     * against: the defence would need the instrument to know whether the rate was actually pinned,
     * which is a different module's answer. */
    ut_check(framerate_stats_classify_window(CLOCK_PRESENT, 21.0f * SUBSTEP_32, 42u)
             == WINDOW_VERDICT_CLEAN,
             "double the ticks aliases onto the 64 Hz band, and this pins that it does");
}

int main(void)
{
    a_clean_gameplay_window_is_accepted();
    the_menu_window_that_started_this_is_rejected();
    a_window_that_straddles_gameplay_and_a_menu_is_rejected();
    the_clock_going_backwards_is_a_level_load();
    an_unresolved_clock_never_claims_a_verdict();
    simulation_without_any_tick_is_impossible_and_never_clean();
    the_tolerance_reaches_exactly_this_far();
    exactly_double_the_ticks_aliases_onto_the_faster_rate();

    return ut_summary("framerate_stats");
}
