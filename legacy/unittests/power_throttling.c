/* power_throttling.c: how the log names the state of one power throttling switch.
 *
 * Windows answers the question with two masks. A switch absent from the control mask is Windows'
 * own to decide, and that is the state a process without HighQos starts in; one present in the
 * control mask is either held on or held off by the state mask. Reading only the state mask, as is
 * easy to do, reports "off" for a process Windows is free to throttle whenever its window goes
 * behind another, which is the opposite of what the line exists to show.
 */
#include "unittest.h"

#include "power_throttling.h"

#include <string.h>

#define EXECUTION_SPEED 0x1ul
#define TIMER_RESOLUTION 0x4ul

static void test_the_three_states(void)
{
    ut_section("the three states of one switch");

    ut_check(strcmp(power_throttling_state_name(0ul, 0ul, EXECUTION_SPEED),
                    "left to Windows") == 0,
             "a switch outside the control mask is Windows' to decide");
    ut_check(strcmp(power_throttling_state_name(0ul, EXECUTION_SPEED, EXECUTION_SPEED),
                    "left to Windows") == 0,
             "and a state bit without its control bit means nothing");
    ut_check(strcmp(power_throttling_state_name(EXECUTION_SPEED, 0ul, EXECUTION_SPEED),
                    "held off") == 0,
             "control without state is held off, which is what HighQos asks for");
    ut_check(strcmp(power_throttling_state_name(EXECUTION_SPEED, EXECUTION_SPEED,
                                                EXECUTION_SPEED), "held on") == 0,
             "control with state is held on, which is what EcoQoS looks like");
}

static void test_the_switches_apart(void)
{
    unsigned long control = EXECUTION_SPEED | TIMER_RESOLUTION;

    ut_section("two switches read apart");

    ut_check(strcmp(power_throttling_state_name(control, TIMER_RESOLUTION, EXECUTION_SPEED),
                    "held off") == 0,
             "the execution speed is read from its own bit");
    ut_check(strcmp(power_throttling_state_name(control, TIMER_RESOLUTION, TIMER_RESOLUTION),
                    "held on") == 0,
             "and the timer resolution from its own");
    ut_check(strcmp(power_throttling_state_name(EXECUTION_SPEED, 0ul, TIMER_RESOLUTION),
                    "left to Windows") == 0,
             "a Windows 10 that only took the execution speed leaves the timer to itself");
}

int main(void)
{
    test_the_three_states();
    test_the_switches_apart();

    return ut_summary("power throttling");
}
