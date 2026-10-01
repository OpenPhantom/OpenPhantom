/* limb_mode_pick.c: which dismemberment mode the poll runs, a multiplayer host's or this machine's
 * own, and the table of settings a host may decide held against the modes this DLL knows.
 *
 * The poll keeps what it had when the ini holds a mode it does not know, which is how it always
 * behaved; the host's mode is taken only while it is named and is one of the three.
 */
#include "unittest.h"

#include "dismemberment.h"
#include "limb_mode_pick.h"

#include "common/host_settings_note.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>

static void check_the_table(void)
{
    const host_setting_key_t *keys = host_settings_keys(NULL);

    ut_section("the table a host's mode is held to is the modes this DLL knows");
    ut_check(keys[HOST_SETTING_DISMEMBERMENT_MODE].minimum == (float)LIMB_MODE_OFF &&
                 keys[HOST_SETTING_DISMEMBERMENT_MODE].maximum == (float)LIMB_MODE_ON_DEATH,
             "from off to on the killing blow");
    ut_check(keys[HOST_SETTING_DISMEMBERMENT_MODE].default_value == (float)LIMB_MODE_OFF &&
                 keys[HOST_SETTING_DISMEMBERMENT_MODE].whole_numbers,
             "a whole number that starts off, as the load reads a missing key");
    ut_check(limb_mode_is_known(0) && limb_mode_is_known(2) && !limb_mode_is_known(3) &&
                 !limb_mode_is_known(-1),
             "three modes and no fourth");
}

static void check_the_pick(void)
{
    limb_mode_choice_t choice;

    ut_section("the host's mode while it names one, else this machine's own");
    choice = limb_mode_pick(true, 2.0f, 0, 0);
    ut_check(choice.mode == 2 && choice.from_host && choice.own == 0,
             "the host's 2 over this machine's 0, which stays its own");
    choice = limb_mode_pick(true, 0.0f, 1, 1);
    ut_check(choice.mode == 0 && choice.from_host, "and the host's off over an own 1");
    choice = limb_mode_pick(false, 2.0f, 1, 0);
    ut_check(choice.mode == 1 && !choice.from_host && choice.own == 1,
             "with nothing named, the ini's own 1");
    choice = limb_mode_pick(true, 1.5f, 1, 1);
    ut_check(choice.mode == 1 && !choice.from_host, "a host's 1.5 is not a mode");
    choice = limb_mode_pick(true, 3.0f, 1, 1);
    ut_check(choice.mode == 1 && !choice.from_host, "nor is a host's 3");
    choice = limb_mode_pick(true, NAN, 2, 2);
    ut_check(choice.mode == 2 && !choice.from_host, "nor a NaN");

    ut_section("an ini mode this DLL does not know leaves the own one standing");
    choice = limb_mode_pick(false, 0.0f, 7, 2);
    ut_check(choice.mode == 2 && choice.own == 2, "a 7 in the ini keeps the own 2");
    choice = limb_mode_pick(true, 1.0f, -1, 2);
    ut_check(choice.mode == 1 && choice.own == 2,
             "and while the host rules, the own 2 is kept for when the session is over");
}

int main(void)
{
    check_the_table();
    check_the_pick();
    return ut_summary("limb_mode_pick");
}
