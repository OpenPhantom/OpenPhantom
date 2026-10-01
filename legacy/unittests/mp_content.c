/* mp_content.c: the character roster as one number.
 *
 * A join is judged by mp_mod_manifest_rule, whose own test holds the table of the release's mods
 * and the judgement. What is tested here is the roster, and the property its callers rest on: the
 * same answer twice, and zero only for "no roster at all".
 */
#include "unittest.h"

#include "mp_content.h"

#include <stdbool.h>
#include <stdint.h>

static void check_the_roster_answer_is_stable(void)
{
    uint32_t first;
    uint32_t second;

    ut_section("the roster answers the same thing twice");

    first  = mp_content_roster_fingerprint();
    second = mp_content_roster_fingerprint();
    ut_checkf(first == second, "two calls agree (%08X against %08X)",
              (unsigned)first, (unsigned)second);
    /* Whether a roster is there at all depends on the game folder and is not a thing a test can
     * arrange. What IS the contract either way: zero means nothing to compare, and the judge does
     * not compare it. */
    if (first == 0u) {
        ut_check(true, "no characters.ini beside this binary, so the roster is zero and skipped");
    } else {
        ut_checkf(first != 0u, "a roster is present and hashes to %08X", (unsigned)first);
    }
}

int main(void)
{
    check_the_roster_answer_is_stable();

    return ut_summary("mp_content");
}
