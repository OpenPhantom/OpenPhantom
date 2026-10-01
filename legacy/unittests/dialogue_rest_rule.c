/* dialogue_rest_rule.c: whether dialogue_anim_fix may move a speaker's body.
 *
 * The rule it replaces asked the clip's name alone, and a clip whose name says nothing of a death
 * let the rest stand a corpse back up. That rule is kept here as the reference: the new one must
 * say no to every corpse the old one stood up, must never move a body the old one left alone, and
 * must answer exactly as the old one did for a body with no actor behind it.
 */
#include "unittest.h"

#include "rest_rule.h"

#include <stdbool.h>
#include <stdint.h>

/* The rule as it stood: a clip named for a death is left down, anything else goes to the stand. */
static rest_verdict_t old_rule(bool death_named)
{
    return death_named ? REST_NEVER : REST_MAY;
}

static void the_named_cases(void)
{
    ut_section("the cases the rule was written for");
    ut_check(rest_verdict(true, -3, 14, false) == REST_NEVER,
             "a thrown droid killed on brnockdi, a corpse in state 14, is left down");
    ut_check(old_rule(false) == REST_MAY,
             "the old rule stood that same corpse up, which is the defect this rule removes");
    ut_check(rest_verdict(true, 40, 1, false) == REST_MAY,
             "a live droid in its script, parked on brnfirwt after a line, goes to its stand");
    ut_check(rest_verdict(true, 40, 7, false) == REST_NOT_NOW,
             "a live droid in the landing state 7 is passed over, not let go");
    ut_check(rest_verdict(true, 40, 9, false) == REST_NOT_NOW,
             "a live droid getting up, state 9, is passed over, not let go");
    ut_check(rest_verdict(true, -1, 14, false) == REST_NEVER,
             "a corpse, state 14, is let go");
    ut_check(rest_verdict(false, 0, 0, true) == REST_NEVER,
             "a body with no actor on a clip named for a death is left down, as before");
    ut_check(rest_verdict(false, 0, 0, false) == REST_MAY,
             "a body with no actor on any other clip goes to its stand, as before");
    ut_check(rest_verdict(true, 40, 1, true) == REST_NEVER,
             "a live actor in its script on a clip named for a death is still left alone");
}

static void the_boundaries(void)
{
    int32_t state;

    ut_section("the boundaries");
    ut_check(rest_verdict(true, 0, 1, false) == REST_NEVER,
             "health 0 is dead: the engine's own test is health above zero");
    ut_check(rest_verdict(true, 1, 1, false) == REST_MAY, "health 1 in the script may rest");
    ut_check(rest_verdict(true, INT32_MIN, 1, false) == REST_NEVER,
             "the lowest health there is is dead");
    ut_check(rest_verdict(true, INT32_MAX, 1, false) == REST_MAY,
             "the highest health there is lives");
    for (state = REST_STATE_DEATH_FIRST; state <= REST_STATE_DEATH_LAST; ++state) {
        ut_checkf(rest_verdict(true, 40, state, false) == REST_NEVER,
                  "state %d with health left is a death state and is let go", (int)state);
    }
    ut_check(rest_verdict(true, 40, REST_STATE_DEATH_FIRST - 1, false) == REST_NOT_NOW,
             "state 10, the sabre hit, is one below the death states and is passed over");
    ut_check(rest_verdict(true, 40, REST_STATE_DEATH_LAST + 1, false) == REST_NOT_NOW,
             "state 15, the zapped, is one above the death states and is passed over");
    ut_check(rest_verdict(true, 40, 3, false) == REST_NOT_NOW,
             "state 3, a parked replica or a mounted turret, is passed over");
    ut_check(rest_verdict(true, 40, 0, false) == REST_NOT_NOW,
             "state 0 is not the script's and is passed over");
    ut_check(rest_verdict(true, 40, -1, false) == REST_NOT_NOW,
             "a state below any the engine writes is not the script's either");
}

/* Every combination the two rules can be asked, held side by side. */
static void against_the_old_rule(void)
{
    static const int32_t HEALTHS[] = { INT32_MIN, -300, -7, -1, 0, 1, 2, 40, 600, INT32_MAX };
    uint32_t corpses_old_stood = 0;
    uint32_t dead_old_stood = 0;
    uint32_t moved_where_old_did_not = 0;
    uint32_t differs_without_actor = 0;
    uint32_t live_script_kept = 0;
    uint32_t live_script_cases = 0;
    size_t   h;
    int32_t  state;
    int      named;

    ut_section("against the old rule, every combination");
    for (h = 0; h < sizeof HEALTHS / sizeof HEALTHS[0]; ++h) {
        for (state = -1; state <= 32; ++state) {
            for (named = 0; named <= 1; ++named) {
                rest_verdict_t old_answer = old_rule(named != 0);
                rest_verdict_t with_actor = rest_verdict(true, HEALTHS[h], state, named != 0);
                rest_verdict_t no_actor   = rest_verdict(false, HEALTHS[h], state, named != 0);

                if (with_actor == REST_MAY && old_answer != REST_MAY) {
                    ++moved_where_old_did_not;
                }
                if (no_actor != old_answer) {
                    ++differs_without_actor;
                }
                if (old_answer == REST_MAY && HEALTHS[h] <= 0) {
                    ++dead_old_stood;
                    corpses_old_stood += (with_actor == REST_NEVER) ? 1u : 0u;
                }
                if (HEALTHS[h] > 0 && state == REST_STATE_SCRIPT && named == 0) {
                    ++live_script_cases;
                    live_script_kept += (with_actor == old_answer) ? 1u : 0u;
                }
            }
        }
    }
    ut_check(moved_where_old_did_not == 0,
             "the new rule never moves a body the old one left alone");
    ut_check(differs_without_actor == 0,
             "a body with no actor behind it is answered exactly as before");
    ut_checkf(corpses_old_stood > 0 && corpses_old_stood == dead_old_stood,
              "every dead actor the old rule stood up is let go now (%u of %u combinations)",
              (unsigned)corpses_old_stood, (unsigned)dead_old_stood);
    ut_checkf(live_script_cases > 0 && live_script_kept == live_script_cases,
              "a live actor in its script, its clip not named for a death, rests as before "
              "(%u of %u)",
              (unsigned)live_script_kept, (unsigned)live_script_cases);
}

int main(void)
{
    the_named_cases();
    the_boundaries();
    against_the_old_rule();
    return ut_summary("dialogue rest rule");
}
