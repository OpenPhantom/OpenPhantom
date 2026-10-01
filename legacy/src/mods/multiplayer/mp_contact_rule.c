/* mp_contact_rule.c: what each delivery path does with a verdict. See the header. */
#include "mp_contact_rule.h"

#include <stdbool.h>

bool mp_contact_rule_puppet_reports(mp_contact_verdict_t verdict)
{
    return verdict == MP_CONTACT_ALLOWED;
}

bool mp_contact_rule_carries_out(mp_contact_verdict_t verdict)
{
    return verdict != MP_CONTACT_REFUSED;
}
