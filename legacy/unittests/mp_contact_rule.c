/* mp_contact_rule.c: what each delivery path does with the verdict on a contact between players. */
#include "unittest.h"

#include "mp_contact_rule.h"

#include <stdbool.h>

/* The puppet branch reports a hit to the victim's machine and shows the hurt here. Anything but an
 * allowed contact sends nothing: a refused one was refused on both machines by the same rule, and
 * one between two far players is judged on the victim's own machine, where the attacker's shot or
 * swing is replayed on its own body. Reported from here as well, it hurt twice with friendly fire
 * on and once with it off. */
static void test_the_puppet_branch(void)
{
    ut_section("a puppet's contact");
    ut_check(mp_contact_rule_puppet_reports(MP_CONTACT_ALLOWED),
             "an allowed contact is reported to the victim's machine");
    ut_check(!mp_contact_rule_puppet_reports(MP_CONTACT_REFUSED),
             "a refused one is not");
    ut_check(!mp_contact_rule_puppet_reports(MP_CONTACT_NOT_OURS),
             "and one between two far players is not either: the victim's machine judges it");
}

/* A routed delivery and one to this machine's own player run the engine's handler here and report
 * nothing to another machine, so only a refusal stops them. */
static void test_the_paths_that_carry_out(void)
{
    ut_section("a delivery carried out on this machine");
    ut_check(mp_contact_rule_carries_out(MP_CONTACT_ALLOWED), "an allowed contact is carried out");
    ut_check(!mp_contact_rule_carries_out(MP_CONTACT_REFUSED),
             "a refused one is answered and nothing of it is carried out");
    ut_check(mp_contact_rule_carries_out(MP_CONTACT_NOT_OURS),
             "and one between two far players is carried out as it always was");
}

int main(void)
{
    test_the_puppet_branch();
    test_the_paths_that_carry_out();

    return ut_summary("mp_contact_rule");
}
